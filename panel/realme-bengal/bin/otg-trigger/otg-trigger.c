// Jacktor Audio Framework - Safe OTG Trigger & State Utility
// Controls USB OTG modes: host, device, auto, trigger, status

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define MAX_LINE 512
#define MAX_PATH 256
#define MODDIR "/data/adb/modules/jacktor-audio-framework"
#define CONF_DIR MODDIR "/config"
#define STATE_FILE CONF_DIR "/usb_mode_state"

static const char *SSUSB_MODE = "/sys/devices/platform/soc/4e00000.ssusb/mode";
static const char *OTG_SWITCH_PMIC = "/sys/class/power_supply/usb/otg_switch";
static const char *OTG_SWITCH_OPLUS = "/sys/class/oplus_chg/usb/otg_switch";
static const char *OTG_ONLINE = "/sys/class/power_supply/usb/otg_online";
static const char *VBUS_VOLTAGE = "/sys/class/power_supply/usb/voltage_now";
static const char *EXTCON_STATE = "/sys/class/extcon/extcon1/state";
static const char *GADGET_UDC = "/config/usb_gadget/g1/UDC";

static void write_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd >= 0) {
        write(fd, value, strlen(value));
        close(fd);
    }
}

static char *read_file(const char *path, char *buf, size_t sz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    ssize_t n = read(fd, buf, sz - 1);
    close(fd);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    // Trim newline
    char *p = strchr(buf, '\n');
    if (p) *p = '\0';
    return buf;
}

static void save_state(const char *mode) {
    mkdir(CONF_DIR, 0755);
    write_file(STATE_FILE, mode);
    // Also set property
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "setprop persist.jacktor.usb_mode %s", mode);
    system(cmd);
}

static void trigger_usb(void) {
    system("lsusb >/dev/null 2>&1");
}

static void do_host(int internal) {
    if (!internal) save_state("host");
    printf("[*] Activating USB OTG Host Mode with Auto Re-Trigger...\n");
    write_file(GADGET_UDC, "");
    usleep(200000);
    write_file(SSUSB_MODE, "host");
    write_file(OTG_SWITCH_PMIC, "1");
    write_file(OTG_SWITCH_OPLUS, "1");
    usleep(300000);
    trigger_usb();
    char buf[MAX_LINE];
    read_file(SSUSB_MODE, buf, sizeof(buf));
    printf("[+] Host Mode active and USB re-triggered. Mode: %s\n", buf);
}

static void do_device(int internal) {
    if (!internal) save_state("device");
    printf("[*] Switching to USB Peripheral (Gadget) Mode...\n");
    write_file(OTG_SWITCH_PMIC, "0");
    write_file(OTG_SWITCH_OPLUS, "0");
    usleep(200000);
    write_file(SSUSB_MODE, "peripheral");
    char conf[64];
    FILE *fp = popen("getprop persist.sys.usb.config", "r");
    if (fp) {
        fgets(conf, sizeof(conf), fp);
        pclose(fp);
        conf[strcspn(conf, "\n")] = '\0';
    }
    if (strlen(conf) == 0) strcpy(conf, "adb");
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "setprop sys.usb.config %s", conf);
    system(cmd);
    char buf[MAX_LINE];
    read_file(SSUSB_MODE, buf, sizeof(buf));
    printf("[+] Device / Gadget Mode active. Mode: %s\n", buf);
}

static void do_auto(int internal) {
    if (!internal) save_state("auto");
    printf("[*] Applying Auto Mode (dynamic ID-pin detection)...\n");
    write_file(OTG_SWITCH_PMIC, "1");
    write_file(OTG_SWITCH_OPLUS, "1");
    usleep(300000);
    char extcon[MAX_LINE];
    read_file(EXTCON_STATE, extcon, sizeof(extcon));
    if (strstr(extcon, "USB-HOST=1")) {
        write_file(GADGET_UDC, "");
        write_file(SSUSB_MODE, "host");
        trigger_usb();
        printf("[+] Auto Mode: OTG cable detected -> Host active.\n");
    } else {
        write_file(SSUSB_MODE, "peripheral");
        char conf[64];
        FILE *fp = popen("getprop persist.sys.usb.config", "r");
        if (fp) {
            fgets(conf, sizeof(conf), fp);
            pclose(fp);
            conf[strcspn(conf, "\n")] = '\0';
        }
        if (strlen(conf) == 0) strcpy(conf, "adb");
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "setprop sys.usb.config %s", conf);
        system(cmd);
        printf("[+] Auto Mode: No OTG cable detected -> Peripheral standby.\n");
    }
}

static void do_trigger(void) {
    printf("[*] Re-triggering USB bus enumeration...\n");
    trigger_usb();
    printf("[+] USB bus re-scanned.\n");
}

static void do_status(void) {
    char saved[32] = "auto";
    read_file(STATE_FILE, saved, sizeof(saved));

    char otg_sw[32], otg_online[32], vbus[32], extcon[256], ssusb[32], gadget_udc[64];

    read_file(OTG_SWITCH_PMIC, otg_sw, sizeof(otg_sw));
    read_file(OTG_ONLINE, otg_online, sizeof(otg_online));
    read_file(VBUS_VOLTAGE, vbus, sizeof(vbus));
    read_file(EXTCON_STATE, extcon, sizeof(extcon));
    read_file(SSUSB_MODE, ssusb, sizeof(ssusb));
    read_file(GADGET_UDC, gadget_udc, sizeof(gadget_udc));

    // Replace newlines in extcon with spaces
    for (char *p = extcon; *p; p++) {
        if (*p == '\n') *p = ' ';
    }

    printf("==================================================\n");
    printf("  JACKTOR AUDIO FRAMEWORK - USB OTG STATUS        \n");
    printf("==================================================\n");
    printf("Saved Target Mode : %s\n", saved);
    printf("OTG Switch (PMIC) : %s\n", otg_sw[0] ? otg_sw : "N/A");
    printf("OTG Online        : %s\n", otg_online[0] ? otg_online : "N/A");
    printf("VBUS Voltage      : %s uV\n", vbus[0] ? vbus : "N/A");
    printf("Extcon1 State     : %s\n", extcon[0] ? extcon : "N/A");
    printf("SSUSB Controller  : %s\n", ssusb[0] ? ssusb : "N/A");
    printf("Gadget UDC Bound  : %s\n", gadget_udc[0] ? gadget_udc : "N/A");
    printf("--------------------------------------------------\n");
    printf("Connected USB Devices:\n");
    system("lsusb 2>/dev/null");
    printf("==================================================\n");
}

static void print_usage(const char *prog) {
    printf("Usage: %s {host|device|auto|trigger|status} [internal]\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        do_status();
        return 0;
    }

    const char *action = argv[1];
    int internal = (argc > 2 && strcmp(argv[2], "internal") == 0);

    if (strcmp(action, "host") == 0 || strcmp(action, "1") == 0) {
        do_host(internal);
    } else if (strcmp(action, "device") == 0 || strcmp(action, "peripheral") == 0 ||
               strcmp(action, "gadget") == 0 || strcmp(action, "0") == 0) {
        do_device(internal);
    } else if (strcmp(action, "auto") == 0) {
        do_auto(internal);
    } else if (strcmp(action, "trigger") == 0) {
        do_trigger();
    } else if (strcmp(action, "status") == 0) {
        do_status();
    } else {
        print_usage(argv[0]);
        return 1;
    }
    return 0;
}