// https://www.youtube.com/watch?v=e-GhSmWmlZ0

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "raylib.h"

#define SCREEN_WIDTH 1200
#define SCREEN_HEIGHT 700

#define SAMPLE_RATE 44100
#define SAMPLE_DURATION (1.0f / SAMPLE_RATE)
#define STREAM_BUFFER_SIZE 1024
#define NUM_OSCILLATORS 32
#define MAX_UI_OSC 32
#define BASE_NOTE_FREQ 440

#define LEFT_PANEL_WIDTH (SCREEN_WIDTH / 4.0f)

#define WAVE_SHAPE_OPTIONS "sine;sawtooth;square;triangle;rounded square"
typedef enum
{
    WaveSin = 0,
    WaveSaw = 1,
    WaveSqr = 2,
    WaveTri = 3,
    WaveRsq = 4
} WaveShape;

typedef struct
{
    float freq;
    float amp;
    float shape_parm_0;
    WaveShape shape;
    bool is_dropdown_open;
    Rectangle shape_dropdown_rect;
} UIOsc;

typedef struct
{
    float phase;
    float phase_dt;
    float freq;
    float amp;
    float shape_parm_0;
} Oscillator;
typedef struct
{
    Oscillator *osc;
    size_t count;
} OscillatorArray;
typedef struct
{
    OscillatorArray sinOsc;
    OscillatorArray sawOsc;
    OscillatorArray triOsc;
    OscillatorArray sqrOsc;
    OscillatorArray rsqOsc;
    float *signal;
    size_t signal_length;
    float audio_frame_duration;

    UIOsc ui_osc[MAX_UI_OSC];
    size_t ui_osc_count;
} Synth;

typedef float (*WaveShapeFn)(const Oscillator);

float semitone2freq(float semitone)
{
    return powf(2.0f, semitone / 12.0f) * BASE_NOTE_FREQ;
}

float freq2semitone(float freq) { return 12.0f * log2f(freq / BASE_NOTE_FREQ); }

Oscillator *makeOscillator(OscillatorArray *osc_arr)
{
    return osc_arr->osc + (osc_arr->count++);
}

void clearOscillatorArray(OscillatorArray *osc_arr) { osc_arr->count = 0; }

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

float sinWaveOsc(const Oscillator osc) { return sinf(2.0f * PI * osc.phase); }

float sawWaveOsc(const Oscillator osc)
{
    float sample = ((osc.phase * 2.0f) - 1.0f);
    sample -= bandLimitedRippleFx(osc.phase, osc.phase_dt);
    return sample;
}

float triWaveOsc(const Oscillator osc)
{
    if (osc.phase < 0.5f)
        return ((osc.phase * 4.0f) - 1.0f);
    else
        return ((osc.phase * -4.0f) + 3.0f);
}

float sqrWaveOsc(const Oscillator osc)
{
    float duty_cycle = osc.shape_parm_0;
    float sample = (osc.phase < duty_cycle) ? 1.0f : -1.0f;
    sample += bandLimitedRippleFx(osc.phase, osc.phase_dt);
    sample -= bandLimitedRippleFx(fmodf(osc.phase + (1.0f - duty_cycle), 1.0f),
                                  osc.phase_dt);
    return sample;
}

float rsqWaveOsc(const Oscillator osc)
{
    float s = (osc.shape_parm_0 * 8.0f) + 2.0f;
    float base = (float)fabs(s);
    float pow = s * sinf(osc.phase * PI * 2);
    float denominator = powf(base, pow) + 1.0f;
    float sample = (2.0f / denominator) - 1.0f;
    return sample;
}

void updateOscArray(WaveShapeFn base_osc_shape_fn, Synth *synth,
                    OscillatorArray osc_array)
{
    for (size_t i = 0; i < osc_array.count; i++)
    {
        // prevent aliasing (Nyquist )
        if (osc_array.osc[i].freq > (SAMPLE_RATE / 2.0f) ||
            osc_array.osc[i].freq < -(SAMPLE_RATE / 2.0f))
            continue;
        for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
        {
            updateOsc(&osc_array.osc[i], 0.0f);
            synth->signal[t] +=
                base_osc_shape_fn(osc_array.osc[i]) * osc_array.osc[i].amp;
        }
    }
}

void handleAudioStream(AudioStream stream, Synth *synth)
{
    float audio_frame_duration = 0.0f;

    if (IsAudioStreamProcessed(stream))
    {
        const float audio_frame_start_time = GetTime();
        zeroSignal(synth->signal);

        updateOscArray(&sinWaveOsc, synth, synth->sinOsc);
        updateOscArray(&sawWaveOsc, synth, synth->sawOsc);
        updateOscArray(&triWaveOsc, synth, synth->triOsc);
        updateOscArray(&sqrWaveOsc, synth, synth->sqrOsc);
        updateOscArray(&rsqWaveOsc, synth, synth->rsqOsc);

        UpdateAudioStream(stream, synth->signal, synth->signal_length);
        synth->audio_frame_duration = GetTime() - audio_frame_start_time;
    }
}

void draw_ui(Synth *synth)
{
    const int panel_x_start = 0;
    const int panel_y_start = 0;
    const int panel_width = LEFT_PANEL_WIDTH;
    const int panel_height = SCREEN_WIDTH;

    bool is_shape_dropdown_open = false;
    int shape_index = 0;

    GuiGrid((Rectangle){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, "grid",
            SCREEN_HEIGHT / 8.0f, 2, 0);
    GuiPanel(
        (Rectangle){panel_x_start, panel_y_start, panel_width, panel_height},
        NULL);

    bool click_add_oscillator =
        GuiButton((Rectangle){panel_x_start + 10, panel_y_start + 10,
                              panel_width - 20, 25},
                  "Add osc");
    if (click_add_oscillator)
    {
        synth->ui_osc_count += 1;
    }

    float panel_y_offset = 0;
    for (size_t ui_osc_i = 0; ui_osc_i < synth->ui_osc_count; ui_osc_i++)
    {
        UIOsc *ui_osc = &synth->ui_osc[ui_osc_i];
        const bool has_shape_param =
            (ui_osc->shape == WaveSqr || ui_osc->shape == WaveRsq);

        const int osc_panel_width = panel_width - 20;
        const int osc_panel_height = has_shape_param ? 130 : 100;
        const int osc_panel_x = panel_x_start + 10;
        const int osc_panel_y = panel_y_start + 50 + panel_y_offset;
        panel_y_offset += osc_panel_height + 5;
        GuiPanel((Rectangle){osc_panel_x, osc_panel_y, osc_panel_width,
                             osc_panel_height},
                 NULL);

        const float slider_padding = 50.f;
        const float el_spacing = 5.f;
        Rectangle el_rect = {.x = osc_panel_x + slider_padding + 30,
                             .y = osc_panel_y + 10,
                             .width = osc_panel_width - (slider_padding * 2),
                             .height = 25};

        // Frequency slider
        char freq_slider_label[16];
        sprintf(freq_slider_label, "%.1fHz", ui_osc->freq);
        float log_freq = log10f(ui_osc->freq);
        GuiSlider(el_rect, freq_slider_label, "", &log_freq, 0.0f,
                  log10f((float)(SAMPLE_RATE / 2.0f)));
        ui_osc->freq = powf(10.f, log_freq);
        el_rect.y += el_rect.height + el_spacing;

        // Amplitude slider
        float decibels = (20.f * log10f(ui_osc->amp));
        char amp_slider_label[32];
        sprintf(amp_slider_label, "%.1f dB", decibels);
        GuiSlider(el_rect, amp_slider_label, "", &decibels, -60.0f, 0.0f);
        ui_osc->amp = powf(10.f, decibels * (1.f / 20.f));
        el_rect.y += el_rect.height + el_spacing;

        // Shape parameter slider
        if (has_shape_param)
        {
            float shape_param = ui_osc->shape_parm_0;
            char shape_param_label[32];
            sprintf(shape_param_label, "%.1f", shape_param);
            GuiSlider(el_rect, shape_param_label, "", &shape_param, 0.f, 1.f);
            ui_osc->shape_parm_0 = shape_param;
            el_rect.y += el_rect.height + el_spacing;
        }

        // Defer shape drop-down box.
        ui_osc->shape_dropdown_rect = el_rect;
        el_rect.y += el_rect.height + el_spacing;

        Rectangle delete_button_rect = el_rect;
        delete_button_rect.x = osc_panel_x + 5;
        delete_button_rect.y -= el_rect.height + el_spacing;
        delete_button_rect.width = 30;
        bool is_delete_button_pressed = GuiButton(delete_button_rect, "X");
        if (is_delete_button_pressed)
        {
            memmove(synth->ui_osc + ui_osc_i, synth->ui_osc + ui_osc_i + 1,
                    (synth->ui_osc_count - ui_osc_i) * sizeof(UIOsc));
            synth->ui_osc_count -= 1;
        }
    }

    for (size_t ui_osc_i = 0; ui_osc_i < synth->ui_osc_count; ui_osc_i += 1)
    {
        UIOsc *ui_osc = &synth->ui_osc[ui_osc_i];
        // Shape select
        int shape_index = (int)(ui_osc->shape);
        bool is_dropdown_click =
            GuiDropdownBox(ui_osc->shape_dropdown_rect, WAVE_SHAPE_OPTIONS,
                           &shape_index, ui_osc->is_dropdown_open);
        ui_osc->shape = (WaveShape)(shape_index);
        if (is_dropdown_click)
        {
            ui_osc->is_dropdown_open = !ui_osc->is_dropdown_open;
        }
        if (ui_osc->is_dropdown_open)
            break;
    }

    // Reset synth
    clearOscillatorArray(&synth->sinOsc);
    clearOscillatorArray(&synth->sawOsc);
    clearOscillatorArray(&synth->triOsc);
    clearOscillatorArray(&synth->sqrOsc);
    clearOscillatorArray(&synth->rsqOsc);

    // float note_freq = 0;
    // for (int note_idx = midi_keys.count - 1; note_idx >= 0; note_idx -= 1)
    // {
    //     if (midi_keys.data[note_idx].is_on)
    //     {
    //         float semitone =
    //             (float)(midi_keys.data[note_idx].note - BASE_MIDI_NOTE);
    //         note_freq = getFrequencyForSemitone(semitone);
    //         break;
    //     }
    // }
    int key = 0;
    for (int i = 0; i < KEY_NINE - KEY_ONE; i++)
    {
        if (IsKeyDown(KEY_ONE + i))
        {
            key = KEY_ONE + i;
            break;
        }
    }
    float note_freq = 0;
    if (key >= KEY_ONE)
    {
        float semitone = (float)(1 + (key - KEY_ONE));
        note_freq = semitone2freq(semitone);
    }

    for (size_t ui_osc_i = 0; ui_osc_i < synth->ui_osc_count; ui_osc_i++)
    {
        UIOsc *ui_osc = &synth->ui_osc[ui_osc_i];
        Oscillator *osc = NULL;
        switch (ui_osc->shape)
        {
        case WaveSin:
        {
            osc = makeOscillator(&synth->sinOsc);
            break;
        }
        case WaveSaw:
        {
            osc = makeOscillator(&synth->sawOsc);
            break;
        }
        case WaveSqr:
        {
            osc = makeOscillator(&synth->sqrOsc);
            break;
        }
        case WaveTri:
        {
            osc = makeOscillator(&synth->triOsc);
            break;
        }
        case WaveRsq:
        {
            osc = makeOscillator(&synth->rsqOsc);
            break;
        }
        }
        if (osc != NULL)
        {
            // osc->freq = ui_osc->freq;
            osc->freq = note_freq;
            osc->amp = ui_osc->amp;
            osc->shape_parm_0 = ui_osc->shape_parm_0;
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
    Oscillator rsqOsc[NUM_OSCILLATORS] = {0};
    float signal[STREAM_BUFFER_SIZE] = {0};
    Synth synth = {.sinOsc = {.osc = sinOsc, .count = 0},
                   .sawOsc = {.osc = sawOsc, .count = 0},
                   .triOsc = {.osc = triOsc, .count = 0},
                   .sqrOsc = {.osc = sqrOsc, .count = 0},
                   .rsqOsc = {.osc = rsqOsc, .count = 0},
                   .signal = signal,
                   .signal_length = STREAM_BUFFER_SIZE,
                   .audio_frame_duration = 0.0f,
                   .ui_osc_count = 0};

    for (size_t i = 0; i < NUM_OSCILLATORS; i++)
    {
        // const float amp = 1.0f / (i + 1);
        sinOsc[i].amp = 0.0f;
        sawOsc[i].amp = 0.0f;
        triOsc[i].amp = 0.0f;
        sqrOsc[i].amp = 0.0f;
        rsqOsc[i].amp = 0.0f;
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