#ifndef RS_HOPPER_SHAPE_H
#define RS_HOPPER_SHAPE_H
#include "../include/rs/block.h"

/* Model units are sixteenths. Face bits: south, north, east, west, up, down. */
typedef struct { float minimum[3], maximum[3]; unsigned faces; } HopperBox;
enum { HOPPER_BOX_COUNT = 7, HOPPER_QUAD_COUNT = 32 };

static inline void hopper_shape(Direction facing, HopperBox boxes[HOPPER_BOX_COUNT])
{
    static const HopperBox base[HOPPER_BOX_COUNT] = {
        {{0,10,0}, {16,11,16}, 63},
        {{0,11,0}, {2,16,16}, 31},
        {{14,11,0}, {16,16,16}, 31},
        {{2,11,0}, {14,16,2}, 19},
        {{2,11,14}, {14,16,16}, 19},
        {{4,4,4}, {12,10,12}, 47},
        {{6,0,6}, {10,4,10}, 47}
    };
    for (int i = 0; i < HOPPER_BOX_COUNT; ++i) boxes[i] = base[i];
    switch (facing) {
    case DIR_EAST: boxes[6] = (HopperBox){{12,4,6},{16,8,10},55}; break;
    case DIR_WEST: boxes[6] = (HopperBox){{0,4,6},{4,8,10},59}; break;
    case DIR_SOUTH: boxes[6] = (HopperBox){{6,4,12},{10,8,16},61}; break;
    case DIR_NORTH: boxes[6] = (HopperBox){{6,4,0},{10,8,4},62}; break;
    default: break;
    }
}
#endif
