#include "mcmux.h"

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
