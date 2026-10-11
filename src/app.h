#ifndef RS_APP_H
#define RS_APP_H

#include <stdio.h>

/* Owns the world for one application session. No prebuilt circuits. */
int app_run(FILE *input, FILE *output, FILE *errors);

#endif
