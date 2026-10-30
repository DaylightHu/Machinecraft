#include "block_internal.h"

const BlockDef rs_target_def = {
    .name = "target",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    /* Dust connects to the target; it has no signal of its own. */
    .signal_source = true,
    .push_reaction = PUSH_NORMAL
};
