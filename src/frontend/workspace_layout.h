#ifndef MC_WORKSPACE_LAYOUT_H
#define MC_WORKSPACE_LAYOUT_H

#include <stdbool.h>

enum { WORKSPACE_CLI_LIMIT = 8, WORKSPACE_NODE_LIMIT = 15,
       WORKSPACE_DIVIDER = 3, WORKSPACE_MIN_WIDTH = 180,
       WORKSPACE_MIN_HEIGHT = 112 };
typedef struct { int x, y, width, height; } WorkspaceRect;
typedef enum { WORKSPACE_LEFT, WORKSPACE_RIGHT, WORKSPACE_UP, WORKSPACE_DOWN } WorkspaceDirection;
typedef struct {
    bool used, split_x;
    int parent, first, second, pane, ratio;
} WorkspaceNode;
typedef struct {
    WorkspaceNode nodes[WORKSPACE_NODE_LIMIT];
    int root, focus, zoom; /* pane 0 is GUI; CLI panes are 1..8; zoom -1 is off */
    bool gui_visible;
} WorkspaceLayout;
typedef struct {
    WorkspaceRect gui, cli, panes[WORKSPACE_CLI_LIMIT];
    bool visible[WORKSPACE_CLI_LIMIT], compact;
} WorkspaceGeometry;

void workspace_layout_init(WorkspaceLayout *layout);
void workspace_layout_geometry(const WorkspaceLayout *layout, int width, int height,
                                WorkspaceGeometry *geometry);
bool workspace_layout_has_pane(const WorkspaceLayout *layout, int pane);
/* Split only the CLI tree. GUI focus targets the first CLI (or creates one).
 * Return the new pane ID, or -1 without changing layout when there is no room. */
int workspace_layout_split(WorkspaceLayout *layout, bool split_x, int width, int height);
bool workspace_layout_close(WorkspaceLayout *layout);
void workspace_layout_next(WorkspaceLayout *layout);
bool workspace_layout_focus(WorkspaceLayout *layout, WorkspaceDirection direction,
                             int width, int height);
bool workspace_layout_resize(WorkspaceLayout *layout, WorkspaceDirection direction,
                              int pixels, int width, int height);
bool workspace_layout_zoom(WorkspaceLayout *layout);
void workspace_layout_show_gui(WorkspaceLayout *layout);

#endif
