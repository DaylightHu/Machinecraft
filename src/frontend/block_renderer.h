#ifndef MC_BLOCK_RENDERER_H
#define MC_BLOCK_RENDERER_H

#include "raylib.h"
#include "rs/block.h"

typedef struct{
    Texture2D atlas;
} BlockRenderer;

bool block_renderer_init(BlockRenderer *renderer);
void block_renderer_unload(BlockRenderer *renderer);
void block_renderer_draw_cube(const BlockRenderer *renderer, BlockId id, Vector3 center);

#endif
