#include "rs/world.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    WorldConfig config = world_config_default();
    World *world = world_create(config);
    if (!world) {
        fputs("Cannot create world.\n", stderr);
        return EXIT_FAILURE;
    }

    printf("World ready: %d x %d x %d (%zu cells).\n",
           config.size_x, config.size_y, config.size_z, world_volume(world));
    world_destroy(world);
    return EXIT_SUCCESS;
}
