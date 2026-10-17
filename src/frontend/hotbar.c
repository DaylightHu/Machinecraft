#include "hotbar.h"

enum {
    HOTBAR_SLOT_SIZE = 48,
    HOTBAR_PADDING = 4,
    HOTBAR_BOTTOM_MARGIN = 12
};

static Rectangle hotbar_slot_rectangle(int index)
{
    int bar_width = HOTBAR_SLOT_COUNT*HOTBAR_SLOT_SIZE + 2*HOTBAR_PADDING;
    int bar_x = (GetScreenWidth() - bar_width)/2;
    int bar_y = GetScreenHeight() -
                (HOTBAR_SLOT_SIZE + 2*HOTBAR_PADDING) - HOTBAR_BOTTOM_MARGIN;
    return (Rectangle){
        (float)(bar_x + HOTBAR_PADDING + index*HOTBAR_SLOT_SIZE),
        (float)(bar_y + HOTBAR_PADDING),
        HOTBAR_SLOT_SIZE,
        HOTBAR_SLOT_SIZE
    };
}

bool hotbar_init(Hotbar *hotbar, const Inventory *inventory)
{
    if (!hotbar || !inventory) return false;
    *hotbar = (Hotbar){0};
    int count = inventory->count < HOTBAR_SLOT_COUNT ?
                inventory->count : HOTBAR_SLOT_COUNT;
    for (int index = 0; index < count; ++index)
        hotbar->slots[index] = inventory->slots[index];
    hotbar->selected_slot = 0;
    return true;
}

void hotbar_unload(Hotbar *hotbar)
{
    if (!hotbar) return;
    *hotbar = (Hotbar){0};
}

void hotbar_update(Hotbar *hotbar)
{
    if (!hotbar) return;

    static const int number_keys[HOTBAR_SLOT_COUNT] = {
        KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE,
        KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE
    };
    for (int slot = 0; slot < HOTBAR_SLOT_COUNT; ++slot) {
        if (IsKeyPressed(number_keys[slot])) {
            hotbar->selected_slot = slot;
            return;
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        for (int slot = 0; slot < HOTBAR_SLOT_COUNT; ++slot) {
            if (CheckCollisionPointRec(mouse, hotbar_slot_rectangle(slot))) {
                hotbar->selected_slot = slot;
                return;
            }
        }
    }

    float wheel = GetMouseWheelMove();
    if (wheel > 0.0f) {
        hotbar->selected_slot =
            (hotbar->selected_slot + HOTBAR_SLOT_COUNT - 1)%HOTBAR_SLOT_COUNT;
    } else if (wheel < 0.0f) {
        hotbar->selected_slot =
            (hotbar->selected_slot + 1)%HOTBAR_SLOT_COUNT;
    }
}

BlockId hotbar_selected_block(const Hotbar *hotbar)
{
    if (!hotbar || hotbar->selected_slot < 0 ||
        hotbar->selected_slot >= HOTBAR_SLOT_COUNT)
        return BLOCK_AIR;
    return hotbar->slots[hotbar->selected_slot];
}

bool hotbar_set_selected_block(Hotbar *hotbar, BlockId id)
{
    if (!hotbar || hotbar->selected_slot < 0 ||
        hotbar->selected_slot >= HOTBAR_SLOT_COUNT ||
        id <= BLOCK_AIR || id >= BLOCK_COUNT) return false;
    hotbar->slots[hotbar->selected_slot] = id;
    return true;
}

void hotbar_draw(const Hotbar *hotbar, const Inventory *inventory)
{
    if (!hotbar || !inventory) return;

    const int bar_width = HOTBAR_SLOT_COUNT*HOTBAR_SLOT_SIZE + 2*HOTBAR_PADDING;
    const int bar_height = HOTBAR_SLOT_SIZE + 2*HOTBAR_PADDING;
    int bar_x = (GetScreenWidth() - bar_width)/2;
    int bar_y = GetScreenHeight() - bar_height - HOTBAR_BOTTOM_MARGIN;

    DrawRectangle(bar_x, bar_y, bar_width, bar_height,
                  (Color){20, 20, 20, 210});

    for (int slot_index = 0; slot_index < HOTBAR_SLOT_COUNT; ++slot_index) {
        Rectangle slot = hotbar_slot_rectangle(slot_index);
        DrawRectangleRec(slot, (Color){75, 75, 75, 190});

        BlockId id = hotbar->slots[slot_index];
        inventory_draw_icon(inventory, id, slot);

        DrawRectangleLinesEx(slot, 1.0f, (Color){145, 145, 145, 255});
        DrawText(TextFormat("%d", slot_index + 1),
                 (int)slot.x + 3, (int)slot.y + 2, 12, LIGHTGRAY);

        if (slot_index == hotbar->selected_slot) {
            Rectangle selection = {
                slot.x - 2.0f, slot.y - 2.0f,
                slot.width + 4.0f, slot.height + 4.0f
            };
            DrawRectangleLinesEx(selection, 3.0f, RAYWHITE);
        }
    }
}

