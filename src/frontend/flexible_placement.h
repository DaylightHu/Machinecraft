#ifndef MC_FLEXIBLE_PLACEMENT_H
#define MC_FLEXIBLE_PLACEMENT_H

#include "voxel_pick.h"

typedef enum {
    PLACEMENT_CENTER, PLACEMENT_LEFT, PLACEMENT_RIGHT,
    PLACEMENT_TOP, PLACEMENT_BOTTOM
} PlacementRegion;

typedef struct {
    BlockPos pos;
    Direction placement; /* world_place() direction/support argument. */
    Direction right, up; /* Axes of the five-region face overlay. */
    Direction direction; /* Selected outward direction, also used for offset. */
    PlacementRegion region;
    bool valid;
    bool directional;
} PlacementTarget;

/* Pure placement geometry; the world still checks occupancy and support.
 * looking is the closest six-axis look direction; horizontal is its yaw.
 * A rotation selects output/push/lid/outlet direction. For torches it selects
 * the direction away from the support. Unsupported directions are rejected. */
PlacementTarget flexible_placement_target(VoxelHit hit, BlockId block,
    Direction horizontal, Direction looking, bool offset, bool rotate);

#endif
