#include "plot_command.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { COMMAND_CAPACITY = 128, TOKEN_CAPACITY = 5 };

static int split(char *line, char *tokens[TOKEN_CAPACITY])
{
    int count = 0;
    char *cursor = line;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n') ++cursor;
        if (!*cursor) break;
        if (count == TOKEN_CAPACITY) return -1;
        tokens[count++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' &&
               *cursor != '\r' && *cursor != '\n') ++cursor;
        if (*cursor) *cursor++ = '\0';
    }
    return count;
}

static bool parse_direction(const char *text, Direction *direction)
{
    static const char *const names[] = {"east", "west", "south", "north"};
    static const Direction values[] = {DIR_EAST, DIR_WEST, DIR_SOUTH, DIR_NORTH};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if (strcmp(text, names[i]) == 0) {
            *direction = values[i];
            return true;
        }
    }
    return false;
}

static bool parse_count(const char *text, uint32_t *count)
{
    if (!*text || *text == '-') return false;
    errno = 0;
    char *end = NULL;
    uintmax_t value = strtoumax(text, &end, 10);
    if (errno == ERANGE || end == text || *end || !value || value > UINT32_MAX)
        return false;
    *count = (uint32_t)value;
    return true;
}

bool plot_command_parse(const char *line, PlotCommand *command)
{
    if (!line || !command) return false;
    size_t length = strlen(line);
    if (length >= COMMAND_CAPACITY) return false;
    char copy[COMMAND_CAPACITY];
    memcpy(copy, line, length + 1);
    char *tokens[TOKEN_CAPACITY];
    int count = split(copy, tokens);
    if (count < 2) return false;
    const char *name = tokens[0][0] == '/' ? tokens[0] + 1 : tokens[0];
    if (strcmp(name, "plot") != 0) return false;
    *command = (PlotCommand){.count = 1};
    if (count == 2 && strcmp(tokens[1], "info") == 0) {
        command->kind = PLOT_COMMAND_INFO;
        return true;
    }
    if ((count == 3 || count == 4) &&
        (strcmp(tokens[1], "expand") == 0 || strcmp(tokens[1], "shrink") == 0) &&
        parse_direction(tokens[2], &command->direction) &&
        (count == 3 || parse_count(tokens[3], &command->count))) {
        command->kind = strcmp(tokens[1], "expand") == 0 ? PLOT_COMMAND_EXPAND : PLOT_COMMAND_SHRINK;
        return true;
    }
    return false;
}

static void describe(const World *world, const char *prefix,
                     char *message, size_t capacity)
{
    WorldPlotBounds bounds = world_plot_bounds(world);
    uint64_t width = (uint64_t)((int64_t)bounds.max_x - bounds.min_x + 1);
    uint64_t depth = (uint64_t)((int64_t)bounds.max_z - bounds.min_z + 1);
    snprintf(message, capacity,
             "%sX %d..%d, Z %d..%d; %" PRIu64 "x%" PRIu64
             " blocks (%" PRIu64 "x%" PRIu64 " plots)",
             prefix, bounds.min_x, bounds.max_x, bounds.min_z, bounds.max_z,
             width, depth, width / WORLD_PLOT_SIZE, depth / WORLD_PLOT_SIZE);
}

RsResult plot_command_run(World *world, const PlotCommand *command,
                          PlotCommandState *state, char *message, size_t capacity)
{
    if (!world || !command || !state || !message || !capacity || state->pending)
        return RS_INVALID_ARGUMENT;
    message[0] = '\0';
    if (command->kind == PLOT_COMMAND_INFO) {
        describe(world, "Plot: ", message, capacity);
        return RS_OK;
    }
    if (command->kind == PLOT_COMMAND_SHRINK) {
        WorldShrinkPreview preview;
        RsResult result = world_preview_shrink(world, command->direction, command->count, &preview);
        if (result == RS_OUT_OF_BOUNDS)
            snprintf(message, capacity, "Cannot shrink: both axes must keep at least %d blocks", WORLD_PLOT_SIZE);
        if (result != RS_OK) return result;
        if (preview.has_edits) {
            *state = (PlotCommandState){.command = *command, .preview = preview, .pending = true};
            WorldPlotBounds b = preview.removed;
            snprintf(message, capacity,
                     "WARNING: removing X %d..%d, Z %d..%d may delete edits and inventories. Enter y/n:",
                     b.min_x,b.max_x,b.min_z,b.max_z);
            return RS_OK;
        }
        result = world_shrink_plot(world, command->direction, command->count);
        if (result == RS_OK) describe(world, "Shrunk: ", message, capacity);
        return result;
    }
    if (command->kind != PLOT_COMMAND_EXPAND) return RS_INVALID_ARGUMENT;
    RsResult result = world_expand_plot(world, command->direction, command->count);
    if (result == RS_OK) describe(world, "Expanded: ", message, capacity);
    return result;
}

RsResult plot_command_answer(World *world, PlotCommandState *state,
                              const char *answer, char *message, size_t capacity)
{
    if (!world || !state || !state->pending || !answer || !message || !capacity)
        return RS_INVALID_ARGUMENT;
    message[0] = '\0';
    answer += strspn(answer, " \t\r\n");
    size_t length = strcspn(answer, " \t\r\n");
    const char *end = answer + length;
    end += strspn(end, " \t\r\n");
    bool yes = length == 1 && (answer[0] == 'y' || answer[0] == 'Y') && !*end;
    bool no = length == 1 && (answer[0] == 'n' || answer[0] == 'N') && !*end;
    if (!yes && !no) {
        snprintf(message, capacity, "Please enter y to shrink, or n to cancel.");
        return RS_OK;
    }
    state->pending = false;
    if (no) { snprintf(message, capacity, "Shrink cancelled"); return RS_OK; }
    WorldPlotBounds before = state->preview.before, now = world_plot_bounds(world);
    if (before.min_x != now.min_x || before.max_x != now.max_x ||
        before.min_z != now.min_z || before.max_z != now.max_z) {
        snprintf(message, capacity, "Plot bounds changed; run the shrink command again.");
        return RS_INVALID_ARGUMENT;
    }
    RsResult result = world_shrink_plot(world, state->command.direction, state->command.count);
    if (result == RS_OK) describe(world, "Shrunk: ", message, capacity);
    return result;
}
