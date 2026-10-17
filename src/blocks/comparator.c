#include "block_internal.h"
#include "sim_internal.h"

enum { COMPARATOR_DELAY = 2 };

bool comparator_supported(const World *world, BlockPos position)
{
    return block_supports_wire(world, block_offset(position, DIR_DOWN));
}

static bool valid_facing(Direction facing)
{
    return direction_horizontal(facing);
}

static uint8_t comparator_rear_input(const World *world, BlockPos position,
                                     BlockState state)
{
    Direction facing = (Direction)state.data.comparator.facing;
    if (!valid_facing(facing)) return 0;
    BlockPos source = block_offset(position, direction_opposite(facing));
    return power_signal(world, source, facing, SIGNAL_ALL);
}

static uint8_t comparator_side_input(const World *world, BlockPos position,
                                     BlockState state)
{
    Direction facing = (Direction)state.data.comparator.facing;
    if (!valid_facing(facing)) return 0;
    const Direction *sides;
    static const Direction east_west_sides[] = {DIR_NORTH, DIR_SOUTH};
    static const Direction north_south_sides[] = {DIR_EAST, DIR_WEST};
    sides = (facing == DIR_EAST || facing == DIR_WEST) ?
            east_west_sides : north_south_sides;

    uint8_t best = 0;
    for (int index = 0; index < 2; ++index) {
        Direction side = sides[index];
        BlockPos source = block_offset(position, side);
        BlockId source_id = (BlockId)world_get(world, source).id;
        /* Vanilla side inputs are deliberately narrower than rear input:
         * dust, a redstone block, or a diode aimed into this comparator. */
        if (source_id != BLOCK_REDSTONE_WIRE &&
            source_id != BLOCK_REDSTONE_BLOCK &&
            source_id != BLOCK_REDSTONE_REPEATER &&
            source_id != BLOCK_REDSTONE_COMPARATOR) continue;
        uint8_t signal = power_signal(world, source,
                                      direction_opposite(side), SIGNAL_ALL);
        if (signal > best) best = signal;
    }
    return best;
}

static uint8_t comparator_desired_output(const World *world, BlockPos position,
                                         BlockState state)
{
    uint8_t rear = comparator_rear_input(world, position, state);
    uint8_t side = comparator_side_input(world, position, state);
    if (state.data.comparator.subtract)
        return rear > side ? (uint8_t)(rear - side) : 0;
    return rear >= side ? rear : 0;
}

static uint8_t comparator_signal(const World *world, BlockPos position,
                                 Direction face, SignalMode mode)
{
    (void)mode;
    BlockState state = world_get(world, position);
    return face == (Direction)state.data.comparator.facing ?
           state.data.comparator.output : 0;
}

void comparator_check_input(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (state.id != BLOCK_REDSTONE_COMPARATOR) return;
    if (state.data.comparator.output !=
        comparator_desired_output(world, position, state))
        sim_schedule(world, position, COMPARATOR_DELAY);
}

static void comparator_neighbor(World *world, BlockPos position)
{
    if (!comparator_supported(world, position)) {
        world_remove_internal(world, position);
        return;
    }
    sim_mark_input(world, position);
}

static TickAction comparator_tick(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    TickAction action = {state, false, 0};
    uint8_t output = comparator_desired_output(world, position, state);
    if (state.data.comparator.output != output) {
        action.state.data.comparator.output = output;
        action.changed = true;
    }
    return action;
}

const BlockDef rs_comparator_def = {
    .name = "redstone_comparator",
    .diode = true,
    .signal = comparator_signal,
    .direct_signal = comparator_signal,
    .neighbor_changed = comparator_neighbor,
    .input_changed = comparator_check_input,
    .scheduled_tick = comparator_tick
};
