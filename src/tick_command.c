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

RsResult tick_command_run(World *world, TickControl *control,
                          const TickCommand *command,
                          char *message, size_t capacity)
{
    if (!world || !control || !command || !message || !capacity)
        return RS_INVALID_ARGUMENT;
    message[0] = '\0';
    RsResult result = sim_flush(world);
    if (result != RS_OK) return result;
    switch (command->kind) {
    case TICK_COMMAND_FREEZE:
        control->frozen = !control->frozen;
        control->remaining = 0;
        snprintf(message, capacity, "%s at tick %" PRIu64,
                 control->frozen ? "Frozen" : "Running", world_tick(world));
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
        if (!control->frozen) {
            snprintf(message, capacity, "Use /tick freeze before stepping");
            return RS_INVALID_ARGUMENT;
        }
        if (!command->count || control->remaining > UINT64_MAX - world_tick(world) ||
            command->count > UINT64_MAX - world_tick(world) - control->remaining) {
            snprintf(message, capacity, "Tick count exceeds the remaining game time");
            return RS_INVALID_ARGUMENT;
        }
        control->remaining += command->count;
        snprintf(message, capacity, "Queued %" PRIu32 " ticks; %" PRIu64 " pending (frozen)",
                 command->count, control->remaining);
        return RS_OK;
    default:
        return RS_INVALID_ARGUMENT;
    }
}

RsResult tick_control_step(World *world, TickControl *control)
{
    if (!world || !control) return RS_INVALID_ARGUMENT;
    RsResult status = sim_flush(world);
    if (status != RS_OK) return status;
    if (control->frozen && !control->remaining) return RS_UNCHANGED;
    uint64_t before = world_tick(world);
    RsResult result = sim_step(world);
    /* A failed callback can still have advanced the clock. Never repeat it. */
    if (control->remaining && world_tick(world) != before) --control->remaining;
    return result;
}
