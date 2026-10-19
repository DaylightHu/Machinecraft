#ifndef MC_WORLD_RENDERER_H
#define MC_WORLD_RENDERER_H

#include "block_renderer.h"
#include "rs/world.h"

typedef struct WorldRenderer WorldRenderer;

/* Visible Section render cache only. Owns the world's optional change observer.
 * Destroy before the World and before unloading the BlockRenderer textures. */
WorldRenderer *world_renderer_create(World *world,
                                     const BlockRenderer *blocks);
void world_renderer_destroy(WorldRenderer *renderer);
bool world_renderer_sync(WorldRenderer *renderer);
void world_renderer_draw(WorldRenderer *renderer);
/* Local visual override for the one barrel whose palette is open. */
void world_renderer_set_barrel_open(WorldRenderer *renderer, BlockPos pos,
                                    bool open);

#endif
