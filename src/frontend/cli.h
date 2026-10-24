#ifndef RS_CLI_H
#define RS_CLI_H

#include "rs/session.h"

#include <stdio.h>

typedef enum {
    CLI_RUN_EXIT,
    CLI_RUN_ERROR,
    CLI_RUN_TITLE
} CliRunResult;

CliRunResult cli_run(GameSession *session, FILE *input, FILE *output,
                     FILE *errors);

#endif
