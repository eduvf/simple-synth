#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "raylib.h"

#define SAMPLE_RATE 44100
#define STREAM_BUFFER_SIZE 1024

typedef struct
{
    float phase;
    float phase_stride;
} Oscillator;

void updateOsc(Oscillator *osc)
{
    osc->phase += osc->phase_stride;
    if (osc->phase >= 1.0f)
        osc->phase -= 1.0f;
}

void updateSignal(float *signal, Oscillator *osc)
{
    for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
    {
        updateOsc(osc);
        signal[t] = sinf(2.0f * PI * osc->phase);
    }
}

int main()
{
    const int width = 800;
    const int height = 480;

    InitWindow(width, height, "Synth");
    SetTargetFPS(60);

    InitAudioDevice();
    SetMasterVolume(0.25f);

    unsigned int sample_rate = SAMPLE_RATE;
    SetAudioStreamBufferSizeDefault(STREAM_BUFFER_SIZE);

    AudioStream synth_stream =
        LoadAudioStream(sample_rate, sizeof(float) * 8, 1);
    PlayAudioStream(synth_stream);

    float frequency = 5.0f;
    float sample_duration = (1.0f / sample_rate);

    Oscillator osc = {.phase = 0.0f,
                      .phase_stride = frequency * sample_duration};
    Oscillator lfo = {.phase = 0.0f, .phase_stride = 100.0f * sample_duration};

    float signal[1024];

    while (!WindowShouldClose())
    {
        Vector2 mouse_pos = GetMousePosition();

        if (IsAudioStreamProcessed(synth_stream))
        {
            updateOsc(&lfo);
            frequency = 220.0f + ((mouse_pos.x / width) * 100.0f);
            // frequency = 220.0f + (sinf(2.0f * PI * lfo.phase) * 50.0f);
            osc.phase_stride = frequency * sample_duration;

            updateSignal(signal, &osc);
            UpdateAudioStream(synth_stream, signal, STREAM_BUFFER_SIZE);
        }

        BeginDrawing();
        ClearBackground(BLACK);

        DrawText(TextFormat("Freq: %f", frequency), 100, 100, 32, WHITE);

        for (size_t i = 0; i < 1024; i++)
        {
            DrawPixel(i, height / 2 + (int)(signal[i] * 100), YELLOW);
        }

        EndDrawing();
    }

    UnloadAudioStream(synth_stream);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}