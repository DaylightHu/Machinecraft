#ifndef RS_WORLD_INTERNAL_H
#define RS_WORLD_INTERNAL_H

#include "rs/world.h"

struct World {
    WorldConfig config;
    size_t volume;
    BlockState *cells;
    uint64_t tick, revision;
};

uint32_t world_index(const World *world, BlockPos position);

#endif
