#ifndef RS_SIM_INTERNAL_H
#define RS_SIM_INTERNAL_H
#include "rs/sim.h"
#include "world_internal.h"
#include "queue.h"
#include "scheduler.h"
#include "blocks/block_internal.h"

enum { ROOT_WIRE = 1u, ROOT_INPUT = 2u };

typedef struct {
    uint64_t off_times[8], burned_until;
    uint8_t off_count, off_head;
    uint32_t next_free;
} TorchRuntime;
typedef struct { uint32_t index; TickAction action; } DueAction;
struct Simulator {
    IndexQueue dirty;
    Scheduler scheduler;
    uint32_t *wire_roots, *input_roots, *nodes, *visited;
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
bool sim_init_internal(World *);
void sim_destroy_internal(World *);
RsResult sim_prepare_torch(World *);
void sim_attach_torch(World *, uint32_t);
void sim_forget_cell(World *, uint32_t);
void sim_notify_change(World *, BlockPos);
void sim_notify_power(World *, BlockPos); /* Only deferred inputs need updates. */
void sim_mark_wire(World *, uint32_t);
void sim_mark_input(World *, uint32_t);
void sim_schedule(World *, uint32_t, uint32_t delay);
TorchRuntime *sim_torch_runtime(const World *, uint32_t);
#endif
