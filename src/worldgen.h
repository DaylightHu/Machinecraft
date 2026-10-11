#ifndef RS_WORLDGEN_H
#define RS_WORLDGEN_H

#include "world_internal.h"

/* Initialization only: fills fresh cells, without edits or simulation events. */
void world_generate_flat(World *world);

#endif
