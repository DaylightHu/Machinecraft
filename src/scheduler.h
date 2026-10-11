#ifndef RS_SCHEDULER.h
#define RS_SCHEDULER.h

typedef struct {uint64_t due; uint32_t index;} ScheduledEvent;
typedef struct {
    ScheduledEvent *heap;
    uint32_t *slots; /* Indexed heap: at most ONE delayed event per cell. */
    size_t count, capacity, volume;
} Scheduler;

bool scheduler_init(Scheduler *s, size_t volume);
bool scheduler_reserve(Scheduler *s, size_t capacity);
void scheduler_destroy(Scheduler *s);
bool scheduler_pending(const Scheduler *s, uint32_t index);
bool scheduler_add(Scheduler *s, uint32_t index, uint64_t due);
void scheduler_cancel(Scheduler *s, uint32_t index);
bool scheduler_pop_due(Scheduler *s, uint64_t now, ScheduledEvent *event);

#endif
