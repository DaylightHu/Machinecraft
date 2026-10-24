#include "block_internal.h"
#include "sim_internal.h"
#include <limits.h>

RsResult world_open_barrel(World *world, BlockPos position)
{
    if (!world) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(world, position)) return RS_OUT_OF_BOUNDS;
    RsResult result = sim_flush(world);
    if (result != RS_OK) return result;
    ContainerRuntime *container = container_runtime(world, position);
    if (!container || container->id != BLOCK_BARREL) return RS_INVALID_ARGUMENT;
    if (container->openers == UINT_MAX) return RS_INTERNAL_ERROR;
    ++container->openers;
    BlockState state = world_get(world, position);
    if (!state.data.barrel.open) {
        state.data.barrel.open = true;
        if (!world_set_block(world, position, state, 3)) return world->sim->error;
    }
    sim_schedule(world, position, 5);
    return world->sim->error;
}

RsResult world_close_barrel(World *world, BlockPos position)
{
    if (!world) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(world, position)) return RS_OUT_OF_BOUNDS;
    RsResult result = sim_flush(world);
    if (result != RS_OK) return result;
    ContainerRuntime *container = container_runtime(world, position);
    if (!container || container->id != BLOCK_BARREL) return RS_INVALID_ARGUMENT;
    if (!container->openers) return RS_UNCHANGED;
    --container->openers;
    return RS_OK;
}

static TickAction barrel_tick(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    ContainerRuntime *container = container_runtime(world, position);
    if (container && container->openers)
        return (TickAction){state, false, 5};
    if (state.data.barrel.open) {
        state.data.barrel.open = false;
        (void)world_set_block(world, position, state, 3);
    }
    return (TickAction){state, false, 0};
}

static uint8_t barrel_analog_output(const World *world, BlockPos position)
{
    ContainerRuntime *container = container_runtime(world, position);
    return container ? container_level(container) : 0;
}

const BlockDef rs_barrel_def = {
    .name = "barrel",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    /* Java Edition does not let pistons move block-entity containers. */
    .push_reaction = PUSH_BLOCK,
    .analog_output = barrel_analog_output,
    .scheduled_tick = barrel_tick
};
