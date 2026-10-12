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
    camera.fovy = 120.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_FREE);
        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
        
        Vector3 origin = {0.0f, 0.0f, 0.0f};
        DrawCube(origin, 1, 1, 1, DARKPURPLE);
        DrawCubeWires(origin, 1, 1, 1, PURPLE);
        EndMode3D();
        EndDrawing();
    }
    return 0;
}



