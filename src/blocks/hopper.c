#include "block_internal.h"
#include "sim_internal.h"

static uint8_t hopper_analog(const World *world, BlockPos pos)
{
    return world_get(world, pos).data.hopper.level;
}

static void hopper_input(World *world, BlockPos pos)
{
    BlockState state = world_get(world, pos);
    if (state.id != BLOCK_HOPPER) return;
    bool enabled = power_received(world, pos, SIGNAL_ALL) == 0;
    if (state.data.hopper.enabled != enabled) {
        state.data.hopper.enabled = enabled;
        if (!world_write_derived(world, pos, state)) return;
    }
    container_sync_hopper(world, pos, state);
}

const BlockDef rs_hopper_def = {
    .name = "hopper",
    .supports_wire = true,
    .push_reaction = PUSH_BLOCK,
    .analog_output = hopper_analog,
    .neighbor_changed = hopper_input,
    .input_changed = hopper_input
};
