#ifndef MC_INVENTORY_H
#define MC_INVENTORY_H

#include "raylib.h"
#include "rs/block.h"

#include <stdbool.h>

enum {INVENTORY_SLOT_COUNT = 32};

typedef struct {
    Texture2D item_textures[BLOCK_COUNT];
    BlockId slots[INVENTORY_SLOT_COUNT];
    int count;
} Inventory;

bool inventory_init(Inventory *inventory);
void inventory_unload(Inventory *inventory);
BlockId inventory_update(const Inventory *inventory);
void inventory_draw(const Inventory *inventory, BlockId selected);
void inventory_draw_icon(const Inventory *inventory, BlockId id,
                         Rectangle slot);

#endif
