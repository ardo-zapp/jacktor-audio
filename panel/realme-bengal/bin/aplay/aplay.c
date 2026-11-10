// Jacktor Audio Framework - ALSA aplay Compatibility Layer for Android
// Wraps tinyplay with device selection logic

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_ARGS 64
#define MAX_PATH 256

static int card_exists(int card, int dev) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "/dev/snd/pcmC%dD%dp", card, dev);
    return access(path, F_OK) == 0;
}

static int detect_card(void) {
    // Prefer card 1 (QCC5125 USB DAC) if exists
    if (card_exists(1, 0)) return 1;
    // Fallback to card 0
    if (card_exists(0, 0)) return 0;
    return 1; // Default to 1
}

static void list_devices(void) {
    printf("**** List of PLAYBACK Hardware Devices ****\n");
    FILE *f = fopen("/proc/asound/cards", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *bracket = strchr(line, '[');
        if (bracket) {
            char *end = strchr(bracket, ']');
            if (end) {
                *end = '\0';
                char *num = line;
                while (*num == ' ') num++;
                char *name = bracket + 1;
                // Trim spaces
                char *p = name + strlen(name) - 1;
                while (p > name && *p == ' ') *p-- = '\0';
                printf("card %s: %s [%s], device 0: USB Audio [USB Audio]\n", num, name, name);
                printf("  Subdevices: 1/1\n");
                printf("  Subdevice #0: subdevice #0\n");
            }
        }
    }
    fclose(f);
}

static void list_pcms(void) {
    printf("null\n");
    printf("    Discard all samples (playback) or generate zero samples (capture)\n");
    printf("default\n");
    printf("    Default Audio Device\n");
    FILE *f = fopen("/proc/asound/cards", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *bracket = strchr(line, '[');
        if (bracket) {
            char *end = strchr(bracket, ']');
            if (end) {
                *end = '\0';
                char *name = bracket + 1;
                char *p = name + strlen(name) - 1;
                while (p > name && *p == ' ') *p-- = '\0';
                printf("hw:CARD=%s,DEV=0\n", name);
                printf("    %s, USB Audio\n", name);
                printf("    Direct hardware device without any conversions\n");
                printf("plughw:CARD=%s,DEV=0\n", name);
                printf("    %s, USB Audio\n", name);
                printf("    Hardware device with all software conversions\n");
            }
        }
    }
    fclose(f);
}

int main(int argc, char *argv[]) {
    // Handle -l / --list-devices
    if (argc > 1 && (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list-devices") == 0)) {
        list_devices();
        return 0;
    }

    // Handle -L / --list-pcms
    if (argc > 1 && (strcmp(argv[1], "-L") == 0 || strcmp(argv[1], "--list-pcms") == 0)) {
        list_pcms();
        return 0;
    }

    int card = detect_card();
    int dev = 0;
    char *file = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            const char *spec = argv[++i];
            // Parse device spec like "hw:CARD=1,DEV=0" or "1" or "QCC"
            if (strstr(spec, "CARD=1") || strstr(spec, "card 1") ||
                strstr(spec, "1,0") || strstr(spec, "QCC") || strstr(spec, "qcc") ||
                strcmp(spec, "1") == 0) {
                card = 1;
            } else if (strstr(spec, "CARD=0") || strstr(spec, "card 0") ||
                       strstr(spec, "0,0") || strstr(spec, "bengal") ||
                       strcmp(spec, "0") == 0) {
                card = 0;
            }
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            dev = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "-r") == 0 ||
                   strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-t") == 0 ||
                   strncmp(argv[i], "--buffer-time", 13) == 0 ||
                   strncmp(argv[i], "--period-time", 13) == 0 ||
                   strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "-v") == 0) {
            // Skip these options (handled by tinyplay or ignored)
            if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "-r") == 0 ||
                strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-t") == 0) {
                i++; // Skip next arg
            }
        } else if (argv[i][0] != '-') {
            if (!file) file = argv[i];
        }
    }

    // Verify device exists, fallback if not
    if (!card_exists(card, dev)) {
        if (card_exists(1, 0)) { card = 1; dev = 0; }
        else if (card_exists(0, 0)) { card = 0; dev = 0; }
    }

    // Execute tinyplay
    char *tinyplay_argv[MAX_ARGS];
    int idx = 0;
    tinyplay_argv[idx++] = "/system/bin/tinyplay";

    if (file && strcmp(file, "-") != 0) {
        tinyplay_argv[idx++] = file;
    } else {
        tinyplay_argv[idx++] = "/dev/stdin";
    }

    tinyplay_argv[idx++] = "-D";
    char card_str[16];
    snprintf(card_str, sizeof(card_str), "%d", card);
    tinyplay_argv[idx++] = card_str;

    tinyplay_argv[idx++] = "-d";
    char dev_str[16];
    snprintf(dev_str, sizeof(dev_str), "%d", dev);
    tinyplay_argv[idx++] = dev_str;

    tinyplay_argv[idx++] = "-p";
    tinyplay_argv[idx++] = "2048";
    tinyplay_argv[idx++] = "-n";
    tinyplay_argv[idx++] = "4";
    tinyplay_argv[idx++] = NULL;

    execv("/system/bin/tinyplay", tinyplay_argv);
    perror("execv tinyplay failed");
    return 1;
}