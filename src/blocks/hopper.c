#include "block_internal.h"
#include "sim_internal.h"

static uint8_t hopper_analog(const World *world, BlockPos pos)
{
    ContainerRuntime *container = container_runtime(world, pos);
    return container ? container_level(container) : 0;
}

static void hopper_input(World *world, BlockPos pos)
{
    BlockState state = world_get(world, pos);
    if (state.id != BLOCK_HOPPER) return;
    bool enabled = power_received(world, pos, SIGNAL_ALL) == 0;
    if (state.data.hopper.enabled != enabled) {
        state.data.hopper.enabled = enabled;
        if (!world_set_block(world, pos, state, UPDATE_NO_RERENDER)) return;
    }
}

const BlockDef rs_hopper_def = {
    .name = "hopper",
    .supports_wire = true,
    .push_reaction = PUSH_BLOCK,
    .analog_output = hopper_analog,
    .neighbor_changed = hopper_input,
    .input_changed = hopper_input
};
