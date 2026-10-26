#ifndef MC_GUI_VIEW_H
#define MC_GUI_VIEW_H

#include "raylib.h"

/* GUI widgets use local logical coordinates, whether rendered to the main
 * window or to the fixed GUI panel's texture. Mouse coordinates match them. */
void gui_view_set(Rectangle screen, int width, int height);
void gui_view_reset(void);
int gui_view_width(void);
int gui_view_height(void);
Vector2 gui_view_mouse(void);

#endif
