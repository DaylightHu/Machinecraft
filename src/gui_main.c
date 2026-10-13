#include "raylib.h"

int main(void)
{
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Machinecraft");

    Camera3D camera = {0};
    camera.position = (Vector3){ 0.0f, 3.0f, 3.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_FREE);
        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
            DrawCube((Vector3){0, 0, 0}, 1, 1, 1, LIGHTGRAY);
            DrawCubeWires((Vector3){0, 0, 0}, 1, 1, 1, GRAY);
            DrawCube((Vector3){0, 0, 1}, 1, 1, 1, LIGHTGRAY);
            DrawCubeWires((Vector3){0, 0, 1}, 1, 1, 1, GRAY);            
        
        EndMode3D();

        DrawLine(screenWidth/2 - 5, screenHeight/2, 
                screenWidth/2 + 5, screenHeight/2, WHITE);
        DrawLine(screenWidth/2, screenHeight/2 - 5,
                screenWidth/2, screenHeight/2 + 5, WHITE);

        EndDrawing();
    }
    CloseWindow();
    return 0;
}



