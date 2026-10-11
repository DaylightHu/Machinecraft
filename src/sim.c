#include "sim.h"

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
