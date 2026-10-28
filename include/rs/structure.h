#ifndef RS_STRUCTURE_H
#define RS_STRUCTURE_H

#include "world.h"

typedef struct Structure Structure;
typedef enum {
    STRUCTURE_OK = 0, STRUCTURE_INVALID_ARGUMENT, STRUCTURE_OUT_OF_BOUNDS,
    STRUCTURE_TOO_LARGE, STRUCTURE_OUT_OF_MEMORY, STRUCTURE_IO_ERROR,
    STRUCTURE_EXISTS, STRUCTURE_CORRUPT, STRUCTURE_UNSUPPORTED,
    STRUCTURE_SIMULATION_ERROR
} StructureResult;
enum { STRUCTURE_VERSION = 1, STRUCTURE_BLOCK_LIMIT = 4*1024*1024 };
typedef struct {
    BlockPos origin, anchor; /* Absolute source coordinates; anchor may be outside. */
    uint32_t width, height, length;
    uint64_t source_tick; /* Informational; this file is not a running checkpoint. */
    size_t volume, container_count;
} StructureInfo;

/* Normalize inclusive corners. NULL anchor uses the minimum corner. Captures
 * all cells (including air), configurations, current states and inventories.
 * Never edits or advances the world or uses the editor's clipboard. Active
 * moving pistons are rejected. Does not capture scheduled ticks, cooldowns,
 * torch history, random state or dependencies outside the selected cuboid.
 * Call only between simulation callbacks, on the world's owning thread. */
Structure *structure_capture(const World *world, BlockPos first, BlockPos second,
                             const BlockPos *anchor, StructureResult *result);
void structure_destroy(Structure *structure);
StructureInfo structure_info(const Structure *structure);
/* Local coordinates: [0,width), [0,height), [0,length). False on bad arguments. */
bool structure_get(const Structure *structure, BlockPos local, BlockState *state);
size_t structure_container_size(const Structure *structure, BlockPos local);
StructureResult structure_get_container_slot(const Structure *structure,
    BlockPos local, size_t slot, ContainerStack *stack);

/* Versioned .mcs format; C usage documented in docs/c-programs.md. Atomic publish
 * through a temporary file beside path; no directory creation. Existing files
 * are preserved unless overwrite is true. Read allocates a detached immutable
 * structure; neither operation requires or modifies a World. NULL result is OK. */
StructureResult structure_write(const Structure *structure, const char *path,
                                 bool overwrite);
Structure *structure_read(const char *path, StructureResult *result);

/* Translate a local cell (same coordinates as structure_get) to a placement
 * whose saved anchor is at target_anchor. On failure, world_pos is unchanged.
 * This checks integer range and local bounds, not a particular world's bounds. */
StructureResult structure_resolve(const Structure *structure,
    BlockPos target_anchor, BlockPos local, BlockPos *world_pos);

/* Create an owned, independent world; destroy it with world_destroy().
 * All cells, including air, saved states and inventories are installed before
 * the world is returned. No placement/neighbor callbacks or ticks are replayed.
 * The world starts frozen at tick 0 with updates enabled, empty scheduled work,
 * fresh torch history, default random state and zero container cooldowns.
 * Hoppers register in local Y/Z/X order and first tick when the caller steps.
 * source_tick is NOT restored. This is a new instance, not checkpoint recovery.
 * Terrain outside the cuboid follows config. The plot grows in whole plots to
 * contain the placement; vertical limits and coordinate margins still apply.
 * Failure destroys the incomplete world and returns NULL with a result code.
 * The detached structure remains reusable and may be destroyed after success. */
World *structure_instantiate(const Structure *structure, WorldConfig config,
    BlockPos target_anchor, StructureResult *result);
const char *structure_result_name(StructureResult result);

#endif
