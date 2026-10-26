#ifndef MC_INVENTORY_H
#define MC_INVENTORY_H

#include "raylib.h"
#include "rs/block.h"
#include "rs/item.h"

#include <stdbool.h>

enum {INVENTORY_SLOT_COUNT = 32};

typedef enum {
    INVENTORY_EMPTY,
    INVENTORY_BLOCK,
    INVENTORY_ITEM,
    INVENTORY_TOOL
} InventorySlotKind;

typedef enum { TOOL_NONE, TOOL_WOODEN_AXE, TOOL_COUNT } InventoryTool;

typedef struct {
    InventorySlotKind kind;
    union {
        BlockId block;
        ItemStack stack;
        InventoryTool tool;
    };
} InventorySlot;

typedef struct {
    Texture2D block_textures[BLOCK_COUNT];
    Texture2D item_textures[ITEM_COUNT];
    Texture2D tool_textures[TOOL_COUNT];
    InventorySlot slots[INVENTORY_SLOT_COUNT];
    int count;
} Inventory;

bool inventory_init(Inventory *inventory);
void inventory_unload(Inventory *inventory);
InventorySlot inventory_update(const Inventory *inventory);
const char *inventory_slot_name(InventorySlot slot);
void inventory_draw(const Inventory *inventory, InventorySlot selected);
void inventory_draw_slot(const Inventory *inventory, InventorySlot contents,
                          Rectangle slot);
void inventory_draw_stack(const Inventory *inventory, ItemStack stack,
                           Rectangle slot);

#endif
