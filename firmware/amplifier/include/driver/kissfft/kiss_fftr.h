#ifndef KISS_FFTR_H
#define KISS_FFTR_H

#include "kiss_fft.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kiss_fftr_state *kiss_fftr_cfg;

kiss_fftr_cfg kiss_fftr_alloc(int nfft, int inverse_fft, void * mem, size_t * len);

void kiss_fftr(kiss_fftr_cfg cfg, const kiss_fft_scalar *timedata, kiss_fft_cpx *freqdata);

void kiss_fftri(kiss_fftr_cfg cfg, const kiss_fft_cpx *freqdata, kiss_fft_scalar *timedata);

#define kiss_fftr_free KISS_FFT_FREE

/*
 * High-Performance Direct DSP Utilities
 */
static inline void kiss_fft_apply_window_hann(kiss_fft_scalar *timedata, int nfft) {
    if (!timedata || nfft <= 1) return;
    float invN = 1.0f / (float)(nfft - 1);
    for (int i = 0; i < nfft; ++i) {
        float window = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i * invN));
        timedata[i] *= window;
    }
}

static inline void kiss_fft_apply_window_hamming(kiss_fft_scalar *timedata, int nfft) {
    if (!timedata || nfft <= 1) return;
    float invN = 1.0f / (float)(nfft - 1);
    for (int i = 0; i < nfft; ++i) {
        float window = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * i * invN);
        timedata[i] *= window;
    }
}

static inline float kiss_fft_bin_to_frequency(int bin, float sampleRate, int nfft) {
    if (bin < 0 || nfft <= 0) return 0.0f;
    return ((float)bin * sampleRate) / (float)nfft;
}

static inline int kiss_fft_dominant_bin(const kiss_fft_cpx *freqdata, int nfft) {
    if (!freqdata || nfft <= 0) return 0;
    int numBins = nfft / 2 + 1;
    int maxIdx = 1;
    float maxMagSq = freqdata[1].r * freqdata[1].r + freqdata[1].i * freqdata[1].i;

    for (int i = 2; i < numBins; ++i) {
        float magSq = freqdata[i].r * freqdata[i].r + freqdata[i].i * freqdata[i].i;
        if (magSq > maxMagSq) {
            maxMagSq = magSq;
            maxIdx = i;
        }
    }
    return maxIdx;
}

static inline float kiss_fft_dominant_frequency(const kiss_fft_cpx *freqdata, float sampleRate, int nfft) {
    int bin = kiss_fft_dominant_bin(freqdata, nfft);
    return kiss_fft_bin_to_frequency(bin, sampleRate, nfft);
}

static inline float kiss_fft_rms(const kiss_fft_scalar *timedata, int nfft) {
    if (!timedata || nfft <= 0) return 0.0f;
    double sumSq = 0.0;
    for (int i = 0; i < nfft; ++i) {
        double val = (double)timedata[i];
        sumSq += val * val;
    }
    return (float)sqrt(sumSq / (double)nfft);
}

#ifdef __cplusplus
}
#endif

#endif
