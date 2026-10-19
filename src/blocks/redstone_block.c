#include "block_internal.h"

static uint8_t source_signal(const World *world, BlockPos position,
                             Direction face, SignalMode mode)
{
    (void)world;
    (void)position;
    (void)face;
    (void)mode;
    return 15;
}

/* Supports dust and torches, but is not a passive redstone conductor. */
const BlockDef rs_redstone_block_def = {
    .name = "redstone_block",
    .full_cube = true,
    .supports_wire = true,
    .signal_source = true,
    .push_reaction = PUSH_NORMAL,
    .signal = source_signal
};
