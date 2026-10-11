#include "block_internal.h"
#include "sim_internal.h"

#define FACE_BIT(face) ((uint8_t)(1u << (unsigned)(face)))
#define X_CONNECTIONS ((uint8_t)(FACE_BIT(DIR_EAST) | FACE_BIT(DIR_WEST)))
#define Z_CONNECTIONS ((uint8_t)(FACE_BIT(DIR_SOUTH) | FACE_BIT(DIR_NORTH)))
#define HORIZONTAL_CONNECTIONS ((uint8_t)(X_CONNECTIONS | Z_CONNECTIONS))

static const Direction horizontal_faces[] = {DIR_EAST, DIR_WEST, DIR_SOUTH, DIR_NORTH};

static bool is_wire(const World *world, BlockPos position)
{
    return world_get(world, position).id == BLOCK_REDSTONE_WIRE;
}

static bool connects(const World *world, BlockPos position)
{
    uint8_t id = world_get(world, position).id;
    return id == BLOCK_REDSTONE_WIRE || id == BLOCK_REDSTONE_TORCH || id == BLOCK_REDSTONE_BLOCK;
}

bool wire_supported(const World *world, BlockPos position)
{
    return block_supports_wire(world, block_offset(position, DIR_DOWN));
}

size_t wire_neighbors(const World *world, BlockPos position, uint32_t output[12])
{
    size_t count = 0;
    bool can_ascend = !block_is_conductor(world, block_offset(position, DIR_UP));

    for (size_t cursor = 0; cursor < 4; ++cursor) {
        BlockPos neighbor = block_offset(position, horizontal_faces[cursor]);
        if (is_wire(world, neighbor)) {
            output[count++] = world_index(world, neighbor);
        }
        BlockPos above = block_offset(neighbor, DIR_UP);
        BlockPos below = block_offset(neighbor, DIR_DOWN);
        if (can_ascend && block_supports_wire(world, neighbor) && is_wire(world, above)) {
            output[count++] = world_index(world, above);
        }
        if (!block_is_conductor(world, neighbor) && is_wire(world, below)) {
            output[count++] = world_index(world, below);
        }
    }
    return count;
}

void wire_refresh_shape(World *world, BlockPos position)
{
    uint32_t index = world_index(world, position);
    BlockState state = world->cells[index];
    uint8_t connections = 0;
    uint8_t ascending = 0;
    bool can_ascend = !block_is_conductor(world, block_offset(position, DIR_UP));

    for (size_t cursor = 0; cursor < 4; ++cursor) {
        Direction face = horizontal_faces[cursor];
        BlockPos neighbor = block_offset(position, face);
        if (can_ascend && block_supports_wire(world, neighbor) &&
            is_wire(world, block_offset(neighbor, DIR_UP))) {
            connections |= FACE_BIT(face);
            ascending |= FACE_BIT(face);
        } else if (connects(world, neighbor) ||
                   (!block_is_conductor(world, neighbor) &&
                    is_wire(world, block_offset(neighbor, DIR_DOWN)))) {
            connections |= FACE_BIT(face);
        }
    }

    if (connections || !state.data.wire.prefer_dot) {
        bool no_x_connection = !(connections & X_CONNECTIONS);
        bool no_z_connection = !(connections & Z_CONNECTIONS);
        if (no_z_connection) {
            connections |= X_CONNECTIONS;
        }
        if (no_x_connection) {
            connections |= Z_CONNECTIONS;
        }
    }
    connections &= HORIZONTAL_CONNECTIONS;
    if (connections != state.data.wire.connections || ascending != state.data.wire.up_connections) {
        state.data.wire.connections = connections;
        state.data.wire.up_connections = ascending;
        world_write_derived(world, index, state);
        sim_notify_power(world, position);
    }
}

uint8_t wire_external_power(const World *world, BlockPos position)
{
    /* Suppress wire output throughout solid-block queries to prevent feedback.
     * Wire-to-wire transfer belongs exclusively to the explicit connection graph. */
    return power_received(world, position, SIGNAL_WITHOUT_WIRE);
}

static uint8_t wire_signal(const World *world, BlockPos position,
                           Direction face, SignalMode mode)
{
    if (mode == SIGNAL_WITHOUT_WIRE || face == DIR_UP) {
        return 0;
    }
    BlockState state = world_get(world, position);
    bool connected = face == DIR_DOWN || (state.data.wire.connections & FACE_BIT(face));
    return connected ? state.data.wire.power : 0;
}

static void wire_neighbor(World *world, BlockPos position)
{
    if (!wire_supported(world, position)) {
        world_remove_internal(world, world_index(world, position));
        return;
    }
    wire_refresh_shape(world, position);
    sim_mark_wire(world, world_index(world, position));
}

/* Both query forms are suppressed in SIGNAL_WITHOUT_WIRE mode. */
const BlockDef rs_wire_def = {
    .name = "redstone_wire",
    .signal = wire_signal,
    .direct_signal = wire_signal,
    .neighbor_changed = wire_neighbor
};
