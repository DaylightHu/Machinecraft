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
    Direction rear = direction_opposite(facing);
    BlockPos source = block_offset(position, rear);
    uint8_t signal = power_signal(world, source, facing, SIGNAL_ALL);

    if (block_has_analog_output(world, source))
        return block_analog_output(world, source);

    /* Vanilla comparators can read a container through one conducting block,
     * provided the direct rear signal has not already reached 15. */
    if (signal < 15 && block_is_conductor(world, source)) {
        BlockPos second = block_offset(source, rear);
        if (block_has_analog_output(world, second))
            return block_analog_output(world, second);
    }
    return signal;
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
        BlockState source_state = world_get(world, source);
        BlockId source_id = (BlockId)source_state.id;
        uint8_t signal = 0;
        if (source_id == BLOCK_REDSTONE_BLOCK) {
            signal = 15;
        } else if (source_id == BLOCK_REDSTONE_WIRE) {
            /* Diode side input reads the dust level, independent of its
             * rendered connection shape. */
            signal = source_state.data.wire.power;
        } else if (block_is_signal_source(world, source)) {
            signal = power_direct(world, source,
                                  direction_opposite(side), SIGNAL_ALL);
        }
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

static bool comparator_should_prioritize(const World *world,
                                         BlockPos position, BlockState state)
{
    Direction output = (Direction)state.data.comparator.facing;
    BlockState next = world_get(world, block_offset(position, output));
    Direction next_output;
    if (next.id == BLOCK_REDSTONE_REPEATER) {
        next_output = (Direction)next.data.repeater.facing;
    } else if (next.id == BLOCK_REDSTONE_COMPARATOR) {
        next_output = (Direction)next.data.comparator.facing;
    } else {
        return false;
    }
    return next_output != direction_opposite(output);
}

void comparator_check_input(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (state.id != BLOCK_REDSTONE_COMPARATOR) return;
    if (state.data.comparator.output !=
        comparator_desired_output(world, position, state)) {
        TickPriority priority = comparator_should_prioritize(
            world, position, state) ? TICK_HIGH : TICK_NORMAL;
        sim_schedule_priority(world, position, COMPARATOR_DELAY, priority);
    }
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
    .signal_source = true,
    .diode = true,
    .push_reaction = PUSH_DESTROY,
    .signal = comparator_signal,
    .direct_signal = comparator_signal,
    .neighbor_changed = comparator_neighbor,
    .input_changed = comparator_check_input,
    .scheduled_tick = comparator_tick
};
