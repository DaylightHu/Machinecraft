#ifndef MC_BLOCK_SHAPE_H
#define MC_BLOCK_SHAPE_H

#include "rs/block.h"

enum { BLOCK_SHAPE_BOXES = 9, BLOCK_SHAPE_EDGES = 256 };

/* Local model coordinates: 0..16 covers one block. These are selection
 * shapes, independent of textures, rendered geometry and collision shapes. */
typedef struct { float minimum[3], maximum[3]; } BlockShapeBox;
typedef struct {
    BlockShapeBox boxes[BLOCK_SHAPE_BOXES];
    unsigned count;
} BlockShape;
typedef struct { float start[3], end[3]; } BlockShapeEdge;

void block_outline_shape(BlockState state, BlockShape *shape);
void block_interaction_shape(BlockState state, BlockShape *shape);
/* Edges of the union: shared faces and internal box seams are omitted. */
unsigned block_shape_edges(const BlockShape *shape,
                           BlockShapeEdge edges[BLOCK_SHAPE_EDGES]);

#endif
