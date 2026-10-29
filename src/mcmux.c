#include "mcmux.h"
#include "save_file.h"

#include <stdio.h>
#include <stdlib.h>

World *load_mcs(const char *path)
{
    StructureResult result;
    World *world = structure_load(path, &result);
    if (!world) {
        fprintf(stderr, "Error loading '%s': %s\n",
                path ? path : "(null)", structure_result_name(result));
        exit(EXIT_FAILURE);
    }
    printf("%s loaded. \n", path);
    return world;
}

GameSessionResult save_mcw(World *world, const char *path)
{
    if (!world || !path || !*path)
        return GAME_SESSION_INVALID_ARGUMENT;

    if (sim_flush(world) != RS_OK)
        return GAME_SESSION_SIMULATION_ERROR;

    return save_file_write(world, path);
}
