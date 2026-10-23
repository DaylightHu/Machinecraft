#ifndef RS_RAIL_SOLVER_H
#define RS_RAIL_SOLVER_H

#include "world_internal.h"

/* Synchronous PoweredRailBlock.updateState, including ordered notifications. */
void rail_update_power(World *world, BlockPos pos, BlockState state);

#endif
