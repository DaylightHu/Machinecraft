#ifndef RS_QUEUE_H
#define RS_QUEUE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct{
    uint32_t *items;
    uint8_t *pending;
    size_t capacity, head, tail, count;
} IndexQueue;

bool queue_init(IndexQueue *queue, size_t capacity);
void queue_destroy(IndexQueue *queue);
bool queue_push(IndexQueue *queue, uint32_t index);
bool queue_pop(IndexQueue *queue, uint32_t *index);

#endif
