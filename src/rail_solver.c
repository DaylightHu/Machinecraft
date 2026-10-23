#include "rail_solver.h"
#include "sim_internal.h"

enum { RAIL_RANGE = 8 };

static bool find_power(const World *, BlockPos, BlockState, bool, unsigned);

static bool same_rail_with_power(const World *world, BlockPos pos, BlockId kind,
                                  bool x_axis, bool forward, unsigned depth)
{
    BlockState state = world_get(world, pos);
    if (state.id != kind ||
        (state.data.rail.facing/2 == DIR_EAST/2) != x_axis ||
        !state.data.rail.powered)
        return false;
    /* Vanilla follows only already-powered rails. A graph reachability
     * solution would erase the intermediate states seen by other blocks. */
    return power_received(world, pos, SIGNAL_ALL) > 0 ||
           find_power(world, pos, state, forward, depth + 1);
}

static bool find_power(const World *world, BlockPos pos, BlockState state,
                        bool forward, unsigned depth)
{
    if (depth >= RAIL_RANGE) return false;
    bool x_axis = state.data.rail.facing/2 == DIR_EAST/2;
    Direction direction = x_axis ? (forward ? DIR_WEST : DIR_EAST) :
                                  (forward ? DIR_SOUTH : DIR_NORTH);
    BlockPos next = block_offset(pos, direction);
    bool may_descend = true;
    if (state.data.rail.ascending && state.data.rail.facing == direction) {
        ++next.y;
        may_descend = false;
    }
    if (same_rail_with_power(world, next, (BlockId)state.id,
                             x_axis, forward, depth))
        return true;
    return may_descend &&
           same_rail_with_power(world, block_offset(next, DIR_DOWN),
                                 (BlockId)state.id, x_axis, forward, depth);
}

void rail_update_power(World *world, BlockPos pos, BlockState state)
{
    ++world->sim->stats.rails_visited;
    bool powered = power_received(world, pos, SIGNAL_ALL) > 0 ||
                   find_power(world, pos, state, true, 0) ||
                   find_power(world, pos, state, false, 0);
    if (state.data.rail.powered == powered) return;
    state.data.rail.powered = powered;
    if (!world_set_block(world, pos, state, UPDATE_NEIGHBORS | UPDATE_CLIENTS))
        return;
    ++world->sim->stats.rail_changes;
    sim_update_neighbors_at(world, block_offset(pos, DIR_DOWN), DIR_COUNT);
    if (state.data.rail.ascending)
        sim_update_neighbors_at(world, block_offset(pos, DIR_UP), DIR_COUNT);
}
