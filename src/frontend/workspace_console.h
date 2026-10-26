#ifndef MC_WORKSPACE_CONSOLE_H
#define MC_WORKSPACE_CONSOLE_H

#include "console_command.h"
#include "raylib.h"

typedef struct WorkspaceConsole WorkspaceConsole;
typedef bool (*WorkspaceConsoleWorldCommand)(void *context,
    WorkspaceConsole *console, const char *line, GameSession **session);
WorkspaceConsole *workspace_console_create(void);
void workspace_console_destroy(WorkspaceConsole *console);
/* Drop command state tied to the previous world while preserving this pane's
 * output and input history. */
void workspace_console_detach_world(WorkspaceConsole *console);
ConsoleCommandReply workspace_console_update(WorkspaceConsole *console,
    GameSession **session, WorkspaceConsoleWorldCommand world_command,
    void *context);
void workspace_console_draw(WorkspaceConsole *console, Rectangle box, int pane,
                              bool focused, bool zoomed);
void workspace_console_message(WorkspaceConsole *console, const char *text, bool error);

#endif
