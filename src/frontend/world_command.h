#ifndef MC_FRONTEND_WORLD_COMMAND_H
#define MC_FRONTEND_WORLD_COMMAND_H

#include "rs/session.h"
#include "world_session_manager.h"

typedef enum {
    WORKSPACE_WORLD_NOT_COMMAND,
    WORKSPACE_WORLD_INVALID,
    WORKSPACE_WORLD_CREATE,
    WORKSPACE_WORLD_ATTACH,
    WORKSPACE_WORLD_LIST,
    WORKSPACE_WORLD_DETACH
} WorkspaceWorldCommandKind;

typedef struct {
    WorkspaceWorldCommandKind kind;
    char name[GAME_SESSION_NAME_CAPACITY];
} WorkspaceWorldCommand;

typedef void (*WorkspaceWorldCommandWrite)(void *context, const char *text,
                                           bool error);
typedef void (*WorkspaceWorldCommandReset)(void *context);

typedef struct {
    WorldSessionManager *sessions;
    const char *save_directory;
    const char *frontend_name;
    WorkspaceWorldCommandWrite write;
    void *write_context;
    WorkspaceWorldCommandReset reset_target_state;
    void *reset_context;
    bool saves_changed;
} WorkspaceWorldCommandIO;

/* World names may contain spaces; the rest of the line is the name. */
WorkspaceWorldCommand workspace_world_command_parse(const char *line);
/* Executes world commands for a CLI target without depending on the GUI. */
bool workspace_world_command_run(WorkspaceWorldCommandIO *io,
                                 const char *line, GameSession **target);

#endif
