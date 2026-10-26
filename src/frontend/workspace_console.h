#ifndef MC_WORKSPACE_CONSOLE_H
#define MC_WORKSPACE_CONSOLE_H

#include "console_command.h"
#include "raylib.h"

typedef struct WorkspaceConsole WorkspaceConsole;
WorkspaceConsole *workspace_console_create(void);
void workspace_console_destroy(WorkspaceConsole *console);
ConsoleCommandReply workspace_console_update(WorkspaceConsole *console, GameSession *session);
void workspace_console_draw(WorkspaceConsole *console, Rectangle box, int pane,
                              bool focused, bool zoomed);
void workspace_console_message(WorkspaceConsole *console, const char *text, bool error);

#endif
