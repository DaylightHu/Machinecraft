#include "scheduler.h"

#include <stdlib.h>
#include <string.h>

uint8_t scheduler_block_type(BlockState state)
{
    return state.id == BLOCK_REDSTONE_TORCH &&
           direction_horizontal((Direction)state.data.torch.support_dir) ?
           TICK_WALL_TORCH : state.id;
}

static bool valid_key(BlockPos pos, uint8_t block)
{
    return pos.y >= WORLD_MIN_Y && pos.y <= WORLD_MAX_Y &&
           block > BLOCK_AIR && block < TICK_BLOCK_TYPES;
}

static BlockPos event_key(BlockPos pos, uint8_t block)
{
    /* The bounded building height leaves room to pack the type without
     * changing the general-purpose position map or its constant-time lookup. */
    pos.y = (pos.y - WORLD_MIN_Y)*TICK_BLOCK_TYPES + block;
    return pos;
}

static bool before(ScheduledEvent first, ScheduledEvent second)
{
    if (first.due != second.due) return first.due < second.due;
    if (first.priority != second.priority)
        return first.priority < second.priority;
    return first.sequence < second.sequence;
}

static void swap(Scheduler *scheduler, size_t first, size_t second)
{
    ScheduledEvent temporary = scheduler->heap[first];
    scheduler->heap[first] = scheduler->heap[second];
    scheduler->heap[second] = temporary;
    ScheduledEvent a = scheduler->heap[first], b = scheduler->heap[second];
    (void)posmap_set(&scheduler->slots, event_key(a.pos, a.block), first);
    (void)posmap_set(&scheduler->slots, event_key(b.pos, b.block), second);
}

static void sift_up(Scheduler *scheduler, size_t index)
{
    while (index) {
        size_t parent = (index - 1) / 2;
        if (!before(scheduler->heap[index], scheduler->heap[parent])) {
            break;
        }
        swap(scheduler, index, parent);
        index = parent;
    }
}

static void sift_down(Scheduler *scheduler, size_t index)
{
    for (;;) {
        size_t left = 2 * index + 1;
        size_t right = left + 1;
        size_t best = index;
        if (left < scheduler->count && before(scheduler->heap[left], scheduler->heap[best])) {
            best = left;
        }
        if (right < scheduler->count && before(scheduler->heap[right], scheduler->heap[best])) {
            best = right;
        }
        if (best == index) {
            break;
        }
        swap(scheduler, index, best);
        index = best;
    }
}

void scheduler_init(Scheduler *scheduler)
{
    memset(scheduler, 0, sizeof(*scheduler));
}

bool scheduler_reserve(Scheduler *scheduler, size_t capacity)
{
    if (capacity <= scheduler->capacity) {
        return true;
    }
    if (capacity > SIZE_MAX / sizeof(*scheduler->heap)) {
        return false;
    }
    ScheduledEvent *heap = realloc(scheduler->heap, capacity * sizeof(*heap));
    if (!heap) {
        return false;
    }
    scheduler->heap = heap;
    scheduler->capacity = capacity;
    return true;
}

void scheduler_destroy(Scheduler *scheduler)
{
    free(scheduler->heap);
    free(scheduler->ticking);
    posmap_destroy(&scheduler->slots);
    posmap_destroy(&scheduler->ticking_slots);
    memset(scheduler, 0, sizeof(*scheduler));
}

bool scheduler_pending(const Scheduler *s, BlockPos pos, uint8_t block)
{
    return valid_key(pos, block) &&
           posmap_get(&s->slots, event_key(pos, block), NULL);
}

bool scheduler_will_tick(const Scheduler *s, BlockPos pos, uint8_t block)
{
    return valid_key(pos, block) &&
           posmap_get(&s->ticking_slots, event_key(pos, block), NULL);
}

bool scheduler_add(Scheduler *s, BlockPos pos, uint8_t block, uint64_t due)
{
    return scheduler_add_priority(s, pos, block, due, TICK_NORMAL);
}

bool scheduler_add_priority(Scheduler *s, BlockPos pos, uint8_t block, uint64_t due,
                            TickPriority priority)
{
    if (!valid_key(pos, block) || (unsigned)priority > TICK_EXTREMELY_LOW)
        return false;
    if (scheduler_pending(s, pos, block)) return true;
    if (s->next_sequence == UINT64_MAX) return false;
    if (s->count == s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 8;
        if (capacity < s->capacity || !scheduler_reserve(s, capacity)) return false;
    }
    if (!posmap_set(&s->slots, event_key(pos, block), s->count)) return false;
    size_t slot = s->count++;
    s->heap[slot] = (ScheduledEvent){
        .due = due,
        .sequence = s->next_sequence++,
        .pos = pos,
        .priority = priority,
        .block = block
    };
    sift_up(s, slot); return true;
}

void scheduler_cancel(Scheduler *s, BlockPos pos, uint8_t block)
{
    size_t slot;
    if (!valid_key(pos, block) ||
        !posmap_get(&s->slots, event_key(pos, block), &slot)) return;
    posmap_remove(&s->slots, event_key(pos, block));
    --s->count;
    if (slot == s->count) return;
    s->heap[slot] = s->heap[s->count];
    ScheduledEvent moved = s->heap[slot];
    (void)posmap_set(&s->slots, event_key(moved.pos, moved.block), slot);
    if (slot && before(s->heap[slot], s->heap[(slot - 1) / 2])) sift_up(s, slot);
    else sift_down(s, slot);
}

void scheduler_cancel_positions(Scheduler *s, const PosMap *positions)
{
    size_t output = 0;
    for (size_t i = 0; i < s->count; ++i) {
        ScheduledEvent event = s->heap[i];
        BlockPos key = event_key(event.pos, event.block);
        if (posmap_get(positions, event.pos, NULL)) {
            posmap_remove(&s->slots, key);
        } else {
            s->heap[output] = event;
            (void)posmap_set(&s->slots, key, output++);
        }
    }
    s->count = output;
    for (size_t i = output/2; i > 0; --i) sift_down(s, i - 1);
}

bool scheduler_pop_due(Scheduler *scheduler, uint64_t now, ScheduledEvent *event)
{
    if (!scheduler->count || scheduler->heap[0].due > now) {
        return false;
    }
    *event = scheduler->heap[0];
    scheduler_cancel(scheduler, event->pos, event->block);
    return true;
}

bool scheduler_begin_tick(Scheduler *s, uint64_t now)
{
    if (s->ticking_head != s->ticking_count) return false;
    s->ticking_head = s->ticking_count = 0;
    /* next_tick removes each entry. Keep the empty map and its tombstones,
     * so an idle tick does not clear storage sized for a previous large batch. */
    while (s->count && s->heap[0].due <= now &&
           s->ticking_count < TICKS_PER_STEP_LIMIT) {
        if (s->ticking_count == s->ticking_capacity) {
            size_t capacity = s->ticking_capacity ? s->ticking_capacity*2 : 8;
            if (capacity > SIZE_MAX/sizeof(*s->ticking)) return false;
            ScheduledEvent *next = realloc(s->ticking, capacity*sizeof(*next));
            if (!next) return false;
            s->ticking = next;
            s->ticking_capacity = capacity;
        }
        ScheduledEvent event = s->heap[0];
        if (!posmap_set(&s->ticking_slots, event_key(event.pos, event.block), 0))
            return false;
        (void)scheduler_pop_due(s, now, &s->ticking[s->ticking_count++]);
    }
    return true;
}

bool scheduler_next_tick(Scheduler *s, ScheduledEvent *event)
{
    if (s->ticking_head == s->ticking_count) return false;
    *event = s->ticking[s->ticking_head++];
    posmap_remove(&s->ticking_slots, event_key(event->pos, event->block));
    return true;
}
