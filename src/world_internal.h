#ifndef RS_WORLD_INTERNAL_H
#define RS_WORLD_INTERNAL_H
#include "rs/world.h"
#include "chunk.h"
#include "posmap.h"
typedef struct Simulator Simulator;
enum { WORLD_READ_CACHE_SIZE = 64 };
typedef struct {
    BlockPos pos;
    BlockState state;
    uint64_t stamp;
} WorldReadEntry;
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
    uint64_t read_stamp;
    WorldReadEntry reads[WORLD_READ_CACHE_SIZE];
};
bool world_write_derived(World *world, BlockPos pos, BlockState state);
enum { UPDATE_NEIGHBORS = 1, UPDATE_CLIENTS = 2, UPDATE_KNOWN_SHAPE = 16,
       UPDATE_MOVING = 64, UPDATE_SHAPE_LIMIT = 512 };
bool world_set_block(World *, BlockPos, BlockState, unsigned flags);
bool world_set_block_depth(World *, BlockPos, BlockState, unsigned flags,
                           unsigned remaining_shapes);
/* Writes a stored/rendered state which does not exist in vanilla's observable
 * block state. Used for cosmetic input indicators without firing observers. */
bool world_write_visual(World *world, BlockPos pos, BlockState state);
void world_remove_internal(World *world, BlockPos pos);
Chunk *world_find_chunk(const World *world, BlockPos key);
Chunk *world_ensure_chunk(World *world, BlockPos key);
bool world_insert_chunk(World *world, Chunk *chunk);
bool world_chunk_snapshot(World *world, Chunk *chunk, ByteBuffer *bytes);
bool world_archive_chunk(World *world, Chunk *chunk);
#endif
