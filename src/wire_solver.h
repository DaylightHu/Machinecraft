#ifndef RS_WIRE_SOLVER_H
#define RS_WIRE_SOLVER_H

#include "world_internal.h"

/* Resolves only dirty wire components. Does not advance simulation time. */
void wire_solver_flush(World *world);

#endif
