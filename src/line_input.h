#ifndef MC_LINE_INPUT_H
#define MC_LINE_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum { LINE_INPUT_ERROR = -2, LINE_INPUT_TOO_LONG = -1,
       LINE_INPUT_EOF = 0, LINE_INPUT_READY = 1, LINE_INPUT_PENDING = 2 };

typedef struct {
    FILE *echo;
    char *line;
    size_t capacity, length, discarded;
    int fd;
    bool ready, eof;
#ifdef _WIN32
    void *handle;
    bool console, pipe;
    unsigned repeat;
    unsigned char repeated;
#endif
} LineInput;

/* Uses descriptor reads without stdio read-ahead. World selection uses the
 * same reader, so pasted/piped commands stay available across session changes.
 * input must not have unread data already buffered by other stdio readers. */
bool line_input_init(LineInput *reader, FILE *input, FILE *echo,
                     char *line, size_t capacity);
/* Returns a complete line without its newline, or PENDING after at most one
 * input byte/event and timeout_ms of waiting. Long lines are fully discarded. */
int line_input_poll(LineInput *reader, unsigned timeout_ms);

#endif
