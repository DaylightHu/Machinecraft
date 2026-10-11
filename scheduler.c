#include "scheduler.h"

#include <stdlib.h>
#include <string.h>

static bool before(ScheduledEvent first, ScheduledEvent second)
{
    return first.due < second.due || (first.due == second.due && first.index < second.index);
}

static void swap(Scheduler *scheduler, size_t first, size_t second)
{
    ScheduledEvent temporary = scheduler->heap[first];
    scheduler->heap[first] = scheduler->heap[second];
    scheduler->heap[second] = temporary;
    scheduler->slots[scheduler->heap[first].index] = (uint32_t)first;
    scheduler->slots[scheduler->heap[second].index] = (uint32_t)second;
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

bool scheduler_init(Scheduler *scheduler, size_t volume)
{
    memset(scheduler, 0, sizeof(*scheduler));
    scheduler->volume = volume;
    scheduler->slots = malloc(volume * sizeof(*scheduler->slots));
    if (!scheduler->slots) {
        return false;
    }
    for (size_t index = 0; index < volume; ++index) {
        scheduler->slots[index] = RS_NONE;
    }
    return true;
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
    free(scheduler->slots);
    memset(scheduler, 0, sizeof(*scheduler));
}

bool scheduler_pending(const Scheduler *scheduler, uint32_t index)
{
    return index < scheduler->volume && scheduler->slots[index] != RS_NONE;
}

bool scheduler_add(Scheduler *scheduler, uint32_t index, uint64_t due)
{
    if (index >= scheduler->volume) {
        return false;
    }
    if (scheduler_pending(scheduler, index)) {
        return true; /* Keep the first due time. */
    }
    if (scheduler->count == scheduler->capacity) {
        return false;
    }
    size_t slot = scheduler->count++;
    scheduler->heap[slot] = (ScheduledEvent){due, index};
    scheduler->slots[index] = (uint32_t)slot;
    sift_up(scheduler, slot);
    return true;
}

void scheduler_cancel(Scheduler *scheduler, uint32_t index)
{
    if (!scheduler_pending(scheduler, index)) {
        return;
    }
    size_t slot = scheduler->slots[index];
    scheduler->slots[index] = RS_NONE;
    --scheduler->count;
    if (slot == scheduler->count) {
        return;
    }
    scheduler->heap[slot] = scheduler->heap[scheduler->count];
    scheduler->slots[scheduler->heap[slot].index] = (uint32_t)slot;
    if (slot && before(scheduler->heap[slot], scheduler->heap[(slot - 1) / 2])) {
        sift_up(scheduler, slot);
    } else {
        sift_down(scheduler, slot);
    }
}

bool scheduler_pop_due(Scheduler *scheduler, uint64_t now, ScheduledEvent *event)
{
    if (!scheduler->count || scheduler->heap[0].due > now) {
        return false;
    }
    *event = scheduler->heap[0];
    scheduler_cancel(scheduler, event->index);
    return true;
}

