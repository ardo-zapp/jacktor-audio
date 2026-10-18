#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

#define NES_SCREEN_WIDTH  256
#define NES_SCREEN_HEIGHT 240

struct NesEmuState {
    bool isRunning;
    bool isPaused;
    uint32_t frameCount;
    float fps;
    std::string currentRomName;
};

void nesEmuInit();
bool nesEmuLoadRomBuffer(const uint8_t* romData, size_t romSize, const char* romName);
bool nesEmuLoadRomFile(const char* filePath);
void nesEmuStepFrame(uint16_t* outFrameRgb565);
void nesEmuPause(bool pause);
void nesEmuReset();
void nesEmuStop();
NesEmuState nesEmuGetState();
