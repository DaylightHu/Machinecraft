#ifndef RS_POSMAP_H
#define RS_POSMAP_H
#include "rs/block.h"
#include <stddef.h>
typedef struct { BlockPos key; size_t value; unsigned char status; } PosEntry;
typedef struct { PosEntry *entries; size_t count, used, capacity; } PosMap;
bool pos_equal(BlockPos a, BlockPos b);
int pos_compare(BlockPos a, BlockPos b);
bool posmap_get(const PosMap *map, BlockPos key, size_t *value);
bool posmap_reserve(PosMap *map, size_t count);
bool posmap_set(PosMap *map, BlockPos key, size_t value);
void posmap_remove(PosMap *map, BlockPos key);
void posmap_clear(PosMap *map);
void posmap_destroy(PosMap *map);
#endif
