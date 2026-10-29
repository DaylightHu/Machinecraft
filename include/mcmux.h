#ifndef MCMUX_H
#define MCMUX_H

#include "rs/session.h"
#include "rs/structure.h"
#include "rs/sim.h"

/* Load an MCS structure with its saved anchor placed at anchor. */
World *load_mcs(const char *path, BlockPos anchor);

/* Save world to an MCW file. */
GameSessionResult save_mcw(World *world, const char *path);

/* Write world back to the original MCS selection in the MCW file. The anchor
 * must match the one passed to load_mcs(). */
GameSessionResult writeback_mcs(World *world, const char *mcs_path,
                                BlockPos anchor, const char *mcw_path);
/* Place the Block in the world */
// RsResult world_place(World *w, BlockPos p, BlockId id, Direction support)
#endif
