#ifndef RS_SIM_INTERNAL_H
#define RS_SIM_INTERNAL_H
#include "rs/sim.h"
#include "world_internal.h"
#include "queue.h"
#include "scheduler.h"
#include "container.h"
#include "blocks/block_internal.h"
typedef struct {
    uint64_t off_times[8], burned_until;
    uint8_t off_count, off_head;
    BlockPos pos;
    size_t next_free;
    bool used;
} TorchRuntime;
typedef struct {
    BlockPos pos;
    uint8_t block, type, facing;
} PistonEvent;
typedef struct {
    PistonEvent *items;
    size_t head, count, capacity;
    PosMap pending;
} PistonEvents;
typedef struct {
    BlockPos pos;
    BlockState previous, current;
    unsigned flags, remaining_shapes;
} StateChange;
struct Simulator {
    PositionQueue rail_roots;
    Scheduler scheduler;
    PistonEvents piston_events;
    StateChange *held_changes;
    size_t held_count, held_capacity;
    unsigned hold_updates;
    ContainerTable containers;
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
void sim_notify_analog(World *, BlockPos);
void sim_update_neighbors_at(World *, BlockPos, Direction except);
void sim_neighbor_changed(World *, BlockPos);
void sim_update_output_neighbors(World *, BlockPos, Direction output);
void sim_update_shape_at(World *, BlockPos, Direction toward_source,
                          BlockPos source, unsigned flags, unsigned remaining_shapes);
void sim_state_changed(World *, StateChange);
bool sim_hold_change(World *, StateChange);
void sim_begin_updates(World *);
void sim_end_updates(World *);
bool block_states_equal(BlockState, BlockState);
void sim_mark_wire(World *, BlockPos);
void sim_mark_rail(World *, BlockPos);
void sim_mark_input(World *, BlockPos);
void sim_schedule(World *, BlockPos, uint32_t delay);
void sim_schedule_priority(World *, BlockPos, uint32_t delay,
                           TickPriority priority);
void sim_cancel(World *, BlockPos);
bool sim_has_scheduled_tick(const World *, BlockPos);
bool sim_will_tick(const World *, BlockPos);
bool sim_queue_piston_event(World *, PistonEvent);
void piston_trigger_event(World *, PistonEvent);
TorchRuntime *sim_torch_runtime(const World *, BlockPos);
bool sim_chunk_pinned(const World *, BlockPos chunk);
#endif
