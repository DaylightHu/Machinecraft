#ifndef RS_CLI_H
#define RS_CLI_H

#include "rs/session.h"

#include <stdio.h>

int cli_run(GameSession *session, FILE *input, FILE *output, FILE *errors);

#endif
