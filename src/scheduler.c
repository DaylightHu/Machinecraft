#include "scheduler.h"

#include <stdlib.h>
#include <string.h>

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
    (void)posmap_set(&scheduler->slots, scheduler->heap[first].pos, first);
    (void)posmap_set(&scheduler->slots, scheduler->heap[second].pos, second);
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
    posmap_destroy(&scheduler->slots);
    memset(scheduler, 0, sizeof(*scheduler));
}

bool scheduler_pending(const Scheduler *s, BlockPos pos)
{
    return posmap_get(&s->slots, pos, NULL);
}

bool scheduler_add(Scheduler *s, BlockPos pos, uint64_t due)
{
    return scheduler_add_priority(s, pos, due, TICK_NORMAL);
}

bool scheduler_add_priority(Scheduler *s, BlockPos pos, uint64_t due,
                            TickPriority priority)
{
    if (scheduler_pending(s, pos)) return true;
    if ((unsigned)priority > TICK_EXTREMELY_LOW ||
        s->next_sequence == UINT64_MAX) return false;
    if (s->count == s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 8;
        if (capacity < s->capacity || !scheduler_reserve(s, capacity)) return false;
    }
    if (!posmap_set(&s->slots, pos, s->count)) return false;
    size_t slot = s->count++;
    s->heap[slot] = (ScheduledEvent){
        .due = due,
        .sequence = s->next_sequence++,
        .pos = pos,
        .priority = priority
    };
    sift_up(s, slot); return true;
}

void scheduler_cancel(Scheduler *s, BlockPos pos)
{
    size_t slot;
    if (!posmap_get(&s->slots, pos, &slot)) return;
    posmap_remove(&s->slots, pos);
    --s->count;
    if (slot == s->count) return;
    s->heap[slot] = s->heap[s->count];
    (void)posmap_set(&s->slots, s->heap[slot].pos, slot);
    if (slot && before(s->heap[slot], s->heap[(slot - 1) / 2])) sift_up(s, slot);
    else sift_down(s, slot);
}

bool scheduler_pop_due(Scheduler *scheduler, uint64_t now, ScheduledEvent *event)
{
    if (!scheduler->count || scheduler->heap[0].due > now) {
        return false;
    }
    *event = scheduler->heap[0];
    scheduler_cancel(scheduler, event->pos);
    return true;
}
