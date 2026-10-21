#include "block_internal.h"

/* Slime has a full supporting shape but, like vanilla's translucent blocks,
 * it does not conduct redstone power through itself. */
const BlockDef rs_slime_def = {
    .name = "slime_block",
    .full_cube = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL
};
