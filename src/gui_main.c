#include "raylib.h"

int main(void)
{
    InitWindow(800, 450, "raylib example - basic window");

    while (!WindowShouldClose())
    {
        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawText("Hell world!!", 190, 200, 20, BLUE);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
