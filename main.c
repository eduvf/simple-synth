#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "raylib.h"

typedef struct
{
    float phase;
} Oscillator;

void updateSignal(float *signal, float frequency, float sample_duration)
{
    for (size_t t = 0; t < 1024; t++)
    {
        signal[t] = sinf(2.0f * PI * frequency * sample_duration * (float)t);
    }
}

int main()
{
    const int width = 800;
    const int height = 480;

    InitWindow(width, height, "Synth");
    SetTargetFPS(60);

    Oscillator osc = {.phase = 0.0f};
    float signal[1024];
    float frequency = 5;
    unsigned int sample_rate = 1024;
    float sample_duration = (1.0f / sample_rate);
    updateSignal(signal, frequency, sample_duration);

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(BLACK);

        DrawText("Sine wave:", 100, 100, 32, WHITE);

        for (size_t i = 0; i < 1024; i++)
        {
            DrawPixel(i, height / 2 + (int)(signal[i] * 100), YELLOW);
        }

        EndDrawing();
    }
    CloseWindow();

    return 0;
}