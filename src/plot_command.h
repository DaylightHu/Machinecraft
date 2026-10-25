#ifndef MC_PLOT_COMMAND_H
#define MC_PLOT_COMMAND_H

#include "rs/world.h"

#include <stddef.h>

enum { PLOT_COMMAND_MESSAGE_CAPACITY = 192 };

typedef enum {
    PLOT_COMMAND_INVALID,
    PLOT_COMMAND_INFO,
    PLOT_COMMAND_EXPAND,
    PLOT_COMMAND_SHRINK
} PlotCommandKind;

typedef struct {
    PlotCommandKind kind;
    Direction direction;
    uint32_t count;
} PlotCommand;

typedef struct {
    PlotCommand command;
    WorldShrinkPreview preview;
    bool pending;
} PlotCommandState;

bool plot_command_parse(const char *line, PlotCommand *command);
RsResult plot_command_run(World *world, const PlotCommand *command,
                          PlotCommandState *state, char *message, size_t capacity);
RsResult plot_command_answer(World *world, PlotCommandState *state,
                              const char *answer, char *message, size_t capacity);

#endif
