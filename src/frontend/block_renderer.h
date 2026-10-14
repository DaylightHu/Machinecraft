#ifndef MC_BLOCK_RENDERER_H
#define MC_BLOCK_RENDERER_H

#include "raylib.h"
#include "rs/block.h"

typedef struct{
    Texture2D atlas;
    Texture2D wire_atlas;
    Texture2D torch_atlas;
    Shader cutout_shader;
} BlockRenderer;

bool block_renderer_init(BlockRenderer *renderer);
void block_renderer_unload(BlockRenderer *renderer);
void block_renderer_draw_cube(const BlockRenderer *renderer, BlockId id, Vector3 center);

/* Draw dust and torches after solids, inside one shared cutout shader pass. */
void block_renderer_begin_cutouts(const BlockRenderer *renderer);
void block_renderer_draw_wire(const BlockRenderer *renderer, BlockState state,
                              Vector3 center);
void block_renderer_draw_torch(const BlockRenderer *renderer, BlockState state,
                               Vector3 center);
void block_renderer_end_cutouts(void);

#endif
