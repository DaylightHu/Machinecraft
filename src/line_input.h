#ifndef MC_LINE_INPUT_H
#define MC_LINE_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum { LINE_INPUT_ERROR = -2, LINE_INPUT_TOO_LONG = -1,
       LINE_INPUT_EOF = 0, LINE_INPUT_READY = 1, LINE_INPUT_PENDING = 2 };

typedef bool (*LineInputComplete)(char *line, size_t capacity,
                                  bool restart, void *context);

typedef struct {
    FILE *echo;
    char *line;
    size_t capacity, length, discarded;
    int fd;
    bool ready, eof;
    bool interactive, completing;
    unsigned escape;
    LineInputComplete complete;
    void *complete_context;
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
/* Enable immediate Tab handling for a terminal. Files and pipes stay literal.
 * Call close before returning or changing readers to restore terminal settings. */
bool line_input_set_completion(LineInput *reader, LineInputComplete complete,
                                void *context);
void line_input_close(LineInput *reader);
/* Returns a complete line without its newline, or PENDING after at most one
 * input byte/event and timeout_ms of waiting. Long lines are fully discarded. */
int line_input_poll(LineInput *reader, unsigned timeout_ms);

#endif
