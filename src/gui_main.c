#include "frontend/gui.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    WorldSessionManager *world_sessions = world_session_manager_create();
    if (!world_sessions) {
        fputs("Cannot create world session manager\n", stderr);
        return EXIT_FAILURE;
    }
    int result = gui_run(world_sessions);
    world_session_manager_destroy(world_sessions);
    return result;
}
