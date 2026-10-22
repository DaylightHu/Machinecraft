#ifndef RS_SIM_H
#define RS_SIM_H
#include "world.h"

typedef struct {
    uint64_t neighbor_checks, components_solved; /* components: graph backend passes. */
    uint64_t wires_visited; /* Individual dust-strength evaluations. */
    uint64_t wire_changes, rail_components_solved, rails_visited;
    uint64_t rail_changes, events_processed;
    size_t pending_events;
} SimStats;
typedef struct {
    uint8_t received;  /* Any incident signal, e.g. dust can turn off a torch. */
    uint8_t strong;    /* Direct signal with ALL wire outputs suppressed. */
} SolidPower;

/* Edits synchronously notify neighbors and update dust; flush drains pending
 * rail work without advancing time.
 * step flushes edits, advances ONE game tick, then runs due events in
 * priority/insertion order from a frozen due batch, then piston block events,
 * then containers. flush does not execute piston block events or advance time.
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
