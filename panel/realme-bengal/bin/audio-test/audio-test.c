// Jacktor Audio Framework - Safe 24-bit Audio Calibration & Signal Generator
// Generates and plays test tones (sine, sweep) via tinyplay

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>

#define SAMPLE_RATE 44100
#define CHANNELS 2
#define BITS_PER_SAMPLE 24
#define BYTES_PER_FRAME 6  // 2 channels * 3 bytes (24-bit)
#define MAX_DURATION 300
#define MAX_FREQ 20000
#define MIN_FREQ 10

#define MODDIR "/data/adb/modules/jacktor-audio-framework"
#define API_SCRIPT MODDIR "/webroot/cgi-bin/api.sh"
#define TINYMIX "/system/bin/tinymix"
#define TINYPLAY "/system/bin/tinyplay"
#define BUSYBOX MODDIR "/system/bin/busybox"

static volatile int g_running = 1;
static char g_wav_tmp[256];
static int g_card = 1;
static int g_prev_vol = -1;

static void cleanup_and_restore(void) {
    unlink(g_wav_tmp);
    // Restore web audio settings
    if (access(API_SCRIPT, X_OK) == 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "REQUEST_METHOD= QUERY_STRING= sh %s set_gain >/dev/null 2>&1", API_SCRIPT);
        system(cmd);
    }
    // Restore hardware volume
    if (g_prev_vol >= 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s -D %d 3 %d 2>/dev/null || %s -D %d \"PCM Playback Volume\" %d 2>/dev/null",
                 TINYMIX, g_card, g_prev_vol, TINYMIX, g_card, g_prev_vol);
        system(cmd);
    }
}

static void signal_handler(int sig) {
    g_running = 0;
    cleanup_and_restore();
    _exit(0);
}

static int detect_card(void) {
    if (access("/dev/snd/pcmC1D0p", F_OK) == 0) return 1;
    if (access("/dev/snd/pcmC0D0p", F_OK) == 0) return 0;
    return 1;
}

static int get_master_volume(void) {
    // Try cmd media_session
    FILE *fp = popen("cmd media_session volume --stream 3 --get 2>/dev/null", "r");
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "volume is")) {
                int vol = atoi(strstr(buf, "volume is") + 9);
                pclose(fp);
                return vol;
            }
        }
        pclose(fp);
    }
    return 25; // Default
}

static int read_hw_volume(void) {
    char cmd[256], buf[64];
    snprintf(cmd, sizeof(cmd), "%s -D %d 3 2>/dev/null", TINYMIX, g_card);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, ": ")) {
                int vol = atoi(strstr(buf, ": ") + 2);
                pclose(fp);
                return vol;
            }
        }
        pclose(fp);
    }
    // Try by name
    snprintf(cmd, sizeof(cmd), "%s -D %d \"PCM Playback Volume\" 2>/dev/null", TINYMIX, g_card);
    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, ": ")) {
                int vol = atoi(strstr(buf, ": ") + 2);
                pclose(fp);
                return vol;
            }
        }
        pclose(fp);
    }
    return 15;
}

static void write_wav_header(int fd, int data_bytes) {
    int riff_size = data_bytes + 36;
    int byte_rate = SAMPLE_RATE * BYTES_PER_FRAME;

    // RIFF header
    write(fd, "RIFF", 4);
    // riff_size (little endian)
    for (int i = 0; i < 4; i++) {
        char b = (riff_size >> (i * 8)) & 0xFF;
        write(fd, &b, 1);
    }
    write(fd, "WAVEfmt ", 8);
    // fmt chunk size = 16
    char fmt_size[4] = {16, 0, 0, 0};
    write(fd, fmt_size, 4);
    // audio format = PCM (1)
    char fmt_tag[2] = {1, 0};
    write(fd, fmt_tag, 2);
    // channels = 2
    char chans[2] = {2, 0};
    write(fd, chans, 2);
    // sample rate
    for (int i = 0; i < 4; i++) {
        char b = (SAMPLE_RATE >> (i * 8)) & 0xFF;
        write(fd, &b, 1);
    }
    // byte rate
    for (int i = 0; i < 4; i++) {
        char b = (byte_rate >> (i * 8)) & 0xFF;
        write(fd, &b, 1);
    }
    // frame size = 6
    char frame_size[2] = {6, 0};
    write(fd, frame_size, 2);
    // bits per sample = 24
    char bps[2] = {24, 0};
    write(fd, bps, 2);
    // data chunk
    write(fd, "data", 4);
    // data size
    for (int i = 0; i < 4; i++) {
        char b = (data_bytes >> (i * 8)) & 0xFF;
        write(fd, &b, 1);
    }
}

static void write_silence(int fd, int bytes) {
    char *zeros = calloc(1, bytes);
    if (zeros) {
        write(fd, zeros, bytes);
        free(zeros);
    }
}

static void write_sine_wave(int fd, int freq, int duration, int amplitude) {
    int total_samples = SAMPLE_RATE * duration;
    int data_bytes = total_samples * BYTES_PER_FRAME;

    // Find period for efficient generation
    int a = SAMPLE_RATE, b = freq;
    while (b != 0) { int t = b; b = a % b; a = t; }
    int gcd = (a < 1) ? 1 : a;
    int period = SAMPLE_RATE / gcd;

    // Clamp period
    while (period < 441 && period * 2 <= 4410) period *= 2;
    if (period > 4410) period = 4410;

    // Pre-compute one period
    int32_t *period_l = malloc(period * sizeof(int32_t));
    int32_t *period_r = malloc(period * sizeof(int32_t));
    if (!period_l || !period_r) {
        free(period_l);
        free(period_r);
        return;
    }

    const double pi2 = 2.0 * M_PI;
    for (int n = 0; n < period; n++) {
        double phase = pi2 * freq * n / SAMPLE_RATE;
        int v = (int)(amplitude * sin(phase));
        if (v < 0) v += 16777216;  // 24-bit unsigned
        period_l[n] = v;
        period_r[n] = v;
    }

    // Write data by repeating period
    int written = 0;
    unsigned char frame[6];
    while (written < data_bytes && g_running) {
        int idx = (written / BYTES_PER_FRAME) % period;
        int32_t v = period_l[idx];
        frame[0] = v & 0xFF;
        frame[1] = (v >> 8) & 0xFF;
        frame[2] = (v >> 16) & 0xFF;
        frame[3] = v & 0xFF;
        frame[4] = (v >> 8) & 0xFF;
        frame[5] = (v >> 16) & 0xFF;
        write(fd, frame, 6);
        written += 6;
    }

    free(period_l);
    free(period_r);
}

static void write_log_sweep(int fd, int duration, int amplitude) {
    int total_samples = SAMPLE_RATE * duration;
    int data_bytes = total_samples * BYTES_PER_FRAME;

    const double pi2 = 2.0 * M_PI;
    double f_start = 20.0, f_end = 20000.0;
    double phase = 0.0;

    unsigned char frame[6];
    for (int n = 0; n < total_samples && g_running; n++) {
        if (n % 16 == 0) {
            double prog = (double)n / total_samples;
            double cur_f = f_start * pow(f_end / f_start, prog);
            double phase_inc = pi2 * cur_f / SAMPLE_RATE;
            phase += phase_inc;
        } else {
            // Use previous phase_inc
        }
        int v = (int)(amplitude * sin(phase));
        if (v < 0) v += 16777216;
        frame[0] = v & 0xFF;
        frame[1] = (v >> 8) & 0xFF;
        frame[2] = (v >> 16) & 0xFF;
        frame[3] = v & 0xFF;
        frame[4] = (v >> 8) & 0xFF;
        frame[5] = (v >> 16) & 0xFF;
        write(fd, frame, 6);
    }
}

static int play_wav(const char *wav_file) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s %s -D %d -d 0 -p 2048 -n 4", TINYPLAY, wav_file, g_card);
    return system(cmd);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [freq_hz|sweep] [duration_sec] [volume]\n", prog);
    printf("Safe Frequency & Jitter-Free 24-bit Output\n");
    printf("Examples:\n");
    printf("  %s           : Play 1000 Hz safe calibration tone (5s, 24-bit 44.1kHz)\n", prog);
    printf("  %s 440 3     : Play 440 Hz tone (3s, 24-bit 44.1kHz)\n", prog);
    printf("  %s sweep 5   : Play safe logarithmic frequency sweep (5s, 24-bit 44.1kHz)\n", prog);
    printf("  %s stop      : Stop playback and restore web audio settings\n", prog);
}

int main(int argc, char *argv[]) {
    // Install signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);

    g_card = detect_card();

    // Parse arguments
    if (argc > 1 && (strcmp(argv[1], "stop") == 0 || strcmp(argv[1], "kill") == 0)) {
        printf("[*] Stopping active audio test streams...\n");
        system("pkill -f tinyplay 2>/dev/null");
        system("rm -f /data/local/tmp/jacktor_tone_*.wav /data/local/tmp/cycle*.raw /data/local/tmp/block*.raw 2>/dev/null");
        if (access(API_SCRIPT, X_OK) == 0) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "REQUEST_METHOD= QUERY_STRING= sh %s set_gain >/dev/null 2>&1", API_SCRIPT);
            system(cmd);
        }
        printf("[+] Audio test stopped. Restored audio settings from web configuration.\n");
        return 0;
    }

    if (argc > 1 && (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    int freq = 1000;
    int duration = 5;
    int req_vol = -1;

    if (argc > 1) {
        if (strcmp(argv[1], "sweep") == 0) {
            freq = -1; // sweep mode
        } else {
            freq = atoi(argv[1]);
        }
    }
    if (argc > 2) duration = atoi(argv[2]);
    if (argc > 3) req_vol = atoi(argv[3]);

    // Clamp values
    if (duration < 1) duration = 1;
    if (duration > MAX_DURATION) duration = MAX_DURATION;
    if (freq > 0) {
        if (freq < MIN_FREQ) freq = MIN_FREQ;
        if (freq > MAX_FREQ) freq = MAX_FREQ;
    }

    // Get volume
    int vol = (req_vol >= 0) ? req_vol : get_master_volume();
    if (vol < 0) vol = 0;
    if (vol > 25) vol = 25;

    // Scale amplitude: VOL 0 = silence, VOL 1..25 = 220,000..5,500,000
    int amp = (5500000 * vol) / 25;

    // Save original hardware volume
    g_prev_vol = read_hw_volume();

    // Set hardware volume
    if (vol <= 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s -D %d 3 0 2>/dev/null || %s -D %d \"PCM Playback Volume\" 0 2>/dev/null",
                 TINYMIX, g_card, TINYMIX, g_card);
        system(cmd);
    } else {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s -D %d 3 15 2>/dev/null || %s -D %d \"PCM Playback Volume\" 15 2>/dev/null",
                 TINYMIX, g_card, TINYMIX, g_card);
        system(cmd);
    }

    printf("[*] Jacktor Audio Test -> Mode: %s | Dur: %ds | Master Vol: %d/25 (amp=%d) | Format: 24-bit 44.1kHz Stereo | Card: %d\n",
           (freq < 0) ? "sweep" : "tone", duration, vol, amp, g_card);

    // Create temp WAV file
    snprintf(g_wav_tmp, sizeof(g_wav_tmp), "/data/local/tmp/jacktor_tone_%d.wav", getpid());
    int fd = open(g_wav_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Failed to create WAV file");
        cleanup_and_restore();
        return 1;
    }

    int total_samples = SAMPLE_RATE * duration;
    int data_bytes = total_samples * BYTES_PER_FRAME;

    // Write header
    write_wav_header(fd, data_bytes);

    // Generate audio data
    if (amp <= 0) {
        // Silence
        write_silence(fd, data_bytes);
    } else if (freq < 0) {
        // Sweep mode - check for pre-rendered sweep
        const char *pre_sweep = MODDIR "/system/bin/jacktor_sweep_5s.wav";
        if (duration == 5 && access(pre_sweep, F_OK) == 0) {
            close(fd);
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "cp %s %s", pre_sweep, g_wav_tmp);
            system(cmd);
        } else {
            write_log_sweep(fd, duration, amp);
        }
    } else {
        // Fixed tone
        write_sine_wave(fd, freq, duration, amp);
    }

    close(fd);

    // Play
    int ret = play_wav(g_wav_tmp);

    // Cleanup
    unlink(g_wav_tmp);

    if (ret == 0) {
        printf("[+] Audio test playback complete. Audio settings restored to web configuration.\n");
    } else {
        printf("[!] Playback failed (exit code: %d)\n", ret);
    }

    cleanup_and_restore();
    return ret == 0 ? 0 : 1;
}