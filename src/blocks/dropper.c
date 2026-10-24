#include "block_internal.h"
#include "sim_internal.h"

static uint8_t dropper_analog(const World *world, BlockPos pos)
{
    ContainerRuntime *container = container_runtime(world, pos);
    return container ? container_level(container) : 0;
}

static void dropper_input(World *world, BlockPos pos)
{
    BlockState state = world_get(world, pos);
    if (state.id != BLOCK_DROPPER) return;
    bool powered = power_received(world, pos, SIGNAL_ALL) > 0 ||
                   power_received(world, block_offset(pos, DIR_UP), SIGNAL_ALL) > 0;
    if (powered == state.data.dropper.triggered) return;
    if (powered) sim_schedule(world, pos, 4);
    state.data.dropper.triggered = powered;
    /* Vanilla's flags=4 changes the latch without notifying neighborChanged.
     * Falling edges leave an already scheduled dispense in the tick queue. */
    (void)world_write_derived(world, pos, state);
}

static void dropper_neighbor(World *world, BlockPos pos)
{
    sim_mark_input(world, pos);
}

static TickAction dropper_tick(World *world, BlockPos pos)
{
    container_dispense(world, pos);
    return (TickAction){world_get(world, pos), false, 0};
}

const BlockDef rs_dropper_def = {
    .name = "dropper",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    .push_reaction = PUSH_BLOCK,
    .analog_output = dropper_analog,
    .neighbor_changed = dropper_neighbor,
    .input_changed = dropper_input,
    .scheduled_tick = dropper_tick
};
