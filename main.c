#include "raylib.h"
#include <stdbool.h>
#include <stdio.h>

int main()
{
    const int width = 800;
    const int height = 480;

    InitWindow(width, height, "Synth");
    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText("Hi", 100, 100, 32, WHITE);
        EndDrawing();
    }
    CloseWindow();

    return 0;
}