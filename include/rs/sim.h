#ifndef RS_SIM_H
#define RS_SIM_H
#include "world.h"

typedef struct{
    uint64_t neighbor_checks, components_solved, wires_visited;
    uint64_t wire_changes, events_processed;
    size_t pending_events;
} SimStats;

typedef struct {
    uint8_t received; /* Any incident signal, e.g. dust can turn off a torch. */
    uint8_t strong;   /* Direct signal with ALL wire outputs suppressed. */
} SolidPower;

RsResult sim_flush(World *world);

#endif
