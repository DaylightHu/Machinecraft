#ifndef RS_WORLD_H
#define RS_WORLD_H

#include <stddef.h>
#include <stdint.h>

#include "rs/block.h"

typedef struct {
    int size_x, size_y, size_z, ground_layers;
} WorldConfig;

typedef struct World{
    WorldConfig config;
    size_t volume;
    BlockState *cells;
    uint64_t tick, revision;
} World;

WorldConfig world_config_default(void);
World *world_create(WorldConfig, config);
void world_destroy(World *world);


#endif
