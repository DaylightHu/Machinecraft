#ifndef RS_SCHEDULER_H
#define RS_SCHEDULER_H
#include "posmap.h"
#include "rs/world.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum {
    TICK_EXTREMELY_HIGH = 0,
    TICK_VERY_HIGH,
    TICK_HIGH,
    TICK_NORMAL,
    TICK_LOW,
    TICK_VERY_LOW,
    TICK_EXTREMELY_LOW
} TickPriority;
/* Standing and wall torches are separate block types in Java 1.16.2. Keep
 * this synthetic type stable when internal BlockIds are appended. */
enum { TICK_WALL_TORCH = UINT8_MAX - 1, TICK_BLOCK_TYPES = UINT8_MAX,
       TICKS_PER_STEP_LIMIT = 65536 };
typedef struct {
    uint64_t due, sequence;
    BlockPos pos;
    uint8_t block, priority; /* TickPriority fits in one byte. */
} ScheduledEvent;
typedef struct {
    ScheduledEvent *heap;
    PosMap slots; /* Indexed by position AND scheduled block type. */
    size_t count, capacity;
    uint64_t next_sequence;
    ScheduledEvent *ticking;
    PosMap ticking_slots;
    size_t ticking_head, ticking_count, ticking_capacity;
} Scheduler;
uint8_t scheduler_block_type(BlockState state);
void scheduler_init(Scheduler *s);
bool scheduler_reserve(Scheduler *s, size_t capacity);
void scheduler_destroy(Scheduler *s);
bool scheduler_pending(const Scheduler *s, BlockPos pos, uint8_t block);
bool scheduler_will_tick(const Scheduler *s, BlockPos pos, uint8_t block);
bool scheduler_add(Scheduler *s, BlockPos pos, uint8_t block, uint64_t due);
bool scheduler_add_priority(Scheduler *s, BlockPos pos, uint8_t block, uint64_t due,
                            TickPriority priority);
void scheduler_cancel(Scheduler *s, BlockPos pos, uint8_t block);
bool scheduler_pop_due(Scheduler *s, uint64_t now, ScheduledEvent *event);
/* Freeze the due batch before callbacks; newly scheduled ticks stay queued. */
bool scheduler_begin_tick(Scheduler *s, uint64_t now);
bool scheduler_next_tick(Scheduler *s, ScheduledEvent *event);
#endif
