#include "queue.h"
#include <stdlib.h>
#include <string.h>
void queue_destroy(PositionQueue *q)
{
    free(q->items); posmap_destroy(&q->pending); *q = (PositionQueue){0};
}
bool queue_push(PositionQueue *q, BlockPos p)
{
    if (posmap_get(&q->pending, p, NULL)) return true;
    if (q->head && q->count == q->capacity) {
        memmove(q->items, q->items + q->head, (q->count - q->head) * sizeof(BlockPos));
        q->count -= q->head; q->head = 0;
    }
    if (q->count == q->capacity) {
        size_t capacity = q->capacity ? q->capacity * 2 : 16;
        if (capacity < q->capacity || capacity > SIZE_MAX / sizeof(BlockPos)) return false;
        BlockPos *next = realloc(q->items, capacity * sizeof(BlockPos));
        if (!next) return false;
        q->items = next; q->capacity = capacity;
    }
    if (!posmap_set(&q->pending, p, 0)) return false;
    q->items[q->count++] = p; return true;
}
bool queue_pop(PositionQueue *q, BlockPos *p)
{
    if (q->head == q->count) return false;
    *p = q->items[q->head++]; posmap_remove(&q->pending, *p);
    if (q->head == q->count) q->head = q->count = 0;
    return true;
}
