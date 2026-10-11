#include "sim_internal.h"
#include "wire_solver.h"

#include <stdlib.h>
#include <string.h>

enum { NOTIFICATION_RADIUS = 3 };

bool sim_init_internal(World *world)
{
    Simulator *sim = calloc(1, sizeof(*sim));
    if (!sim) {
        return false;
    }
    world->sim = sim;
    sim->free_torch = RS_NONE;
    size_t volume = world->volume;

    if (!queue_init(&sim->dirty, volume) || !scheduler_init(&sim->scheduler, volume)) {
        return false;
    }
    sim->wire_roots = malloc(volume * sizeof(uint32_t));
    sim->input_roots = malloc(volume * sizeof(uint32_t));
    sim->nodes = malloc(volume * sizeof(uint32_t));
    sim->visited = calloc(volume, sizeof(uint32_t));
    sim->bucket_next = malloc(volume * sizeof(uint32_t));
    sim->bucket_prev = malloc(volume * sizeof(uint32_t));
    sim->torch_slots = malloc(volume * sizeof(uint32_t));
    sim->root_flags = calloc(volume, 1);
    sim->staged_power = malloc(volume);

    if (!sim->wire_roots || !sim->input_roots || !sim->nodes || !sim->visited ||
        !sim->bucket_next || !sim->bucket_prev || !sim->torch_slots ||
        !sim->root_flags || !sim->staged_power) {
        return false;
    }
    for (size_t index = 0; index < volume; ++index) {
        sim->torch_slots[index] = RS_NONE;
    }
    return true;
}

void sim_destroy_internal(World *world)
{
    Simulator *sim = world->sim;
    if (!sim) {
        return;
    }
    queue_destroy(&sim->dirty);
    scheduler_destroy(&sim->scheduler);
    free(sim->wire_roots);
    free(sim->input_roots);
    free(sim->nodes);
    free(sim->visited);
    free(sim->bucket_next);
    free(sim->bucket_prev);
    free(sim->torch_slots);
    free(sim->root_flags);
    free(sim->staged_power);
    free(sim->torches);
    free(sim->due_actions);
    free(sim);
    world->sim = NULL;
}

RsResult sim_prepare_torch(World *world)
{
    Simulator *sim = world->sim;
    if (sim->free_torch != RS_NONE || sim->torch_used < sim->torch_capacity) {
        return RS_OK;
    }
    size_t capacity = sim->torch_capacity ? sim->torch_capacity * 2 : 8;
    if (capacity > world->volume) {
        capacity = world->volume;
    }
    if (capacity <= sim->torch_capacity || capacity > SIZE_MAX / sizeof(TorchRuntime) ||
        capacity > SIZE_MAX / sizeof(DueAction)) {
        return RS_OUT_OF_MEMORY;
    }

    TorchRuntime *torches = realloc(sim->torches, capacity * sizeof(*torches));
    if (!torches) {
        return RS_OUT_OF_MEMORY;
    }
    sim->torches = torches;
    DueAction *actions = realloc(sim->due_actions, capacity * sizeof(*actions));
    if (!actions) {
        return RS_OUT_OF_MEMORY;
    }
    sim->due_actions = actions;
    if (!scheduler_reserve(&sim->scheduler, capacity)) {
        return RS_OUT_OF_MEMORY;
    }
    sim->torch_capacity = capacity;
    return RS_OK;
}

void sim_attach_torch(World *world, uint32_t index)
{
    Simulator *sim = world->sim;
    uint32_t slot;
    if (sim->free_torch != RS_NONE) {
        slot = sim->free_torch;
        sim->free_torch = sim->torches[slot].next_free;
    } else {
        slot = (uint32_t)sim->torch_used++;
    }
    memset(&sim->torches[slot], 0, sizeof(TorchRuntime));
    sim->torch_slots[index] = slot;
}

void sim_forget_cell(World *world, uint32_t index)
{
    Simulator *sim = world->sim;
    scheduler_cancel(&sim->scheduler, index);
    uint32_t slot = sim->torch_slots[index];
    if (slot != RS_NONE) {
        sim->torches[slot].next_free = sim->free_torch;
        sim->free_torch = slot;
        sim->torch_slots[index] = RS_NONE;
    }
}

TorchRuntime *sim_torch_runtime(const World *world, uint32_t index)
{
    uint32_t slot = world->sim->torch_slots[index];
    return slot == RS_NONE ? NULL : &world->sim->torches[slot];
}

void sim_mark_wire(World *world, uint32_t index)
{
    Simulator *sim = world->sim;
    if (sim->root_flags[index] & ROOT_WIRE) {
        return;
    }
    sim->root_flags[index] |= ROOT_WIRE;
    sim->wire_roots[sim->wire_count++] = index;
}

void sim_mark_input(World *world, uint32_t index)
{
    Simulator *sim = world->sim;
    if (sim->root_flags[index] & ROOT_INPUT) {
        return;
    }
    sim->root_flags[index] |= ROOT_INPUT;
    sim->input_roots[sim->input_count++] = index;
}

static void notify(World *world, BlockPos position, bool topology)
{
    /* Covers step connections, removed supports and input queries through iron.
     * Only blocks with registered callbacks enter a queue. */
    for (int dy = -NOTIFICATION_RADIUS; dy <= NOTIFICATION_RADIUS; ++dy) {
        for (int dz = -NOTIFICATION_RADIUS; dz <= NOTIFICATION_RADIUS; ++dz) {
            for (int dx = -NOTIFICATION_RADIUS; dx <= NOTIFICATION_RADIUS; ++dx) {
                if (abs(dx) + abs(dy) + abs(dz) > NOTIFICATION_RADIUS) {
                    continue;
                }
                int64_t x = (int64_t)position.x + dx;
                int64_t y = (int64_t)position.y + dy;
                int64_t z = (int64_t)position.z + dz;
                if (x < 0 || y < 0 || z < 0 || x >= world->config.size_x ||
                    y >= world->config.size_y || z >= world->config.size_z) {
                    continue;
                }
                BlockPos neighbor = {(int)x, (int)y, (int)z};
                uint32_t index = world_index(world, neighbor);
                const BlockDef *definition = block_definition((BlockId)world->cells[index].id);
                if (topology && definition->neighbor_changed) {
                    if (!queue_push(&world->sim->dirty, index)) {
                        world->sim->error = RS_INTERNAL_ERROR;
                    }
                } else if (!topology && definition->input_changed) {
                    sim_mark_input(world, index);
                }
            }
        }
    }
}

void sim_notify_change(World *world, BlockPos position)
{
    notify(world, position, true);
}

void sim_notify_power(World *world, BlockPos position)
{
    notify(world, position, false);
}

void sim_schedule(World *world, uint32_t index, uint32_t delay)
{
    if (!delay || UINT64_MAX - world->tick < delay ||
        !scheduler_add(&world->sim->scheduler, index, world->tick + delay)) {
        world->sim->error = RS_INTERNAL_ERROR;
    }
}

static void flush_neighbors(World *world)
{
    uint32_t index;
    while (queue_pop(&world->sim->dirty, &index)) {
        const BlockDef *definition = block_definition((BlockId)world->cells[index].id);
        if (definition->neighbor_changed) {
            ++world->sim->stats.neighbor_checks;
            definition->neighbor_changed(world, world_position(world, index));
        }
    }
}

static void flush_inputs(World *world)
{
    Simulator *sim = world->sim;
    for (size_t cursor = 0; cursor < sim->input_count; ++cursor) {
        uint32_t index = sim->input_roots[cursor];
        sim->root_flags[index] &= (uint8_t)~ROOT_INPUT;
        const BlockDef *definition = block_definition((BlockId)world->cells[index].id);
        if (definition->input_changed) {
            definition->input_changed(world, world_position(world, index));
        }
    }
    sim->input_count = 0;
}

RsResult sim_flush(World *world)
{
    if (!world) {
        return RS_INVALID_ARGUMENT;
    }
    if (world->sim->error != RS_OK) {
        return world->sim->error;
    }
    flush_neighbors(world);
    wire_solver_flush(world);
    flush_inputs(world);
    return world->sim->error;
}

static size_t evaluate_due_events(World *world)
{
    Simulator *sim = world->sim;
    ScheduledEvent event;
    size_t count = 0;
    while (scheduler_pop_due(&sim->scheduler, world->tick, &event)) {
        const BlockDef *definition = block_definition((BlockId)world->cells[event.index].id);
        if (!definition->scheduled_tick) {
            continue;
        }
        TickAction action = definition->scheduled_tick(world, world_position(world, event.index));
        sim->due_actions[count++] = (DueAction){event.index, action};
        ++sim->stats.events_processed;
    }
    return count;
}

static void commit_due_events(World *world, size_t count)
{
    for (size_t cursor = 0; cursor < count; ++cursor) {
        DueAction action = world->sim->due_actions[cursor];
        if (action.action.changed) {
            world_write_derived(world, action.index, action.action.state);
            sim_notify_change(world, world_position(world, action.index));
        }
        if (action.action.next_delay) {
            sim_schedule(world, action.index, action.action.next_delay);
        }
    }
}

RsResult sim_step(World *world)
{
    RsResult result = sim_flush(world);
    if (result != RS_OK) {
        return result;
    }
    if (world->tick == UINT64_MAX) {
        return RS_INTERNAL_ERROR;
    }

    ++world->tick;
    /* Every due input sees the same world before any due state is committed. */
    size_t count = evaluate_due_events(world);
    commit_due_events(world, count);
    return sim_flush(world);
}

SimStats sim_stats(const World *world)
{
    SimStats stats = {0};
    if (world) {
        stats = world->sim->stats;
        stats.pending_events = world->sim->scheduler.count;
    }
    return stats;
}

void sim_reset_stats(World *world)
{
    if (world) {
        memset(&world->sim->stats, 0, sizeof(SimStats));
    }
}
