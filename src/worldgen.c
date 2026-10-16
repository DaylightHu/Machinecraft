#include "worldgen.h"
BlockState world_generated_state(const World *world, BlockPos p)
{
    return block_default(p.y <= 0 && p.y > -world->config.ground_layers ? BLOCK_IRON : BLOCK_AIR);
}
bool world_generate_chunk(const World *world, Chunk *c)
{
    for (int y = 1 - world->config.ground_layers; y <= 0; ++y) {
        unsigned section = (unsigned)(y - WORLD_MIN_Y) / 16;
        if (!c->sections[section]) c->sections[section] = section_create();
        if (!c->sections[section]) return false;
        ChunkSection *s = c->sections[section];
        for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) {
            size_t index = (size_t)floor_mod16(y) * 256 + (size_t)z * 16 + (size_t)x;
            if (!palette_set(&s->blocks, index, BLOCK_IRON)) return false;
            ++s->block_count;
        }
    }
    c->resident = true; return true;
}
