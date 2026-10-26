#include "ui_font.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { FONT_CACHE_SIZE = 64, FALLBACK_SIZE = 16 };
static const char font_path[] = "res/fonts/ComicMono-Bold.ttf";
/* Rasterize at the requested pixel size so small text stays sharp. */
static Font fonts[FONT_CACHE_SIZE];
static bool attempted[FONT_CACHE_SIZE];

static Font font_for_size(int size)
{
    int index = size > 0 && size < FONT_CACHE_SIZE ? size : FALLBACK_SIZE;
    if (!attempted[index]) {
        attempted[index] = true;
        Font font = LoadFontEx(font_path, index, NULL, 0);
        if (font.texture.id && font.texture.id != GetFontDefault().texture.id) {
            fonts[index] = font;
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
        } else if (font.glyphs && font.glyphs != GetFontDefault().glyphs) UnloadFont(font);
    }
    return fonts[index].texture.id ? fonts[index] : fonts[FALLBACK_SIZE];
}

bool ui_font_init(void)
{
    ui_font_unload();
    if (!font_for_size(FALLBACK_SIZE).texture.id) {
        fprintf(stderr, "Cannot load UI font: %s\n", font_path);
        return false;
    }
    return true;
}

void ui_font_unload(void)
{
    for (int i = 0; i < FONT_CACHE_SIZE; ++i)
        if (fonts[i].texture.id) UnloadFont(fonts[i]);
    memset(fonts, 0, sizeof(fonts));
    memset(attempted, 0, sizeof(attempted));
}

void ui_text_draw(const char *text, int x, int y, int size, Color color)
{
    if (!text || !text[0] || size <= 0) return;
    DrawTextEx(font_for_size(size), text, (Vector2){(float)x, (float)y}, (float)size, 0, color);
}

int ui_text_width(const char *text, int size)
{
    if (!text || !text[0] || size <= 0) return 0;
    return (int)ceilf(MeasureTextEx(font_for_size(size), text, (float)size, 0).x);
}
