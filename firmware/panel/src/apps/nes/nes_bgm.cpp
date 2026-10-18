#include "nes_bgm.h"
#include <cmath>
#include <cstring>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/panel_audio.h"

static TaskHandle_t s_bgm_task_handle = nullptr;
static bool s_bgm_running = false;
static bool s_bgm_paused = false;

// NES Chiptune Note Frequencies (Hz)
#define NOTE_C4  261.63f
#define NOTE_D4  293.66f
#define NOTE_E4  329.63f
#define NOTE_F4  349.23f
#define NOTE_G4  392.00f
#define NOTE_A4  440.00f
#define NOTE_B4  493.88f
#define NOTE_C5  523.25f
#define NOTE_D5  587.33f
#define NOTE_E5  659.25f
#define NOTE_F5  698.46f
#define NOTE_G5  783.99f
#define NOTE_A5  880.00f
#define NOTE_REST 0.0f

// Classic NES style arpeggiated menu melody
struct Note {
    float freq;
    uint16_t duration_ms;
};

static const Note s_nes_melody[] = {
    {NOTE_E5, 120}, {NOTE_E5, 120}, {NOTE_REST, 120}, {NOTE_E5, 120},
    {NOTE_REST, 120}, {NOTE_C5, 120}, {NOTE_E5, 120}, {NOTE_REST, 120},
    {NOTE_G5, 240}, {NOTE_REST, 240}, {NOTE_G4, 240}, {NOTE_REST, 240},

    {NOTE_C5, 180}, {NOTE_REST, 120}, {NOTE_G4, 180}, {NOTE_REST, 120},
    {NOTE_E4, 180}, {NOTE_REST, 120}, {NOTE_A4, 180}, {NOTE_B4, 180},
    {NOTE_A4, 180}, {NOTE_G4, 180}, {NOTE_E5, 180}, {NOTE_G5, 180},
    {NOTE_A5, 240}, {NOTE_F5, 180}, {NOTE_G5, 180}, {NOTE_REST, 120},
    {NOTE_E5, 180}, {NOTE_C5, 180}, {NOTE_D5, 180}, {NOTE_B4, 240}
};

static const size_t s_melody_len = sizeof(s_nes_melody) / sizeof(Note);

static void nes_bgm_task_func(void* arg) {
    const uint32_t sample_rate = 30000;
    panel_audio_init(sample_rate);

    size_t note_idx = 0;
    float phase_lead = 0.0f;
    float phase_bass = 0.0f;

    const size_t chunk_samples = 512;
    int32_t stereo_buf[chunk_samples * 2];

    while (s_bgm_running) {
        if (s_bgm_paused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        const Note& current_note = s_nes_melody[note_idx];
        note_idx = (note_idx + 1) % s_melody_len;

        uint32_t total_samples = (uint32_t)((current_note.duration_ms * sample_rate) / 1000);
        uint32_t samples_generated = 0;

        while (samples_generated < total_samples && s_bgm_running && !s_bgm_paused) {
            size_t count = (total_samples - samples_generated > chunk_samples) ? chunk_samples : (total_samples - samples_generated);

            for (size_t i = 0; i < count; i++) {
                int16_t sample_val = 0;

                if (current_note.freq > 0.0f) {
                    float step_lead = current_note.freq / (float)sample_rate;
                    phase_lead += step_lead;
                    if (phase_lead >= 1.0f) phase_lead -= 1.0f;

                    // 50% NES Square wave
                    float lead_wave = (phase_lead < 0.5f) ? 0.3f : -0.3f;

                    // Triangle bass sub-octave
                    float step_bass = (current_note.freq * 0.5f) / (float)sample_rate;
                    phase_bass += step_bass;
                    if (phase_bass >= 1.0f) phase_bass -= 1.0f;
                    float bass_wave = (phase_bass < 0.5f) ? (4.0f * phase_bass - 1.0f) : (3.0f - 4.0f * phase_bass);

                    float mix = (lead_wave * 0.6f + bass_wave * 0.4f) * 8000.0f;
                    sample_val = (int16_t)mix;
                }

                int32_t sample32 = ((int32_t)sample_val) << 16;
                stereo_buf[i * 2]     = sample32;
                stereo_buf[i * 2 + 1] = sample32;
            }

            panel_audio_write(stereo_buf, count * sizeof(int32_t) * 2, 50);
            samples_generated += count;
        }
    }

    s_bgm_task_handle = nullptr;
    vTaskDelete(NULL);
}

void nes_bgm_init(void) {
    s_bgm_running = false;
    s_bgm_paused = false;
}

void nes_bgm_start(void) {
    if (s_bgm_task_handle != nullptr) return;
    s_bgm_running = true;
    s_bgm_paused = false;
    xTaskCreatePinnedToCore(nes_bgm_task_func, "nes_bgm_task", 4096, NULL, 3, &s_bgm_task_handle, 0);
}

void nes_bgm_pause(void) {
    s_bgm_paused = true;
    panel_audio_stop();
}

void nes_bgm_resume(void) {
    s_bgm_paused = false;
}

void nes_bgm_stop(void) {
    s_bgm_running = false;
    s_bgm_paused = false;
}

bool nes_bgm_is_playing(void) {
    return (s_bgm_running && !s_bgm_paused);
}
