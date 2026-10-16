#ifndef RS_WORLD_INTERNAL_H
#define RS_WORLD_INTERNAL_H
#include "rs/world.h"
#include "chunk.h"
#include "posmap.h"
typedef struct Simulator Simulator;
struct World {
    WorldConfig config;
    PosMap chunk_map;
    Chunk **chunks;
    size_t chunk_count, chunk_capacity, loaded_count;
    FILE *backing;
    BlockPos view_center;
    int view_radius;
    uint64_t tick, revision, topology_revision;
    Simulator *sim;
    WorldChangeObserver observer;
    void *observer_context;
};
bool world_write_derived(World *world, BlockPos pos, BlockState state);
void world_remove_internal(World *world, BlockPos pos);
Chunk *world_find_chunk(const World *world, BlockPos key);
Chunk *world_ensure_chunk(World *world, BlockPos key);
bool world_insert_chunk(World *world, Chunk *chunk);
bool world_chunk_snapshot(World *world, Chunk *chunk, ByteBuffer *bytes);
bool world_archive_chunk(World *world, Chunk *chunk);
#endif
