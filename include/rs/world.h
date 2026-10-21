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
typedef struct {
    uint32_t item;                  /* User-defined identity; zero means empty. */
    uint8_t count, limit;           /* Stack limit: 1, 16 or 64. */
} ContainerStack;
typedef void (*WorldSectionVisitor)(void *context, BlockPos origin);

/* Fixed iron ground, no RNG, no seed, no immovable bedrock. */
WorldConfig world_config_default(void);
World *world_create(WorldConfig config);
void world_destroy(World *world);
bool world_in_bounds(const World *world, BlockPos pos);
BlockState world_get(const World *world, BlockPos pos); /* Outside vertical/coordinate limits = air; reads may restore archived chunks. */
/* direction is the supporting block relative to a torch; the output direction
 * for a repeater/comparator/observer; the push direction for a piston; the lid
 * direction for a barrel; the outlet for a hopper (vertical maps to down);
 * and the texture-top direction for a rail. */
RsResult world_place(World *world, BlockPos pos, BlockId id, Direction support_dir);
RsResult world_remove(World *world, BlockPos pos);
RsResult world_toggle_wire_dot(World *world, BlockPos pos);
RsResult world_cycle_repeater_delay(World *world, BlockPos pos);
RsResult world_toggle_comparator_mode(World *world, BlockPos pos);
RsResult world_set_barrel_level(World *world, BlockPos pos, uint8_t level);
/* Five slots in a hopper, 27 in a barrel. Slot numbers are zero based.
 * Empty stacks use {0,0,0}; nonempty stacks require count <= limit.
 * Identities supplied by the caller include any item/tag distinctions. */
size_t world_container_size(const World *world, BlockPos pos);
RsResult world_get_container_slot(const World *world, BlockPos pos, size_t slot,
                                  ContainerStack *stack);
RsResult world_set_container_slot(World *world, BlockPos pos, size_t slot,
                                  ContainerStack stack);
uint8_t world_hopper_cooldown(const World *world, BlockPos pos);
/* Inject a projectile impact without simulating entities. u/v are finite
 * coordinates in [0,1] on the struck face, with the bullseye at (0.5,0.5).
 * persistent_projectile means Java's AbstractArrow, including tridents:
 * true gives 20 game ticks; false gives 8. Hits during a pending pulse return
 * RS_UNCHANGED without changing its strength or expiry. Edits require flush. */
RsResult world_hit_target(World *world, BlockPos pos, double u, double v,
                          bool persistent_projectile);
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
