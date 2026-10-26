#include "world_command.h"
#include "console_command.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *skip_spaces(char *text)
{
    while (*text && isspace((unsigned char)*text)) ++text;
    return text;
}

WorkspaceWorldCommand workspace_world_command_parse(const char *input)
{
    WorkspaceWorldCommand command = {0};
    if (!input) {
        command.kind = WORKSPACE_WORLD_INVALID;
        return command;
    }
    size_t length = strlen(input);
    if (length >= CONSOLE_COMMAND_CAPACITY) {
        command.kind = WORKSPACE_WORLD_INVALID;
        return command;
    }
    char line[CONSOLE_COMMAND_CAPACITY];
    memcpy(line, input, length + 1);
    char *cursor = skip_spaces(line);
    if (strncmp(cursor, "world", 5) != 0 ||
        (cursor[5] && !isspace((unsigned char)cursor[5]))) {
        command.kind = WORKSPACE_WORLD_NOT_COMMAND;
        return command;
    }
    cursor = skip_spaces(cursor + 5);
    char *verb = cursor;
    while (*cursor && !isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor) *cursor++ = '\0';
    char *argument = skip_spaces(cursor);
    size_t argument_length = strlen(argument);
    while (argument_length && isspace((unsigned char)argument[argument_length - 1]))
        argument[--argument_length] = '\0';

    if (strcmp(verb, "list") == 0) {
        command.kind = *argument ? WORKSPACE_WORLD_INVALID : WORKSPACE_WORLD_LIST;
        return command;
    }
    if (strcmp(verb, "detach") == 0) {
        command.kind = *argument ? WORKSPACE_WORLD_INVALID : WORKSPACE_WORLD_DETACH;
        return command;
    }
    if (strcmp(verb, "create") != 0 && strcmp(verb, "attach") != 0) {
        command.kind = WORKSPACE_WORLD_INVALID;
        return command;
    }
    if (!*argument || argument_length >= sizeof(command.name)) {
        command.kind = WORKSPACE_WORLD_INVALID;
        return command;
    }
    memcpy(command.name, argument, argument_length + 1);
    command.kind = strcmp(verb, "create") == 0 ?
        WORKSPACE_WORLD_CREATE : WORKSPACE_WORLD_ATTACH;
    return command;
}

static bool names_equal(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

static void command_write(WorkspaceWorldCommandIO *io, const char *text,
                          bool error)
{
    if (io->write) io->write(io->write_context, text, error);
}

static void command_error(WorkspaceWorldCommandIO *io, const char *text)
{
    command_write(io, text, true);
}

static void command_reset_target(WorkspaceWorldCommandIO *io)
{
    if (io->reset_target_state)
        io->reset_target_state(io->reset_context);
}

static bool bind_target(WorkspaceWorldCommandIO *io, GameSession **target,
                        GameSession *session,
                        const char *name, bool created)
{
    if (*target == session) {
        char message[GAME_SESSION_NAME_CAPACITY + 48];
        snprintf(message, sizeof(message), "CLI already attached to '%s'", name);
        command_write(io, message, false);
        return true;
    }
    command_reset_target(io);
    *target = session;
    char message[GAME_SESSION_NAME_CAPACITY + 48];
    snprintf(message, sizeof(message), created ? "Created and attached CLI to '%s'" :
             "CLI attached to '%s'", name);
    command_write(io, message, false);
    return true;
}

static GameSessionResult list_saves(WorkspaceWorldCommandIO *io,
                                    GameSaveList *saves)
{
    return game_session_list_saves(io->save_directory, saves);
}

static bool run_list(WorkspaceWorldCommandIO *io, GameSession *target)
{
    GameSaveList saves;
    GameSessionResult result = list_saves(io, &saves);
    if (result != GAME_SESSION_OK && result != GAME_SESSION_TOO_MANY_SAVES) {
        char message[128];
        snprintf(message, sizeof(message), "Cannot list worlds: %s",
                 game_session_result_name(result));
        command_error(io, message);
        return true;
    }
    if (!saves.count) {
        command_write(io, "No saved worlds", false);
        return true;
    }
    for (size_t index = 0; index < saves.count; ++index) {
        GameSession *session = world_session_manager_find(io->sessions,
                                                           saves.names[index]);
        const char *marker = session && session == target ? "> " :
                             session ? "+ " : "  ";
        char message[GAME_SESSION_NAME_CAPACITY + 24];
        snprintf(message, sizeof(message), "%s%s", marker, saves.names[index]);
        command_write(io, message, false);
    }
    return true;
}

static bool run_create(WorkspaceWorldCommandIO *io, GameSession **target,
                       const char *name)
{
    if (world_session_manager_find(io->sessions, name)) {
        command_error(io, "world is already loaded; use world attach <name>");
        return true;
    }
    GameSaveList saves;
    GameSessionResult result = list_saves(io, &saves);
    if (result != GAME_SESSION_OK && result != GAME_SESSION_TOO_MANY_SAVES) {
        char message[128];
        snprintf(message, sizeof(message), "Cannot create world: %s",
                 game_session_result_name(result));
        command_error(io, message);
        return true;
    }
    for (size_t index = 0; index < saves.count; ++index) {
        if (!names_equal(saves.names[index], name)) continue;
        command_error(io, "world already exists; use world attach <name>");
        return true;
    }
    if (world_session_manager_count(io->sessions) >= GAME_SESSION_MAX_SAVES) {
        command_error(io, "too many loaded worlds");
        return true;
    }
    if (saves.count >= GAME_SESSION_MAX_SAVES) {
        command_error(io, "maximum number of saved worlds reached");
        return true;
    }
    char path[GAME_SESSION_PATH_CAPACITY];
    result = game_session_make_path(io->save_directory, name, path);
    if (result != GAME_SESSION_OK) {
        char message[128];
        snprintf(message, sizeof(message), "Cannot create world: %s",
                 game_session_result_name(result));
        command_error(io, message);
        return true;
    }
    GameSession *session = game_session_create(path, world_config_default(),
                                               &result);
    if (!session) {
        char message[128];
        snprintf(message, sizeof(message), "Cannot create world: %s",
                 game_session_result_name(result));
        command_error(io, message);
        return true;
    }
    result = game_session_save(session);
    if (result != GAME_SESSION_OK) {
        game_session_destroy(session);
        char message[128];
        snprintf(message, sizeof(message), "Cannot save world: %s",
                 game_session_result_name(result));
        command_error(io, message);
        return true;
    }
    if (!world_session_manager_add(io->sessions, name, session)) {
        game_session_destroy(session);
        command_error(io,
            "world was saved, but its session could not be registered");
        return true;
    }
    (void)bind_target(io, target, session, name, true);
    io->saves_changed = true;
    return true;
}

static bool run_attach(WorkspaceWorldCommandIO *io, GameSession **target,
                       const char *name)
{
    GameSession *session = world_session_manager_find(io->sessions, name);
    const char *saved_name = name;
    if (session) {
        const char *registered_name =
            world_session_manager_name(io->sessions, session);
        if (registered_name) saved_name = registered_name;
    }
    if (!session) {
        GameSaveList saves;
        GameSessionResult result = list_saves(io, &saves);
        if (result != GAME_SESSION_OK && result != GAME_SESSION_TOO_MANY_SAVES) {
            char message[128];
            snprintf(message, sizeof(message), "Cannot list worlds: %s",
                     game_session_result_name(result));
            command_error(io, message);
            return true;
        }
        saved_name = NULL;
        for (size_t index = 0; index < saves.count; ++index) {
            if (!names_equal(saves.names[index], name)) continue;
            saved_name = saves.names[index];
            break;
        }
        if (!saved_name) {
            char message[GAME_SESSION_NAME_CAPACITY + 48];
            snprintf(message, sizeof(message), "No saved world named '%s'", name);
            command_error(io, message);
            return true;
        }
        if (world_session_manager_count(io->sessions) >= GAME_SESSION_MAX_SAVES) {
            command_error(io, "too many loaded worlds");
            return true;
        }
        char path[GAME_SESSION_PATH_CAPACITY];
        result = game_session_make_path(io->save_directory, saved_name, path);
        if (result == GAME_SESSION_OK) session = game_session_load(path, &result);
        if (!session) {
            char message[128];
            snprintf(message, sizeof(message), "Cannot attach to world: %s",
                     game_session_result_name(result));
            command_error(io, message);
            return true;
        }
        if (!world_session_manager_add(io->sessions, saved_name, session)) {
            game_session_destroy(session);
            command_error(io, "cannot register world session");
            return true;
        }
    }
    return bind_target(io, target, session, saved_name, false);
}

bool workspace_world_command_run(WorkspaceWorldCommandIO *io,
                                 const char *line, GameSession **target)
{
    if (!io || !io->sessions || !io->save_directory || !target) return false;
    io->saves_changed = false;
    WorkspaceWorldCommand command = workspace_world_command_parse(line);
    if (command.kind == WORKSPACE_WORLD_NOT_COMMAND) return false;
    if (command.kind == WORKSPACE_WORLD_INVALID) {
        command_error(io,
            "usage: world create <name> | attach <name> | list | detach");
        return true;
    }
    if (command.kind == WORKSPACE_WORLD_LIST) return run_list(io, *target);
    if (command.kind == WORKSPACE_WORLD_DETACH) {
        if (!*target) {
            command_error(io, "CLI is not attached to a world");
            return true;
        }
        *target = NULL;
        command_reset_target(io);
        command_write(io, "CLI detached from world", false);
        return true;
    }
    if (!game_session_valid_name(command.name)) {
        command_error(io,
            "invalid world name; use letters, numbers, spaces, '-' or '_'");
        return true;
    }
    if (command.kind == WORKSPACE_WORLD_CREATE)
        return run_create(io, target, command.name);
    return run_attach(io, target, command.name);
}
