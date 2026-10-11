#ifndef RS_WORLD_INTERNAL_H
#define RS_WORLD_INTERNAL_H
#include "rs/world.h"
typedef struct Simulator Simulator;
struct World {
    WorldConfig config;
    size_t volume;
    BlockState *cells;
    uint64_t tick, revision;
    Simulator *sim;
};
uint32_t world_index(const World *world, BlockPos pos);
BlockPos world_position(const World *world, uint32_t index);
void world_write_derived(World *world, uint32_t index, BlockState state);
void world_remove_internal(World *world, uint32_t index);
#endif
