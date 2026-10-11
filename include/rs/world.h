#ifndef RS_WORLD_H
#define RS_WORLD_H

#include <stddef.h>
#include <stdint.h>

#include "rs/block.h"

typedef struct {
    int size_x, size_y, size_z, ground_layers;
} WorldConfig;

typedef struct World World;

WorldConfig world_config_default(void);
World *world_create(WorldConfig config);
void world_destroy(World *world);
bool world_in_bounds(const World *world, BlockPos position);
BlockState world_get(const World *world, BlockPos position);
size_t world_volume(const World *world);
bool world_set(World *world, BlockPos position, BlockState state);

#endif
