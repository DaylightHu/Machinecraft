#ifndef MC_FRONTEND_WORLD_SESSION_MANAGER_H
#define MC_FRONTEND_WORLD_SESSION_MANAGER_H

#include "rs/session.h"

typedef struct WorldSessionManager WorldSessionManager;

/* The host owns registered sessions; frontend targets are borrowed pointers. */
WorldSessionManager *world_session_manager_create(void);
void world_session_manager_destroy(WorldSessionManager *manager);
/* Detach all frontend targets before clearing the owned sessions. */
void world_session_manager_clear(WorldSessionManager *manager);
size_t world_session_manager_count(const WorldSessionManager *manager);
GameSession *world_session_manager_at(const WorldSessionManager *manager,
                                      size_t index);
GameSession *world_session_manager_find(const WorldSessionManager *manager,
                                        const char *name);
const char *world_session_manager_name(const WorldSessionManager *manager,
                                       const GameSession *session);
bool world_session_manager_add(WorldSessionManager *manager, const char *name,
                               GameSession *session);
bool world_session_manager_any_dirty(const WorldSessionManager *manager);
GameSessionResult world_session_manager_save_dirty(
    WorldSessionManager *manager, GameSession **failed_session);
/* Advance every host-owned world, whether or not a pane currently targets it. */
RsResult world_session_manager_advance(WorldSessionManager *manager,
                                       double elapsed_seconds,
                                       GameSession **failed_session);

#endif
