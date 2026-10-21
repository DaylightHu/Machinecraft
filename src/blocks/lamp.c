#include "block_internal.h"
#include "sim_internal.h"

enum { LAMP_OFF_DELAY = 4 };

static void lamp_input(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (state.id != BLOCK_REDSTONE_LAMP) return;
    bool powered = power_received(world, position, SIGNAL_ALL) > 0;
    if (state.data.lamp.lit == powered) return;
    if (state.data.lamp.lit) {
        /* Keep the first pending check, including across brief power returns. */
        sim_schedule(world, position, LAMP_OFF_DELAY);
    } else {
        state.data.lamp.lit = true;
        /* Vanilla flags=2 updates shapes/observers without neighborChanged. */
        (void)world_write_derived(world, position, state);
    }
}

static TickAction lamp_tick(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (state.data.lamp.lit &&
        power_received(world, position, SIGNAL_ALL) == 0) {
        state.data.lamp.lit = false;
        /* Write here so sim_step does not broadcast a power/topology change. */
        (void)world_write_derived(world, position, state);
    }
    return (TickAction){state, false, 0};
}

const BlockDef rs_lamp_def = {
    .name = "redstone_lamp",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = lamp_input,
    .input_changed = lamp_input,
    .scheduled_tick = lamp_tick
};
