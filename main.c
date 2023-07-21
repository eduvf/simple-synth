#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "raylib.h"

#define SAMPLE_RATE 44100
#define STREAM_BUFFER_SIZE 1024
#define NUM_OSCILLATORS 128

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

void zeroSignal(float *signal)
{
    for (size_t i = 0; i < STREAM_BUFFER_SIZE; i++)
    {
        signal[i] = 0.0f;
    }
}

void accumulateSignal(float *signal, Oscillator *osc, float amplitude)
{
    for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
    {
        updateOsc(osc);
        signal[t] += sinf(2.0f * PI * osc->phase) * amplitude;
    }
}

int main()
{
    const int width = 800;
    const int height = 480;

    InitWindow(width, height, "Synth");
    SetTargetFPS(120);

    InitAudioDevice();
    SetMasterVolume(0.25f);

    unsigned int sample_rate = SAMPLE_RATE;
    SetAudioStreamBufferSizeDefault(STREAM_BUFFER_SIZE);

    AudioStream synth_stream =
        LoadAudioStream(sample_rate, sizeof(float) * 8, 1);
    PlayAudioStream(synth_stream);

    float frequency = 5.0f;
    float sample_duration = (1.0f / sample_rate);

    Oscillator osc[NUM_OSCILLATORS] = {0};
    // Oscillator lfo = {.phase = 0.0f, .phase_stride = 100.0f *
    // sample_duration};

    float signal[STREAM_BUFFER_SIZE];

    float detune = 0.01f;

    while (!WindowShouldClose())
    {
        Vector2 mouse_pos = GetMousePosition();
        float normalized_mouse_x = mouse_pos.x / (float)width;
        detune = 1.0f + normalized_mouse_x * 10.0f;

        if (IsAudioStreamProcessed(synth_stream))
        {
            zeroSignal(signal);

            // frequency = 220.0f + (sinf(2.0f * PI * lfo.phase) * 50.0f);

            for (size_t i = 0; i < NUM_OSCILLATORS; i++)
            {
                if (i % 2 != 0)
                {
                    float normalized_index = (float)i / NUM_OSCILLATORS;
                    float base_freq = 20.0f + (normalized_mouse_x * 50.0f);
                    frequency = base_freq * i;
                    float phase_stride = frequency * sample_duration;

                    osc[i].phase_stride = phase_stride;
                    accumulateSignal(signal, &osc[i], 1.0f / NUM_OSCILLATORS);
                }
            }

            UpdateAudioStream(synth_stream, signal, STREAM_BUFFER_SIZE);
        }

        BeginDrawing();
        ClearBackground(BLACK);

        DrawText(TextFormat("FPS: %i, delta: %f", GetFPS(), GetFrameTime()),
                 100, 50, 16, RED);
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