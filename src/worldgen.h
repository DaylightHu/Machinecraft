#ifndef RS_WORLDGEN_H
#define RS_WORLDGEN_H
#include "world_internal.h"
bool world_generate_chunk(const World *world, Chunk *chunk);
BlockState world_generated_state(const World *world, BlockPos position);
#endif
