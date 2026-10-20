#include "inventory.h"

#include <stddef.h>

enum {
    INVENTORY_COLUMNS = 8,
    INVENTORY_ROWS = 4,
    INVENTORY_SLOT_SIZE = 58,
    INVENTORY_SLOT_GAP = 4,
    INVENTORY_PANEL_PADDING = 18,
    INVENTORY_HEADER_HEIGHT = 52,
    INVENTORY_FOOTER_HEIGHT = 64
};

_Static_assert(INVENTORY_COLUMNS*INVENTORY_ROWS == INVENTORY_SLOT_COUNT,
        "inventory grid must match slot capacity");

typedef struct {
    BlockId id;
    const char *path;
} ItemTexture;

typedef struct {
    Rectangle panel;
    float grid_x;
    float grid_y;
} InventoryLayout;

static const ItemTexture item_textures[] = {
    {BLOCK_IRON, "res/textures/item/iron_block.png"},
    {BLOCK_GLASS, "res/textures/item/glass.png"},
    {BLOCK_REDSTONE_BLOCK, "res/textures/item/redstone_block.png"},
    {BLOCK_REDSTONE_WIRE, "res/textures/item/redstone_wire.png"},
    {BLOCK_REDSTONE_TORCH, "res/textures/item/redstone_torch.png"},
    {BLOCK_REDSTONE_REPEATER, "res/textures/item/redstone_repeater.png"},
    {BLOCK_REDSTONE_COMPARATOR, "res/textures/item/redstone_comparator.png"},
    {BLOCK_POWERED_RAIL, "res/textures/item/powered_rail.png"},
    {BLOCK_ACTIVATOR_RAIL, "res/textures/item/activator_rail.png"},
    {BLOCK_OBSERVER, "res/textures/item/observer.png"},
    {BLOCK_PISTON, "res/textures/item/piston.png"},
    {BLOCK_STICKY_PISTON, "res/textures/item/sticky_piston.png"},
    {BLOCK_BARREL, "res/textures/item/barrel.png"},
    {BLOCK_SLIME, "res/textures/item/slime_block.png"},
    {BLOCK_HONEY, "res/textures/item/honey_block.png"},
    {BLOCK_NOTE_BLOCK, "res/textures/item/note_block.png"},
    {BLOCK_TARGET, "res/textures/item/target_block.png"}
};

static InventoryLayout inventory_layout(void)
{
    const int grid_width =
        INVENTORY_COLUMNS*INVENTORY_SLOT_SIZE +
        (INVENTORY_COLUMNS - 1)*INVENTORY_SLOT_GAP;
    const int grid_height =
        INVENTORY_ROWS*INVENTORY_SLOT_SIZE +
        (INVENTORY_ROWS - 1)*INVENTORY_SLOT_GAP;
    const int panel_width = grid_width + 2*INVENTORY_PANEL_PADDING;
    const int panel_height = INVENTORY_PANEL_PADDING +
        INVENTORY_HEADER_HEIGHT + grid_height +
        INVENTORY_FOOTER_HEIGHT + INVENTORY_PANEL_PADDING;
    InventoryLayout layout = {
        {
            (GetScreenWidth() - panel_width)*0.5f,
            (GetScreenHeight() - panel_height)*0.5f,
            (float)panel_width,
            (float)panel_height
        },
        0.0f,
        0.0f
    };
    layout.grid_x = layout.panel.x + INVENTORY_PANEL_PADDING;
    layout.grid_y = layout.panel.y + INVENTORY_PANEL_PADDING +
                    INVENTORY_HEADER_HEIGHT;
    return layout;
}

static Rectangle inventory_slot_rectangle(InventoryLayout layout, int index)
{
    int column = index%INVENTORY_COLUMNS;
    int row = index/INVENTORY_COLUMNS;
    return (Rectangle){
        layout.grid_x + column*(INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP),
        layout.grid_y + row*(INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP),
        INVENTORY_SLOT_SIZE,
        INVENTORY_SLOT_SIZE
    };
}

bool inventory_init(Inventory *inventory)
{
    if (!inventory) return false;
    *inventory = (Inventory){0};

    size_t count = sizeof(item_textures)/sizeof(item_textures[0]);
    if (count > INVENTORY_SLOT_COUNT) {
        TraceLog(LOG_ERROR, "INVENTORY: Too many item textures (%d > %d)",
                 (int)count, INVENTORY_SLOT_COUNT);
        return false;
    }

    for (size_t index = 0; index < count; ++index) {
        ItemTexture item = item_textures[index];
        Texture2D texture = LoadTexture(item.path);
        if (!texture.id) {
            TraceLog(LOG_ERROR, "INVENTORY: Cannot load %s", item.path);
            inventory_unload(inventory);
            return false;
        }
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        inventory->item_textures[item.id] = texture;
        inventory->slots[index] = item.id;
    }
    inventory->count = (int)count;
    return true;
}

void inventory_unload(Inventory *inventory)
{
    if (!inventory) return;
    for (int id = 0; id < BLOCK_COUNT; ++id) {
        if (inventory->item_textures[id].id)
            UnloadTexture(inventory->item_textures[id]);
    }
    *inventory = (Inventory){0};
}

void inventory_draw_icon(const Inventory *inventory, BlockId id,
                         Rectangle slot)
{
    if (!inventory || id <= BLOCK_AIR || id >= BLOCK_COUNT) return;
    Texture2D texture = inventory->item_textures[id];
    if (!texture.id) return;

    const float padding = 5.0f;
    float available = slot.width - 2.0f*padding;
    float scale_x = available/(float)texture.width;
    float scale_y = available/(float)texture.height;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    float width = texture.width*scale;
    float height = texture.height*scale;
    Rectangle source = {0, 0, (float)texture.width, (float)texture.height};
    Rectangle destination = {
        slot.x + (slot.width - width)*0.5f,
        slot.y + (slot.height - height)*0.5f,
        width,
        height
    };
    DrawTexturePro(texture, source, destination, (Vector2){0, 0}, 0.0f, WHITE);
}

BlockId inventory_update(const Inventory *inventory)
{
    if (!inventory || !IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        return BLOCK_AIR;

    InventoryLayout layout = inventory_layout();
    Vector2 mouse = GetMousePosition();
    for (int index = 0; index < inventory->count; ++index) {
        Rectangle slot = inventory_slot_rectangle(layout, index);
        if (CheckCollisionPointRec(mouse, slot)) return inventory->slots[index];
    }
    return BLOCK_AIR;
}

void inventory_draw(const Inventory *inventory, BlockId selected)
{
    if (!inventory) return;

    InventoryLayout layout = inventory_layout();
    Vector2 mouse = GetMousePosition();
    BlockId hovered = BLOCK_AIR;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){10, 18, 20, 155});
    DrawRectangleRec(layout.panel, (Color){226, 218, 196, 250});
    DrawRectangleLinesEx(layout.panel, 3.0f, (Color){34, 63, 66, 255});

    const char *title = "BLOCK INVENTORY";
    int title_size = 25;
    DrawText(title,
             (int)(layout.panel.x +
                   (layout.panel.width - MeasureText(title, title_size))*0.5f),
             (int)layout.panel.y + 17, title_size, (Color){26, 50, 55, 255});

    for (int index = 0; index < INVENTORY_SLOT_COUNT; ++index) {
        Rectangle slot = inventory_slot_rectangle(layout, index);
        bool occupied = index < inventory->count;
        bool under_mouse = CheckCollisionPointRec(mouse, slot);
        BlockId id = occupied ? inventory->slots[index] : BLOCK_AIR;
        Color fill = under_mouse && occupied ? (Color){102, 116, 112, 255}
                                             : (Color){75, 75, 75, 235};
        DrawRectangleRec(slot, fill);
        DrawRectangleLinesEx(slot, 1.0f, (Color){145, 145, 145, 255});
        if (occupied) inventory_draw_icon(inventory, id, slot);
        if (id == selected)
            DrawRectangleLinesEx(slot, 3.0f, (Color){244, 190, 88, 255});
        if (under_mouse && occupied) hovered = id;
    }

    const char *label = hovered != BLOCK_AIR ? block_name(hovered) :
                        "Click a block to fill the selected hotbar slot";
    int label_size = hovered != BLOCK_AIR ? 19 : 15;
    int label_y = (int)(layout.grid_y +
        INVENTORY_ROWS*INVENTORY_SLOT_SIZE +
        (INVENTORY_ROWS - 1)*INVENTORY_SLOT_GAP + 8);
    DrawText(label,
             (int)(layout.panel.x +
                   (layout.panel.width - MeasureText(label, label_size))*0.5f),
             label_y, label_size, (Color){26, 50, 55, 255});

    const char *hint = "1-9/click: slot   E/ESC: close";
    int hint_size = 14;
    DrawText(hint,
             (int)(layout.panel.x +
                   (layout.panel.width - MeasureText(hint, hint_size))*0.5f),
             label_y + 27, hint_size, (Color){83, 99, 94, 255});
}
