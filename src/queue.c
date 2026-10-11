#include "queue.h"

#include <stdlib.h>
#include <string.h>

bool queue_init(IndexQueue *queue, size_t capacity)
{
    memset(queue, 0, sizeof(*queue));
    queue->capacity = capacity;
    queue->items = malloc(capacity * sizeof(*queue->items));
    queue->pending = calloc(capacity, 1);
    if (!queue->items || !queue->pending) {
        queue_destroy(queue);
        return false;
    }
    return true;
}

void queue_destroy(IndexQueue *queue)
{
    free(queue->items);
    free(queue->pending);
    memset(queue, 0, sizeof(*queue));
}

bool queue_push(IndexQueue *queue, uint32_t index)
{
    if (index >= queue->capacity) {
        return false;
    }
    if (queue->pending[index]) {
        return true;
    }
    if (queue->count == queue->capacity) {
        return false;
    }
    queue->pending[index] = 1;
    queue->items[queue->tail] = index;
    queue->tail = (queue->tail + 1) % queue->capacity;
    ++queue->count;
    return true;
}

bool queue_pop(IndexQueue *queue, uint32_t *index)
{
    if (!queue->count) {
        return false;
    }
    *index = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    --queue->count;
    queue->pending[*index] = 0;
    return true;
}
