#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "noftypes.h"
#include "bitmap.h"
#include "osd.h"
#include "nofrendo.h"
#include "nesinput.h"
#include "event.h"
#include "nofconfig.h"
#include "panel_audio.h"
#include "panel_hardware.h"
#include "nes_manager.h"

#define TAG "NES_OSD"

#define NES_SCREEN_WIDTH 256
#define NES_SCREEN_HEIGHT 240
#define AUDIO_SAMPLE_RATE 30000

static uint16_t myPalette565[256];
static bitmap_t *game_bitmap = NULL;
static uint16_t *frame_buffer = NULL;
static bool video_ready = false;

static esp_timer_handle_t nes_timer_handle = NULL;
static void (*emu_timer_callback)(void) = NULL;

static nesinput_t joypad_p1;
static nesinput_t joypad_p2;
static uint32_t current_pad1_state = 0;
static uint32_t current_pad2_state = 0;

static char *global_rom_data = NULL;
static int global_rom_size = 0;
static FILE *rom_file_ptr = NULL;

static void (*emulator_audio_callback)(void *buffer, int length) = NULL;
static int16_t mono_audio_buffer[2048];
static int32_t stereo_audio_buffer_32[4096];

static inline uint32_t get_millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void IRAM_ATTR timer_callback_handler(void *arg) {
    if (emu_timer_callback) emu_timer_callback();
}

void nes_set_gamepad_state(uint32_t state) {
    current_pad1_state = state;
}

void nes_set_gamepad2_state(uint32_t state) {
    current_pad2_state = state;
}

char *osd_getromdata(void) {
    return global_rom_data;
}

void osd_release_romdata(void) {
    if (global_rom_data) {
        heap_caps_free(global_rom_data);
        global_rom_data = NULL;
        global_rom_size = 0;
    }
}

bool vid_preload_rom(const char *path) {
    osd_release_romdata();
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open ROM file: %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    global_rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    global_rom_data = (char *)heap_caps_malloc(global_rom_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!global_rom_data) {
        global_rom_data = (char *)malloc(global_rom_size);
    }
    if (!global_rom_data) {
        fclose(f);
        return false;
    }
    size_t read_bytes = fread(global_rom_data, 1, global_rom_size, f);
    fclose(f);
    return (read_bytes == (size_t)global_rom_size);
}

uint16_t *nes_get_frame_buffer(void) {
    return frame_buffer;
}

int osd_init_sound(void) {
    panel_audio_init(AUDIO_SAMPLE_RATE);
    return 0;
}

void osd_getsoundinfo(sndinfo_t *info) {
    info->sample_rate = AUDIO_SAMPLE_RATE;
    info->bps = 16;
}

void osd_setsound(void (*playfunc)(void *buffer, int length)) {
    emulator_audio_callback = playfunc;
}

void osd_stopsound(void) {
    panel_audio_stop();
}

void osd_writesound(void *stream, int len) {}

void osd_initvideo(int *lines) {
    *lines = NES_SCREEN_HEIGHT;
}

void osd_shutdownvideo(void) {}
void osd_setscreen(int x, int y, int width, int height) {}

static inline uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (uint16_t)((c >> 8) | (c << 8));
}

void osd_setpalette(rgb_t *pal) {
    if (!pal) return;
    for (int i = 0; i < 256; i++) {
        myPalette565[i] = color565(pal[i].r, pal[i].g, pal[i].b);
    }
}

void osd_getinput(void) {
    joypad_p1.data = current_pad1_state;
    joypad_p2.data = current_pad2_state;
}

int osd_init(void) {
    joypad_p1.type = INP_JOYPAD0;
    joypad_p1.data = 0;
    input_register(&joypad_p1);

    joypad_p2.type = INP_JOYPAD1;
    joypad_p2.data = 0;
    input_register(&joypad_p2);

    osd_init_sound();
    return 0;
}

void osd_shutdown(void) {
    if (nes_timer_handle) {
        esp_timer_stop(nes_timer_handle);
        esp_timer_delete(nes_timer_handle);
        nes_timer_handle = NULL;
    }
}

int osd_installtimer(int freq, void *func, int func_param, void *func2, int func2_param) {
    emu_timer_callback = (void (*)(void))func;
    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback_handler,
        .name = "nes_timer"
    };
    esp_timer_create(&timer_args, &nes_timer_handle);
    esp_timer_start_periodic(nes_timer_handle, 1000000 / freq);
    return 0;
}

int osd_gettime(void) {
    return (int)get_millis();
}

int osd_makesnapname(char *buf, int len) { return 0; }
void osd_getmouse(int *x, int *y, int *button) { *x = 0; *y = 0; *button = 0; }

void IRAM_ATTR osd_blit(bitmap_t *bmp) {
    if (!video_ready || !bmp || !bmp->line || !frame_buffer) return;
    for (int y = 0; y < NES_SCREEN_HEIGHT; y++) {
        uint8_t *src = bmp->line[y];
        uint16_t *dst = &frame_buffer[y * NES_SCREEN_WIDTH];
        for (int x = 0; x < NES_SCREEN_WIDTH; x++) {
            dst[x] = myPalette565[src[x]];
        }
    }
}

void vid_flush(void) {
    if (frame_buffer && game_bitmap) {
        osd_blit(game_bitmap);

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        uint32_t last_touch = panel_hardware_get_last_touch_ms();

        if (now - last_touch < 5000) {
            // Render semi-transparent overlay top banner (Y = 0..24)
            for (int y = 0; y < 24; y++) {
                for (int x = 0; x < NES_SCREEN_WIDTH; x++) {
                    uint16_t px = frame_buffer[y * NES_SCREEN_WIDTH + x];
                    uint16_t blended = (px & 0xF7DE) >> 1; // 50% opacity blend
                    frame_buffer[y * NES_SCREEN_WIDTH + x] = blended;
                }
            }

            int16_t tx = -1, ty = -1;
            if (panel_hardware_get_touch_pos(&tx, &ty)) {
                if (ty < 40) {
                    nes_manager_set_pending_exit(true);
                }
            }
        }

        if (nes_manager_check_pending_exit()) {
            main_quit();
            return;
        }

        ili9341_draw_direct_frame(frame_buffer, 32, 0, NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT);
    }

    if (emulator_audio_callback) {
        int samples_needed = AUDIO_SAMPLE_RATE / 60; // 500 samples per frame at 30kHz
        int bytes_needed = samples_needed * 2;       // 16-bit mono bytes

        memset(mono_audio_buffer, 0, sizeof(mono_audio_buffer));
        emulator_audio_callback(mono_audio_buffer, bytes_needed);

        for (int i = 0; i < samples_needed; i++) {
            int32_t sample32 = ((int32_t)mono_audio_buffer[i]) << 16; // Upscale 16-bit to 32-bit
            stereo_audio_buffer_32[i * 2]     = sample32; // Left channel
            stereo_audio_buffer_32[i * 2 + 1] = sample32; // Right channel
        }

        panel_audio_write(stereo_audio_buffer_32, samples_needed * 8, 20);
    } else {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

int vid_init(int width, int height, viddriver_t *osd_driver) {
    if (!game_bitmap) game_bitmap = bmp_create(NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT, 0);
    if (!frame_buffer) {
        frame_buffer = (uint16_t *)heap_caps_malloc(NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!frame_buffer) {
            frame_buffer = (uint16_t *)malloc(NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT * sizeof(uint16_t));
        }
    }
    video_ready = (game_bitmap && game_bitmap->data && frame_buffer);
    return video_ready ? 0 : -1;
}

int osd_rom_open(const char *path) {
    if (rom_file_ptr) fclose(rom_file_ptr);
    rom_file_ptr = fopen(path, "rb");
    return !rom_file_ptr ? -1 : 0;
}

int osd_rom_read(void *dst, int len) {
    if (!rom_file_ptr || !dst || len <= 0) return -1;
    return fread(dst, 1, len, rom_file_ptr);
}

void osd_rom_close(void) {
    if (rom_file_ptr) {
        fclose(rom_file_ptr);
        rom_file_ptr = NULL;
    }
}

void vid_shutdown(void) {
    video_ready = false;
    if (frame_buffer) {
        heap_caps_free(frame_buffer);
        frame_buffer = NULL;
    }
}

int vid_setmode(int width, int height) { return 0; }
void vid_setpalette(rgb_t *pal) { osd_setpalette(pal); }

bitmap_t *vid_getbuffer(void) {
    if (!game_bitmap) game_bitmap = bmp_create(NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT, 0);
    video_ready = (game_bitmap && game_bitmap->data && frame_buffer);
    return game_bitmap;
}

void osd_getvideoinfo(vidinfo_t *info) {
    info->default_width = NES_SCREEN_WIDTH;
    info->default_height = NES_SCREEN_HEIGHT;
    info->driver = 0;
}

void osd_togglefullscreen(int code) {}
char *osd_newextension(char *string, char *ext) { return string; }
void osd_fullname(char *fullname, const char *shortname) { strcpy(fullname, shortname); }

int osd_main(int argc, char *argv[]) {
    char *rom_name = "rom";
    if (argc > 0) rom_name = argv[argc - 1];
    return main_loop(rom_name, system_nes);
}

int nofrendo_log_init(void) { return 0; }
void nofrendo_log_shutdown(void) {}
int nofrendo_log_print(const char *s) { return 0; }
int nofrendo_log_printf(const char *format, ...) { return 0; }
void nofrendo_log_assert(int expr, int line, const char *file, char *msg) {}
