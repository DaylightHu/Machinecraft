#ifndef RS_SCHEDULER_H
#define RS_SCHEDULER_H
#include "posmap.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { uint64_t due; BlockPos pos; } ScheduledEvent;
typedef struct {
    ScheduledEvent *heap;
    PosMap slots; /* Indexed heap: at most ONE delayed event per cell. */
    size_t count, capacity;
} Scheduler;
void scheduler_init(Scheduler *s);
bool scheduler_reserve(Scheduler *s, size_t capacity);
void scheduler_destroy(Scheduler *s);
bool scheduler_pending(const Scheduler *s, BlockPos pos);
bool scheduler_add(Scheduler *s, BlockPos pos, uint64_t due);
void scheduler_cancel(Scheduler *s, BlockPos pos);
bool scheduler_pop_due(Scheduler *s, uint64_t now, ScheduledEvent *event);
#endif
