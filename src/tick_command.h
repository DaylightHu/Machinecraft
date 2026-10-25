#ifndef MC_TICK_COMMAND_H
#define MC_TICK_COMMAND_H

#include "rs/sim.h"

enum { TICK_COMMAND_MESSAGE_CAPACITY = 192 };

typedef enum {
    TICK_COMMAND_INVALID, TICK_COMMAND_FREEZE,
    TICK_COMMAND_STEP, TICK_COMMAND_NOUPDATE
} TickCommandKind;

typedef struct { TickCommandKind kind; uint32_t count; } TickCommand;
bool tick_command_parse(const char *line, TickCommand *command);
RsResult tick_command_run(World *world, const TickCommand *command,
                          char *message, size_t capacity);

#endif
