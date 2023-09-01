#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

#include "keys.h"

////////////////////////////////////////////////////////////////

#define WAVE_SHAPE_OPTIONS "sine;sawtooth;square;triangle;rounded square"
typedef enum WaveShape
{
    WaveSin = 0,
    WaveSaw = 1,
    WaveSqr = 2,
    WaveTri = 3,
    WaveRsq = 4,
    WaveCount
} WaveShape;

typedef struct UIOsc
{
    float freq;
    float amp;
    float shape_parm_0;
    WaveShape shape;
    bool is_dropdown_open;
    Rectangle shape_dropdown_rect;
    int mod_state;
} UIOsc;

typedef struct Oscillator
{
    float phase;
    float phase_dt;
    float freq;
    float amp;
    float shape_parm_0;
    float buf[STREAM_BUFFER_SIZE];
    bool is_mod;
    size_t ui_id;
} Oscillator;

typedef float (*WaveShapeFn)(const float phase, const float phase_dt,
                             const float shape_parm);

typedef struct OscillatorArray
{
    Oscillator osc[NUM_OSCILLATORS];
    size_t count;
    WaveShapeFn shape_fn;
} OscillatorArray;

typedef struct ModulationPair
{
    Oscillator *modulator;
    Oscillator *carrier;
    size_t mod_id;
    float mod_ratio;
} ModulationPair;

typedef struct ModulationPairArray
{
    ModulationPair data[NUM_OSCILLATORS];
    size_t count;
} ModulationPairArray;

typedef struct Synth
{
    OscillatorArray osc_groups[WaveCount];
    size_t osc_groups_count;
    float *signal;
    size_t signal_length;
    float audio_frame_duration;

    UIOsc ui_osc[MAX_UI_OSC];
    size_t ui_osc_count;

    ModulationPairArray mod_pair_array;
} Synth;

////////////////////////////////////////////////////////////////

float midi2freq(float midi)
{
    return powf(2.0f, (midi - 69) / 12.0f) * BASE_NOTE_FREQ;
}

float freq2midi(float freq) { return 12.0f * log2f(freq / BASE_NOTE_FREQ); }

Oscillator *makeOscillator(OscillatorArray *osc_arr)
{
    return osc_arr->osc + (osc_arr->count++);
}

void updatePhase(float *phase, float *phase_dt, float freq, float freq_mod)
{
    *phase_dt = (freq + freq_mod) * SAMPLE_DURATION;
    *phase += *phase_dt;
    if (*phase < 0.0f)
        *phase += 1.0f;
    if (*phase >= 1.0f)
        *phase -= 1.0f;
}

void updatePhaseOsc(Oscillator *osc)
{
    osc->phase_dt = osc->freq * SAMPLE_DURATION;
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

// float sinWaveOsc(const Oscillator osc) { return sinf(2.0f * PI * osc.phase);
// }
float sinShape(float phase, float phase_dt, float shape_parm)
{
    return sinf(2.0f * PI * phase);
}

// float sawWaveOsc(const Oscillator osc)
// {
//     float sample = ((osc.phase * 2.0f) - 1.0f);
//     sample -= bandLimitedRippleFx(osc.phase, osc.phase_dt);
//     return sample;
// }
float sawShape(float phase, float phase_dt, float shape_parm)
{
    float sample = ((phase * 2.0f) - 1.0f);
    sample -= bandLimitedRippleFx(phase, phase_dt);
    return sample;
}

// float triWaveOsc(const Oscillator osc)
// {
//     if (osc.phase < 0.5f)
//         return ((osc.phase * 4.0f) - 1.0f);
//     else
//         return ((osc.phase * -4.0f) + 3.0f);
// }
float triShape(float phase, float phase_dt, float shape_parm)
{
    if (phase < 0.5f)
        return (phase * 4.0f) - 1.0f;
    else
        return (phase * -4.0f) + 3.0f;
}

// float sqrWaveOsc(const Oscillator osc)
// {
//     float duty_cycle = osc.shape_parm_0;
//     float sample = (osc.phase < duty_cycle) ? 1.0f : -1.0f;
//     sample += bandLimitedRippleFx(osc.phase, osc.phase_dt);
//     sample -= bandLimitedRippleFx(fmodf(osc.phase + (1.0f -
//     duty_cycle), 1.0f),
//                                   osc.phase_dt);
//     return sample;
// }
float sqrShape(float phase, float phase_dt, float shape_parm)
{
    float duty_cycle = shape_parm;
    float sample = (phase < duty_cycle) ? 1.0f : -1.0f;
    sample += bandLimitedRippleFx(phase, phase_dt);
    sample -=
        bandLimitedRippleFx(fmodf(phase + (1.0f - duty_cycle), 1.0f), phase_dt);
    return sample;
}

// float rsqWaveOsc(const Oscillator osc)
// {
//     float s = (osc.shape_parm_0 * 8.0f) + 2.0f;
//     float base = (float)fabs(s);
//     float pow = s * sinf(osc.phase * PI * 2);
//     float denominator = powf(base, pow) + 1.0f;
//     float sample = (2.0f / denominator) - 1.0f;
//     return sample;
// }
float rsqShape(float phase, float phase_dt, float shape_parm)
{
    float s = (shape_parm * 8.0f) + 2.0f;
    float base = (float)fabs(s);
    float pow = s * sinf(phase * PI * 2);
    float denominator = powf(base, pow) + 1.0f;
    float sample = (2.0f / denominator) - 1.0f;
    return sample;
}

// void updateOsc(Oscillator *osc, float freq_mod)
// {
//     osc->phase_dt = (osc->freq + freq_mod) * SAMPLE_DURATION;
//     osc->phase += osc->phase_dt;
//     if (osc->phase < 0.0f)
//         osc->phase += 1.0f;
//     if (osc->phase >= 1.0f)
//         osc->phase -= 1.0f;
// }

// void updateOscArray(WaveShapeFn base_osc_shape_fn, Synth *synth,
//                     OscillatorArray osc_array)
// {
//     for (size_t i = 0; i < osc_array.count; i++)
//     {
//         // prevent aliasing (Nyquist )
//         if (osc_array.osc[i].freq > (SAMPLE_RATE / 2.0f) ||
//             osc_array.osc[i].freq < -(SAMPLE_RATE / 2.0f))
//             continue;
//         for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
//         {
//             updateOsc(&osc_array.osc[i], 0.0f);
//             osc_array.osc[i].buf[t] +=
//                 base_osc_shape_fn(osc_array.osc[i]) * osc_array.osc[i].amp;
//         }
//     }
// }
void updateOscArray(OscillatorArray *osc_array, ModulationPairArray *mod_array)
{
    for (size_t i = 0; i < osc_array->count; i++)
    {
        Oscillator *osc = &(osc_array->osc[i]);
        if (osc->freq > (SAMPLE_RATE / 2.0f) ||
            osc->freq < -(SAMPLE_RATE / 2.0f))
            continue;
        ModulationPair *mod = 0;
        for (size_t mod_i = 0; mod_i < mod_array->count; mod_i++)
        {
            if (mod_array->data[mod_i].carrier == osc)
            {
                mod = &mod_array->data[mod_i];
                break;
            }
        }
        for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
        {
            float freq_mod = 0.0f;
            if (mod)
            {
                freq_mod = mod->modulator->buf[t] * mod->mod_ratio;
            }

            updatePhase(&osc->phase, &osc->phase_dt, osc->freq, freq_mod);
            float sample = osc_array->shape_fn(osc->phase, osc->phase_dt,
                                               osc->shape_parm_0);
            sample *= osc->amp;
            osc->buf[t] = sample;
        }
    }
}

void accumOscToSignal(Synth *synth)
{
    for (size_t i = 0; i < synth->osc_groups_count; i++)
    {
        OscillatorArray *osc_array = &synth->osc_groups[i];
        for (size_t osc_i = 0; osc_i < osc_array->count; osc_i++)
        {
            Oscillator *osc = &(osc_array->osc[osc_i]);
            if (osc->is_mod)
                continue;

            for (size_t t = 0; t < STREAM_BUFFER_SIZE; t++)
            {
                synth->signal[t] += osc->buf[t];
            }
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

        for (size_t i = 0; i < synth->osc_groups_count; i++)
        {
            OscillatorArray *osc_array = &synth->osc_groups[i];
            updateOscArray(osc_array, &synth->mod_pair_array);
        }

        accumOscToSignal(synth);

        UpdateAudioStream(stream, synth->signal, synth->signal_length);
        synth->audio_frame_duration = GetTime() - audio_frame_start_time;
    }
}

void drawSignal(Synth *synth)
{
    // Draw signal
    size_t zero_crossing_idx = 0;
    for (size_t i = 1; i < synth->signal_length; i++)
    {
        if (synth->signal[i] >= 0.0f && synth->signal[i - 1] < 0.0f)
        {
            zero_crossing_idx = i;
            break;
        }
    }

    Vector2 signal_points[STREAM_BUFFER_SIZE];
    const float screen_vert_midpoint = (float)(SCREEN_HEIGHT) / 2;
    for (size_t p_i = 0; p_i < synth->signal_length; p_i++)
    {
        const size_t signal_idx =
            (p_i + zero_crossing_idx) % STREAM_BUFFER_SIZE;
        signal_points[p_i].x = (float)p_i + LEFT_PANEL_WIDTH;
        signal_points[p_i].y =
            screen_vert_midpoint + (int)(synth->signal[signal_idx] * 100);
    }

    DrawLineStrip(signal_points, STREAM_BUFFER_SIZE - zero_crossing_idx,
                  YELLOW);
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
    if (click_add_oscillator && synth->ui_osc_count < NUM_OSCILLATORS)
    {
        synth->ui_osc_count += 1;
        // Set defaults
        UIOsc *ui_osc = synth->ui_osc + (synth->ui_osc_count - 1);
        ui_osc->shape = WaveSin;
        ui_osc->freq = BASE_NOTE_FREQ;
        ui_osc->amp = 0.5f;
        ui_osc->shape_parm_0 = 0.5f;
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

        // Mod button
        Rectangle mod_btn_rect = delete_button_rect;
        mod_btn_rect.x += 40;
        const char *mod_btn_text = (ui_osc->mod_state == 0)
                                       ? "N/A"
                                       : TextFormat("%d", ui_osc->mod_state);
        bool mod_btn_pressed = GuiButton(mod_btn_rect, mod_btn_text);
        if (mod_btn_pressed)
        {
            ui_osc->mod_state = (ui_osc->mod_state + 1) % 8;
        }
    }

    for (size_t ui_osc_i = 0; ui_osc_i < synth->ui_osc_count; ui_osc_i++)
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
}

void apply_ui_state(Synth *synth)
{
    // Reset synth
    for (size_t i = 0; i < synth->osc_groups_count; i++)
    {
        // Clear osc array
        synth->osc_groups[i].count = 0;
    }
    synth->mod_pair_array.count = 0;

    for (size_t ui_osc_i = 0; ui_osc_i < synth->ui_osc_count; ui_osc_i++)
    {
        UIOsc *ui_osc = &synth->ui_osc[ui_osc_i];

        for (size_t k = 0; k < KEYS_LENGTH; k++)
        {
            if (!IsKeyDown(KEYS[k].k))
                continue;
            bool octave_up =
                IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

            Oscillator *osc = NULL;
            if (ui_osc->shape < WaveCount)
            {
                OscillatorArray *group = &synth->osc_groups[ui_osc->shape];
                if (group->count < NUM_OSCILLATORS)
                    osc = makeOscillator(group);
            }

            if (osc != NULL)
            {
                osc->ui_id = ui_osc_i;
                osc->freq = midi2freq(KEYS[k].midi + (12 * (int)octave_up));
                osc->amp = ui_osc->amp;
                osc->shape_parm_0 = ui_osc->shape_parm_0;
                osc->is_mod = false;

                if (ui_osc->mod_state > 0 &&
                    (ui_osc->mod_state - 1) < synth->ui_osc_count)
                {
                    ModulationPair *mod_pair = synth->mod_pair_array.data +
                                               synth->mod_pair_array.count++;
                    mod_pair->modulator = 0;
                    mod_pair->carrier = osc;
                    mod_pair->mod_id = ui_osc->mod_state - 1;
                    mod_pair->mod_ratio = 100.0f;
                }
            }
        }
    }

    for (size_t mod_i = 0; mod_i < synth->mod_pair_array.count; mod_i++)
    {
        ModulationPair *mod_pair = &synth->mod_pair_array.data[mod_i];
        WaveShape shape_id = synth->ui_osc[mod_pair->mod_id].shape;
        OscillatorArray *osc_array = &synth->osc_groups[shape_id];

        for (size_t osc_i = 0; osc_i < osc_array->count; osc_i++)
        {
            Oscillator *osc = &osc_array->osc[osc_i];
            if (osc->ui_id == mod_pair->mod_id)
            {
                if (mod_pair->modulator == 0)
                    mod_pair->modulator = osc;
                osc->is_mod = true;
            }
        }
    }
}

int main()
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Simple Synth");
    SetTargetFPS(120);
    InitAudioDevice();

    GuiLoadStyle("./cyber/cyber.rgs");

    SetAudioStreamBufferSizeDefault(STREAM_BUFFER_SIZE);
    AudioStream synth_stream =
        LoadAudioStream(SAMPLE_RATE, sizeof(float) * 8, 1);
    SetAudioStreamVolume(synth_stream, 0.05f);
    PlayAudioStream(synth_stream);

    // Oscillator sinOsc[NUM_OSCILLATORS] = {0};
    // Oscillator sawOsc[NUM_OSCILLATORS] = {0};
    // Oscillator triOsc[NUM_OSCILLATORS] = {0};
    // Oscillator sqrOsc[NUM_OSCILLATORS] = {0};
    // Oscillator rsqOsc[NUM_OSCILLATORS] = {0};

    ModulationPair mod_pairs[256] = {0};
    float signal[STREAM_BUFFER_SIZE] = {0};

    Synth *synth = (Synth *)malloc(sizeof(Synth));

    synth->osc_groups_count = WaveCount;
    synth->signal = signal;
    synth->signal_length = STREAM_BUFFER_SIZE;

    synth->osc_groups[WaveSin].count = 0;
    synth->osc_groups[WaveSaw].count = 0;
    synth->osc_groups[WaveTri].count = 0;
    synth->osc_groups[WaveSqr].count = 0;
    synth->osc_groups[WaveRsq].count = 0;
    synth->osc_groups[WaveSin].shape_fn = sinShape;
    synth->osc_groups[WaveSaw].shape_fn = sawShape;
    synth->osc_groups[WaveTri].shape_fn = triShape;
    synth->osc_groups[WaveSqr].shape_fn = sqrShape;
    synth->osc_groups[WaveRsq].shape_fn = rsqShape;

    synth->mod_pair_array.count = 0;

    while (!WindowShouldClose())
    {
        handleAudioStream(synth_stream, synth);

        BeginDrawing();
        ClearBackground(BLACK);

        draw_ui(synth);
        apply_ui_state(synth);
        drawSignal(synth);

        DrawText(TextFormat("Fundamental freq: %.1f",
                            synth->osc_groups[0].osc[0].freq),
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