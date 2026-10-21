#include "block_internal.h"

/* In 1.16.2, Material.CLAY and the full collision shape make slime a
 * conductor. noOcclusion controls rendering, not this power property. */
const BlockDef rs_slime_def = {
    .name = "slime_block",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL
};
