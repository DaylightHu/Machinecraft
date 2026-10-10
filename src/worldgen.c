#include "worldgen.h"

void world_generate_flat(World *world){
    const WorldConfig *config = &world->config;

    for (int y = 0; y < config->ground_layers; ++y){
        for (int z = 0; z < config->size_z; ++z){
            for(int x = 0; x < config->size_x; ++x){
                BlockPos position = {x, y, z};
                world->cells[world_index(world, position)] = block_default(BLOCK_IRON);
            }
        }
    }
}
