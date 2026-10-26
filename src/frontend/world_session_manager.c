#include "world_session_manager.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WORLD_SESSION_LIMIT = GAME_SESSION_MAX_SAVES };

typedef struct {
    char name[GAME_SESSION_NAME_CAPACITY];
    GameSession *session;
} ManagedWorld;

struct WorldSessionManager {
    ManagedWorld worlds[WORLD_SESSION_LIMIT];
    size_t count;
};

static bool names_equal(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

WorldSessionManager *world_session_manager_create(void)
{
    return calloc(1, sizeof(WorldSessionManager));
}

void world_session_manager_destroy(WorldSessionManager *manager)
{
    if (!manager) return;
    for (size_t index = 0; index < manager->count; ++index)
        game_session_destroy(manager->worlds[index].session);
    free(manager);
}

size_t world_session_manager_count(const WorldSessionManager *manager)
{
    return manager ? manager->count : 0;
}

GameSession *world_session_manager_at(const WorldSessionManager *manager,
                                      size_t index)
{
    return manager && index < manager->count ? manager->worlds[index].session : NULL;
}

GameSession *world_session_manager_find(const WorldSessionManager *manager,
                                        const char *name)
{
    if (!manager || !name) return NULL;
    for (size_t index = 0; index < manager->count; ++index)
        if (names_equal(manager->worlds[index].name, name))
            return manager->worlds[index].session;
    return NULL;
}

const char *world_session_manager_name(const WorldSessionManager *manager,
                                       const GameSession *session)
{
    if (!manager || !session) return NULL;
    for (size_t index = 0; index < manager->count; ++index)
        if (manager->worlds[index].session == session)
            return manager->worlds[index].name;
    return NULL;
}

bool world_session_manager_add(WorldSessionManager *manager, const char *name,
                               GameSession *session)
{
    if (!manager || !game_session_valid_name(name) || !session) return false;
    for (size_t index = 0; index < manager->count; ++index) {
        if (manager->worlds[index].session == session)
            return names_equal(manager->worlds[index].name, name);
        if (names_equal(manager->worlds[index].name, name))
            return false;
    }
    if (manager->count >= WORLD_SESSION_LIMIT) return false;
    ManagedWorld *world = &manager->worlds[manager->count++];
    snprintf(world->name, sizeof(world->name), "%s", name);
    world->session = session;
    return true;
}

bool world_session_manager_any_dirty(const WorldSessionManager *manager)
{
    if (!manager) return false;
    for (size_t index = 0; index < manager->count; ++index)
        if (game_session_dirty(manager->worlds[index].session)) return true;
    return false;
}

GameSessionResult world_session_manager_save_dirty(
    WorldSessionManager *manager, GameSession **failed_session)
{
    if (failed_session) *failed_session = NULL;
    if (!manager) return GAME_SESSION_INVALID_ARGUMENT;
    for (size_t index = 0; index < manager->count; ++index) {
        GameSession *session = manager->worlds[index].session;
        if (!game_session_dirty(session)) continue;
        GameSessionResult result = game_session_save(session);
        if (result != GAME_SESSION_OK) {
            if (failed_session) *failed_session = session;
            return result;
        }
    }
    return GAME_SESSION_OK;
}

RsResult world_session_manager_advance(WorldSessionManager *manager,
                                       double elapsed_seconds,
                                       GameSession **failed_session)
{
    if (failed_session) *failed_session = NULL;
    if (!manager || !isfinite(elapsed_seconds) || elapsed_seconds < 0.0)
        return RS_INVALID_ARGUMENT;
    for (size_t index = 0; index < manager->count; ++index) {
        GameSession *session = manager->worlds[index].session;
        RsResult result = game_session_advance(session, elapsed_seconds);
        if (result != RS_OK && result != RS_UNCHANGED) {
            if (failed_session) *failed_session = session;
            return result;
        }
    }
    return RS_OK;
}
