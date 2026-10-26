#include "world_command.h"
#include "console_command.h"

#include <ctype.h>
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
