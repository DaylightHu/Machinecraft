#include "sim_internal.h"
#include "wire_solver.h"
#include "rail_solver.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { NOTIFICATION_RADIUS = 3 };
bool sim_init_internal(World *w)
{
    w->sim = calloc(1, sizeof(*w->sim));
    if (!w->sim) return false;
    w->sim->containers.dropper_random = UINT64_C(0x5deece66d); /* Java seed 0. */
    w->sim->free_torch = SIZE_MAX; scheduler_init(&w->sim->scheduler); return true;
}
void sim_destroy_internal(World *w)
{
    Simulator *s = w->sim;
    if (!s) return;
    container_destroy(w);
    queue_destroy(&s->dirty); queue_destroy(&s->wire_roots);
    queue_destroy(&s->rail_roots); queue_destroy(&s->input_roots);
    free(s->piston_events.items); posmap_destroy(&s->piston_events.pending);
    scheduler_destroy(&s->scheduler); posmap_destroy(&s->torch_slots);
    free(s->torches); free(s); w->sim = NULL;
}
RsResult sim_prepare_torch(World *w)
{
    Simulator *s = w->sim;
    if (!posmap_reserve(&s->torch_slots, s->torch_slots.count + 1)) return RS_OUT_OF_MEMORY;
    if (s->free_torch != SIZE_MAX || s->torch_used < s->torch_capacity) return RS_OK;
    size_t capacity = s->torch_capacity ? s->torch_capacity * 2 : 8;
    if (capacity < s->torch_capacity || capacity > SIZE_MAX / sizeof(TorchRuntime)) return RS_OUT_OF_MEMORY;
    TorchRuntime *next = realloc(s->torches, capacity * sizeof(*next));
    if (!next) return RS_OUT_OF_MEMORY;
    s->torches = next; s->torch_capacity = capacity; return RS_OK;
}
bool sim_attach_torch(World *w, BlockPos p)
{
    Simulator *s = w->sim;
    if (sim_prepare_torch(w) != RS_OK) return false;
    size_t slot = s->free_torch != SIZE_MAX ? s->free_torch : s->torch_used;
    if (!posmap_set(&s->torch_slots, p, slot)) return false;
    if (s->free_torch != SIZE_MAX) s->free_torch = s->torches[slot].next_free;
    else ++s->torch_used;
    s->torches[slot] = (TorchRuntime){.pos = p, .used = true}; return true;
}
void sim_forget_cell(World *w, BlockPos p)
{
    container_forget(w, p);
    Simulator *s = w->sim; size_t slot;
    /* Vanilla scheduled block ticks survive replacement. Piston entries are
     * this engine's motion markers, not vanilla scheduled block ticks. */
    scheduler_cancel(&s->scheduler, p, BLOCK_PISTON);
    scheduler_cancel(&s->scheduler, p, BLOCK_STICKY_PISTON);
    if (posmap_get(&s->torch_slots, p, &slot)) {
        s->torches[slot].used = false; s->torches[slot].next_free = s->free_torch;
        s->free_torch = slot; posmap_remove(&s->torch_slots, p);
    }
}
TorchRuntime *sim_torch_runtime(const World *w, BlockPos p)
{
    size_t slot;
    return posmap_get(&w->sim->torch_slots, p, &slot) ? &w->sim->torches[slot] : NULL;
}
void sim_mark_wire(World *w, BlockPos p)
{
    if (!queue_push(&w->sim->wire_roots, p)) w->sim->error = RS_OUT_OF_MEMORY;
}
void sim_mark_rail(World *w, BlockPos p)
{
    if (!queue_push(&w->sim->rail_roots, p)) w->sim->error = RS_OUT_OF_MEMORY;
}
void sim_mark_input(World *w, BlockPos p)
{
    if (!queue_push(&w->sim->input_roots, p)) w->sim->error = RS_OUT_OF_MEMORY;
}
static bool dropper_secondary_update(BlockState source, int dx, int dy, int dz)
{
    if (source.id == BLOCK_REDSTONE_WIRE || source.id == BLOCK_REDSTONE_TORCH)
        return true;
    Direction facing;
    if (source.id == BLOCK_REDSTONE_REPEATER)
        facing = (Direction)source.data.repeater.facing;
    else if (source.id == BLOCK_REDSTONE_COMPARATOR)
        facing = (Direction)source.data.comparator.facing;
    else if (source.id == BLOCK_OBSERVER)
        facing = (Direction)source.data.observer.facing;
    else return false;
    BlockPos output = block_offset((BlockPos){0}, facing);
    return abs(dx - output.x) + abs(dy - output.y) + abs(dz - output.z) == 1;
}

static void notify(World *w, BlockPos p, bool topology, BlockState previous)
{
    BlockState source = world_get(w, p);
    for (int dy = -3; dy <= 3; ++dy) for (int dz = -3; dz <= 3; ++dz) for (int dx = -3; dx <= 3; ++dx) {
        int distance = abs(dx) + abs(dy) + abs(dz);
        if (distance > NOTIFICATION_RADIUS) continue;
        int64_t x = (int64_t)p.x + dx, y = (int64_t)p.y + dy, z = (int64_t)p.z + dz;
        if (x < INT_MIN || x > INT_MAX || y < WORLD_MIN_Y || y > WORLD_MAX_Y || z < INT_MIN || z > INT_MAX) continue;
        BlockPos n = {(int)x, (int)y, (int)z};
        const BlockDef *d = block_definition((BlockId)world_get(w, n).id);
        /* QC samples above the dropper only when a neighbor update reaches it.
         * A plain block two cells away must not wake this BUD-sensitive latch. */
        if (d == &rs_dropper_def && distance != 1 &&
            (distance != 2 || (!dropper_secondary_update(source, dx, dy, dz) &&
                               !dropper_secondary_update(previous, dx, dy, dz))))
            continue;
        if (d == &rs_dropper_def) {
            sim_mark_input(w, n);
            continue;
        }
        if (topology && d->neighbor_changed) {
            if (!queue_push(&w->sim->dirty, n)) w->sim->error = RS_OUT_OF_MEMORY;
        } else if (!topology && d->input_changed) sim_mark_input(w, n);
    }
}
void sim_notify_change(World *w, BlockPos p) { notify(w, p, true, world_get(w, p)); }
void sim_notify_replacement(World *w, BlockPos p, BlockState previous)
{
    notify(w, p, true, previous);
}
void sim_notify_power(World *w, BlockPos p) { notify(w, p, false, world_get(w, p)); }
void sim_notify_analog(World *w, BlockPos p)
{
    for (Direction direction = 0; direction < DIR_COUNT; ++direction) {
        if (!direction_horizontal(direction)) continue;
        BlockPos next = block_offset(p, direction);
        if (world_get(w, next).id == BLOCK_REDSTONE_COMPARATOR)
            sim_mark_input(w, next);
        else if (block_is_conductor(w, next)) {
            next = block_offset(next, direction);
            if (world_get(w, next).id == BLOCK_REDSTONE_COMPARATOR)
                sim_mark_input(w, next);
        }
    }
}
void sim_notify_observers(World *w, BlockPos changed)
{
    for (Direction direction = 0; direction < DIR_COUNT; ++direction) {
        BlockPos position = block_offset(changed, direction);
        const BlockDef *definition = block_definition(
            (BlockId)world_get(w, position).id);
        if (definition->observed_changed)
            definition->observed_changed(w, position, changed);
    }
}
void sim_schedule(World *w, BlockPos p, uint32_t delay)
{
    sim_schedule_priority(w, p, delay, TICK_NORMAL);
}

void sim_schedule_priority(World *w, BlockPos p, uint32_t delay,
                           TickPriority priority)
{
    if (!delay || UINT64_MAX - w->tick < delay) { w->sim->error = RS_INTERNAL_ERROR; return; }
    if (!scheduler_add_priority(&w->sim->scheduler, p,
                                scheduler_block_type(world_get(w, p)), w->tick + delay,
                                priority))
        w->sim->error = RS_OUT_OF_MEMORY;
}
void sim_cancel(World *w, BlockPos p)
{
    scheduler_cancel(&w->sim->scheduler, p, scheduler_block_type(world_get(w, p)));
}
bool sim_has_scheduled_tick(const World *w, BlockPos p)
{
    return scheduler_pending(&w->sim->scheduler, p,
                             scheduler_block_type(world_get(w, p)));
}
bool sim_will_tick(const World *w, BlockPos p)
{
    return scheduler_will_tick(&w->sim->scheduler, p,
                               scheduler_block_type(world_get(w, p)));
}

static BlockPos piston_event_key(PistonEvent event)
{
    BlockPos key = event.pos;
    unsigned type = ((event.block == BLOCK_STICKY_PISTON ? 3u : 0u) +
                     event.type)*DIR_COUNT + event.facing;
    key.y = (key.y - WORLD_MIN_Y)*(6*DIR_COUNT) + (int)type;
    return key;
}

bool sim_queue_piston_event(World *w, PistonEvent event)
{
    if (!world_in_bounds(w, event.pos) ||
        !block_is_piston_base((BlockId)event.block) ||
        event.type > 2 || event.facing >= DIR_COUNT) {
        w->sim->error = RS_INTERNAL_ERROR;
        return false;
    }
    PistonEvents *q = &w->sim->piston_events;
    BlockPos key = piston_event_key(event);
    if (posmap_get(&q->pending, key, NULL)) return true;
    if (q->head && q->count == q->capacity) {
        memmove(q->items, q->items + q->head,
                (q->count - q->head)*sizeof(*q->items));
        q->count -= q->head;
        q->head = 0;
    }
    if (q->count == q->capacity) {
        size_t capacity = q->capacity ? q->capacity*2 : 16;
        if (capacity < q->capacity || capacity > SIZE_MAX/sizeof(*q->items))
            goto fail;
        PistonEvent *next = realloc(q->items, capacity*sizeof(*next));
        if (!next) goto fail;
        q->items = next;
        q->capacity = capacity;
    }
    if (!posmap_set(&q->pending, key, 0)) goto fail;
    q->items[q->count++] = event;
    return true;
fail:
    w->sim->error = RS_OUT_OF_MEMORY;
    return false;
}

static bool pop_piston_event(PistonEvents *q, PistonEvent *event)
{
    if (q->head == q->count) return false;
    *event = q->items[q->head++];
    posmap_remove(&q->pending, piston_event_key(*event));
    if (q->head == q->count) q->head = q->count = 0;
    return true;
}
RsResult sim_flush(World *w)
{
    if (!w) return RS_INVALID_ARGUMENT;
    Simulator *s = w->sim; BlockPos p;
    if (s->error != RS_OK) return s->error;
    do {
        while (queue_pop(&s->dirty, &p)) {
            const BlockDef *d = block_definition((BlockId)world_get(w, p).id);
            if (d->neighbor_changed) { ++s->stats.neighbor_checks; d->neighbor_changed(w, p); }
            if (s->error != RS_OK) return s->error;
        }
        wire_solver_flush(w);
        if (s->error != RS_OK) return s->error;
        while (queue_pop(&s->input_roots, &p)) {
            const BlockDef *d = block_definition((BlockId)world_get(w, p).id);
            if (d->input_changed) d->input_changed(w, p);
            if (s->error != RS_OK) return s->error;
        }
        rail_solver_flush(w);
        if (s->error != RS_OK) return s->error;
    } while (s->dirty.count || s->wire_roots.count ||
             s->input_roots.count || s->rail_roots.count);
    return s->error;
}
RsResult sim_step(World *w)
{
    RsResult result = sim_flush(w);
    if (result != RS_OK) return result;
    if (w->tick == UINT64_MAX) return RS_INTERNAL_ERROR;
    Simulator *s = w->sim;
    ++w->tick;
    if (!scheduler_begin_tick(&s->scheduler, w->tick))
        return s->error = RS_OUT_OF_MEMORY;
    ScheduledEvent event;
    while (scheduler_next_tick(&s->scheduler, &event)) {
        BlockState state = world_get(w, event.pos);
        if (scheduler_block_type(state) != event.block) continue;
        const BlockDef *d = block_definition((BlockId)state.id);
        if (!d->scheduled_tick) continue;
        TickAction action = d->scheduled_tick(w, event.pos);
        ++s->stats.events_processed;
        if (action.changed && world_write_derived(w, event.pos, action.state))
            sim_notify_change(w, event.pos);
        if (action.next_delay)
            sim_schedule(w, event.pos, action.next_delay);
        result = sim_flush(w);
        if (result != RS_OK) return result;
    }
    /* ServerLevel runs block events after the complete scheduled-tick batch,
     * before block entities. A block event may append more events to this FIFO. */
    PistonEvent piston;
    while (pop_piston_event(&s->piston_events, &piston)) {
        if (world_get(w, piston.pos).id != piston.block) continue;
        piston_trigger_event(w, piston);
        ++s->stats.events_processed;
        result = sim_flush(w);
        if (result != RS_OK) return result;
    }
    container_step(w);
    return s->error;
}
SimStats sim_stats(const World *w)
{
    SimStats stats = {0};
    if (w) {
        const Simulator *s = w->sim;
        stats = s->stats;
        stats.pending_events = s->scheduler.count +
            s->scheduler.ticking_count - s->scheduler.ticking_head +
            s->piston_events.count - s->piston_events.head;
    }
    return stats;
}
void sim_reset_stats(World *w) { if (w) memset(&w->sim->stats, 0, sizeof(SimStats)); }
static bool near_chunk(BlockPos p, BlockPos chunk)
{
    int64_t dx = (int64_t)floor_div16(p.x) - chunk.x, dz = (int64_t)floor_div16(p.z) - chunk.z;
    return dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1;
}
bool sim_chunk_pinned(const World *w, BlockPos chunk)
{
    if (container_chunk_pinned(w, chunk)) return true;
    const Simulator *s = w->sim;
    for (size_t i = 0; i < s->scheduler.count; ++i) if (near_chunk(s->scheduler.heap[i].pos, chunk)) return true;
    for (size_t i = s->scheduler.ticking_head; i < s->scheduler.ticking_count; ++i)
        if (near_chunk(s->scheduler.ticking[i].pos, chunk)) return true;
    for (size_t i = s->piston_events.head; i < s->piston_events.count; ++i)
        if (near_chunk(s->piston_events.items[i].pos, chunk)) return true;
    const PositionQueue *queues[] = {
        &s->dirty, &s->wire_roots, &s->rail_roots, &s->input_roots
    };
    for (size_t q = 0; q < 4; ++q)
        for (size_t i = queues[q]->head; i < queues[q]->count; ++i)
            if (near_chunk(queues[q]->items[i], chunk)) return true;
    return false;
}
