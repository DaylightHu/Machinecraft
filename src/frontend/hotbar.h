#ifndef MC_HOTBAR_H
#define MC_HOTBAR_H

#include "inventory.h"
#include "rs/block.h"

#include <stdbool.h>

enum {HOTBAR_SLOT_COUNT = 9};

typedef struct {
    InventorySlot slots[HOTBAR_SLOT_COUNT];
    int selected_slot;
} Hotbar;

bool hotbar_init(Hotbar *hotbar, const Inventory *inventory);
void hotbar_unload(Hotbar *hotbar);
void hotbar_update(Hotbar *hotbar);
int hotbar_slot_at(Vector2 position);
InventorySlot hotbar_selected(const Hotbar *hotbar);
BlockId hotbar_selected_block(const Hotbar *hotbar);
bool hotbar_set_selected(Hotbar *hotbar, InventorySlot slot);
bool hotbar_set_selected_block(Hotbar *hotbar, BlockId id);
bool hotbar_pick(Hotbar *hotbar, InventorySlot picked);
void hotbar_draw(const Hotbar *hotbar, const Inventory *inventory);
#endif
