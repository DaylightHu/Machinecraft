#include "sim_internal.h"
#include "wire_solver.h"

#include <stdlib.h>
#include <string.h>

enum{NOTIFICATION_RADIUS = 3};

bool sim_init_internal(World *world)
{
    Simulator *sim = calloc(1, sizeof(*sim));
    if (!sim){
        return false;
    }
    world->sim = sim;
    sim->free_torch = RS_NONE;
    size_t = volume = world->volume;

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


static void flush_neighbors(World *world)
{
    uint32_t index;
    while (queue_pop(&world->sim->dirty, &index)){
        const BlockDef *definition = block_definition((BlockId)world->cells[index].id);
        if (definition->neighbor_changed){
            ++world->sim->stats.neighbor_checks;
            definition->neighbor_changed(world, world_position(world, index));
        }
    }
}

static void flush_inputs(World *world)
{
    Simulator *sim = world->sim;
    for (size_t cursor = 0; cursor < sim->input_count; ++cursor){
        uint32_t index = sim->input_roots[cursor];
        sim->root_flags[index] &= (uint8_t)~ROOT_INPUT;
        const BlockDef *definition = block_definition((BlockId)world->cells[index].id);
        if(definition->input_changed){
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
