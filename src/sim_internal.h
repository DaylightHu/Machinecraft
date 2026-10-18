#ifndef RS_SIM_INTERNAL_H
#define RS_SIM_INTERNAL_H
#include "rs/sim.h"
#include "world_internal.h"
#include "queue.h"
#include "scheduler.h"
#include "blocks/block_internal.h"
typedef struct {
    uint64_t off_times[8], burned_until;
    uint8_t off_count, off_head;
    BlockPos pos;
    size_t next_free;
    bool used;
} TorchRuntime;
struct Simulator {
    PositionQueue dirty, wire_roots, rail_roots, input_roots;
    Scheduler scheduler;
    PosMap torch_slots;
    TorchRuntime *torches;
    size_t torch_used, torch_capacity, free_torch;
    SimStats stats;
    RsResult error;
};
bool sim_init_internal(World *);
void sim_destroy_internal(World *);
RsResult sim_prepare_torch(World *);
bool sim_attach_torch(World *, BlockPos);
void sim_forget_cell(World *, BlockPos);
void sim_notify_change(World *, BlockPos);
void sim_notify_power(World *, BlockPos);
void sim_notify_observers(World *, BlockPos);
void sim_mark_wire(World *, BlockPos);
void sim_mark_rail(World *, BlockPos);
void sim_mark_input(World *, BlockPos);
void sim_schedule(World *, BlockPos, uint32_t delay);
void sim_schedule_priority(World *, BlockPos, uint32_t delay,
                           TickPriority priority);
void sim_cancel(World *, BlockPos);
TorchRuntime *sim_torch_runtime(const World *, BlockPos);
bool sim_chunk_pinned(const World *, BlockPos chunk);
#endif
