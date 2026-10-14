#include "world_internal.h"
#include "sim_internal.h"
#include "worldgen.h"

#include <stdlib.h>

WorldConfig world_config_default(void)
{
    return (WorldConfig){32, 16, 32, 1};
}

uint32_t world_index(const World *world, BlockPos position)
{
    return (uint32_t)(((size_t)position.y * (size_t)world->config.size_z +
                      (size_t)position.z) * (size_t)world->config.size_x +
                      (size_t)position.x);
}

BlockPos world_position(const World *world, uint32_t index)
{
    BlockPos position;
    position.x = (int)(index % (uint32_t)world->config.size_x);
    index /= (uint32_t)world->config.size_x;
    position.z = (int)(index % (uint32_t)world->config.size_z);
    position.y = (int)(index / (uint32_t)world->config.size_z);
    return position;
}

static bool calculate_volume(WorldConfig config, size_t *volume)
{
    if (config.size_x <= 0 || config.size_y <= 0 || config.size_z <= 0 ||
        config.ground_layers < 0 || config.ground_layers > config.size_y) {
        return false;
    }

    size_t cells = (size_t)config.size_x;
    if (cells > SIZE_MAX / (size_t)config.size_y) {
        return false;
    }
    cells *= (size_t)config.size_y;
    if (cells > SIZE_MAX / (size_t)config.size_z) {
        return false;
    }
    cells *= (size_t)config.size_z;
    if (cells >= UINT32_MAX || cells > SIZE_MAX / sizeof(BlockState) ||
        cells > SIZE_MAX / sizeof(ScheduledEvent)) {
        return false;
    }
    *volume = cells;
    return true;
}

World *world_create(WorldConfig config)
{
    size_t volume;
    if (!calculate_volume(config, &volume)) {
        return NULL;
    }

    World *world = calloc(1, sizeof(*world));
    if (!world) {
        return NULL;
    }
    world->config = config;
    world->volume = volume;
    world->cells = calloc(volume, sizeof(*world->cells));
    if (!world->cells || !sim_init_internal(world)) {
        world_destroy(world);
        return NULL;
    }

    world_generate_flat(world);
    return world;
}

void world_destroy(World *world)
{
    if (!world) {
        return;
    }
    sim_destroy_internal(world);
    free(world->cells);
    free(world);
}

bool world_in_bounds(const World *world, BlockPos position)
{
    return world &&
           position.x >= 0 && position.x < world->config.size_x &&
           position.y >= 0 && position.y < world->config.size_y &&
           position.z >= 0 && position.z < world->config.size_z;
}

BlockState world_get(const World *world, BlockPos position)
{
    if (!world_in_bounds(world, position)) {
        return block_default(BLOCK_AIR);
    }
    return world->cells[world_index(world, position)];
}

void world_write_derived(World *world, uint32_t index, BlockState state)
{
    BlockState previous = world->cells[index];
    world->cells[index] = state;
    ++world->revision;
    if (world->observer)
        world->observer(world->observer_context, world_position(world, index),
                        previous, state);
}

void world_remove_internal(World *world, uint32_t index)
{
    if (world->cells[index].id == BLOCK_AIR) {
        return;
    }
    sim_forget_cell(world, index);
    world_write_derived(world, index, block_default(BLOCK_AIR));
    sim_notify_change(world, world_position(world, index));
}

RsResult world_place(World *world, BlockPos position, BlockId id, Direction support)
{
    if (!world) {
        return RS_INVALID_ARGUMENT;
    }
    if (!world_in_bounds(world, position)) {
        return RS_OUT_OF_BOUNDS;
    }
    if (id <= BLOCK_AIR || id >= BLOCK_COUNT) {
        return RS_INVALID_ARGUMENT;
    }
    if (id == BLOCK_REDSTONE_WIRE && !wire_supported(world, position)) {
        return RS_UNSUPPORTED;
    }
    if (id == BLOCK_REDSTONE_TORCH) {
        if (support != DIR_DOWN && !direction_horizontal(support)) {
            return RS_INVALID_ARGUMENT;
        }
        if (!torch_supported(world, position, support)) {
            return RS_UNSUPPORTED;
        }
    }

    uint32_t index = world_index(world, position);
    BlockState previous = world->cells[index];
    if (previous.id == id &&
        (id != BLOCK_REDSTONE_TORCH || previous.data.torch.support_dir == support)) {
        return RS_UNCHANGED;
    }
    if (id == BLOCK_REDSTONE_TORCH) {
        RsResult result = sim_prepare_torch(world);
        if (result != RS_OK) {
            return result;
        }
    }

    sim_forget_cell(world, index);
    BlockState state = block_default(id);
    if (id == BLOCK_REDSTONE_TORCH) {
        state.data.torch.support_dir = (uint8_t)support;
    }
    world_write_derived(world, index, state);
    if (id == BLOCK_REDSTONE_TORCH) {
        sim_attach_torch(world, index);
    }
    sim_notify_change(world, position);
    return world->sim->error;
}

RsResult world_remove(World *world, BlockPos position)
{
    if (!world) {
        return RS_INVALID_ARGUMENT;
    }
    if (!world_in_bounds(world, position)) {
        return RS_OUT_OF_BOUNDS;
    }
    uint32_t index = world_index(world, position);
    if (world->cells[index].id == BLOCK_AIR) {
        return RS_UNCHANGED;
    }
    world_remove_internal(world, index);
    return world->sim->error;
}

RsResult world_toggle_wire_dot(World *world, BlockPos position)
{
    if (!world) {
        return RS_INVALID_ARGUMENT;
    }
    if (!world_in_bounds(world, position)) {
        return RS_OUT_OF_BOUNDS;
    }
    RsResult result = sim_flush(world);
    if (result != RS_OK) {
        return result;
    }
    uint32_t index = world_index(world, position);
    BlockState state = world->cells[index];
    if (state.id != BLOCK_REDSTONE_WIRE) {
        return RS_INVALID_ARGUMENT;
    }
    state.data.wire.prefer_dot = !state.data.wire.prefer_dot;
    world_write_derived(world, index, state);
    sim_notify_change(world, position);
    return world->sim->error;
}

uint64_t world_tick(const World *world)
{
    return world ? world->tick : 0;
}

uint64_t world_revision(const World *world)
{
    return world ? world->revision : 0;
}

size_t world_volume(const World *world)
{
    return world ? world->volume : 0;
}

void world_set_change_observer(World *world, WorldChangeObserver observer,
                               void *context)
{
    if (!world) return;
    world->observer = observer;
    world->observer_context = observer ? context : NULL;
}

const char *rs_result_name(RsResult result)
{
    static const char *const names[] = {
        "ok", "unchanged", "out of bounds", "unsupported", "invalid argument",
        "out of memory", "internal error"
    };
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown";
}
