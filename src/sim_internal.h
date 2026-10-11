#ifndef RS_SIM_INTERNAL_H
#define RS_SIM_INTERNAL_H

#include "rs/sim.h"
#include "world_internal.h"

struct Simulator{
    IndexQueue dirty;
    Scheduler scheduler;
    uint32_t *wire_roots, *input_roots. *nodes, *visited;
    uint32_t *bucket_next, *bucket_prev, *torch_slots;
    uint8_t *root_flags, *staged_power;
    size_t wire_count, input_count;
    uint32_t epoch;
    TorchRuntime *torches;
    DueAction *due_actions;
    size_t torch_used, torch_capacity;
    uint32_t free_torch;
    SimStats stats;
    RsResult error;
};


#endif
