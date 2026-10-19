#include "block_internal.h"
#include "sim_internal.h"

static bool same_axis(Direction first, Direction second)
{
    if (!direction_horizontal(first) || !direction_horizontal(second))
        return false;
    bool first_x = first == DIR_EAST || first == DIR_WEST;
    bool second_x = second == DIR_EAST || second == DIR_WEST;
    return first_x == second_x;
}

bool block_is_rail(BlockId id)
{
    return id == BLOCK_POWERED_RAIL || id == BLOCK_ACTIVATOR_RAIL;
}

bool rail_supported(const World *world, BlockPos position)
{
    return block_supports_wire(world, block_offset(position, DIR_DOWN));
}

static int endpoint_height(BlockState state, Direction toward)
{
    return state.data.rail.ascending &&
           state.data.rail.facing == toward ? 1 : 0;
}

size_t rail_neighbors(const World *world, BlockPos position, bool same_type,
                      BlockPos output[6])
{
    BlockState state = world_get(world, position);
    if (!block_is_rail((BlockId)state.id) ||
        !direction_horizontal((Direction)state.data.rail.facing)) return 0;

    Direction forward = (Direction)state.data.rail.facing;
    Direction directions[2] = {forward, direction_opposite(forward)};
    size_t count = 0;
    for (int side = 0; side < 2; ++side) {
        Direction direction = directions[side];
        int current_height = position.y + endpoint_height(state, direction);
        BlockPos horizontal = block_offset(position, direction);
        for (int dy = -1; dy <= 1; ++dy) {
            BlockPos candidate = horizontal;
            candidate.y += dy;
            BlockState neighbor = world_get(world, candidate);
            if (!block_is_rail((BlockId)neighbor.id) ||
                (same_type && neighbor.id != state.id) ||
                !same_axis(forward, (Direction)neighbor.data.rail.facing))
                continue;
            int neighbor_height = candidate.y + endpoint_height(
                neighbor, direction_opposite(direction));
            if (current_height == neighbor_height) output[count++] = candidate;
        }
    }
    return count;
}

static bool upper_neighbor(const World *world, BlockPos position,
                           Direction direction, Direction axis)
{
    BlockPos upper = block_offset(position, direction);
    upper.y += 1;
    BlockState state = world_get(world, upper);
    return block_is_rail((BlockId)state.id) &&
           same_axis(axis, (Direction)state.data.rail.facing) &&
           endpoint_height(state, direction_opposite(direction)) == 0;
}

void rail_refresh_shape(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (!block_is_rail((BlockId)state.id)) return;
    Direction facing = (Direction)state.data.rail.facing;
    if (!direction_horizontal(facing)) return;

    Direction uphill = DIR_COUNT;
    if (upper_neighbor(world, position, facing, facing)) {
        uphill = facing;
    } else {
        Direction opposite = direction_opposite(facing);
        if (upper_neighbor(world, position, opposite, facing)) uphill = opposite;
    }

    bool ascending = uphill != DIR_COUNT;
    if (ascending) state.data.rail.facing = (uint8_t)uphill;
    if (state.data.rail.ascending == ascending &&
        (!ascending || facing == uphill)) return;
    state.data.rail.ascending = ascending;
    if (!world_write_derived(world, position, state)) return;
    sim_notify_change(world, position);
}

void rail_check_input(World *world, BlockPos position)
{
    if (block_is_rail((BlockId)world_get(world, position).id))
        sim_mark_rail(world, position);
}

static void rail_neighbor_changed(World *world, BlockPos position)
{
    if (!rail_supported(world, position)) {
        world_remove_internal(world, position);
        return;
    }
    rail_refresh_shape(world, position);
    sim_mark_rail(world, position);
}

const BlockDef rs_powered_rail_def = {
    .name = "powered_rail",
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = rail_neighbor_changed,
    .input_changed = rail_check_input
};

const BlockDef rs_activator_rail_def = {
    .name = "activator_rail",
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = rail_neighbor_changed,
    .input_changed = rail_check_input
};
