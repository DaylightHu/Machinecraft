#include "block_internal.h"

const BlockDef rs_iron_def = {
    .name = "iron",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL
};
