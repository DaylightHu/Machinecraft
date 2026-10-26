#include "gui_view.h"

static Rectangle screen;
static int view_width, view_height;
void gui_view_set(Rectangle rectangle, int width, int height)
{
    screen = rectangle;
    view_width = width;
    view_height = height;
}
void gui_view_reset(void) { view_width = view_height = 0; }
int gui_view_width(void) { return view_width > 0 ? view_width : GetScreenWidth(); }
int gui_view_height(void) { return view_height > 0 ? view_height : GetScreenHeight(); }
Vector2 gui_view_mouse(void)
{
    Vector2 mouse = GetMousePosition();
    if (view_width > 0 && screen.width > 0 && screen.height > 0) {
        mouse.x = (mouse.x - screen.x)*view_width/screen.width;
        mouse.y = (mouse.y - screen.y)*view_height/screen.height;
    }
    return mouse;
}
