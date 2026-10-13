#include "raylib.h"

int main(void)
{
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Machinecraft");
    SetExitKey(KEY_NULL);

    Camera3D camera = {0};
    camera.position = (Vector3){ 0.0f, 3.0f, 3.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    DisableCursor();
    bool mouseCaptured = true;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        bool justCaptured = false;
        bool focused = IsWindowFocused();

        if (!focused && mouseCaptured)
        {
            EnableCursor();
            mouseCaptured = false;
        }
        else if (focused)
        {
            if (IsKeyPressed(KEY_ESCAPE))
            {
                if (mouseCaptured)
                {
                    EnableCursor();
                    mouseCaptured = false;
                }
                else
                {
                    DisableCursor();
                    mouseCaptured = true;
                    justCaptured = true;
                }
            }
            else if (!mouseCaptured && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                DisableCursor();
                mouseCaptured = true;
                justCaptured = true;
            }
        }

        if (mouseCaptured && focused && !justCaptured)
        {
            UpdateCamera(&camera, CAMERA_FIRST_PERSON);
        }

        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
            DrawCube((Vector3){0, 0, 0}, 1, 1, 1, LIGHTGRAY);
            DrawCubeWires((Vector3){0, 0, 0}, 1, 1, 1, GRAY);
            DrawCube((Vector3){0, 0, 1}, 1, 1, 1, LIGHTGRAY);
            DrawCubeWires((Vector3){0, 0, 1}, 1, 1, 1, GRAY);            
        
        EndMode3D();

        if (mouseCaptured)
        {
            int centerX = GetScreenWidth() / 2;
            int centerY = GetScreenHeight() / 2;
            DrawLine(centerX - 5, centerY, centerX + 5, centerY, WHITE);
            DrawLine(centerX, centerY - 5, centerX, centerY + 5, WHITE);
        }
        else
        {
            DrawText("Paused - Click or press ESC to resume", 20, 20, 20, WHITE);
        }

        EndDrawing();
    }
    EnableCursor();
    CloseWindow();
    return 0;
}



