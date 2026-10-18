#include "nofrendo/nes_emu.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include "esp_timer.h"

namespace {

static NesEmuState s_state = {false, false, 0, 60.0f, "No ROM Loaded"};
static uint16_t s_paletteRgb565[64];

// Built-in NES Palette (NTSC RGB565)
static const uint32_t s_defaultNesPalette[64] = {
    0x7C7C7C, 0x0022B7, 0x0000DA, 0x4200D4, 0xC00088, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x0073EF, 0x213CFF, 0x8A00D4, 0xE20092, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0xFFFFFF, 0x3FBFFF, 0x5F97FF, 0xA78BFF, 0xF77BFF, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0xFFFFFF, 0xABE7FF, 0xC7D7FF, 0xD7CBFF, 0xFFC7FF, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
};

static uint16_t rgb888ToRgb565(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void initPalette() {
    for (int i = 0; i < 64; ++i) {
        s_paletteRgb565[i] = rgb888ToRgb565(s_defaultNesPalette[i]);
    }
}

}

void nesEmuInit() {
    initPalette();
    s_state.isRunning = false;
    s_state.isPaused = false;
    s_state.frameCount = 0;
    s_state.fps = 60.0f;
    s_state.currentRomName = "No ROM Loaded";
}

bool nesEmuLoadRomBuffer(const uint8_t* romData, size_t romSize, const char* romName) {
    if (!romData || romSize < 16) return false;

    // Check iNES header "NES\x1A"
    if (romData[0] != 'N' || romData[1] != 'E' || romData[2] != 'S' || romData[3] != 0x1A) {
        return false;
    }

    s_state.isRunning = true;
    s_state.isPaused = false;
    s_state.frameCount = 0;
    s_state.currentRomName = romName ? romName : "Loaded ROM";
    return true;
}

bool nesEmuLoadRomFile(const char* filePath) {
    if (!filePath) return false;
    FILE* f = fopen(filePath, "rb");
    if (!f) return false;

    uint8_t header[16];
    if (fread(header, 1, 16, f) != 16) {
        fclose(f);
        return false;
    }
    fclose(f);

    return nesEmuLoadRomBuffer(header, 16, filePath);
}

void nesEmuStepFrame(uint16_t* outFrameRgb565) {
    if (!outFrameRgb565 || !s_state.isRunning || s_state.isPaused) return;

    s_state.frameCount++;

    // Generate test pattern / NES frame rendering
    uint16_t color1 = s_paletteRgb565[(s_state.frameCount / 10) % 16];
    uint16_t color2 = s_paletteRgb565[(s_state.frameCount / 10 + 1) % 16];

    for (int y = 0; y < NES_SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < NES_SCREEN_WIDTH; ++x) {
            if ((x / 16 + y / 16 + s_state.frameCount / 5) % 2 == 0) {
                outFrameRgb565[y * NES_SCREEN_WIDTH + x] = color1;
            } else {
                outFrameRgb565[y * NES_SCREEN_WIDTH + x] = color2;
            }
        }
    }
}

void nesEmuPause(bool pause) {
    s_state.isPaused = pause;
}

void nesEmuReset() {
    s_state.frameCount = 0;
}

void nesEmuStop() {
    s_state.isRunning = false;
    s_state.isPaused = false;
    s_state.currentRomName = "No ROM Loaded";
}

NesEmuState nesEmuGetState() {
    return s_state;
}
