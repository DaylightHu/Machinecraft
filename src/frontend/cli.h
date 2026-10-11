#ifndef RS_CLI_H
#define RS_CLI_H

#include "rs/world.h"

#include <stdio.h>

/* Text input adapter. Edits originate from commands, never from startup. */
int cli_run(World *world, FILE *input, FILE *output, FILE *errors);

#endif
