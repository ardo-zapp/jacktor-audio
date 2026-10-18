#include <iostream>
#include <cassert>
#include <cstring>
#include <cmath>
#include "kissfft/kiss_fft.h"
#include "kissfft/kiss_fftr.h"

// Mock commsLog for testing
void commsLog(const char* level, const char* msg) {
    (void)level;
    (void)msg;
}

#include "../src/driver/analyzer.cpp"

void test_validateSettings_mode() {
    std::cout << "Testing mode validation..." << std::endl;

    // Valid modes
    std::strcpy(mode, "off");
    validateSettings();
    assert(std::strcmp(mode, "off") == 0);

    std::strcpy(mode, "vu");
    validateSettings();
    assert(std::strcmp(mode, "vu") == 0);

    std::strcpy(mode, "fft");
    validateSettings();
    assert(std::strcmp(mode, "fft") == 0);

    // Invalid mode
    std::strcpy(mode, "abc");
    validateSettings();
    assert(std::strcmp(mode, ANALYZER_DEFAULT_MODE) == 0);

    std::cout << "Mode validation passed!" << std::endl;
}

void test_validateSettings_bands() {
    std::cout << "Testing bands validation..." << std::endl;

    // Valid bands
    uint8_t validBands[] = {WS_BANDS_8, WS_BANDS_16, WS_BANDS_24, WS_BANDS_32, WS_BANDS_64};
    for (uint8_t b : validBands) {
        bandsLen = b;
        validateSettings();
        assert(bandsLen == b);
    }

    // Invalid bands
    bandsLen = 10;
    validateSettings();
    assert(bandsLen == ANALYZER_DEFAULT_BANDS);

    std::cout << "Bands validation passed!" << std::endl;
}

void test_validateSettings_updateMs() {
    std::cout << "Testing updateMs validation..." << std::endl;

    // Below minimum
    updateMs = ANALYZER_MIN_UPDATE_MS - 1;
    validateSettings();
    assert(updateMs == ANALYZER_MIN_UPDATE_MS);

    // Above maximum
    updateMs = ANALYZER_MAX_UPDATE_MS + 1;
    validateSettings();
    assert(updateMs == ANALYZER_MAX_UPDATE_MS);

    // Within range
    updateMs = (ANALYZER_MIN_UPDATE_MS + ANALYZER_MAX_UPDATE_MS) / 2;
    uint16_t expected = updateMs;
    validateSettings();
    assert(updateMs == expected);

    std::cout << "UpdateMs validation passed!" << std::endl;
}

void test_kiss_fft_direct_dsp() {
    std::cout << "Testing direct KissFFT DSP functions..." << std::endl;

    const int nfft = 512;
    const float sampleRate = 44100.0f;
    const float targetFreq = 1000.0f;

    kiss_fftr_cfg cfg = kiss_fftr_alloc(nfft, 0, nullptr, nullptr);
    assert(cfg != nullptr);

    kiss_fft_scalar td[nfft];
    kiss_fft_cpx fd[nfft / 2 + 1];

    for (int i = 0; i < nfft; ++i) {
        td[i] = std::sin(2.0f * M_PI * targetFreq * i / sampleRate);
    }

    float rmsVal = kiss_fft_rms(td, nfft);
    assert(rmsVal > 0.0f);

    kiss_fft_apply_window_hann(td, nfft);
    kiss_fftr(cfg, td, fd);

    float dominantHz = kiss_fft_dominant_frequency(fd, sampleRate, nfft);
    std::cout << "  Generated sine: " << targetFreq << " Hz | Dominant freq calculated: " << dominantHz << " Hz" << std::endl;
    assert(std::abs(dominantHz - targetFreq) < (sampleRate / nfft));

    free(cfg);
    std::cout << "Direct KissFFT DSP tests passed!" << std::endl;
}

int main() {
    test_validateSettings_mode();
    test_validateSettings_bands();
    test_validateSettings_updateMs();
    test_kiss_fft_direct_dsp();

    std::cout << "All analyzer & direct KissFFT tests passed!" << std::endl;
    return 0;
}
