#include "block_internal.h"

static uint8_t barrel_analog_output(const World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    return state.id == BLOCK_BARREL && state.data.barrel.level <= 15 ?
           state.data.barrel.level : 0;
}

const BlockDef rs_barrel_def = {
    .name = "barrel",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    /* Java Edition does not let pistons move block-entity containers. */
    .push_reaction = PUSH_BLOCK,
    .analog_output = barrel_analog_output
};
