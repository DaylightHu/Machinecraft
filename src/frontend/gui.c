#include "gui.h"

#include "block_renderer.h"
#include "block_shape.h"
#include "hotbar.h"
#include "inventory.h"
#include "plot_command.h"
#include "voxel_pick.h"
#include "world_renderer.h"
#include "raylib.h"
#include "rs/session.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SCREEN_WIDTH = 1000,
    SCREEN_HEIGHT = 650,
    MESSAGE_CAPACITY = 192,
    COMMAND_CAPACITY = 128,
    VISIBLE_SAVE_ROWS = 7
};

static const char save_directory[] = "saves";

typedef enum {
    GUI_TITLE,
    GUI_WORLD,
    GUI_COMMAND,
    GUI_INVENTORY,
    GUI_BARREL,
    GUI_HOPPER,
    GUI_DROPPER,
    GUI_PAUSE,
    GUI_SAVE_ERROR
} GuiScreen;

typedef enum {
    AFTER_SAVE_NONE,
    AFTER_SAVE_TITLE,
    AFTER_SAVE_EXIT
} AfterSave;

typedef struct {
    BlockRenderer blocks;
    Inventory inventory;
    Hotbar hotbar;
    GameSession *session;
    WorldRenderer *world_renderer;
    Camera3D camera;
    GameSaveList saves;
    GuiScreen screen;
    GuiScreen screen_before_error;
    AfterSave after_save;
    int selected_save;
    int save_scroll;
    char world_name[GAME_SESSION_NAME_CAPACITY];
    size_t world_name_length;
    char message[MESSAGE_CAPACITY];
    char command[COMMAND_CAPACITY];
    size_t command_length;
    BlockPos barrel_pos;
    BlockPos container_pos;
    double accumulator;
    bool mouse_captured;
    bool close_latched;
    bool running;
    bool fatal_error;
} GuiApp;

typedef struct {
    Rectangle panel;
    float grid_x;
    float grid_y;
    int columns;
    int rows;
} ContainerLayout;

enum {
    CONTAINER_SLOT_SIZE = 54,
    CONTAINER_SLOT_GAP = 4,
    SIGNAL_BUTTON_SIZE = 29,
    SIGNAL_BUTTON_GAP = 3
};

static bool point_in(Rectangle rectangle)
{
    return CheckCollisionPointRec(GetMousePosition(), rectangle);
}

static bool button(Rectangle rectangle, const char *label, bool enabled)
{
    bool hovered = enabled && point_in(rectangle);
    Color fill = enabled ? (hovered ? (Color){78, 101, 116, 255}
                                    : (Color){55, 72, 84, 255})
                         : (Color){48, 48, 48, 190};
    Color border = enabled ? (hovered ? (Color){244, 190, 88, 255}
                                      : (Color){142, 158, 168, 255})
                           : (Color){85, 85, 85, 180};
    Color text = enabled ? RAYWHITE : (Color){125, 125, 125, 255};
    DrawRectangleRec(rectangle, fill);
    DrawRectangleLinesEx(rectangle, 2.0f, border);
    int font_size = 20;
    int width = MeasureText(label, font_size);
    DrawText(label, (int)(rectangle.x + (rectangle.width - width)*0.5f),
             (int)(rectangle.y + (rectangle.height - font_size)*0.5f),
             font_size, text);
    return hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void draw_centered(const char *text, int y, int font_size, Color color)
{
    DrawText(text, (GetScreenWidth() - MeasureText(text, font_size))/2,
             y, font_size, color);
}

static bool names_equal(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

static bool save_name_exists(const GuiApp *app, const char *name)
{
    for (size_t index = 0; index < app->saves.count; ++index)
        if (names_equal(app->saves.names[index], name)) return true;
    return false;
}

static void refresh_saves(GuiApp *app)
{
    GameSessionResult result = game_session_list_saves(save_directory,
                                                       &app->saves);
    if (result != GAME_SESSION_OK && result != GAME_SESSION_TOO_MANY_SAVES) {
        app->saves = (GameSaveList){0};
        snprintf(app->message, sizeof(app->message),
                 "Cannot read saves: %s", game_session_result_name(result));
    } else if (result == GAME_SESSION_TOO_MANY_SAVES) {
        snprintf(app->message, sizeof(app->message),
                 "Only the first %d saves are shown", GAME_SESSION_MAX_SAVES);
    }
    if (!app->saves.count) {
        app->selected_save = -1;
        app->save_scroll = 0;
    } else {
        if (app->selected_save < 0) app->selected_save = 0;
        if ((size_t)app->selected_save >= app->saves.count)
            app->selected_save = (int)app->saves.count - 1;
    }
}

static void release_mouse(GuiApp *app)
{
    if (app->mouse_captured) EnableCursor();
    app->mouse_captured = false;
}

static void capture_mouse(GuiApp *app)
{
    DisableCursor();
    app->mouse_captured = true;
}

static ContainerLayout container_layout(BlockId id)
{
    bool barrel = id == BLOCK_BARREL;
    int columns = barrel ? 9 : id == BLOCK_HOPPER ? 5 : 3;
    int rows = id == BLOCK_HOPPER ? 1 : 3;
    int grid_width = columns*CONTAINER_SLOT_SIZE +
                     (columns - 1)*CONTAINER_SLOT_GAP;
    int grid_height = rows*CONTAINER_SLOT_SIZE +
                      (rows - 1)*CONTAINER_SLOT_GAP;
    int header = barrel ? 76 : 58;
    int panel_width = barrel ? grid_width + 40 : 480;
    int panel_height = header + grid_height + (barrel ? 42 : 0) + 18;
    ContainerLayout layout = {0};
    layout.panel = (Rectangle){
        (GetScreenWidth() - panel_width)*0.5f,
        (GetScreenHeight() - 80 - panel_height)*0.5f,
        (float)panel_width, (float)panel_height
    };
    layout.grid_x = layout.panel.x + (panel_width - grid_width)*0.5f;
    layout.grid_y = layout.panel.y + header;
    layout.columns = columns;
    layout.rows = rows;
    return layout;
}

static Rectangle container_slot_rectangle(ContainerLayout layout, int index)
{
    int column = index % layout.columns;
    int row = index / layout.columns;
    return (Rectangle){
        layout.grid_x + column*(CONTAINER_SLOT_SIZE + CONTAINER_SLOT_GAP),
        layout.grid_y + row*(CONTAINER_SLOT_SIZE + CONTAINER_SLOT_GAP),
        CONTAINER_SLOT_SIZE, CONTAINER_SLOT_SIZE
    };
}

static Rectangle barrel_signal_rectangle(ContainerLayout layout, int level)
{
    int width = 16*SIGNAL_BUTTON_SIZE + 15*SIGNAL_BUTTON_GAP;
    return (Rectangle){
        layout.panel.x + (layout.panel.width - width)*0.5f +
        level*(SIGNAL_BUTTON_SIZE + SIGNAL_BUTTON_GAP),
        layout.grid_y + layout.rows*(CONTAINER_SLOT_SIZE + CONTAINER_SLOT_GAP) + 8,
        SIGNAL_BUTTON_SIZE, 30
    };
}

static void open_barrel(GuiApp *app, BlockPos pos)
{
    RsResult result = world_open_barrel(game_session_world(app->session), pos);
    if (result != RS_OK) {
        snprintf(app->message, sizeof(app->message),
                 "Open barrel: %s", rs_result_name(result));
        return;
    }
    app->barrel_pos = pos;
    release_mouse(app);
    app->screen = GUI_BARREL;
    snprintf(app->message, sizeof(app->message),
             "Barrel: store items or use the signal shortcuts");
}

static void close_barrel(GuiApp *app)
{
    (void)world_close_barrel(game_session_world(app->session), app->barrel_pos);
    app->screen = GUI_WORLD;
    capture_mouse(app);
}

static void close_world(GuiApp *app)
{
    release_mouse(app);
    world_renderer_destroy(app->world_renderer);
    app->world_renderer = NULL;
    game_session_destroy(app->session);
    app->session = NULL;
    app->accumulator = 0.0;
}

static bool open_world(GuiApp *app, GameSession *session)
{
    World *world = game_session_world(session);
    if (world_stream(world,(BlockPos){16,3,16},5) != RS_OK) {
        snprintf(app->message,sizeof(app->message),"Cannot load world chunks");
        return false;
    }
    WorldRenderer *renderer = world_renderer_create(world, &app->blocks);
    if (!renderer || !world_renderer_sync(renderer)) {
        world_renderer_destroy(renderer);
        snprintf(app->message, sizeof(app->message),
                 "Cannot create the world render cache");
        return false;
    }

    app->session = session;
    app->world_renderer = renderer;
    app->camera = (Camera3D){0};
    app->camera.position = (Vector3){16.0f, 3.0f, 16.0f};
    app->camera.target = (Vector3){16.0f, 2.0f, 15.0f};
    app->camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    app->camera.fovy = 65.0f;
    app->camera.projection = CAMERA_PERSPECTIVE;
    app->accumulator = 0.0;
    app->screen = GUI_WORLD;
    snprintf(app->message, sizeof(app->message),
             "LMB remove | RMB use/place | Shift+RMB place | MMB pick | E inventory | / command | ESC menu");
    capture_mouse(app);
    return true;
}

static void return_to_title(GuiApp *app)
{
    close_world(app);
    app->screen = GUI_TITLE;
    app->selected_save = -1;
    refresh_saves(app);
}

static void perform_after_save(GuiApp *app, AfterSave action)
{
    if (action == AFTER_SAVE_TITLE) {
        return_to_title(app);
    } else if (action == AFTER_SAVE_EXIT) {
        app->running = false;
    }
}

static void request_save_action(GuiApp *app, AfterSave action)
{
    if (!app->session) {
        perform_after_save(app, action);
        return;
    }
    GameSessionResult result = game_session_save(app->session);
    if (result == GAME_SESSION_OK) {
        perform_after_save(app, action);
        return;
    }
    release_mouse(app);
    app->screen_before_error = app->screen;
    app->screen = GUI_SAVE_ERROR;
    app->after_save = action;
    snprintf(app->message, sizeof(app->message), "Save failed: %s",
             game_session_result_name(result));
}

static void create_world(GuiApp *app)
{
    if (!game_session_valid_name(app->world_name)) {
        snprintf(app->message, sizeof(app->message),
                 "Use letters, numbers, spaces, '-' or '_'");
        return;
    }
    if (save_name_exists(app, app->world_name)) {
        snprintf(app->message, sizeof(app->message),
                 "A world with that name already exists");
        return;
    }
    char path[GAME_SESSION_PATH_CAPACITY];
    GameSessionResult result = game_session_make_path(
        save_directory, app->world_name, path);
    if (result != GAME_SESSION_OK) {
        snprintf(app->message, sizeof(app->message), "Cannot create path: %s",
                 game_session_result_name(result));
        return;
    }
    GameSession *session = game_session_create(path, world_config_default(),
                                               &result);
    if (!session) {
        snprintf(app->message, sizeof(app->message), "Cannot create world: %s",
                 game_session_result_name(result));
        return;
    }
    result = game_session_save(session);
    if (result != GAME_SESSION_OK) {
        game_session_destroy(session);
        snprintf(app->message, sizeof(app->message), "Cannot save new world: %s",
                 game_session_result_name(result));
        return;
    }
    if (!open_world(app, session)) game_session_destroy(session);
}

static void load_selected_world(GuiApp *app)
{
    if (app->selected_save < 0 ||
        (size_t)app->selected_save >= app->saves.count) return;
    char path[GAME_SESSION_PATH_CAPACITY];
    GameSessionResult result = game_session_make_path(
        save_directory, app->saves.names[app->selected_save], path);
    if (result != GAME_SESSION_OK) {
        snprintf(app->message, sizeof(app->message), "Cannot open path: %s",
                 game_session_result_name(result));
        return;
    }
    GameSession *session = game_session_load(path, &result);
    if (!session) {
        snprintf(app->message, sizeof(app->message), "Cannot load world: %s",
                 game_session_result_name(result));
        return;
    }
    if (!open_world(app, session)) game_session_destroy(session);
}

static void update_name_input(GuiApp *app)
{
    int character;
    while ((character = GetCharPressed()) != 0) {
        bool allowed = character < 128 &&
            (isalnum((unsigned char)character) || character == ' ' ||
             character == '-' || character == '_');
        if (allowed && app->world_name_length + 1 < sizeof(app->world_name)) {
            app->world_name[app->world_name_length++] = (char)character;
            app->world_name[app->world_name_length] = '\0';
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && app->world_name_length) {
        app->world_name[--app->world_name_length] = '\0';
    }
}

static void keep_selection_visible(GuiApp *app)
{
    if (app->selected_save < app->save_scroll)
        app->save_scroll = app->selected_save;
    if (app->selected_save >= app->save_scroll + VISIBLE_SAVE_ROWS)
        app->save_scroll = app->selected_save - VISIBLE_SAVE_ROWS + 1;
    int maximum = (int)app->saves.count - VISIBLE_SAVE_ROWS;
    if (maximum < 0) maximum = 0;
    if (app->save_scroll > maximum) app->save_scroll = maximum;
    if (app->save_scroll < 0) app->save_scroll = 0;
}

static void update_title(GuiApp *app)
{
    update_name_input(app);
    if (app->saves.count) {
        if (IsKeyPressed(KEY_DOWN)) {
            app->selected_save = (app->selected_save + 1) % (int)app->saves.count;
        } else if (IsKeyPressed(KEY_UP)) {
            app->selected_save = (app->selected_save + (int)app->saves.count - 1) %
                                 (int)app->saves.count;
        }
        float wheel = GetMouseWheelMove();
        if (wheel < 0 && app->selected_save + 1 < (int)app->saves.count)
            ++app->selected_save;
        else if (wheel > 0 && app->selected_save > 0)
            --app->selected_save;
        keep_selection_visible(app);
    }
    if (IsKeyPressed(KEY_ENTER)) create_world(app);
}

static void draw_title(GuiApp *app)
{
    const Color paper = {226, 218, 196, 255};
    const Color ink = {26, 50, 55, 255};
    ClearBackground(paper);
    draw_centered("MACHINECRAFT", 48, 42, ink);
    draw_centered("A deterministic redstone workshop", 96, 20,
                  (Color){83, 99, 94, 255});
    DrawLine(120, 132, GetScreenWidth() - 120, 132,
             (Color){126, 119, 100, 255});

    int center = GetScreenWidth()/2;
    Rectangle input = {(float)center - 310, 180, 380, 44};
    DrawText("CREATE WORLD", (int)input.x, 150, 18, ink);
    DrawRectangleRec(input, (Color){247, 243, 230, 255});
    DrawRectangleLinesEx(input, 2.0f, (Color){92, 111, 112, 255});
    if (app->world_name_length)
        DrawText(app->world_name, (int)input.x + 12, (int)input.y + 11, 20, ink);
    else
        DrawText("World name", (int)input.x + 12, (int)input.y + 11, 20,
                 (Color){137, 137, 125, 255});
    if (((int)(GetTime()*2.0)) % 2 == 0) {
        int cursor_x = (int)input.x + 12 + MeasureText(app->world_name, 20);
        DrawLine(cursor_x, (int)input.y + 9, cursor_x, (int)input.y + 34, ink);
    }

    Rectangle create = {(float)center + 85, 180, 225, 44};
    bool can_create = game_session_valid_name(app->world_name) &&
                      !save_name_exists(app, app->world_name);
    if (button(create, "Create", can_create)) create_world(app);

    DrawText("LOAD WORLD", center - 310, 257, 18, ink);
    Rectangle list = {(float)center - 310, 284, 620, 245};
    DrawRectangleRec(list, (Color){238, 232, 214, 255});
    DrawRectangleLinesEx(list, 2.0f, (Color){126, 119, 100, 255});
    if (!app->saves.count) {
        DrawText("No saved worlds yet", (int)list.x + 18,
                 (int)list.y + 18, 20, (Color){117, 116, 103, 255});
    }
    for (int row = 0; row < VISIBLE_SAVE_ROWS; ++row) {
        int index = app->save_scroll + row;
        if ((size_t)index >= app->saves.count) break;
        Rectangle item = {list.x + 8, list.y + 8 + row*32,
                          list.width - 16, 29};
        bool selected = index == app->selected_save;
        bool hovered = point_in(item);
        if (selected || hovered)
            DrawRectangleRec(item, selected ? (Color){80, 105, 108, 220}
                                            : (Color){177, 177, 157, 160});
        DrawText(app->saves.names[index], (int)item.x + 9, (int)item.y + 5,
                 18, selected ? RAYWHITE : ink);
        if (hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            app->selected_save = index;
    }

    Rectangle load = {(float)center - 310, 548, 300, 44};
    Rectangle exit = {(float)center + 10, 548, 300, 44};
    if (button(load, "Load selected", app->selected_save >= 0))
        load_selected_world(app);
    if (button(exit, "Exit", true)) app->running = false;
    draw_centered(app->message, GetScreenHeight() - 34, 17,
                  (Color){112, 65, 52, 255});
}

static Direction camera_horizontal_direction(const Camera3D *camera)
{
    float x = camera->target.x - camera->position.x;
    float z = camera->target.z - camera->position.z;
    if (fabsf(x) > fabsf(z)) return x >= 0.0f ? DIR_EAST : DIR_WEST;
    return z >= 0.0f ? DIR_SOUTH : DIR_NORTH;
}

static const char *horizontal_direction_name(Direction direction)
{
    switch (direction) {
    case DIR_EAST: return "East (+X)";
    case DIR_WEST: return "West (-X)";
    case DIR_SOUTH: return "South (+Z)";
    case DIR_NORTH: return "North (-Z)";
    default: return "Unknown";
    }
}

static Direction camera_direction(const Camera3D *camera)
{
    float x = camera->target.x - camera->position.x;
    float y = camera->target.y - camera->position.y;
    float z = camera->target.z - camera->position.z;
    float ax = fabsf(x), ay = fabsf(y), az = fabsf(z);
    if (ay > ax && ay > az) return y >= 0.0f ? DIR_UP : DIR_DOWN;
    return camera_horizontal_direction(camera);
}

static void apply_edit(GuiApp *app, VoxelHit hit, BlockId selected, bool place,
                       Direction facing)
{
    if (!hit.hit) return;
    if (place && selected == BLOCK_AIR) {
        snprintf(app->message, sizeof(app->message),
                 "Selected hotbar slot is empty");
        return;
    }

    World *world = game_session_world(app->session);
    RsResult result;
    if (place) {
        if (!hit.has_face) {
            snprintf(app->message, sizeof(app->message),
                     "Cannot place while inside a block");
            return;
        }
        BlockPos target = block_offset(hit.pos, hit.face);
        if (!world_in_bounds(world, target) ||
            world_get(world, target).id != BLOCK_AIR) {
            snprintf(app->message, sizeof(app->message),
                     "Target occupied or outside world");
            return;
        }
        if ((selected == BLOCK_REDSTONE_WIRE ||
             selected == BLOCK_REDSTONE_REPEATER ||
             selected == BLOCK_REDSTONE_COMPARATOR ||
             selected == BLOCK_POWERED_RAIL ||
             selected == BLOCK_ACTIVATOR_RAIL) && hit.face != DIR_UP) {
            snprintf(app->message, sizeof(app->message),
                     "%s must be placed on a top face", block_name(selected));
            return;
        }
        bool directional = selected == BLOCK_REDSTONE_REPEATER ||
                           selected == BLOCK_REDSTONE_COMPARATOR ||
                           selected == BLOCK_POWERED_RAIL ||
                           selected == BLOCK_ACTIVATOR_RAIL;
        Direction placement = (selected == BLOCK_OBSERVER ||
                               selected == BLOCK_BARREL ||
                               selected == BLOCK_DROPPER ||
                               block_is_piston_base(selected)) ?
                              direction_opposite(
                                  camera_direction(&app->camera)) :
                              (directional ? facing : direction_opposite(hit.face));
        result = world_place(world, target, selected, placement);
    } else {
        result = world_remove(world, hit.pos);
    }
    if (result == RS_OK || result == RS_UNCHANGED) {
        RsResult flushed = game_session_flush(app->session);
        if (flushed != RS_OK) result = flushed;
    }
    snprintf(app->message, sizeof(app->message), "%s: %s",
             place ? "Place" : "Remove", rs_result_name(result));
}

static BlockId pickable_block(BlockId id)
{
    if (id == BLOCK_PISTON_HEAD) return BLOCK_PISTON;
    if (id == BLOCK_STICKY_PISTON_HEAD) return BLOCK_STICKY_PISTON;
    return id > BLOCK_AIR && id < BLOCK_INTERNAL_FIRST ? id : BLOCK_AIR;
}

static void advance_simulation(GuiApp *app)
{
    double frame = GetFrameTime();
    if (frame > 0.25) frame = 0.25;
    app->accumulator += frame;
    while (app->accumulator >= 0.05) {
        RsResult result = game_session_step(app->session);
        if (result != RS_OK) {
            snprintf(app->message, sizeof(app->message), "Simulation: %s",
                     rs_result_name(result));
            app->fatal_error = true;
            app->running = false;
            break;
        }
        app->accumulator -= 0.05;
    }
}

static void update_command(GuiApp *app)
{
    int character;
    while ((character = GetCharPressed()) != 0) {
        if (!app->command_length && character == '/') continue;
        if (character >= 32 && character < 127 &&
            app->command_length + 1 < sizeof(app->command)) {
            app->command[app->command_length++] = (char)character;
            app->command[app->command_length] = '\0';
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && app->command_length)
        app->command[--app->command_length] = '\0';
    if (IsKeyPressed(KEY_ESCAPE)) {
        app->screen = GUI_WORLD;
        capture_mouse(app);
        return;
    }
    if (!IsKeyPressed(KEY_ENTER)) return;
    PlotCommand command;
    if (!plot_command_parse(app->command, &command)) {
        snprintf(app->message, sizeof(app->message),
                 "Use /plot info or /plot expand DIRECTION [COUNT]");
    } else {
        RsResult result = plot_command_run(game_session_world(app->session),
                                           &command, app->message,
                                           sizeof(app->message));
        if (result != RS_OK)
            snprintf(app->message, sizeof(app->message), "Plot command: %s",
                     rs_result_name(result));
    }
    app->screen = GUI_WORLD;
    capture_mouse(app);
}

static void constrain_camera(GuiApp *app, const World *world)
{
    WorldPlotBounds bounds = world_plot_bounds(world);
    float x = app->camera.position.x;
    float z = app->camera.position.z;
    if (x < bounds.min_x) x = (float)bounds.min_x;
    if (x > bounds.max_x) x = (float)bounds.max_x;
    if (z < bounds.min_z) z = (float)bounds.min_z;
    if (z > bounds.max_z) z = (float)bounds.max_z;
    app->camera.target.x += x - app->camera.position.x;
    app->camera.target.z += z - app->camera.position.z;
    app->camera.position.x = x;
    app->camera.position.z = z;
}

static VoxelHit update_world(GuiApp *app)
{
    VoxelHit hit = {0};
    if (IsKeyPressed(KEY_SLASH)) {
        release_mouse(app);
        app->command[0] = '\0';
        app->command_length = 0;
        app->screen = GUI_COMMAND;
        return hit;
    }
    if (IsKeyPressed(KEY_E)) {
        release_mouse(app);
        app->screen = GUI_INVENTORY;
        snprintf(app->message, sizeof(app->message),
                 "Inventory: choose a hotbar slot, then click an icon");
        return hit;
    }
    if (IsKeyPressed(KEY_ESCAPE) || !IsWindowFocused()) {
        release_mouse(app);
        app->screen = GUI_PAUSE;
        app->accumulator = 0.0;
        return hit;
    }

    hotbar_update(&app->hotbar);
    UpdateCamera(&app->camera, CAMERA_FIRST_PERSON);
    const float vertical_speed = 5.0f;
    float vertical_input = 0.0f;
    if (IsKeyDown(KEY_SPACE)) vertical_input += 1.0f;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
        vertical_input -= 1.0f;
    float vertical_delta = vertical_input*vertical_speed*GetFrameTime();
    app->camera.position.y += vertical_delta;
    app->camera.target.y += vertical_delta;

    World *world = game_session_world(app->session);
    constrain_camera(app, world);

    Ray ray = GetScreenToWorldRay(
        (Vector2){GetScreenWidth()*0.5f, GetScreenHeight()*0.5f}, app->camera);
    double camera_x = floor((double)app->camera.position.x + 0.5);
    double camera_z = floor((double)app->camera.position.z + 0.5);
    if (!isfinite(camera_x) || !isfinite(camera_z) || camera_x < (double)INT32_MIN+16 ||
        camera_x > (double)INT32_MAX-16 || camera_z < (double)INT32_MIN+16 || camera_z > (double)INT32_MAX-16 ||
        world_stream(world,(BlockPos){(int)camera_x,0,(int)camera_z},5) != RS_OK) {
        snprintf(app->message,sizeof(app->message),"Cannot stream world chunks");
        app->fatal_error = true; app->running = false;
        return hit;
    }
    hit = voxel_pick(world,
        (PickVec3){ray.position.x, ray.position.y, ray.position.z},
        (PickVec3){ray.direction.x, ray.direction.y, ray.direction.z}, 6.0f);
    BlockId selected = hotbar_selected_block(&app->hotbar);
    Direction facing = camera_horizontal_direction(&app->camera);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        apply_edit(app, hit, selected, false, facing);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) ||
                     IsKeyDown(KEY_RIGHT_SHIFT);
        BlockId target = hit.hit ? (BlockId)world_get(world, hit.pos).id :
                                  BLOCK_AIR;
        if (hit.hit && !shift &&
            target == BLOCK_BARREL) {
            open_barrel(app, hit.pos);
        } else if (hit.hit && !shift &&
                   (target == BLOCK_HOPPER || target == BLOCK_DROPPER)) {
            app->container_pos = hit.pos;
            app->screen = target == BLOCK_HOPPER ? GUI_HOPPER : GUI_DROPPER;
            release_mouse(app);
        } else if (hit.hit && !shift && target == BLOCK_REDSTONE_WIRE) {
            RsResult result = world_toggle_wire_dot(world, hit.pos);
            if (result == RS_OK || result == RS_UNCHANGED)
                result = game_session_flush(app->session);
            snprintf(app->message, sizeof(app->message), "Toggle dot: %s",
                     rs_result_name(result));
        } else if (hit.hit && !shift &&
                   target == BLOCK_REDSTONE_REPEATER) {
            RsResult result = world_cycle_repeater_delay(world, hit.pos);
            if (result == RS_OK || result == RS_UNCHANGED)
                result = game_session_flush(app->session);
            snprintf(app->message, sizeof(app->message),
                     "Cycle repeater delay: %s", rs_result_name(result));
        } else if (hit.hit && !shift &&
                   target == BLOCK_REDSTONE_COMPARATOR) {
            RsResult result = world_toggle_comparator_mode(world, hit.pos);
            if (result == RS_OK || result == RS_UNCHANGED)
                result = game_session_flush(app->session);
            snprintf(app->message, sizeof(app->message),
                     "Toggle comparator mode: %s", rs_result_name(result));
        } else if (hit.hit && !shift && target == BLOCK_NOTE_BLOCK) {
            RsResult result = world_cycle_note(world, hit.pos);
            if (result == RS_OK || result == RS_UNCHANGED)
                result = game_session_flush(app->session);
            BlockState state = world_get(world, hit.pos);
            if ((result == RS_OK || result == RS_UNCHANGED) && state.id == BLOCK_NOTE_BLOCK)
                snprintf(app->message, sizeof(app->message), "Note: %u (%s)",
                    (unsigned)state.data.note_block.note,
                    note_instrument_name((NoteInstrument)state.data.note_block.instrument));
            else
                snprintf(app->message, sizeof(app->message), "Tune note: %s", rs_result_name(result));
        } else {
            if (hotbar_selected(&app->hotbar).kind == INVENTORY_ITEM)
                snprintf(app->message, sizeof(app->message),
                         "Open a hopper, dropper or barrel to store items");
            else apply_edit(app, hit, selected, true, facing);
        }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && hit.hit) {
        BlockId picked = pickable_block(
            (BlockId)world_get(world, hit.pos).id);
        if (picked != BLOCK_AIR &&
            hotbar_pick_block(&app->hotbar, picked)) {
            snprintf(app->message, sizeof(app->message),
                     "Hotbar slot %d: %s", app->hotbar.selected_slot + 1,
                     block_name(picked));
        } else {
            snprintf(app->message, sizeof(app->message),
                     "This block cannot be picked");
        }
    }

    return hit;
}

static void update_inventory(GuiApp *app)
{
    if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ESCAPE)) {
        app->screen = GUI_WORLD;
        capture_mouse(app);
        return;
    }

    hotbar_update(&app->hotbar);
    InventorySlot picked = inventory_update(&app->inventory);
    if (picked.kind != INVENTORY_EMPTY) {
        bool changed = hotbar_pick(&app->hotbar, picked);
        InventorySlot selected = hotbar_selected(&app->hotbar);
        if (selected.kind == INVENTORY_ITEM)
            snprintf(app->message, sizeof(app->message),
                     "Hotbar slot %d: %s x%u%s", app->hotbar.selected_slot + 1,
                     inventory_slot_name(selected), (unsigned)selected.stack.count,
                     changed ? "" : " (full)");
        else
            snprintf(app->message, sizeof(app->message),
                     "Hotbar slot %d: %s", app->hotbar.selected_slot + 1,
                     inventory_slot_name(selected));
    }

}

static bool write_container_slot(World *world, BlockPos pos, size_t index,
                                 ItemStack contents, RsResult *result)
{
    *result = world_set_container_slot(world, pos, index, contents);
    /* A simulation error may be reported after the slot was written. */
    ContainerStack committed;
    if (world_get_container_slot(world, pos, index, &committed) == RS_OK &&
        committed.item == contents.item && committed.count == contents.count &&
        committed.limit == contents.limit) return true;
    if (*result == RS_OK || *result == RS_UNCHANGED)
        *result = RS_INTERNAL_ERROR;
    return false;
}

static unsigned store_hotbar_items(World *world, BlockPos pos,
                                    InventorySlot *slot, unsigned amount,
                                    RsResult *result)
{
    unsigned moved = 0;
    size_t count = world_container_size(world, pos);
    for (int pass = 0; pass < 2 && moved < amount; ++pass) {
        for (size_t index = 0; index < count && moved < amount; ++index) {
            ContainerStack contents;
            *result = world_get_container_slot(world, pos, index, &contents);
            if (*result != RS_OK) return moved;
            if ((contents.count == 0) != (pass == 1)) continue;
            ItemStack remaining = slot->stack;
            unsigned added = item_stack_transfer(&contents, &remaining,
                                                   amount - moved);
            if (!added) continue;
            if (!write_container_slot(world, pos, index, contents, result))
                return moved;
            *slot = remaining.count ? (InventorySlot){
                .kind = INVENTORY_ITEM, .stack = remaining
            } : (InventorySlot){0};
            moved += added;
            if (*result != RS_OK && *result != RS_UNCHANGED) return moved;
        }
    }
    return moved;
}

static unsigned take_container_items(World *world, BlockPos pos, size_t index,
                                      Hotbar *hotbar, ItemStack contents,
                                      unsigned amount, RsResult *result)
{
    Hotbar updated = *hotbar;
    unsigned moved = 0;
    for (int pass = 0; pass < 3 && moved < amount; ++pass) {
        for (int slot = 0; slot < HOTBAR_SLOT_COUNT && moved < amount; ++slot) {
            InventorySlot *destination = &updated.slots[slot];
            if (pass == 0 && destination->kind != INVENTORY_ITEM) continue;
            if (pass == 1 && destination->kind != INVENTORY_EMPTY) continue;
            /* Creative block icons can be replaced in the selected slot. */
            if (pass == 2 && (slot != updated.selected_slot ||
                             destination->kind != INVENTORY_BLOCK)) continue;
            ItemStack stack = destination->kind == INVENTORY_ITEM ?
                              destination->stack : (ItemStack){0};
            unsigned added = item_stack_transfer(&stack, &contents,
                                                   amount - moved);
            if (!added) continue;
            *destination = (InventorySlot){.kind = INVENTORY_ITEM, .stack = stack};
            moved += added;
        }
    }
    if (!moved || !write_container_slot(world, pos, index, contents, result))
        return 0;
    *hotbar = updated;
    return moved;
}

static void update_container_slots(GuiApp *app, BlockPos pos,
                                    ContainerLayout layout)
{
    hotbar_update(&app->hotbar);
    bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool right = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (!left && !(right && shift)) return;

    int hotbar_slot = hotbar_slot_at(GetMousePosition());
    int container_slot = -1;
    if (hotbar_slot >= 0) app->hotbar.selected_slot = hotbar_slot;
    else {
        for (int index = 0; index < layout.columns*layout.rows; ++index) {
            if (point_in(container_slot_rectangle(layout, index))) {
                container_slot = index;
                break;
            }
        }
    }
    if (hotbar_slot < 0 && container_slot < 0) return;

    RsResult result = game_session_flush(app->session);
    if (result != RS_OK) {
        snprintf(app->message, sizeof(app->message), "Transfer failed: %s",
                 rs_result_name(result));
        return;
    }
    World *world = game_session_world(app->session);
    ItemStack source;
    if (hotbar_slot >= 0) {
        InventorySlot slot = app->hotbar.slots[hotbar_slot];
        if (slot.kind != INVENTORY_ITEM) return;
        source = slot.stack;
    } else {
        result = world_get_container_slot(world, pos, (size_t)container_slot,
                                          &source);
        if (result != RS_OK) return;
    }
    if (!source.count) return;
    unsigned amount = !shift ? 1u : left ? source.count :
                      ((unsigned)source.count + 1u)/2u;
    unsigned moved = hotbar_slot >= 0 ?
        store_hotbar_items(world, pos, &app->hotbar.slots[hotbar_slot], amount,
                            &result) :
        take_container_items(world, pos, (size_t)container_slot, &app->hotbar,
                               source, amount, &result);
    if (!moved && (result == RS_OK || result == RS_UNCHANGED)) {
        snprintf(app->message, sizeof(app->message),
                 "No items moved: destination has no free space");
        return;
    }
    if (result == RS_OK || result == RS_UNCHANGED)
        result = game_session_flush(app->session);
    snprintf(app->message, sizeof(app->message), "%s %u: %s",
             hotbar_slot >= 0 ? "Stored" : "Retrieved", moved,
             rs_result_name(result));
}

static void update_barrel(GuiApp *app)
{
    World *world = game_session_world(app->session);
    BlockState state = world_get(world, app->barrel_pos);
    if (state.id != BLOCK_BARREL) {
        close_barrel(app);
        snprintf(app->message, sizeof(app->message),
                 "Barrel is no longer available");
        return;
    }
    if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ESCAPE)) {
        close_barrel(app);
        return;
    }

    ContainerLayout layout = container_layout(BLOCK_BARREL);
    update_container_slots(app, app->barrel_pos, layout);
    int level = -1;
    state = world_get(world, app->barrel_pos);
    if (IsKeyPressed(KEY_LEFT) && state.data.barrel.level > 0)
        level = state.data.barrel.level - 1;
    if (IsKeyPressed(KEY_RIGHT) && state.data.barrel.level < 15)
        level = state.data.barrel.level + 1;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int value = 0; value <= 15; ++value) {
            if (point_in(barrel_signal_rectangle(layout, value))) {
                level = value;
                break;
            }
        }
    }
    if (level >= 0) {
        RsResult result = world_set_barrel_level(world, app->barrel_pos,
                                                 (uint8_t)level);
        if (result == RS_OK || result == RS_UNCHANGED)
            result = game_session_flush(app->session);
        snprintf(app->message, sizeof(app->message), "Barrel signal %d: %s",
                 level, rs_result_name(result));
    }
}

static void update_container(GuiApp *app)
{
    World *world = game_session_world(app->session);
    BlockId id = app->screen == GUI_DROPPER ? BLOCK_DROPPER : BLOCK_HOPPER;
    if (world_get(world, app->container_pos).id != id ||
        IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ESCAPE)) {
        app->screen = GUI_WORLD;
        capture_mouse(app);
        return;
    }
    update_container_slots(app, app->container_pos, container_layout(id));
}

static void draw_container(const GuiApp *app, BlockId id)
{
    bool barrel = id == BLOCK_BARREL;
    World *world = game_session_world(app->session);
    BlockPos pos = barrel ? app->barrel_pos : app->container_pos;
    ContainerLayout layout = container_layout(id);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){10, 18, 20, 155});
    DrawRectangleRec(layout.panel, (Color){226, 218, 196, 250});
    DrawRectangleLinesEx(layout.panel, 3.0f, (Color){73, 48, 24, 255});
    draw_centered(barrel ? "BARREL" : id == BLOCK_HOPPER ? "HOPPER" : "DROPPER",
                  (int)layout.panel.y + 18,
                  26, (Color){73, 48, 24, 255});
    BlockState state = world_get(world, pos);
    int level = barrel && state.id == BLOCK_BARREL ? state.data.barrel.level : 0;
    if (barrel)
        draw_centered(TextFormat("Comparator output: %d", level),
                      (int)layout.panel.y + 49, 17, (Color){100, 71, 36, 255});

    for (int index = 0; index < layout.columns*layout.rows; ++index) {
        Rectangle slot = container_slot_rectangle(layout, index);
        bool hovered = point_in(slot);
        DrawRectangleRec(slot, hovered ? (Color){102, 116, 112, 255} :
                                        (Color){75, 75, 75, 235});
        DrawRectangleLinesEx(slot, hovered ? 2.0f : 1.0f,
                             hovered ? (Color){244, 190, 88, 255} :
                                       (Color){145, 145, 145, 255});
        ContainerStack stack;
        if (world_get_container_slot(world, pos, (size_t)index, &stack) == RS_OK)
            inventory_draw_stack(&app->inventory, stack, slot);
    }
    if (barrel) {
        for (int value = 0; value <= 15; ++value) {
            Rectangle shortcut = barrel_signal_rectangle(layout, value);
            bool hovered = point_in(shortcut);
            DrawRectangleRec(shortcut, hovered ? (Color){111, 79, 50, 255} :
                                                (Color){82, 58, 37, 255});
            DrawRectangleLinesEx(shortcut, value == level ? 3.0f : 1.0f,
                                 value == level ? (Color){244, 190, 88, 255} :
                                                  (Color){142, 119, 86, 255});
            const char *text = TextFormat("%d", value);
            DrawText(text,
                     (int)(shortcut.x + (shortcut.width - MeasureText(text, 16))*0.5f),
                     (int)shortcut.y + 7, 16, RAYWHITE);
        }
    }
    hotbar_draw(&app->hotbar, &app->inventory);
}

static void draw_block_outline(BlockState state, Vector3 center)
{
    static BlockShape cached;
    static BlockShapeEdge edges[BLOCK_SHAPE_EDGES];
    static unsigned edge_count;
    BlockShape shape;
    block_outline_shape(state, &shape);
    if (shape.count != cached.count ||
        memcmp(shape.boxes, cached.boxes, shape.count*sizeof(shape.boxes[0]))) {
        cached = shape;
        edge_count = block_shape_edges(&shape, edges);
    }
    for (unsigned i = 0; i < edge_count; ++i) {
        const BlockShapeEdge *edge = &edges[i];
        Vector3 start = {center.x + edge->start[0]/16.0f - 0.5f,
                         center.y + edge->start[1]/16.0f - 0.5f,
                         center.z + edge->start[2]/16.0f - 0.5f};
        Vector3 end = {center.x + edge->end[0]/16.0f - 0.5f,
                       center.y + edge->end[1]/16.0f - 0.5f,
                       center.z + edge->end[2]/16.0f - 0.5f};
        DrawLine3D(start, end, YELLOW);
    }
}

static void draw_world(GuiApp *app, VoxelHit hit)
{
    ClearBackground((Color){184, 211, 214, 255});
    BeginMode3D(app->camera);
        world_renderer_draw(app->world_renderer);
        if (hit.hit) {
            Vector3 center = {(float)hit.pos.x, (float)hit.pos.y,
                              (float)hit.pos.z};
            draw_block_outline(world_get(game_session_world(app->session), hit.pos),
                                center);
        }
    EndMode3D();

    if (app->screen == GUI_WORLD) {
        int cx = GetScreenWidth()/2;
        int cy = GetScreenHeight()/2;
        DrawLine(cx - 6, cy, cx + 6, cy, BLACK);
        DrawLine(cx, cy - 6, cx, cy + 6, BLACK);
    }
    DrawText(TextFormat("Position: (%.2f, %.2f, %.2f)",
             app->camera.position.x,
             app->camera.position.y,
             app->camera.position.z),
             15, 15, 18, DARKBLUE);
    if (hit.hit) {
        DrawText(TextFormat("Target: (%d, %d, %d)",
                 hit.pos.x, hit.pos.y, hit.pos.z),
                 15, 39, 18, DARKBLUE);
    } else {
        DrawText("Target: none", 15, 39, 18, DARKBLUE);
    }
    DrawText(TextFormat("Facing: %s", horizontal_direction_name(
                 camera_horizontal_direction(&app->camera))),
             15, 63, 18, DARKBLUE);
    hotbar_draw(&app->hotbar, &app->inventory);
}

static void draw_command(const GuiApp *app)
{
    int y = GetScreenHeight() - 88;
    DrawRectangle(18, y, GetScreenWidth() - 36, 52,
                  (Color){15, 27, 30, 235});
    DrawRectangleLines(18, y, GetScreenWidth() - 36, 52,
                       (Color){244, 190, 88, 255});
    DrawText(TextFormat("/%s", app->command), 32, y + 15, 21, RAYWHITE);
}

static void draw_pause(GuiApp *app)
{
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){10, 18, 20, 150});

    int center = GetScreenWidth()/2;
    Rectangle panel = {(float)center - 190, 105, 380, 400};

    DrawRectangleRec(panel, (Color){226, 218, 196, 248});
    DrawRectangleLinesEx(panel, 3.0f, (Color){34, 63, 66, 255});
    draw_centered("WORLD PAUSED", 137, 30, (Color){26, 50, 55, 255});

    Rectangle resume = {(float)center - 140, 205, 280, 44};
    Rectangle save = {(float)center - 140, 265, 280, 44};
    Rectangle title = {(float)center - 140, 325, 280, 44};
    Rectangle exit = {(float)center - 140, 385, 280, 44};

    if (button(resume, "Continue", true)) {
        app->screen = GUI_WORLD;
        capture_mouse(app);
        return;
    }

    if (button(save, "Save", true)) {
        GameSessionResult result = game_session_save(app->session);

        snprintf(app->message, sizeof(app->message), "Save: %s",
                 game_session_result_name(result));

        if (result != GAME_SESSION_OK) {
            app->screen_before_error = GUI_PAUSE;
            app->screen = GUI_SAVE_ERROR;
            app->after_save = AFTER_SAVE_NONE;
        }

        return;
    }

    if (button(title, "Save and title", true)) {
        request_save_action(app, AFTER_SAVE_TITLE);
        return;
    }

    if (button(exit, "Save and exit", true)) {
        request_save_action(app, AFTER_SAVE_EXIT);
        return;
    }
}

static void draw_save_error(GuiApp *app)
{
    ClearBackground((Color){39, 48, 49, 255});
    draw_centered("THE WORLD WAS NOT CLOSED", 130, 30, RAYWHITE);
    draw_centered(app->message, 185, 19, (Color){244, 190, 88, 255});
    draw_centered("The last valid .mcw file is still intact.",
                  220, 18, (Color){193, 205, 204, 255});
    int center = GetScreenWidth()/2;
    Rectangle retry = {(float)center - 160, 290, 320, 46};
    Rectangle cancel = {(float)center - 160, 352, 320, 46};
    Rectangle discard = {(float)center - 160, 414, 320, 46};
    if (button(retry, "Retry save", true)) {
        GameSessionResult result = game_session_save(app->session);
        if (result == GAME_SESSION_OK) {
            if (app->after_save == AFTER_SAVE_NONE)
                app->screen = app->screen_before_error;
            else
                perform_after_save(app, app->after_save);
        } else {
            snprintf(app->message, sizeof(app->message), "Save failed: %s",
                     game_session_result_name(result));
        }
    }
    if (button(cancel, "Return to world", true)) {
        app->screen = app->screen_before_error == GUI_TITLE ?
                      GUI_PAUSE : app->screen_before_error;
        app->after_save = AFTER_SAVE_NONE;
    }
    const char *discard_label = app->after_save == AFTER_SAVE_TITLE ?
                                "Title without saving" : "Exit without saving";
    if (button(discard, discard_label, app->after_save != AFTER_SAVE_NONE))
        perform_after_save(app, app->after_save);
}

static bool initialize(GuiApp *app)
{
    *app = (GuiApp){0};
    app->running = true;
    app->selected_save = -1;
    app->screen = GUI_TITLE;
    snprintf(app->message, sizeof(app->message),
             "Create a world or load an existing .mcw file");

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Machinecraft");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    if (!block_renderer_init(&app->blocks)) {
        fprintf(stderr, "Cannot load block textures; run from the project root\n");
        return false;
    }
    if (!inventory_init(&app->inventory)) {
        fprintf(stderr, "Cannot load inventory textures; run from the project root\n");
        return false;
    }
    if (!hotbar_init(&app->hotbar, &app->inventory)) {
        fprintf(stderr, "Cannot load hotbar textures; run from the project root\n");
        return false;
    }
    refresh_saves(app);
    return true;
}

static void shutdown_gui(GuiApp *app)
{
    close_world(app);
    hotbar_unload(&app->hotbar);
    inventory_unload(&app->inventory);
    block_renderer_unload(&app->blocks);
    CloseWindow();
}

int gui_run(void)
{
    GuiApp app;
    if (!initialize(&app)) {
        shutdown_gui(&app);
        return EXIT_FAILURE;
    }

    while (app.running) {
        bool close_requested = WindowShouldClose();
        if (close_requested && !app.close_latched &&
            app.screen != GUI_SAVE_ERROR) {
            app.close_latched = true;
            if (app.screen == GUI_BARREL) {
                (void)world_close_barrel(game_session_world(app.session), app.barrel_pos);
                app.screen = GUI_WORLD;
            }
            if (app.session && game_session_dirty(app.session))
                request_save_action(&app, AFTER_SAVE_EXIT);
            else
                app.running = false;
        }
        if (!app.running) break;

        GameSession *frame_session = app.session;
        VoxelHit hit = {0};
        if (app.screen == GUI_TITLE) update_title(&app);
        else if (app.screen == GUI_WORLD) hit = update_world(&app);
        else if (app.screen == GUI_COMMAND) update_command(&app);
        else if (app.screen == GUI_INVENTORY) update_inventory(&app);
        else if (app.screen == GUI_BARREL) update_barrel(&app);
        else if (app.screen == GUI_HOPPER || app.screen == GUI_DROPPER)
            update_container(&app);

        if (app.session && app.session == frame_session &&
            app.screen >= GUI_WORLD && app.screen <= GUI_DROPPER) {
            advance_simulation(&app);
            if (!app.running) break;
        }

        if (app.world_renderer && !world_renderer_sync(app.world_renderer)) {
            fprintf(stderr, "Cannot update render cache\n");
            app.fatal_error = true;
            break;
        }

        BeginDrawing();
        if (app.screen == GUI_TITLE) {
            draw_title(&app);
        } else if (app.screen == GUI_SAVE_ERROR) {
            draw_save_error(&app);
        } else {
            draw_world(&app, hit);
            if (app.screen == GUI_INVENTORY) {
                inventory_draw(&app.inventory,
                               hotbar_selected(&app.hotbar));
                hotbar_draw(&app.hotbar, &app.inventory);
            }
            if (app.screen == GUI_BARREL) draw_container(&app, BLOCK_BARREL);
            if (app.screen == GUI_HOPPER) draw_container(&app, BLOCK_HOPPER);
            if (app.screen == GUI_DROPPER) draw_container(&app, BLOCK_DROPPER);
            if (app.screen == GUI_COMMAND) draw_command(&app);
            if (app.screen == GUI_PAUSE) draw_pause(&app);
        }
        EndDrawing();
    }

    if (app.fatal_error && app.session && game_session_dirty(app.session))
        (void)game_session_save(app.session);
    shutdown_gui(&app);
    return app.fatal_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
