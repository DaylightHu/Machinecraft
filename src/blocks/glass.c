#include "block_internal.h"

/* Glass has a full supporting shape, but it is not a redstone conductor.
 * Keeping support and conduction separate is what gives dust its vanilla
 * step behavior around transparent blocks. */
const BlockDef rs_glass_def = {
    .name = "glass",
    .full_cube = true,
    .supports_wire = true
};
