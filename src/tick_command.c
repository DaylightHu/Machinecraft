#include "tick_command.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool parse_count(const char *text, uint32_t *count)
{
    uint32_t value = 0;
    if (!*text) return false;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        unsigned digit = (unsigned)(*text - '0');
        if (value > (UINT32_MAX - digit)/10u) return false;
        value = value*10u + digit;
    }
    *count = value;
    return value != 0;
}

bool tick_command_parse(const char *line, TickCommand *command)
{
    if (!line || !command || strlen(line) >= 128) return false;
    *command = (TickCommand){0};
    char copy[128];
    strcpy(copy, line);
    char *tokens[3];
    int count = 0;
    char *cursor = copy;
    while (*cursor) {
        cursor += strspn(cursor, " \t\r\n");
        if (!*cursor) break;
        if (count == 3) return false;
        tokens[count++] = cursor;
        cursor += strcspn(cursor, " \t\r\n");
        if (*cursor) *cursor++ = '\0';
    }
    if (count < 1 || count > 2) return false;
    const char *name = tokens[0][0] == '/' ? tokens[0] + 1 : tokens[0];
    if (strcmp(name, "tick") != 0) return false;
    if (count == 1) {
        *command = (TickCommand){TICK_COMMAND_STEP, 1};
    } else if (strcmp(tokens[1], "freeze") == 0) {
        command->kind = TICK_COMMAND_FREEZE;
    } else if (strcmp(tokens[1], "noupdate") == 0) {
        command->kind = TICK_COMMAND_NOUPDATE;
    } else if (parse_count(tokens[1], &command->count)) {
        command->kind = TICK_COMMAND_STEP;
    } else {
        return false;
    }
    return true;
}

RsResult tick_command_run(World *world, const TickCommand *command,
                          char *message, size_t capacity)
{
    if (!world || !command || !message || !capacity)
        return RS_INVALID_ARGUMENT;
    message[0] = '\0';
    RsResult result = sim_flush(world);
    if (result != RS_OK) return result;
    switch (command->kind) {
    case TICK_COMMAND_FREEZE:
        result = sim_set_frozen(world, !sim_is_frozen(world));
        if (result != RS_OK) return result;
        snprintf(message, capacity, "%s", sim_is_frozen(world) ? "Frozen" : "Running");
        return RS_OK;
    case TICK_COMMAND_NOUPDATE: {
        bool enabled = !sim_updates_enabled(world);
        result = sim_set_updates_enabled(world, enabled);
        if (result == RS_OK)
            snprintf(message, capacity, "Block updates %s",
                     enabled ? "enabled (skipped updates are not replayed)" : "disabled");
        return result;
    }
    case TICK_COMMAND_STEP:
        if (!sim_is_frozen(world)) {
            snprintf(message, capacity, "Use /tick freeze before stepping");
            return RS_INVALID_ARGUMENT;
        }
        result = sim_request_steps(world, command->count);
        if (result == RS_OK)
            snprintf(message, capacity, "Queued %" PRIu32 " ticks; %" PRIu64 " pending (frozen)",
                     command->count, sim_pending_steps(world));
        else if (result == RS_INVALID_ARGUMENT)
            snprintf(message, capacity, "Tick count exceeds the remaining game time");
        return result;
    default:
        return RS_INVALID_ARGUMENT;
    }
}
