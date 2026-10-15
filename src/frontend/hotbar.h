#ifndef MC_HOTBAR_H
#define MC_HOTBAR_H

#include "raylib.h"
#include "rs/block.h"

#include <stdbool.h>

enum {HOTBAR_SLOT_COUNT = 9};

typedef struct {
    Texture2D item_textures[BLOCK_COUNT];
    BlockId slots[HOTBAR_SLOT_COUNT];
    int selected_slot;
} Hotbar;

bool hotbar_init(Hotbar *hotbar);
void hotbar_unload(Hotbar *hotbar);
void hotbar_update(Hotbar *hotbar);
BlockId hotbar_selected_block(const Hotbar *hotbar);
void hotbar_draw(const Hotbar *hotbar);
#endif
