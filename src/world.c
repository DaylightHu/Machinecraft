#include "world_internal.h"
#include "worldgen.h"

#include <stdlib.h>

WorldConfig world_config_default(void){
    return (WorldConfig){32, 16, 32, 1};
}

static bool calculate_volume(WorldConfig config, size_t *volume){
    if (config.size_x<=0 || config.size_y<=0 || config.size_z<=0 ||
            config.ground_layers<0 || config.ground_layers > config.size_y){
        return false;
    }

    size_t cells = (size_t)config.size_x;
    if (cells > SIZE_MAX / (size_t)config.size_y){
        return false;
    }

    cells *= (size_t)config.size_y;
    if (cells > SIZE_MAX / (size_t)config.size_z){
        return false;
    }

    cells *= (size_t)config.size_z;
    if (cells >= UINT32_MAX || cells > SIZE_MAX / sizeof(BlockState)){
        return false;
    }
    *volume = cells;
    return true;
}


World *world_create(WorldConfig config){
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
    
    if (!world->cells){
        world_destroy(world);
        return NULL;
    }
    
    world_generate_flat(world);
    return world;
}

void world_destroy(World *world){
    if (!world){
        return;
    }
    free(world->cells);
    free(world);
}

uint32_t world_index(const World *world, BlockPos position)
{
    return (uint32_t)(((size_t)position.y * (size_t)world->config.size_z +
                       (size_t)position.z) * (size_t)world->config.size_x +
                      (size_t)position.x);
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

bool world_set(World *world, BlockPos position, BlockState state)
{
    if (!world_in_bounds(world, position) || state.id >= BLOCK_COUNT){
        return false;
    }

    if (state.id == BLOCK_REDSTONE_DUST && state.data.dust.power > 15){
        return false;
    }
    world->cells[world_index(world, position)] = state;
    return true;
}

size_t world_volume(const World *world)
{
    return world ? world->volume : 0;
}
