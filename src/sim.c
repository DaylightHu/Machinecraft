#include "sim_internal.h"
#include "wire_solver.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool sim_init_internal(World *w)
{
    w->sim = calloc(1, sizeof(*w->sim));
    if (!w->sim) return false;
    w->sim->containers.dropper_random = UINT64_C(0x5deece66d); /* Java seed 0. */
    w->sim->free_torch = SIZE_MAX; scheduler_init(&w->sim->scheduler); return true;
}
void sim_destroy_internal(World *w)
{
    Simulator *s = w->sim;
    if (!s) return;
    container_destroy(w);
    free(s->piston_events.items); posmap_destroy(&s->piston_events.pending);
    free(s->piston_motions.items); posmap_destroy(&s->piston_motions.slots);
    free(s->held_changes);
    scheduler_destroy(&s->scheduler); posmap_destroy(&s->torch_slots);
    free(s->torches); free(s); w->sim = NULL;
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
    container_forget(w, p);
    sim_discard_piston_motion(w, p);
    Simulator *s = w->sim; size_t slot;
    /* Keep accepting version-6 saves, where piston entries were no-op motion
     * markers represented as scheduled block ticks. */
    scheduler_cancel(&s->scheduler, p, BLOCK_PISTON);
    scheduler_cancel(&s->scheduler, p, BLOCK_STICKY_PISTON);
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
    if (world_get(w, p).id == BLOCK_REDSTONE_WIRE)
        wire_update_power(w, p, world_get(w, p));
}
void sim_mark_input(World *w, BlockPos p)
{
    if (w->sim->error != RS_OK) return;
    const BlockDef *d = block_definition((BlockId)world_get(w, p).id);
    if (d->input_changed) d->input_changed(w, p);
}
void sim_notify_change(World *w, BlockPos p)
{
    sim_update_neighbors_at(w, p, DIR_COUNT);
}
void sim_notify_analog(World *w, BlockPos p)
{
    static const Direction horizontal[] = {DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST};
    for (unsigned i = 0; i < 4; ++i) {
        Direction direction = horizontal[i];
        BlockPos next = block_offset(p, direction);
        if (world_get(w, next).id == BLOCK_REDSTONE_COMPARATOR)
            sim_neighbor_changed(w, next);
        else if (block_is_conductor(w, next)) {
            next = block_offset(next, direction);
            if (world_get(w, next).id == BLOCK_REDSTONE_COMPARATOR)
                sim_neighbor_changed(w, next);
        }
    }
}
void sim_schedule(World *w, BlockPos p, uint32_t delay)
{
    sim_schedule_priority(w, p, delay, TICK_NORMAL);
}

void sim_schedule_priority(World *w, BlockPos p, uint32_t delay,
                           TickPriority priority)
{
    if (!delay || UINT64_MAX - w->tick < delay) { w->sim->error = RS_INTERNAL_ERROR; return; }
    if (!scheduler_add_priority(&w->sim->scheduler, p,
                                scheduler_block_type(world_get(w, p)), w->tick + delay,
                                priority))
        w->sim->error = RS_OUT_OF_MEMORY;
}
void sim_cancel(World *w, BlockPos p)
{
    scheduler_cancel(&w->sim->scheduler, p, scheduler_block_type(world_get(w, p)));
}
bool sim_has_scheduled_tick(const World *w, BlockPos p)
{
    return scheduler_pending(&w->sim->scheduler, p,
                             scheduler_block_type(world_get(w, p)));
}
bool sim_will_tick(const World *w, BlockPos p)
{
    return scheduler_will_tick(&w->sim->scheduler, p,
                               scheduler_block_type(world_get(w, p)));
}

static BlockPos piston_event_key(PistonEvent event)
{
    BlockPos key = event.pos;
    unsigned type = ((event.block == BLOCK_STICKY_PISTON ? 3u : 0u) +
                     event.type)*DIR_COUNT + event.facing;
    key.y = (key.y - WORLD_MIN_Y)*(6*DIR_COUNT) + (int)type;
    return key;
}

bool sim_queue_piston_event(World *w, PistonEvent event)
{
    if (!world_in_bounds(w, event.pos) ||
        !block_is_piston_base((BlockId)event.block) ||
        event.type > 2 || event.facing >= DIR_COUNT) {
        w->sim->error = RS_INTERNAL_ERROR;
        return false;
    }
    PistonEvents *q = &w->sim->piston_events;
    BlockPos key = piston_event_key(event);
    if (posmap_get(&q->pending, key, NULL)) return true;
    if (q->head && q->count == q->capacity) {
        memmove(q->items, q->items + q->head,
                (q->count - q->head)*sizeof(*q->items));
        q->count -= q->head;
        q->head = 0;
    }
    if (q->count == q->capacity) {
        size_t capacity = q->capacity ? q->capacity*2 : 16;
        if (capacity < q->capacity || capacity > SIZE_MAX/sizeof(*q->items))
            goto fail;
        PistonEvent *next = realloc(q->items, capacity*sizeof(*next));
        if (!next) goto fail;
        q->items = next;
        q->capacity = capacity;
    }
    if (!posmap_set(&q->pending, key, 0)) goto fail;
    q->items[q->count++] = event;
    return true;
fail:
    w->sim->error = RS_OUT_OF_MEMORY;
    return false;
}

static bool pop_piston_event(PistonEvents *q, PistonEvent *event)
{
    if (q->head == q->count) return false;
    *event = q->items[q->head++];
    posmap_remove(&q->pending, piston_event_key(*event));
    if (q->head == q->count) q->head = q->count = 0;
    return true;
}
RsResult sim_flush(World *w)
{
    if (!w) return RS_INVALID_ARGUMENT;
    return w->sim->error;
}
RsResult sim_step(World *w)
{
    RsResult result = sim_flush(w);
    if (result != RS_OK) return result;
    if (w->tick == UINT64_MAX) return RS_INTERNAL_ERROR;
    Simulator *s = w->sim;
    ++w->tick;
    s->handling_tick = true;
    if (!scheduler_begin_tick(&s->scheduler, w->tick)) {
        s->handling_tick = false;
        return s->error = RS_OUT_OF_MEMORY;
    }
    ScheduledEvent event;
    while (scheduler_next_tick(&s->scheduler, &event)) {
        BlockState state = world_get(w, event.pos);
        if (scheduler_block_type(state) != event.block) continue;
        const BlockDef *d = block_definition((BlockId)state.id);
        if (!d->scheduled_tick) continue;
        TickAction action = d->scheduled_tick(w, event.pos);
        ++s->stats.events_processed;
        if (action.changed)
            (void)world_set_block(w, event.pos, action.state,
                state.id == BLOCK_REDSTONE_TORCH || state.id == BLOCK_TARGET ? 3 : 2);
        if (action.next_delay)
            sim_schedule(w, event.pos, action.next_delay);
        if (state.id == BLOCK_OBSERVER)
            sim_update_output_neighbors(w, event.pos, (Direction)state.data.observer.facing);
        result = sim_flush(w);
        if (result != RS_OK) {
            s->handling_tick = false;
            return result;
        }
    }
    /* ServerLevel runs block events after the complete scheduled-tick batch,
     * before block entities. A block event may append more events to this FIFO. */
    PistonEvent piston;
    while (pop_piston_event(&s->piston_events, &piston)) {
        if (world_get(w, piston.pos).id != piston.block) continue;
        piston_trigger_event(w, piston);
        ++s->stats.events_processed;
        result = sim_flush(w);
        if (result != RS_OK) {
            s->handling_tick = false;
            return result;
        }
    }
    /* ServerLevel clears handlingTick before ticking block entities. */
    s->handling_tick = false;
    sim_tick_piston_motions(w);
    if (s->error != RS_OK) return s->error;
    result = sim_flush(w);
    if (result != RS_OK) return result;
    container_step(w);
    return s->error;
}
SimStats sim_stats(const World *w)
{
    SimStats stats = {0};
    if (w) {
        const Simulator *s = w->sim;
        stats = s->stats;
        stats.pending_events = s->scheduler.count +
            s->scheduler.ticking_count - s->scheduler.ticking_head +
            s->piston_events.count - s->piston_events.head +
            s->piston_motions.active;
    }
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
    if (container_chunk_pinned(w, chunk)) return true;
    const Simulator *s = w->sim;
    for (size_t i = 0; i < s->scheduler.count; ++i) if (near_chunk(s->scheduler.heap[i].pos, chunk)) return true;
    for (size_t i = s->scheduler.ticking_head; i < s->scheduler.ticking_count; ++i)
        if (near_chunk(s->scheduler.ticking[i].pos, chunk)) return true;
    for (size_t i = s->piston_events.head; i < s->piston_events.count; ++i)
        if (near_chunk(s->piston_events.items[i].pos, chunk)) return true;
    for (size_t i = 0; i < s->piston_motions.count; ++i)
        if (s->piston_motions.items[i].used &&
            near_chunk(s->piston_motions.items[i].pos, chunk)) return true;
    return false;
}
