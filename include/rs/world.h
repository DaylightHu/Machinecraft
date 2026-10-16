#ifndef RS_WORLD_H
#define RS_WORLD_H
#include <stddef.h>
#include "block.h"

typedef struct World World;
/* One optional observer, called after a cell is written. It must not edit the
 * world or retain pointers into it. Passing NULL detaches the observer. */
typedef void (*WorldChangeObserver)(void *context, BlockPos pos,
                                    BlockState previous, BlockState current);
typedef enum {
    RS_OK = 0, RS_UNCHANGED, RS_OUT_OF_BOUNDS, RS_UNSUPPORTED,
    RS_INVALID_ARGUMENT, RS_OUT_OF_MEMORY, RS_INTERNAL_ERROR
} RsResult;
enum { CHUNK_SIZE = 16, SECTION_VOLUME = 4096, WORLD_MIN_Y = -64,
       WORLD_MAX_Y = 319, SECTION_COUNT = 24, STATE_ID_LIMIT = 32768 };
typedef struct { int ground_layers; } WorldConfig;
typedef void (*WorldSectionVisitor)(void *context, BlockPos origin);

/* Fixed iron ground, no RNG, no seed, no immovable bedrock. */
WorldConfig world_config_default(void);
World *world_create(WorldConfig config);
void world_destroy(World *world);
bool world_in_bounds(const World *world, BlockPos pos);
BlockState world_get(const World *world, BlockPos pos); /* Outside vertical/coordinate limits = air; reads may restore archived chunks. */
RsResult world_place(World *world, BlockPos pos, BlockId id, Direction support_dir);
RsResult world_remove(World *world, BlockPos pos);
RsResult world_toggle_wire_dot(World *world, BlockPos pos);
uint64_t world_tick(const World *world);
uint64_t world_revision(const World *world);
/* Loaded block capacity, not the extent of the horizontal world. */
size_t world_volume(const World *world);
size_t world_loaded_chunks(const World *world);
size_t world_stored_chunks(const World *world);
/* Stream a square window; pending simulation work pins its surrounding chunks.
 * Inactive edits are spilled to a temporary backing file, then saved in .mcw. */
RsResult world_stream(World *world, BlockPos focus, int radius);
/* Visit nonempty loaded sections within the current view window only. */
void world_visit_sections(const World *world, WorldSectionVisitor visitor, void *context);
uint64_t world_topology_revision(const World *world);
WorldConfig world_config(const World *world);
void world_set_change_observer(World *world, WorldChangeObserver observer,
                               void *context);
const char *rs_result_name(RsResult result);
#endif
