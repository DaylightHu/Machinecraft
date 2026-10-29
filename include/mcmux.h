#ifndef MCMUX_H
#define MCMUX_H

#include "rs/session.h"
#include "rs/structure.h"
#include "rs/sim.h"

/* Load a fresh, frozen world from an MCS structure file.*/
World *load_mcs(const char *path);

/* Save world to an MCW file. */
GameSessionResult save_mcw(World *world, const char *path);

/* Write world back to the corresponding selection in the existing MCW file.
 * mcs_path supplies the selection origin and dimensions. */
GameSessionResult writeback_mcs(World *world, const char *mcs_path,
                                const char *mcw_path);

#endif
