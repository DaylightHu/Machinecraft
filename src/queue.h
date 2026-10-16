#ifndef RS_QUEUE_H
#define RS_QUEUE_H
#include "posmap.h"
typedef struct {
    BlockPos *items;
    size_t capacity, head, count;
    PosMap pending;
} PositionQueue;
void queue_destroy(PositionQueue *queue);
bool queue_push(PositionQueue *queue, BlockPos pos);
bool queue_pop(PositionQueue *queue, BlockPos *pos);
#endif
