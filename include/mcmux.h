#ifndef MCMUX_H
#define MCMUX_H

#include "rs/structure.h"
#include "rs/sim.h"

/* Load a fresh, frozen world from an MCS structure file.
 * On failure, print the path and reason to stderr and exit with EXIT_FAILURE.
 * The caller owns the returned world and releases it with world_destroy(). */
World *load_mcs(const char *path);

#endif
