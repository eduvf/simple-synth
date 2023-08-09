// https://www.youtube.com/watch?v=fqUbYIhLTqw

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "raylib.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

#define SAMPLE_RATE 44100
#define SAMPLE_DURATION (1.0f / SAMPLE_RATE)
#define STREAM_BUFFER_SIZE 1024
#define NUM_OSCILLATORS 32
#define MAX_UI_OSC 32

#define LEFT_PANEL_WIDTH (SCREEN_WIDTH / 4.0f)

#define WAVE_SHAPE_OPTIONS "sine;sawtooth;square;triangle"
typedef enum
{
    WaveSin = 0,
    WaveSaw = 1,
    WaveSqr = 2,
    WaveTri = 3
} WaveShape;

typedef struct
{
    float freq;
    float amp;
    WaveShape shape;
    bool is_dropdown_open;
} UIOsc;

typedef struct
{
    float phase;
    float phase_dt;
    float freq;
    float amp;
} Oscillator;

typedef struct
{
    Oscillator *sinOsc;
    Oscillator *sawOsc;
    Oscillator *triOsc;
    Oscillator *sqrOsc;
    size_t n_sinOsc;
    size_t n_sawOsc;
    size_t n_triOsc;
    size_t n_sqrOsc;
    Oscillator lfo;
    float *signal;
    size_t signal_length;
    float audio_frame_duration;

    UIOsc ui_osc[MAX_UI_OSC];
    size_t ui_osc_count;
} Synth;

typedef float (*WaveShapeFn)(Oscillator *);

float bandLimitedRippleFx(float phase, float phase_dt)
{
    if (phase < phase_dt)
    {
        phase /= phase_dt;
        return phase + phase - phase * phase - 1.0f;
    }
    else if (phase > 1.0f - phase_dt)
    {
        phase = (phase - 1.0f) / phase_dt;
        return phase * phase + phase + phase + 1.0f;
    }
    else
        return 0.0f;
}

void updateOsc(Oscillator *osc, float freq_mod)
{
    osc->phase_dt = (osc->freq + freq_mod) * SAMPLE_DURATION;
    osc->phase += osc->phase_dt;
    if (osc->phase < 0.0f)
        osc->phase += 1.0f;
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

float sinWaveOsc(Oscillator *osc) { return sinf(2.0f * PI * osc->phase); }

float sawWaveOsc(Oscillator *osc)
{
    float sample = ((osc->phase * 2.0f) - 1.0f);
    sample -= bandLimitedRippleFx(osc->phase, osc->phase_dt);
    return sample;
}

float triWaveOsc(Oscillator *osc)
{
    if (osc->phase < 0.5f)
        return ((osc->phase * 4.0f) - 1.0f);
    else
        return ((osc->phase * -4.0f) + 3.0f);
}

float sqrWaveOsc(Oscillator *osc)
{
    float sample = (osc->phase < 0.5f) ? 1.0f : -1.0f;
    sample += bandLimitedRippleFx(osc->phase, osc->phase_dt);
    sample -= bandLimitedRippleFx(fmod(osc->phase + 0.5f, 1.0f), osc->phase_dt);
    return sample;
}

void updateOscArray(WaveShapeFn base_osc_shape_fn, WaveShapeFn lfo_osc_shape_fn,
                    Synth *synth, Oscillator *osc_array, int n_oscillators)
{
    for (size_t i = 0; i < n_oscillators; i++)
    {
        // const float freq = synth->base_freq;
        const float freq = osc_array[i].freq;
        osc_array[i].freq = freq * (i + 1);
        // prevent aliasing (Nyquist )
        if (osc_array[i].freq >= (SAMPLE_RATE / 2.0f))
            continue;
        for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
        {
            updateOsc(&synth->lfo, 0.0f);
            updateOsc(&osc_array[i],
                      lfo_osc_shape_fn(&synth->lfo) * synth->lfo.amp);
            // synth->signal[t] += base_osc_shape_fn(&osc_array[i]) *
            //                     osc_array[i].amp * synth->base_amp;
            synth->signal[t] +=
                base_osc_shape_fn(&osc_array[i]) * osc_array[i].amp;
        }
    }
}

void handleAudioStream(AudioStream stream, Synth *synth)
{
    // Vector2 mouse_pos = GetMousePosition();
    // float normalized_mouse_x = (mouse_pos.x / SCREEN_WIDTH);
    // float normalized_mouse_y = (mouse_pos.y / SCREEN_HEIGHT);
    // synth->base_freq = 50.0f + (normalized_mouse_x * 500.0f);
    // synth->lfo.freq = 0.05f + (normalized_mouse_y * 10.0f);
    float audio_frame_duration = 0.0f;

    if (IsAudioStreamProcessed(stream))
    {
        const float audio_frame_start_time = GetTime();
        zeroSignal(synth->signal);
        updateOscArray(&sinWaveOsc, &sinWaveOsc, synth, synth->sinOsc,
                       synth->n_sinOsc);
        updateOscArray(&sawWaveOsc, &sinWaveOsc, synth, synth->sawOsc,
                       synth->n_sawOsc);
        updateOscArray(&triWaveOsc, &sinWaveOsc, synth, synth->triOsc,
                       synth->n_triOsc);
        updateOscArray(&sqrWaveOsc, &sinWaveOsc, synth, synth->sqrOsc,
                       synth->n_sqrOsc);
        UpdateAudioStream(stream, synth->signal, synth->signal_length);
        synth->audio_frame_duration = GetTime() - audio_frame_start_time;
    }
}

void accumulateSignal(float *signal, Oscillator *osc, Oscillator *lfo)
{
    for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
    {
        updateOsc(lfo, 0.0f);
        updateOsc(osc, sinWaveOsc(lfo));
        signal[t] += sinWaveOsc(osc);
    }
}

void draw_ui(Synth *synth)
{
    const int inner_panel_width = LEFT_PANEL_WIDTH - 20;

    // GUI
    GuiGrid((Rectangle){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, "hi",
            SCREEN_HEIGHT / 10.0f, 2, 0);
    GuiPanel((Rectangle){0, 0, LEFT_PANEL_WIDTH, SCREEN_HEIGHT}, NULL);

    int add_osc =
        GuiButton((Rectangle){10, 10, inner_panel_width, 20}, "Add osc");
    if (add_osc)
    {
        printf("Add synth\n");
        synth->ui_osc_count++;
    }

    for (size_t i = 0; i < synth->ui_osc_count; i++)
    {
        UIOsc *ui_osc = &synth->ui_osc[i];
        const int y_offset = 40 + (i * 160);

        GuiPanel((Rectangle){10, y_offset, inner_panel_width, 150}, NULL);

        // Freq slider
        float log_freq = log10f(ui_osc->freq);
        GuiLabel((Rectangle){20, y_offset, inner_panel_width - 20, 20},
                 TextFormat("Freq: %f", ui_osc->freq));
        GuiSlider((Rectangle){20, y_offset + 20, inner_panel_width - 20, 20},
                  NULL, NULL, &log_freq, 0.0f,
                  log10f((float)(SAMPLE_RATE / 2.0f)));
        ui_osc->freq = powf(10.0f, log_freq);

        // Amp slider
        float decibels = 20.0f * log10f(ui_osc->amp);
        GuiLabel((Rectangle){20, y_offset + 40, inner_panel_width - 20, 20},
                 TextFormat("Amp (Db): %f", decibels));
        GuiSlider((Rectangle){20, y_offset + 60, inner_panel_width - 20, 20},
                  NULL, NULL, &decibels, -50.0f, 0.0f);
        ui_osc->amp = powf(10.0f, decibels * (1.0f / 20.0f));

        // Osc select
        int shape_idx = (int)(ui_osc->shape);
        bool select_click = GuiDropdownBox(
            (Rectangle){20, y_offset + 90, inner_panel_width - 20, 20},
            WAVE_SHAPE_OPTIONS, &shape_idx, ui_osc->is_dropdown_open);
        ui_osc->shape = (WaveShape)(shape_idx);

        if (select_click)
        {
            ui_osc->is_dropdown_open = !ui_osc->is_dropdown_open;
        }

        bool delete_btn = GuiButton(
            (Rectangle){20, y_offset + 120, inner_panel_width - 20, 20}, "X");

        if (delete_btn)
        {
            memmove(synth->ui_osc + i, synth->ui_osc + i + 1,
                    (synth->ui_osc_count - i) * sizeof(UIOsc));
            synth->ui_osc_count--;
        }
    }

    // Reset synth
    synth->n_sinOsc = 0;
    synth->n_sawOsc = 0;
    synth->n_sqrOsc = 0;
    synth->n_triOsc = 0;

    for (size_t i = 0; i < synth->ui_osc_count; i++)
    {
        UIOsc *ui_osc = &synth->ui_osc[i];
        Oscillator *osc = NULL;

        switch (ui_osc->shape)
        {
        case WaveSin:
        {
            osc = synth->sinOsc + (synth->n_sinOsc++);
            osc->freq = ui_osc->freq;
            osc->amp = ui_osc->amp;
            break;
        }
        case WaveSaw:
        {
            osc = synth->sawOsc + (synth->n_sawOsc++);
            osc->freq = ui_osc->freq;
            osc->amp = ui_osc->amp;
            break;
        }
        case WaveSqr:
        {
            osc = synth->sqrOsc + (synth->n_sqrOsc++);
            osc->freq = ui_osc->freq;
            osc->amp = ui_osc->amp;
            break;
        }
        case WaveTri:
        {
            osc = synth->triOsc + (synth->n_triOsc++);
            osc->freq = ui_osc->freq;
            osc->amp = ui_osc->amp;
            break;
        }
        }
    }
}

int main()
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Synth");
    SetTargetFPS(120);
    InitAudioDevice();

    GuiLoadStyle("./cyber/cyber.rgs");

    SetAudioStreamBufferSizeDefault(STREAM_BUFFER_SIZE);
    AudioStream synth_stream =
        LoadAudioStream(SAMPLE_RATE, sizeof(float) * 8, 1);
    SetAudioStreamVolume(synth_stream, 0.05f);
    PlayAudioStream(synth_stream);

    Oscillator sinOsc[NUM_OSCILLATORS] = {0};
    Oscillator sawOsc[NUM_OSCILLATORS] = {0};
    Oscillator triOsc[NUM_OSCILLATORS] = {0};
    Oscillator sqrOsc[NUM_OSCILLATORS] = {0};
    float signal[STREAM_BUFFER_SIZE] = {0};
    Synth synth = {.sinOsc = sinOsc,
                   .sawOsc = sawOsc,
                   .triOsc = triOsc,
                   .sqrOsc = sqrOsc,
                   .n_sinOsc = 0,
                   .n_sawOsc = 0,
                   .n_sqrOsc = 0,
                   .n_triOsc = 0,
                   .lfo = {.phase = 0.0f, .amp = 0.0f},
                   .signal = signal,
                   .signal_length = STREAM_BUFFER_SIZE,
                   .audio_frame_duration = 0.0f,
                   //    .base_freq = 440.0f,
                   //    .base_amp = 0.5f,
                   .ui_osc_count = 0};

    for (size_t i = 0; i < NUM_OSCILLATORS; i++)
    {
        // const float amp = 1.0f / (i + 1);
        sinOsc[i].amp = 0.0f;
        sawOsc[i].amp = 0.0f;
        triOsc[i].amp = 0.0f;
        sqrOsc[i].amp = 0.0f;
    }

    while (!WindowShouldClose())
    {
        handleAudioStream(synth_stream, &synth);

        BeginDrawing();
        ClearBackground(BLACK);

        draw_ui(&synth);

        // Draw signal
        size_t zero_crossing_idx = 0;
        for (size_t i = 1; i < STREAM_BUFFER_SIZE; i++)
        {
            if (signal[i] >= 0.0f && signal[i - 1] < 0.0f)
            {
                zero_crossing_idx = i;
                break;
            }
        }

        Vector2 signal_points[STREAM_BUFFER_SIZE];
        const float screen_vert_midpoint = (float)(SCREEN_HEIGHT) / 2;
        for (size_t point_idx = 0; point_idx < STREAM_BUFFER_SIZE; point_idx++)
        {
            const size_t signal_idx =
                (point_idx + zero_crossing_idx) % STREAM_BUFFER_SIZE;
            signal_points[point_idx].x = (float)point_idx + LEFT_PANEL_WIDTH;
            signal_points[point_idx].y =
                screen_vert_midpoint + (int)(signal[signal_idx] * 100);
        }
        // DrawLine(0, screen_vert_midpoint, SCREEN_WIDTH,
        //          screen_vert_midpoint, DARKGRAY);
        DrawLineStrip(signal_points, STREAM_BUFFER_SIZE - zero_crossing_idx,
                      YELLOW);

        DrawText(TextFormat("Zero crossing index: %i", zero_crossing_idx),
                 LEFT_PANEL_WIDTH + 10, 30, 20, RED);

        DrawText(TextFormat("FPS: %i, delta: %f", GetFPS(), GetFrameTime()),
                 LEFT_PANEL_WIDTH + 10, 50, 16, RED);

        EndDrawing();
    }

    UnloadAudioStream(synth_stream);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}