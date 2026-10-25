#ifndef MC_TICK_COMMAND_H
#define MC_TICK_COMMAND_H

#include "rs/sim.h"

enum { TICK_COMMAND_MESSAGE_CAPACITY = 192 };

typedef enum {
    TICK_COMMAND_INVALID, TICK_COMMAND_FREEZE,
    TICK_COMMAND_STEP, TICK_COMMAND_NOUPDATE
} TickCommandKind;

typedef struct { TickCommandKind kind; uint32_t count; } TickCommand;
/* Frontend/session controls, reset when a world is opened. Not save data. */
typedef struct { bool frozen; uint64_t remaining; } TickControl;

bool tick_command_parse(const char *line, TickCommand *command);
RsResult tick_command_run(World *world, TickControl *control,
                          const TickCommand *command,
                          char *message, size_t capacity);
/* A frontend drives this once per automatic tick, or repeatedly while stepping.
 * Frozen with no requested steps returns RS_UNCHANGED without touching time. */
RsResult tick_control_step(World *world, TickControl *control);

#endif
