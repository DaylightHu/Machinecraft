#ifndef RS_WIRE_SOLVER_H
#define RS_WIRE_SOLVER_H

#include "world_internal.h"

/* Synchronous vanilla dust updates, retaining intermediate observable values. */
void wire_update_power(World *, BlockPos, BlockState);
void wire_on_place(World *, BlockPos, BlockState);
void wire_on_remove(World *, BlockPos, BlockState);
void wire_indirect_shapes(World *, BlockPos, BlockState, unsigned, unsigned);

#endif
