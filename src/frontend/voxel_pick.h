#ifndef MC_VOXEL_PICK_H
#define MC_VOXEL_PICK_H

#include "rs/world.h"
typedef struct {
    float x, y, z;
} PickVec3;

typedef struct {
    bool hit;
    bool has_face;
    BlockPos pos;
    Direction face;
    float distance;
} VoxelHit;

VoxelHit voxel_pick(const World *world, PickVec3 origin,
        PickVec3 direction, float max_distance);

#endif
