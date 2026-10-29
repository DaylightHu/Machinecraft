#ifndef MC_WORKSPACE_EDITOR_H
#define MC_WORKSPACE_EDITOR_H

#include "console_command.h"
#include "raylib.h"

typedef struct WorkspaceEditor WorkspaceEditor;
typedef struct WorkspaceOutput WorkspaceOutput;
typedef struct WorkspaceCode WorkspaceCode;
typedef bool (*WorkspaceEditorWorldCommand)(void *context,
    WorkspaceEditor *editor, const char *line, GameSession **session);
/* Called before launching :run; false aborts. :make does not call it. */
typedef bool (*WorkspaceEditorPrepareRun)(void *context,
    WorkspaceEditor *editor);

WorkspaceEditor *workspace_editor_create(void);
void workspace_editor_destroy(WorkspaceEditor *editor);
void workspace_editor_set_output(WorkspaceEditor *editor, WorkspaceOutput *output,
                                  int pane);
void workspace_editor_set_code(WorkspaceEditor *editor, WorkspaceCode *code);
void workspace_editor_detach_world(WorkspaceEditor *editor);
bool workspace_editor_has_unsaved(const WorkspaceEditor *editor);
ConsoleCommandReply workspace_editor_update(WorkspaceEditor *editor,
    GameSession **session, WorkspaceEditorWorldCommand world_command,
    WorkspaceEditorPrepareRun prepare_run, void *context);
void workspace_editor_draw(WorkspaceEditor *editor, Rectangle box, int pane,
                           bool focused, bool zoomed);
void workspace_editor_message(WorkspaceEditor *editor, const char *text,
                              bool error);

#endif
