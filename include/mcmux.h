#ifndef MCMUX_H
#define MCMUX_H

#include "rs/structure.h"
#include "rs/sim.h"

/* Load a fresh, frozen world from an MCS structure file.*/
World *load_mcs(const char *path);

/* Save an independent world at a completed tick boundary.
 * An existing destination may be replaced.
 * Does not advance time, destroy the world, or exit on failure. */
GameSessionResult save_mcw(World *world, const char *path);
