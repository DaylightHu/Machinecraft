#include "block_internal.h"

static uint8_t iron_signal(const World *world, BlockPos position,
                           Direction face, SignalMode mode)
{
    (void)face;
    /* Relay direct emitters only. Other iron blocks cannot form a recursive chain. */
    return power_conducted(world, position, mode);
}

const BlockDef rs_iron_def = {
    .name = "iron",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    .signal = iron_signal
};
