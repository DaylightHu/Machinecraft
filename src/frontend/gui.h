#ifndef MC_GUI_H
#define MC_GUI_H

#include "world_session_manager.h"

/* The host retains the manager after this frontend detaches or returns. */
int gui_run(WorldSessionManager *world_sessions);

#endif
