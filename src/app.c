#include "app.h"
#include "frontend/cli.h"
#include "rs/world.h"

#include <stdlib.h>

int app_run(FILE *input, FILE *output, FILE *errors)
{
    if (!input || !output || !errors) {
        return EXIT_FAILURE;
    }

    World *world = world_create(world_config_default());
    if (!world) {
        fputs("error: cannot create world\n", errors);
        return EXIT_FAILURE;
    }

    int result = cli_run(world, input, output, errors);
    world_destroy(world);
    return result;
}
