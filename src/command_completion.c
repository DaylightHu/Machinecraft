#include "command_completion.h"
#include "rs/block.h"

#include <string.h>

static const char *const shared_commands[] = {"tick", "plot"};
static const char *const cli_commands[] = {
    "help", "place", "remove", "get", "dot", "delay", "mode", "tune",
    "capacity", "hit", "slot", "slots", "stats", "dropper-seed",
    "save", "title", "quit"
};
static const char *const tick_commands[] = {"freeze", "step", "noupdate"};
static const char *const plot_commands[] = {"info", "expand", "shrink"};
static const char *const plot_directions[] = {"east", "west", "south", "north"};
static const char *const directions[] = {"east", "west", "up", "down", "south", "north"};
static const char *const projectiles[] = {"arrow", "trident", "other"};

#define COUNT(array) (sizeof(array)/sizeof((array)[0]))

bool command_complete(CommandCompletion *state, char *line, size_t capacity,
                      bool gui)
{
    if (!state || !line || !capacity) return false;
    size_t length = 0;
    while (length < capacity && line[length]) ++length;
    if (length == capacity || length >= COMMAND_COMPLETION_CAPACITY) return false;
    if (!state->active || strcmp(line, state->completed) != 0) {
        memcpy(state->original, line, length + 1);
        state->next = 0;
    }
    state->active = false;
    char copy[COMMAND_COMPLETION_CAPACITY];
    strcpy(copy, state->original);
    char *tokens[8];
    size_t count = 0;
    char *cursor = copy;
    for (;;) {
        cursor += strspn(cursor, " \t");
        if (count == COUNT(tokens)) return false;
        tokens[count++] = cursor;
        cursor += strcspn(cursor, " \t");
        if (!*cursor) break;
        *cursor++ = '\0';
    }
    char *word = tokens[count - 1];
    bool slash = tokens[0][0] == '/';
    const char *name = tokens[0] + (slash ? 1 : 0);
    if (count == 1 && slash) ++word;
    size_t offset = (size_t)(word - copy);
    size_t prefix = strlen(word);
    const char *choices[BLOCK_INTERNAL_FIRST + COUNT(cli_commands) +
                         COUNT(shared_commands) + DIR_COUNT];
    size_t choices_count = 0;
#define ADD(array) do { \
    for (size_t i = 0; i < COUNT(array); ++i) choices[choices_count++] = (array)[i]; \
} while (0)
    if (count == 1) {
        ADD(shared_commands);
        if (!gui && !slash) ADD(cli_commands);
    } else if (count == 2 && strcmp(name, "tick") == 0) {
        ADD(tick_commands);
    } else if (count == 2 && strcmp(name, "plot") == 0) {
        ADD(plot_commands);
    } else if (count == 3 && strcmp(name, "plot") == 0 &&
               (strcmp(tokens[1], "expand") == 0 || strcmp(tokens[1], "shrink") == 0)) {
        ADD(plot_directions);
    } else if (!gui && !slash && strcmp(name, "place") == 0) {
        if (count == 5) {
            for (BlockId id = BLOCK_IRON; id < BLOCK_INTERNAL_FIRST; ++id)
                choices[choices_count++] = block_name(id);
        } else if (count == 6) {
            ADD(directions);
        }
    } else if (!gui && !slash && count == 7 && strcmp(name, "hit") == 0) {
        ADD(projectiles);
    }
#undef ADD
    size_t matches = 0;
    for (size_t i = 0; i < choices_count; ++i) {
        if (strncmp(choices[i], word, prefix) == 0 &&
            strlen(choices[i]) < capacity - offset)
            choices[matches++] = choices[i];
    }
    if (!matches) return false;
    const char *choice = choices[state->next % matches];
    state->next = (state->next % matches + 1) % matches;
    memcpy(state->completed, state->original, offset);
    strcpy(state->completed + offset, choice);
    bool changed = strcmp(line, state->completed) != 0;
    strcpy(line, state->completed);
    state->active = true;
    return changed;
}
