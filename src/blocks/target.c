#include "block_internal.h"
#include "sim_internal.h"

enum { TARGET_ARROW_TICKS = 20, TARGET_OTHER_TICKS = 8 };

static uint8_t target_signal(const World *world, BlockPos position,
                             Direction face, SignalMode mode)
{
    (void)face;
    (void)mode;
    return world_get(world, position).data.target.power;
}

static uint8_t target_hit_power(double u, double v)
{
    /* Java 1.16.2 uses the larger face-axis distance, not radial distance.
     * Inputs have been validated within [0,1]. Keep the arithmetic order
     * of 15 * ((0.5 - distance) / 0.5), then ceil, then a minimum of 1. */
    double du = u < 0.5 ? 0.5 - u : u - 0.5;
    double dv = v < 0.5 ? 0.5 - v : v - 0.5;
    double distance = du > dv ? du : dv;
    double strength = 15.0 * ((0.5 - distance) / 0.5);
    unsigned power = (unsigned)strength;
    if ((double)power < strength) ++power;
    return (uint8_t)(power ? power : 1);
}

RsResult world_hit_target(World *world, BlockPos position, double u, double v,
                          bool persistent_projectile)
{
    /* The comparisons also reject NaN and infinities, without a libm call. */
    if (!world || !(u >= 0.0 && u <= 1.0) || !(v >= 0.0 && v <= 1.0))
        return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(world, position)) return RS_OUT_OF_BOUNDS;
    RsResult result = sim_flush(world);
    if (result != RS_OK) return result;
    BlockState state = world_get(world, position);
    if (state.id != BLOCK_TARGET) return RS_INVALID_ARGUMENT;
    if (sim_has_scheduled_tick(world, position))
        return RS_UNCHANGED;

    state.data.target.power = target_hit_power(u, v);
    if (!world_set_block(world, position, state, 3)) return world->sim->error;
    sim_schedule(world, position, persistent_projectile ?
                 TARGET_ARROW_TICKS : TARGET_OTHER_TICKS);
    return world->sim->error;
}

static TickAction target_tick(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    TickAction action = {state, false, 0};
    if (state.id == BLOCK_TARGET && state.data.target.power) {
        action.state.data.target.power = 0;
        action.changed = true;
    }
    return action;
}

const BlockDef rs_target_def = {
    .name = "target",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    /* Dust connects even when the target's own output is zero. */
    .signal_source = true,
    .push_reaction = PUSH_NORMAL,
    .signal = target_signal,
    /* Projectile power is weak power. Do not give it direct_signal. */
    .scheduled_tick = target_tick
};
