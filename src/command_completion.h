#ifndef MC_COMMAND_COMPLETION_H
#define MC_COMMAND_COMPLETION_H

#include <stdbool.h>
#include <stddef.h>

enum { COMMAND_COMPLETION_CAPACITY = 512 };

typedef struct {
    char original[COMMAND_COMPLETION_CAPACITY];
    char completed[COMMAND_COMPLETION_CAPACITY];
    size_t next;
    bool active;
} CommandCompletion;

/* Complete the last word, preserving whitespace and leading '/' or '//'.
 * Consecutive calls cycle through matches; editing the line starts a new search.
 * gui restricts suggestions to commands accepted by the GUI. Never executes. */
bool command_complete(CommandCompletion *state, char *line, size_t capacity,
                      bool gui, bool workspace);

#endif
