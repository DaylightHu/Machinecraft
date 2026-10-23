#ifndef RS_SIM_H
#define RS_SIM_H
#include "world.h"

typedef struct {
    uint64_t neighbor_checks, components_solved; /* components: graph backend passes. */
    uint64_t wires_visited; /* Individual dust-strength evaluations. */
    uint64_t wire_changes;
    uint64_t rail_components_solved; /* Graph passes; zero for synchronous rails. */
    uint64_t rails_visited; /* Individual synchronous rail-state evaluations. */
    uint64_t rail_changes, events_processed;
    size_t pending_events; /* Scheduled ticks, block events and active motions. */
} SimStats;
typedef struct {
    uint8_t received;  /* Any incident signal, e.g. dust can turn off a torch. */
    uint8_t strong;    /* Direct signal with ALL wire outputs suppressed. */
} SolidPower;

/* Edits synchronously settle neighbor/shape notifications, dust and rails.
 * flush reports the current status without advancing time.
 * step checks that status, advances ONE game tick, then runs due events in
 * priority/insertion order from a frozen due batch, then piston block events,
 * moving-piston block entities, and containers. flush does not execute these
 * timed phases or advance time.
 * Each callback's zero-delay consequences settle before the next callback.
 * Torch delay = 2 game ticks (100 ms at 20 TPS). */
RsResult sim_flush(World *world);
RsResult sim_step(World *world);
SimStats sim_stats(const World *world);
void sim_reset_stats(World *world);
SolidPower redstone_solid_power(const World *world, BlockPos pos);
uint8_t redstone_signal(const World *world, BlockPos emitter, Direction toward);
bool redstone_torch_burned_out(const World *world, BlockPos pos);
#endif
