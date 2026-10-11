#ifndef RS_WORLD_H
#define RS_WORLD_H
#include <stddef.h>
#include "block.h"

typedef struct World World;
typedef enum {
    RS_OK = 0, RS_UNCHANGED, RS_OUT_OF_BOUNDS, RS_UNSUPPORTED,
    RS_INVALID_ARGUMENT, RS_OUT_OF_MEMORY, RS_INTERNAL_ERROR
} RsResult;
typedef struct { int size_x, size_y, size_z, ground_layers; } WorldConfig;

/* Fixed iron ground, no RNG, no seed, no immovable bedrock. */
WorldConfig world_config_default(void);
World *world_create(WorldConfig config);
void world_destroy(World *world);
bool world_in_bounds(const World *world, BlockPos pos);
BlockState world_get(const World *world, BlockPos pos); /* Outside = air. */
RsResult world_place(World *world, BlockPos pos, BlockId id, Direction support_dir);
RsResult world_remove(World *world, BlockPos pos);
RsResult world_toggle_wire_dot(World *world, BlockPos pos);
uint64_t world_tick(const World *world);
uint64_t world_revision(const World *world);
size_t world_volume(const World *world);
const char *rs_result_name(RsResult result);
#endif
