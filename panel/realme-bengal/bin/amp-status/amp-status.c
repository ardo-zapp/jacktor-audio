// Jacktor Audio Framework - Amplifier & Audio Diagnostics
// Reads system info from /proc, /sys and displays status

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_LINE 1024
#define MAX_PATH 256

static void print_section(const char *title) {
    printf("\n[%s]\n", title);
}

static void print_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        printf("%s", line);
    }
    fclose(f);
}

static void print_cmd_output(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        printf("%s", line);
    }
    pclose(fp);
}

static void read_sysfs_value(const char *path, const char *label) {
    FILE *f = fopen(path, "r");
    if (f) {
        char buf[128];
        if (fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, "\n")] = 0;
            printf("  %s: %s\n", label, buf);
        }
        fclose(f);
    }
}

static void list_usb_devices(void) {
    print_cmd_output("lsusb 2>/dev/null");
}

int main(void) {
    printf("==================================================\n");
    printf("  JACKTOR AUDIO & AMPLIFIER DSP STATUS            \n");
    printf("==================================================\n");

    // [1] ALSA Sound Cards
    print_section("1] ALSA Sound Cards");
    print_file("/proc/asound/cards");

    // [2] Active Audio Streams (PCM)
    print_section("2] Active Audio Streams (PCM)");
    print_file("/proc/asound/pcm");

    // [3] USB Audio / OTG Devices
    print_section("3] USB Audio / OTG Devices");
    list_usb_devices();

    // [4] Power & VBUS Boost
    print_section("4] Power & VBUS Boost");
    read_sysfs_value("/sys/class/power_supply/usb/otg_online", "OTG Online");
    read_sysfs_value("/sys/class/power_supply/usb/voltage_now", "VBUS Volts (uV)");
    read_sysfs_value("/sys/class/power_supply/battery/capacity", "Battery (%)");
    read_sysfs_value("/sys/class/power_supply/battery/status", "Battery Status");

    // [5] Audio Server Process
    print_section("5] Audio Server Process");
    print_cmd_output("ps -ef 2>/dev/null | grep -E '(audioserver|audio.service)' | grep -v grep");

    printf("==================================================\n");
    return 0;
}