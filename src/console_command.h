#ifndef MC_CONSOLE_COMMAND_H
#define MC_CONSOLE_COMMAND_H

#include "rs/session.h"
#include "plot_command.h"

enum { CONSOLE_COMMAND_CAPACITY = 512 };
typedef struct { PlotCommandState plot; } ConsoleCommandState;
typedef enum { CONSOLE_CONTINUE, CONSOLE_TITLE, CONSOLE_QUIT } ConsoleCommandAction;
typedef struct {
    ConsoleCommandAction action;
    uint32_t steps;
    bool failed, fatal, saved, reset_clock;
} ConsoleCommandReply;
/* Output fragments are UTF-8/ASCII text; a fragment may contain partial lines. */
typedef void (*ConsoleCommandOutput)(void *context, const char *text, bool error);
/* Does not poll input or advance time. Each entry point supplies its own host. */
ConsoleCommandReply console_command_run(GameSession *session,
    ConsoleCommandState *state, const char *line,
    ConsoleCommandOutput output, void *context);

#endif
