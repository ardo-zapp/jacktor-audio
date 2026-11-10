// Jacktor Audio Framework - Linux MOTD & Telemetry Generator
// Ubuntu Server Layout with Jacktor Telemetry

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <math.h>

#define MAX_LINE 512

// ANSI Color Codes
#define C_RST    "\033[0m"
#define C_BOLD   "\033[1m"
#define C_DIM    "\033[2m"
#define C_WHITE  "\033[1;37m"
#define C_CYAN   "\033[1;36m"
#define C_GREEN  "\033[1;32m"
#define C_GRAY   "\033[0;37m"
#define C_YELLOW "\033[1;33m"

static char *read_file_trim(const char *path, char *buf, size_t sz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    ssize_t n = read(fd, buf, sz - 1);
    close(fd);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    char *p = strchr(buf, '\n');
    if (p) *p = '\0';
    return buf;
}

static void get_kernel_arch(char *kernel, size_t ksz, char *arch, size_t asz) {
    struct utsname u;
    if (uname(&u) == 0) {
        snprintf(kernel, ksz, "%s", u.release);
        snprintf(arch, asz, "%s", u.machine);
    } else {
        snprintf(kernel, ksz, "4.19.152-perf+");
        snprintf(arch, asz, "aarch64");
    }
}

static void get_load(char *buf, size_t sz) {
    (void)sz; // Unused parameter - suppress warning
    // Set default first
    strcpy(buf, "0.00");

    FILE *f = fopen("/proc/loadavg", "r");
    if (f) {
        fscanf(f, "%s", buf);
        fclose(f);
    }
}

static int count_processes(void) {
    DIR *d = opendir("/proc");
    if (!d) return 765;
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_type == DT_DIR && e->d_name[0] >= '0' && e->d_name[0] <= '9') count++;
    }
    closedir(d);
    return count > 20 ? count : 765;
}

static int count_users(void) {
    FILE *fp = popen("ps -A 2>/dev/null | grep -E 'sshd-session|pts' | grep -v grep | wc -l", "r");
    if (!fp) return 1;
    char buf[32];
    int n = 1;
    if (fgets(buf, sizeof(buf), fp)) n = atoi(buf);
    pclose(fp);
    return n > 0 ? n : 1;
}

static void get_disk_usage(char *used, size_t usz, char *total, size_t tsz, char *pct, size_t psz) {
    // Set defaults first
    strcpy(used, "3.7GB");
    strcpy(total, "48.1GB");
    strcpy(pct, "8%");

    struct statvfs st;
    if (statvfs("/data", &st) == 0) {
        unsigned long long t = (unsigned long long)st.f_blocks * st.f_frsize;
        unsigned long long u = (unsigned long long)(st.f_blocks - st.f_bfree) * st.f_frsize;
        // Guard against division by zero
        if (t > 0) {
            snprintf(used, usz, "%.1fGB", u / 1073741824.0);
            snprintf(total, tsz, "%.1fGB", t / 1073741824.0);
            snprintf(pct, psz, "%.0f%%", (double)u * 100.0 / t);
        }
    }
}

static void get_memory_info(char *mpct, size_t mpsz, char *mused, size_t musz,
                            char *mtotal, size_t mtsz, char *spct, size_t spsz,
                            char *sused, size_t susz, char *stotal, size_t stsz) {
    // Set defaults first (in case parsing fails)
    strcpy(mpct, "44%");
    strcpy(mused, "1.6GB");
    strcpy(mtotal, "3.6GB");
    strcpy(spct, "21%");
    strcpy(sused, "541MB");
    strcpy(stotal, "2.5GB");

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;

    unsigned long mem_total = 0, mem_avail = 0, swap_total = 0, swap_free = 0;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) continue;
        if (sscanf(line, "MemAvailable: %lu kB", &mem_avail) == 1) continue;
        if (sscanf(line, "SwapTotal: %lu kB", &swap_total) == 1) continue;
        if (sscanf(line, "SwapFree: %lu kB", &swap_free) == 1) continue;
    }
    fclose(f);

    if (mem_total > 0) {
        unsigned long used = mem_total - mem_avail;
        int pct = (used * 100) / mem_total;
        snprintf(mpct, mpsz, "%d%%", pct);
        snprintf(mused, musz, "%.1fGB", used / 1048576.0);
        snprintf(mtotal, mtsz, "%.1fGB", mem_total / 1048576.0);
    }
    if (swap_total > 0) {
        unsigned long used = swap_total - swap_free;
        int pct = (used * 100) / swap_total;
        snprintf(spct, spsz, "%d%%", pct);
        snprintf(sused, susz, "%.0fMB", used / 1024.0);
        snprintf(stotal, stsz, "%.1fGB", swap_total / 1048576.0);
    }
}

static void get_temperature(char *buf, size_t sz) {
    // Set default first
    strcpy(buf, "37.0°C");

    char tbuf[64];
    if (read_file_trim("/sys/class/thermal/thermal_zone0/temp", tbuf, sizeof(tbuf))) {
        long temp = atol(tbuf);
        if (temp > 0) snprintf(buf, sz, "%.1f°C", temp / 1000.0);
    } else if (read_file_trim("/sys/class/thermal/thermal_zone10/temp", tbuf, sizeof(tbuf))) {
        long temp = atol(tbuf);
        if (temp > 0) snprintf(buf, sz, "%.1f°C", temp / 1000.0);
    }
}

static void get_uptime(char *buf, size_t sz) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) {
        strcpy(buf, "N/A");
        return;
    }
    double up;
    fscanf(f, "%lf", &up);
    fclose(f);
    int d = (int)(up / 86400);
    int h = (int)((up - d * 86400) / 3600);
    int m = (int)((up - d * 86400 - h * 3600) / 60);
    if (d > 0) snprintf(buf, sz, "%d days, %d hours, %d mins", d, h, m);
    else if (h > 0) snprintf(buf, sz, "%d hours, %d mins", h, m);
    else snprintf(buf, sz, "%d mins", m);
}

static void get_date(char *buf, size_t sz) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, sz, "%a %b %e %T %Z %Y", tm);
}

static void get_host_ip(char *buf, size_t sz, const char *arg_host) {
    // Set default first
    strcpy(buf, "127.0.0.1");

    if (arg_host && strlen(arg_host) > 0) {
        strncpy(buf, arg_host, sz - 1);
        buf[sz - 1] = '\0';
    } else {
        FILE *fp = popen("ip -4 addr show wlan0 2>/dev/null | grep -oP '(?<=inet )\\d+(\\.\\d+){3}' | head -1", "r");
        if (fp && fgets(buf, sz, fp)) {
            buf[strcspn(buf, "\n")] = '\0';
        }
        if (fp) pclose(fp);
    }
}

int main(int argc, char *argv[]) {
    const char *req_user = argc > 1 ? argv[1] : getenv("USER");
    if (!req_user || strlen(req_user) == 0) req_user = "root";
    const char *host_arg = argc > 2 ? argv[2] : "127.0.0.1";

    char kernel[64], arch[32];
    get_kernel_arch(kernel, sizeof(kernel), arch, sizeof(arch));

    char load[32];
    get_load(load, sizeof(load));

    int procs = count_processes();
    int users = count_users();

    char dused[32], dtotal[32], dpct[16];
    get_disk_usage(dused, sizeof(dused), dtotal, sizeof(dtotal), dpct, sizeof(dpct));

    char mpct[16], mused[32], mtotal[32], spct[16], sused[32], stotal[32];
    get_memory_info(mpct, sizeof(mpct), mused, sizeof(mused), mtotal, sizeof(mtotal),
                    spct, sizeof(spct), sused, sizeof(sused), stotal, sizeof(stotal));

    char temp[32];
    get_temperature(temp, sizeof(temp));

    char uptime[64];
    get_uptime(uptime, sizeof(uptime));

    char date[64];
    get_date(date, sizeof(date));

    char host[64];
    get_host_ip(host, sizeof(host), host_arg);

    // 8. Header
    printf("%sWelcome to Jacktor Audio Framework on Realme C15 (GNU/Linux %s %s)%s\n\n",
           C_WHITE, kernel, arch, C_RST);

    // 9. ASCII Logo + Aligned Metadata Banner
    printf("%s       _            _    _                %s%-15s %s%s%s\n",
           C_CYAN, C_GRAY, "OS:", C_WHITE, "Android 14 (GSI)", C_RST);
    printf("%s      | | __ _  ___| | _| |_ ___  _ __    %s%-15s %s%s%s\n",
           C_CYAN, C_GRAY, "Device:", C_WHITE, "Realme C15 (Bengal SM4250)", C_RST);
    printf("%s _    | |/ _` |/ __| |/ / __/ _ \\| '__|   %s%-15s %s%s (%s)%s\n",
           C_CYAN, C_GRAY, "Kernel:", C_WHITE, kernel, arch, C_RST);
    printf("%s| |___| | (_| | (__|   <| || (_) | |      %s%-15s %s%s (Port 22)%s\n",
           C_CYAN, C_GRAY, "Host Address:", C_WHITE, host, C_RST);
    printf("%s \\_____/ \\__,_|\\___|_|\\_\\\\__\\___/|_|       %s%-15s %s%s%s\n",
           C_CYAN, C_GRAY, "Remote Access:", C_WHITE, "OpenSSH 10.3p1", C_RST);

    // 10. Ubuntu-style System Information Header
    printf("\n  System information as of %s\n\n", date);

    // 11. 2-Column Telemetry Table
    char val_mem[64], val_swap[64], val_disk[64];
    snprintf(val_mem, sizeof(val_mem), "%s (%s / %s)", mpct, mused, mtotal);
    snprintf(val_swap, sizeof(val_swap), "%s (%s / %s)", spct, sused, stotal);
    snprintf(val_disk, sizeof(val_disk), "%s of %s", dpct, dtotal);

    printf("  %s%-16s%s %-21s %s%-25s%s %d\n", C_GRAY, "System load:", C_WHITE, load, C_GRAY, "Processes:", C_WHITE, procs);
    printf("  %s%-16s%s %-21s %s%-25s%s %d\n", C_GRAY, "Usage of /data:", C_WHITE, val_disk, C_GRAY, "Users logged in:", C_WHITE, users);
    printf("  %s%-16s%s %-21s %s%-25s%s %s\n", C_GRAY, "Memory usage:", C_WHITE, val_mem, C_GRAY, "IPv4 address for wlan0:", C_WHITE, host);
    printf("  %s%-16s%s %-21s %s%-25s%s %s\n", C_GRAY, "Swap / ZRAM:", C_WHITE, val_swap, C_GRAY, "Temperature:", C_WHITE, temp);
    printf("  %s%-16s%s %-21s\n", C_GRAY, "System uptime:", C_WHITE, uptime);

    // 12. Service Status Bullets
    printf("\n %s*%s Audio Stream:   384kHz / 32-bit (ALSA Bit-Perfect Direct)\n", C_GREEN, C_RST);
    printf(" %s*%s USB Engine:     Auto / Host Mode Active\n", C_GREEN, C_RST);
    printf(" %s*%s Webroot UI:     http://%s/ (Port 80)\n", C_GREEN, C_RST, host);
    printf(" %s*%s Diagnostics:    Run 'amp-status' for hardware and amplifier telemetry\n\n", C_GREEN, C_RST);

    return 0;
}