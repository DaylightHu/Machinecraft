#include "raylib.h"
#include "rs/sim.h"
#include "frontend/block_renderer.h"
#include "frontend/voxel_pick.h"

#include <stdio.h>
#include <stdlib.h>

enum {SCREEN_WIDTH = 1000, SCREEN_HEIGHT = 650};

// 暂时使用不同颜色来代替红石粉和火把
static Color block_color(BlockState state)
{
    switch ((BlockId)state.id)
    {
    case BLOCK_REDSTONE_WIRE:  return state.data.wire.power
                               ? (Color){235, 65, 55, 255}
                               : (Color){115, 39, 40, 255};
    case BLOCK_REDSTONE_TORCH: return state.data.torch.lit ? RED : MAROON;
    default:                   return BLANK;
    }
}

static void draw_world(const World *world, WorldConfig config, const BlockRenderer *renderer)
{
    /* Prototype renderer: no per-block raycast and no backend mutation. */
    for (int y = 0; y < config.size_y; ++y)
    for (int z = 0; z < config.size_z; ++z)
    for (int x = 0; x < config.size_x; ++x)
    {
        BlockPos pos = {x, y, z};
        BlockState state = world_get(world, pos);
        if (state.id == BLOCK_AIR) continue;

        Vector3 center = {(float)x, (float)y, (float)z};
        Color color = block_color(state);

        if (state.id == BLOCK_REDSTONE_WIRE)
        {
            center.y -= 0.45f;
            DrawCube(center, 0.82f, 0.07f, 0.82f, color);
        }
        else if (state.id == BLOCK_REDSTONE_TORCH)
        {
            Vector3 stem = {(float)x, (float)y - 0.20f, (float)z};
            DrawCube(stem, 0.12f, 0.55f, 0.12f, BROWN);
            Vector3 tip = {(float)x, (float)y + 0.11f, (float)z};
            DrawCube(tip, 0.20f, 0.18f, 0.20f, color);
        }
        else
        {
            block_renderer_draw_cube(renderer, (BlockId)state.id, center);
        }
    }
}

static void apply_edit(World *world, VoxelHit hit, BlockId selected,
        bool place, char message[128])
{
    if (!hit.hit) return;

    RsResult result;
    if (place)
    {
        if (!hit.has_face)
        {
            snprintf(message, 128, "Cannot place while inside a block");
            return;
        }

        BlockPos target = block_offset(hit.pos, hit.face);
        if (!world_in_bounds(world, target) ||
            world_get(world, target).id != BLOCK_AIR)
        {
            snprintf(message, 128, "Target occupied or outside world");
            return;
        }

        /* Minecraft-like policy: dust is placed on the upper face. */
        if (selected == BLOCK_REDSTONE_WIRE && hit.face != DIR_UP)
        {
            snprintf(message, 128, "Wire must be placed on a top face");
            return;
        }

        Direction support = direction_opposite(hit.face);
        result = world_place(world, target, selected, support);
    }
    else
    {
        result = world_remove(world, hit.pos);
    }

    if (result == RS_OK || result == RS_UNCHANGED)
    {
        RsResult flush = sim_flush(world);
        if (flush != RS_OK) result = flush;
    }
    snprintf(message, 128, "%s: %s", place ? "Place" : "Remove",
             rs_result_name(result));
}

int main(void)
{
    WorldConfig config = world_config_default();
    World *world = world_create(config);
    if (!world)
    {
        fprintf(stderr, "Cannot create world\n");
        return EXIT_FAILURE;
    }

    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Machinecraft");
    SetExitKey(KEY_NULL);

    BlockRenderer renderer = {0};
    if (!block_renderer_init(&renderer))
    {
        fprintf(stderr, "Cannot load block textures; run from the project root\n");
        CloseWindow();
        world_destroy(world);
        return EXIT_FAILURE;
    }

    Camera3D camera = {0};
    camera.position = (Vector3){ 8.0f, 3.0f, 8.0f };
    camera.target = (Vector3){ 8.0f, 2.0f, 7.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 65.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    DisableCursor();
    bool mouse_captured = true;
    BlockId selected = BLOCK_IRON;
    char message[128] = "LMB remove | RMB place | MMB wire dot | 1-4 select";
    double accumulator = 0.0;
    bool fatal_error = false;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        bool just_captured = false;
        bool focused = IsWindowFocused();

        if (!focused && mouse_captured)
        {
            EnableCursor();
            mouse_captured = false;
        }
        else if (focused)
        {
            if (IsKeyPressed(KEY_ESCAPE))
            {
                if (mouse_captured)
                {
                    EnableCursor();
                    mouse_captured = false;
                }
                else
                {
                    DisableCursor();
                    mouse_captured = true;
                    just_captured = true;
                }
            }
            else if (!mouse_captured && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                DisableCursor();
                mouse_captured = true;
                just_captured = true;
            }
        }

        if (IsKeyPressed(KEY_ONE))   selected = BLOCK_IRON;
        if (IsKeyPressed(KEY_TWO))   selected = BLOCK_REDSTONE_BLOCK;
        if (IsKeyPressed(KEY_THREE)) selected = BLOCK_REDSTONE_WIRE;
        if (IsKeyPressed(KEY_FOUR))  selected = BLOCK_REDSTONE_TORCH;

        VoxelHit hit = {0};

        if (focused && mouse_captured && !just_captured)
        {
            UpdateCamera(&camera, CAMERA_FIRST_PERSON);

            // Creative Mode: <Space>=up, <Shift>=down
            const float vertical_speed = 5.0f;
            float vertical_input = 0.0f;

            if (IsKeyDown(KEY_SPACE))
                vertical_input += 1.0f;

            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                vertical_input -= 1.0f;

            float vertical_delta = vertical_input * vertical_speed * GetFrameTime();

            camera.position.y += vertical_delta;
            camera.target.y += vertical_delta;

            Ray ray = GetScreenToWorldRay(
                (Vector2){GetScreenWidth()*0.5f, GetScreenHeight()*0.5f},
                camera
            );
            hit = voxel_pick(
                world,
                (PickVec3){ray.position.x, ray.position.y, ray.position.z},
                (PickVec3){ray.direction.x, ray.direction.y, ray.direction.z},
                6.0f
            );

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                apply_edit(world, hit, selected, false, message);
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
                apply_edit(world, hit, selected, true, message);
            if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) &&
                hit.hit && world_get(world, hit.pos).id == BLOCK_REDSTONE_WIRE)
            {
                RsResult result = world_toggle_wire_dot(world, hit.pos);
                if (result == RS_OK || result == RS_UNCHANGED)
                    result = sim_flush(world);
                snprintf(message, sizeof(message), "Toggle dot: %s",
                         rs_result_name(result));
            }

            /* Redstone game ticks are independent of render FPS (20 TPS). */
            double frame = (double)GetFrameTime();
            if (frame > 0.25) frame = 0.25;
            accumulator += frame;
            while (accumulator >= 0.05)
            {
                RsResult result = sim_step(world);
                if (result != RS_OK)
                {
                    fprintf(stderr, "Simulation: %s\n",
                            rs_result_name(result));
                    fatal_error = true;
                    break;
                }
                accumulator -= 0.05;
            }
        }
        else
        {
            accumulator = 0.0;
        }

        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
            draw_world(world, config, &renderer);
            if (hit.hit)
            {
                Vector3 center = {
                    (float)hit.pos.x, (float)hit.pos.y, (float)hit.pos.z
                };
                DrawCubeWires(center, 1.02f, 1.02f, 1.02f, YELLOW);
            }
        EndMode3D();

        if (mouse_captured)
        {
            int cx = GetScreenWidth()/2;
            int cy = GetScreenHeight()/2;
            DrawLine(cx - 6, cy, cx + 6, cy, BLACK);
            DrawLine(cx, cy - 6, cx, cy + 6, BLACK);
        }
        else
        {
            DrawText("Paused: click or press ESC to resume",
                     15, 72, 20, DARKBLUE);
        }

        DrawText("1 Iron | 2 Redstone Block | 3 Wire | 4 Torch",
                 15, 15, 20, DARKBLUE);
        DrawText(TextFormat("Selected: %s | Tick: %llu",
                 block_name(selected), (unsigned long long)world_tick(world)),
                 15, 42, 20, DARKBLUE);
        DrawText(message, 15, GetScreenHeight()-34, 18, DARKBLUE);
        EndDrawing();
    }

    EnableCursor();
    CloseWindow();
    world_destroy(world);
    return fatal_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
