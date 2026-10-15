#ifndef MC_SAVE_FILE_H
#define MC_SAVE_FILE_H

#include "rs/session.h"

GameSessionResult save_file_write(const World *world, const char *path);
World *save_file_read(const char *path, GameSessionResult *result);

#endif
