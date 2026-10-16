#include "sim_internal.h"
#include "wire_solver.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { NOTIFICATION_RADIUS = 3 };
bool sim_init_internal(World *w)
{
    w->sim = calloc(1, sizeof(*w->sim));
    if (!w->sim) return false;
    w->sim->free_torch = SIZE_MAX; scheduler_init(&w->sim->scheduler); return true;
}
void sim_destroy_internal(World *w)
{
    Simulator *s = w->sim;
    if (!s) return;
    queue_destroy(&s->dirty); queue_destroy(&s->wire_roots); queue_destroy(&s->input_roots);
    scheduler_destroy(&s->scheduler); posmap_destroy(&s->torch_slots);
    free(s->torches); free(s->due_actions); free(s); w->sim = NULL;
}
RsResult sim_prepare_torch(World *w)
{
    Simulator *s = w->sim;
    if (!posmap_reserve(&s->torch_slots, s->torch_slots.count + 1)) return RS_OUT_OF_MEMORY;
    if (s->free_torch != SIZE_MAX || s->torch_used < s->torch_capacity) return RS_OK;
    size_t capacity = s->torch_capacity ? s->torch_capacity * 2 : 8;
    if (capacity < s->torch_capacity || capacity > SIZE_MAX / sizeof(TorchRuntime)) return RS_OUT_OF_MEMORY;
    TorchRuntime *next = realloc(s->torches, capacity * sizeof(*next));
    if (!next) return RS_OUT_OF_MEMORY;
    s->torches = next; s->torch_capacity = capacity; return RS_OK;
}
bool sim_attach_torch(World *w, BlockPos p)
{
    Simulator *s = w->sim;
    if (sim_prepare_torch(w) != RS_OK) return false;
    size_t slot = s->free_torch != SIZE_MAX ? s->free_torch : s->torch_used;
    if (!posmap_set(&s->torch_slots, p, slot)) return false;
    if (s->free_torch != SIZE_MAX) s->free_torch = s->torches[slot].next_free;
    else ++s->torch_used;
    s->torches[slot] = (TorchRuntime){.pos = p, .used = true}; return true;
}
void sim_forget_cell(World *w, BlockPos p)
{
    Simulator *s = w->sim; size_t slot;
    scheduler_cancel(&s->scheduler, p);
    if (posmap_get(&s->torch_slots, p, &slot)) {
        s->torches[slot].used = false; s->torches[slot].next_free = s->free_torch;
        s->free_torch = slot; posmap_remove(&s->torch_slots, p);
    }
}
TorchRuntime *sim_torch_runtime(const World *w, BlockPos p)
{
    size_t slot;
    return posmap_get(&w->sim->torch_slots, p, &slot) ? &w->sim->torches[slot] : NULL;
}
void sim_mark_wire(World *w, BlockPos p)
{
    if (!queue_push(&w->sim->wire_roots, p)) w->sim->error = RS_OUT_OF_MEMORY;
}
void sim_mark_input(World *w, BlockPos p)
{
    if (!queue_push(&w->sim->input_roots, p)) w->sim->error = RS_OUT_OF_MEMORY;
}
static void notify(World *w, BlockPos p, bool topology)
{
    for (int dy = -3; dy <= 3; ++dy) for (int dz = -3; dz <= 3; ++dz) for (int dx = -3; dx <= 3; ++dx) {
        if (abs(dx) + abs(dy) + abs(dz) > NOTIFICATION_RADIUS) continue;
        int64_t x = (int64_t)p.x + dx, y = (int64_t)p.y + dy, z = (int64_t)p.z + dz;
        if (x < INT_MIN || x > INT_MAX || y < WORLD_MIN_Y || y > WORLD_MAX_Y || z < INT_MIN || z > INT_MAX) continue;
        BlockPos n = {(int)x, (int)y, (int)z};
        const BlockDef *d = block_definition((BlockId)world_get(w, n).id);
        if (topology && d->neighbor_changed) {
            if (!queue_push(&w->sim->dirty, n)) w->sim->error = RS_OUT_OF_MEMORY;
        } else if (!topology && d->input_changed) sim_mark_input(w, n);
    }
}
void sim_notify_change(World *w, BlockPos p) { notify(w, p, true); }
void sim_notify_power(World *w, BlockPos p) { notify(w, p, false); }
void sim_schedule(World *w, BlockPos p, uint32_t delay)
{
    if (!delay || UINT64_MAX - w->tick < delay) { w->sim->error = RS_INTERNAL_ERROR; return; }
    if (!scheduler_add(&w->sim->scheduler, p, w->tick + delay)) w->sim->error = RS_OUT_OF_MEMORY;
}
RsResult sim_flush(World *w)
{
    if (!w) return RS_INVALID_ARGUMENT;
    Simulator *s = w->sim; BlockPos p;
    if (s->error != RS_OK) return s->error;
    while (queue_pop(&s->dirty, &p)) {
        const BlockDef *d = block_definition((BlockId)world_get(w, p).id);
        if (d->neighbor_changed) { ++s->stats.neighbor_checks; d->neighbor_changed(w, p); }
        if (s->error != RS_OK) return s->error;
    }
    wire_solver_flush(w);
    if (s->error != RS_OK) return s->error;
    while (queue_pop(&s->input_roots, &p)) {
        const BlockDef *d = block_definition((BlockId)world_get(w, p).id);
        if (d->input_changed) d->input_changed(w, p);
        if (s->error != RS_OK) return s->error;
    }
    return s->error;
}
RsResult sim_step(World *w)
{
    RsResult result = sim_flush(w);
    if (result != RS_OK) return result;
    if (w->tick == UINT64_MAX) return RS_INTERNAL_ERROR;
    Simulator *s = w->sim;
    /* Reserve before popping any events: a failure leaves the pending heap intact. */
    if (s->scheduler.count > s->due_capacity) {
        if (s->scheduler.count > SIZE_MAX / sizeof(DueAction)) return RS_OUT_OF_MEMORY;
        DueAction *next = realloc(s->due_actions, s->scheduler.count * sizeof(DueAction));
        if (!next) return RS_OUT_OF_MEMORY;
        s->due_actions = next; s->due_capacity = s->scheduler.count;
    }
    ++w->tick;
    size_t count = 0; ScheduledEvent event;
    while (scheduler_pop_due(&s->scheduler, w->tick, &event)) {
        const BlockDef *d = block_definition((BlockId)world_get(w, event.pos).id);
        if (!d->scheduled_tick) continue;
        TickAction action = d->scheduled_tick(w, event.pos);
        s->due_actions[count++] = (DueAction){event.pos, action}; ++s->stats.events_processed;
    }
    for (size_t i = 0; i < count; ++i) {
        DueAction a = s->due_actions[i];
        if (a.action.changed && world_write_derived(w, a.pos, a.action.state)) sim_notify_change(w, a.pos);
        if (a.action.next_delay) sim_schedule(w, a.pos, a.action.next_delay);
    }
    return sim_flush(w);
}
SimStats sim_stats(const World *w)
{
    SimStats stats = {0};
    if (w) { stats = w->sim->stats; stats.pending_events = w->sim->scheduler.count; }
    return stats;
}
void sim_reset_stats(World *w) { if (w) memset(&w->sim->stats, 0, sizeof(SimStats)); }
static bool near_chunk(BlockPos p, BlockPos chunk)
{
    int64_t dx = (int64_t)floor_div16(p.x) - chunk.x, dz = (int64_t)floor_div16(p.z) - chunk.z;
    return dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1;
}
bool sim_chunk_pinned(const World *w, BlockPos chunk)
{
    const Simulator *s = w->sim;
    for (size_t i = 0; i < s->scheduler.count; ++i) if (near_chunk(s->scheduler.heap[i].pos, chunk)) return true;
    const PositionQueue *queues[] = {&s->dirty, &s->wire_roots, &s->input_roots};
    for (size_t q = 0; q < 3; ++q)
        for (size_t i = queues[q]->head; i < queues[q]->count; ++i)
            if (near_chunk(queues[q]->items[i], chunk)) return true;
    return false;
}
