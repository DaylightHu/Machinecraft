#ifndef MC_FRONTEND_WORLD_COMMAND_H
#define MC_FRONTEND_WORLD_COMMAND_H

#include "rs/session.h"

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

/* World names may contain spaces; the rest of the line is the name. */
WorkspaceWorldCommand workspace_world_command_parse(const char *line);

#endif
