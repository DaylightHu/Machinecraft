#include "block_internal.h"

/* Honey has an inset 14x15x14 collision shape in Java Edition. Machinecraft
 * does not model entity collision shapes yet, but the missing full top and
 * side faces still matter: redstone dust, diodes, rails, and torches cannot
 * use it as a support. It is also transparent to redstone power. */
const BlockDef rs_honey_def = {
    .name = "honey_block",
    .push_reaction = PUSH_NORMAL
};
