#include "block_internal.h"
#include "sim_internal.h"
#include "rail_solver.h"

typedef struct {
    World *world;
    BlockPos pos;
    BlockState state;
    /* RailState.connectTo can temporarily append a duplicate third entry. */
    BlockPos connections[3];
    unsigned count;
} RailState;

bool block_is_rail(BlockId id)
{
    return id == BLOCK_POWERED_RAIL || id == BLOCK_ACTIVATOR_RAIL;
}

bool rail_supported(const World *world, BlockPos position)
{
    return block_supports_rigid(world, block_offset(position, DIR_DOWN));
}

static bool x_axis(BlockState state)
{
    return state.data.rail.facing == DIR_EAST || state.data.rail.facing == DIR_WEST;
}

static void flat_shape(BlockState *state, bool x)
{
    state->data.rail.facing = x ? DIR_EAST : DIR_NORTH;
    state->data.rail.ascending = false;
}

static void update_connections(RailState *rail)
{
    const Direction ns[] = {DIR_NORTH, DIR_SOUTH};
    const Direction ew[] = {DIR_WEST, DIR_EAST};
    const Direction *directions = x_axis(rail->state) ? ew : ns;
    rail->count = 2;
    for (unsigned i = 0; i < 2; ++i) {
        rail->connections[i] = block_offset(rail->pos, directions[i]);
        if (rail->state.data.rail.ascending &&
            rail->state.data.rail.facing == directions[i])
            ++rail->connections[i].y;
    }
}

static bool get_rail(World *world, BlockPos pos, RailState *rail)
{
    /* RailState.getRail prefers this level, then above, then below. Shape
     * connections can join the two rail types; power propagation cannot. */
    const int heights[] = {0, 1, -1};
    for (unsigned i = 0; i < 3; ++i) {
        BlockPos candidate = pos;
        candidate.y += heights[i];
        BlockState state = world_get(world, candidate);
        if (!block_is_rail((BlockId)state.id)) continue;
        *rail = (RailState){.world = world, .pos = candidate, .state = state};
        update_connections(rail);
        return true;
    }
    return false;
}

static bool has_connection(const RailState *rail, BlockPos pos)
{
    for (unsigned i = 0; i < rail->count; ++i)
        if (rail->connections[i].x == pos.x && rail->connections[i].z == pos.z)
            return true;
    return false;
}

static void remove_soft_connections(RailState *rail)
{
    unsigned count = 0;
    for (unsigned i = 0; i < rail->count; ++i) {
        RailState other;
        if (get_rail(rail->world, rail->connections[i], &other) &&
            has_connection(&other, rail->pos))
            rail->connections[count++] = other.pos;
    }
    rail->count = count;
}

static bool can_connect(const RailState *rail, BlockPos pos)
{
    return has_connection(rail, pos) || rail->count != 2;
}

static bool has_neighbor_rail(const RailState *rail, Direction direction)
{
    RailState other;
    if (!get_rail(rail->world, block_offset(rail->pos, direction), &other))
        return false;
    remove_soft_connections(&other);
    return can_connect(&other, rail->pos);
}

static void ascending_shape(World *world, BlockPos pos, BlockState *state)
{
    if (state->data.rail.ascending) return;
    /* Match RailState's tie order: south wins over north, west over east.
     * Any rail above the adjacent cell counts, regardless of its axis/type. */
    const Direction ns[] = {DIR_NORTH, DIR_SOUTH};
    const Direction ew[] = {DIR_EAST, DIR_WEST};
    const Direction *directions = x_axis(*state) ? ew : ns;
    for (unsigned i = 0; i < 2; ++i) {
        BlockPos upper = block_offset(block_offset(pos, directions[i]), DIR_UP);
        if (block_is_rail((BlockId)world_get(world, upper).id)) {
            state->data.rail.facing = (uint8_t)directions[i];
            state->data.rail.ascending = true;
        }
    }
}

static void connect_to(RailState *rail, BlockPos pos)
{
    rail->connections[rail->count++] = pos;
    bool ew = has_connection(rail, block_offset(rail->pos, DIR_WEST)) ||
              has_connection(rail, block_offset(rail->pos, DIR_EAST));
    flat_shape(&rail->state, ew);
    ascending_shape(rail->world, rail->pos, &rail->state);
    (void)world_set_block(rail->world, rail->pos, rail->state, 3);
}

static void neighbor_changed(World *world, BlockPos pos, BlockState state)
{
    if (world->sim->error != RS_OK || world_get(world, pos).id != state.id)
        return;
    if (!rail_supported(world, pos) ||
        (state.data.rail.ascending &&
         !block_supports_rigid(world,
             block_offset(pos, (Direction)state.data.rail.facing)))) {
        world_remove_internal(world, pos);
        return;
    }
    rail_update_power(world, pos, state);
}

void rail_on_place(World *world, BlockPos pos, BlockState state)
{
    RailState rail = {.world = world, .pos = pos, .state = state};
    update_connections(&rail);
    bool north = has_neighbor_rail(&rail, DIR_NORTH);
    bool south = has_neighbor_rail(&rail, DIR_SOUTH);
    bool west = has_neighbor_rail(&rail, DIR_WEST);
    bool east = has_neighbor_rail(&rail, DIR_EAST);
    bool ns = north || south, ew = west || east;
    if (ns && !ew) flat_shape(&rail.state, false);
    else if (ew && !ns) flat_shape(&rail.state, true);
    else if (!rail.state.data.rail.ascending)
        flat_shape(&rail.state, x_axis(rail.state));
    /* With no connectable neighbors, Java falls back to the old shape only
     * after its slope checks. An unrelated upper rail must not create a slope. */
    if (ns || ew) ascending_shape(world, pos, &rail.state);
    update_connections(&rail);
    if (!world_set_block(world, pos, rail.state, 3)) return;
    for (unsigned i = 0; i < rail.count && world->sim->error == RS_OK; ++i) {
        RailState other;
        if (!get_rail(world, rail.connections[i], &other)) continue;
        remove_soft_connections(&other);
        if (can_connect(&other, pos)) connect_to(&other, pos);
    }
    /* BaseRailBlock.updateState samples the state returned by updateDir.
     * Subsequent neighbor notifications never recalculate the rail shape. */
    neighbor_changed(world, pos, rail.state);
}

void rail_check_input(World *world, BlockPos pos)
{
    BlockState state = world_get(world, pos);
    if (block_is_rail((BlockId)state.id)) neighbor_changed(world, pos, state);
}

const BlockDef rs_powered_rail_def = {
    .name = "powered_rail",
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = rail_check_input,
    .input_changed = rail_check_input
};

const BlockDef rs_activator_rail_def = {
    .name = "activator_rail",
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = rail_check_input,
    .input_changed = rail_check_input
};
