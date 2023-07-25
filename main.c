// https://www.youtube.com/watch?v=jZSnH33Vkh4

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "raylib.h"

#define SAMPLE_RATE 44100
#define SAMPLE_DURATION (1.0f / SAMPLE_RATE)
#define STREAM_BUFFER_SIZE 1024
#define NUM_OSCILLATORS 128

typedef struct
{
    float phase;
    float freq;
    float amp;
} Oscillator;

void updateOsc(Oscillator *osc, float freq_mod)
{
    osc->phase += (osc->freq + freq_mod) * SAMPLE_DURATION;
    if (osc->phase < 0.0f)
        osc->phase -= 1.0f;
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

float sineWaveOsc(Oscillator *osc)
{
    return sinf(2.0f * PI * osc->phase) * osc->amp;
}

void accumulateSignal(float *signal, Oscillator *osc, Oscillator *lfo)
{
    for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
    {
        updateOsc(lfo, 0.0f);
        updateOsc(osc, sineWaveOsc(lfo));
        signal[t] += sineWaveOsc(osc);
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

    float sample_duration = (1.0f / sample_rate);

    Oscillator osc[NUM_OSCILLATORS] = {0};
    Oscillator lfo = {.phase = 0.0f};
    lfo.freq = 3.0f;
    lfo.amp = 50.0f;

    float signal[STREAM_BUFFER_SIZE];

    while (!WindowShouldClose())
    {
        Vector2 mouse_pos = GetMousePosition();
        float normalized_mouse_x = mouse_pos.x / (float)width;
        float normalized_mouse_y = mouse_pos.y / (float)height;
        float base_freq = 25.0f + (normalized_mouse_x * 400.0f);
        lfo.freq = 3.0f + (normalized_mouse_y * 3.0f);

        if (IsAudioStreamProcessed(synth_stream))
        {
            zeroSignal(signal);

            for (size_t i = 0; i < NUM_OSCILLATORS; i++)
            {
                if (i % 2 != 0)
                {
                    osc[i].freq = base_freq * i;
                    osc[i].amp = 1.0f / NUM_OSCILLATORS;
                    accumulateSignal(signal, &osc[i], &lfo);
                }
            }

            UpdateAudioStream(synth_stream, signal, STREAM_BUFFER_SIZE);
        }

        BeginDrawing();
        ClearBackground(BLACK);

        DrawText(TextFormat("FPS: %i, delta: %f", GetFPS(), GetFrameTime()),
                 100, 50, 16, RED);

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