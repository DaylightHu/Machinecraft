#include "hotbar.h"

#include <stddef.h>

typedef struct {
    BlockId id;
    const char *path;
} ItemTexture;

static const ItemTexture item_textures[] = {
    {BLOCK_IRON, "res/textures/item/iron_block.png"},
    {BLOCK_GLASS, "res/textures/item/glass.png"},
    {BLOCK_REDSTONE_BLOCK, "res/textures/item/redstone_block.png"},
    {BLOCK_REDSTONE_WIRE, "res/textures/item/redstone_wire.png"},
    {BLOCK_REDSTONE_TORCH, "res/textures/item/redstone_torch.png"},
    {BLOCK_REDSTONE_REPEATER, "res/textures/block/repeater1_0.png"}
};

bool hotbar_init(Hotbar *hotbar)
{
    if (!hotbar) return false;
    *hotbar = (Hotbar){0};

    for (size_t index = 0;
         index < sizeof(item_textures)/sizeof(item_textures[0]); ++index) {
        ItemTexture item = item_textures[index];
        Texture2D texture = LoadTexture(item.path);
        if (!texture.id) {
            TraceLog(LOG_ERROR, "HOTBAR: Cannot load %s", item.path);
            hotbar_unload(hotbar);
            return false;
        }
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        hotbar->item_textures[item.id] = texture;
        hotbar->slots[index] = item.id;
    }
    hotbar->selected_slot = 0;
    return true;
}

void hotbar_unload(Hotbar *hotbar)
{
    if (!hotbar) return;
    for (int id = 0; id < BLOCK_COUNT; ++id) {
        if (hotbar->item_textures[id].id)
            UnloadTexture(hotbar->item_textures[id]);
    }
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
            break;
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

static void draw_item(Texture2D texture, Rectangle slot)
{
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

void hotbar_draw(const Hotbar *hotbar)
{
    if (!hotbar) return;

    const int slot_size = 48;
    const int bar_padding = 4;
    const int bar_width = HOTBAR_SLOT_COUNT*slot_size + 2*bar_padding;
    const int bar_height = slot_size + 2*bar_padding;
    int bar_x = (GetScreenWidth() - bar_width)/2;
    int bar_y = GetScreenHeight() - bar_height - 12;

    DrawRectangle(bar_x, bar_y, bar_width, bar_height,
                  (Color){20, 20, 20, 210});

    for (int slot_index = 0; slot_index < HOTBAR_SLOT_COUNT; ++slot_index) {
        Rectangle slot = {
            (float)(bar_x + bar_padding + slot_index*slot_size),
            (float)(bar_y + bar_padding),
            (float)slot_size,
            (float)slot_size
        };
        DrawRectangleRec(slot, (Color){75, 75, 75, 190});

        BlockId id = hotbar->slots[slot_index];
        if (id > BLOCK_AIR && id < BLOCK_COUNT)
            draw_item(hotbar->item_textures[id], slot);

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

