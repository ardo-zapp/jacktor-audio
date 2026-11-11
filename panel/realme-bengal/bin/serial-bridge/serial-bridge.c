// Jacktor Audio - Native Serial Telemetry Multiplexer & Bridge Daemon (ARM64)
// Opens /dev/ttyUSB0 ONCE at 921,600 bps and multiplexes (broadcasts) full-duplex
// telemetry & commands across:
//   1. Multiple simultaneous TCP clients (127.0.0.1:7777 for Android App, cmd.py, etc.)
//   2. Virtual PTY (/dev/ttyESP for CLI / CGI scripts)
//   3. Unix Domain Socket (/data/local/tmp/jacktor_telemetry.sock)
//   4. Live telemetry cache (/data/local/tmp/amp_telemetry.json) & Screen Power Manager

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <poll.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define MAX_LINE 512
#define MAX_PATH 256
#define MAX_CLIENTS 32
#define LINE_BUF_SIZE 8192

#define MODDIR "/data/adb/modules/jacktor-audio-framework"
#define CONF_DIR MODDIR "/config"
#define PID_FILE "/data/local/tmp/serial_bridge.pid"
#define LOCK_FILE "/data/local/tmp/serial_bridge.lock"
#define LOG_FILE "/data/local/tmp/serial_bridge.log"
#define CONF_FILE CONF_DIR "/serial_bridge.conf"
#define TELEM_CACHE_FILE "/data/local/tmp/amp_telemetry.json"
#define TELEM_CACHE_TMP "/data/local/tmp/amp_telemetry.json.tmp"
#define DEFAULT_UNIX_SOCK "/data/local/tmp/jacktor_telemetry.sock"

static char g_hw_port[MAX_PATH] = "/dev/ttyUSB0";
static int g_baud = 921600;
static char g_mode[32] = "tcp_pty";
static int g_tcp_port = 7777;
static char g_pty_link[MAX_PATH] = "/dev/ttyESP";
static char g_unix_socket[MAX_PATH] = DEFAULT_UNIX_SOCK;
static int g_bridge_enabled = 1;
static pid_t g_bridge_pid = -1;

static volatile sig_atomic_t g_terminate = 0;

static void sig_handler(int sig) {
    (void)sig;
    g_terminate = 1;
}

static void write_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        ssize_t unused = write(fd, value, strlen(value));
        (void)unused;
        close(fd);
    }
}

static char *read_file_trim(const char *path, char *buf, size_t sz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    ssize_t n = read(fd, buf, sz - 1);
    close(fd);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    char *p = strchr(buf, '\n');
    if (p) *p = '\0';
    p = strchr(buf, '\r');
    if (p) *p = '\0';
    return buf;
}

static void log_msg(const char *msg) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char ts[64];
    strftime(ts, sizeof(ts), "[%Y-%m-%d %H:%M:%S]", tm_info);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "%s %s\n", ts, msg);
        fclose(f);
    }
}

static void load_config(void) {
    mkdir(CONF_DIR, 0755);

    if (access(CONF_FILE, F_OK) != 0) {
        FILE *f = fopen(CONF_FILE, "w");
        if (f) {
            fprintf(f,
                    "BRIDGE_ENABLED=1\n"
                    "HW_PORT=/dev/ttyUSB0\n"
                    "BAUD=921600\n"
                    "MODE=tcp_pty\n"
                    "TCP_PORT=7777\n"
                    "PTY_LINK=/dev/ttyESP\n"
                    "UNIX_SOCKET=%s\n",
                    DEFAULT_UNIX_SOCK);
            fclose(f);
        }
    }

    FILE *f = fopen(CONF_FILE, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        val[strcspn(val, "\r\n")] = '\0';

        if (strcmp(key, "BRIDGE_ENABLED") == 0) g_bridge_enabled = atoi(val);
        else if (strcmp(key, "HW_PORT") == 0 && strlen(val) > 0) snprintf(g_hw_port, sizeof(g_hw_port), "%s", val);
        else if (strcmp(key, "BAUD") == 0 && atoi(val) > 0) g_baud = atoi(val);
        else if (strcmp(key, "MODE") == 0 && strlen(val) > 0) snprintf(g_mode, sizeof(g_mode), "%s", val);
        else if (strcmp(key, "TCP_PORT") == 0 && atoi(val) > 0) g_tcp_port = atoi(val);
        else if (strcmp(key, "PTY_LINK") == 0 && strlen(val) > 0) snprintf(g_pty_link, sizeof(g_pty_link), "%s", val);
        else if (strcmp(key, "UNIX_SOCKET") == 0 && strlen(val) > 0) snprintf(g_unix_socket, sizeof(g_unix_socket), "%s", val);
    }
    fclose(f);
}

static void save_config(void) {
    mkdir(CONF_DIR, 0755);
    FILE *f = fopen(CONF_FILE, "w");
    if (!f) return;
    fprintf(f,
            "BRIDGE_ENABLED=%d\n"
            "HW_PORT=%s\n"
            "BAUD=%d\n"
            "MODE=%s\n"
            "TCP_PORT=%d\n"
            "PTY_LINK=%s\n"
            "UNIX_SOCKET=%s\n",
            g_bridge_enabled, g_hw_port, g_baud, g_mode, g_tcp_port, g_pty_link, g_unix_socket);
    fclose(f);
}

static int is_running(void) {
    char buf[64];
    if (read_file_trim(PID_FILE, buf, sizeof(buf))) {
        pid_t pid = atoi(buf);
        if (pid > 0 && pid != getpid() && kill(pid, 0) == 0) {
            g_bridge_pid = pid;
            return 1;
        }
    }
    return 0;
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 500000: return B500000;
        case 576000: return B576000;
        case 921600: return B921600;
#ifdef B1000000
        case 1000000: return B1000000;
#endif
#ifdef B1152000
        case 1152000: return B1152000;
#endif
#ifdef B1500000
        case 1500000: return B1500000;
#endif
#ifdef B2000000
        case 2000000: return B2000000;
#endif
#ifdef B3000000
        case 3000000: return B3000000;
#endif
        default: return B921600;
    }
}

static int configure_serial_fd(int fd, int baud) {
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        return -1;
    }

    cfmakeraw(&tio);
    speed_t spd = baud_to_speed(baud);
    cfsetispeed(&tio, spd);
    cfsetospeed(&tio, spd);

    tio.c_cflag |= (CLOCAL | CREAD | CS8);
    tio.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        return -1;
    }

    // Deassert DTR & RTS so ESP32 auto-reset circuit stays in normal run state (EN=HIGH)
    int status = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIC, &status);

    tcflush(fd, TCIOFLUSH);
    return 0;
}

static int open_hw_serial_port(ino_t *out_ino) {
    const char *candidates[5];
    candidates[0] = g_hw_port;
    candidates[1] = "/dev/ttyUSB0";
    candidates[2] = "/dev/ttyUSB1";
    candidates[3] = "/dev/ttyACM0";
    candidates[4] = NULL;

    for (int i = 0; candidates[i] != NULL; i++) {
        const char *dev = candidates[i];
        struct stat st;
        if (stat(dev, &st) != 0) continue;
        chmod(dev, 0666);

        int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0) {
            set_nonblocking(fd);
            configure_serial_fd(fd, g_baud);
            if (strcmp(g_hw_port, dev) != 0) {
                snprintf(g_hw_port, sizeof(g_hw_port), "%s", dev);
            }
            if (out_ino) *out_ino = st.st_ino;
            char msg[384];
            snprintf(msg, sizeof(msg), "Opened hardware UART %s @ %d bps (fd=%d, ino=%lu)",
                     dev, g_baud, fd, (unsigned long)st.st_ino);
            log_msg(msg);
            return fd;
        }
    }
    return -1;
}

static int is_serial_fd_healthy(int serial_fd, ino_t opened_ino) {
    if (serial_fd < 0) return 0;
    struct stat fst, pst;
    if (fstat(serial_fd, &fst) != 0) return 0;
    if (fst.st_nlink == 0) return 0; // Device node was unlinked (/dev/ttyUSB0 (deleted))
    if (stat(g_hw_port, &pst) != 0) return 0;
    if (opened_ino != 0 && pst.st_ino != opened_ino) return 0; // Re-enumerated with new inode
    return 1;
}

static int create_tcp_server(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(sock);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }

    if (listen(sock, 16) != 0) {
        close(sock);
        return -1;
    }

    return sock;
}

static int create_unix_server(const char *sock_path) {
    unlink(sock_path);
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    set_nonblocking(sock);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }

    chmod(sock_path, 0666);
    if (listen(sock, 8) != 0) {
        close(sock);
        return -1;
    }

    return sock;
}

// Keep slave_fd open inside the daemon so master_fd never gets endless POLLHUP when clients close /dev/ttyESP
static int create_pty_bridge(const char *link_path, int *out_slave_fd) {
    unlink(link_path);
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master_fd < 0) return -1;

    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        close(master_fd);
        return -1;
    }

    char *slave_name = ptsname(master_fd);
    if (!slave_name) {
        close(master_fd);
        return -1;
    }

    int slave_fd = open(slave_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (slave_fd >= 0) {
        struct termios tio;
        if (tcgetattr(slave_fd, &tio) == 0) {
            cfmakeraw(&tio);
            tcsetattr(slave_fd, TCSANOW, &tio);
        }
        if (out_slave_fd) *out_slave_fd = slave_fd;
    }

    chmod(slave_name, 0666);
    symlink(slave_name, link_path);
    chmod(link_path, 0666);
    set_nonblocking(master_fd);

    char msg[384];
    snprintf(msg, sizeof(msg), "Created virtual PTY %s -> %s", link_path, slave_name);
    log_msg(msg);
    return master_fd;
}

#define STAY_ON_NORMAL 0
#define STAY_ON_ALWAYS 1
#define STAY_ON_AUTO   2

static int get_stay_on_mode(void) {
    char buf[32];
    if (read_file_trim(CONF_DIR "/stay_on_mode", buf, sizeof(buf))) {
        if (strcmp(buf, "always") == 0 || strcmp(buf, "1") == 0 || strcmp(buf, "true") == 0) {
            return STAY_ON_ALWAYS;
        }
        if (strcmp(buf, "auto") == 0 || strcmp(buf, "2") == 0) {
            return STAY_ON_AUTO;
        }
        if (strcmp(buf, "normal") == 0 || strcmp(buf, "0") == 0 || strcmp(buf, "false") == 0) {
            return STAY_ON_NORMAL;
        }
    }
    if (read_file_trim(CONF_DIR "/stay_on_standby", buf, sizeof(buf))) {
        if (strcmp(buf, "always") == 0 || strcmp(buf, "1") == 0 || strcmp(buf, "true") == 0) {
            return STAY_ON_ALWAYS;
        }
    }
    return STAY_ON_AUTO;
}

static int extract_json_object(const char *src, const char *key, char *dest, size_t dest_size) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    const char *pos = strstr(src, search_key);
    if (!pos) return 0;
    pos += strlen(search_key);
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos != '{') return 0;

    const char *start = pos;
    int depth = 0;
    while (*pos) {
        if (*pos == '{') depth++;
        else if (*pos == '}') {
            depth--;
            if (depth == 0) {
                pos++;
                size_t len = pos - start;
                if (len < dest_size) {
                    memcpy(dest, start, len);
                    dest[len] = '\0';
                    return 1;
                }
                return 0;
            }
        }
        pos++;
    }
    return 0;
}

static void handle_telemetry_line(const char *line, int *last_amp_pwr) {
    if (!line || line[0] != '{') return;

    static char last_hz1_body[8192] = "";
    static char last_rt_body[8192] = "";
    static time_t last_cache_sec = 0;
    time_t now = time(NULL);

    int is_hz1 = (strstr(line, "\"hz1\"") != NULL);
    int is_rt = (strstr(line, "\"rt\"") != NULL);

    if (is_hz1) {
        extract_json_object(line, "hz1", last_hz1_body, sizeof(last_hz1_body));
    }
    if (is_rt) {
        extract_json_object(line, "rt", last_rt_body, sizeof(last_rt_body));
    }

    if (is_hz1 || is_rt || (now != last_cache_sec && strstr(line, "\"type\":\"telemetry\"") != NULL)) {
        last_cache_sec = now;
        FILE *tf = fopen(TELEM_CACHE_TMP, "w");
        if (tf) {
            if (last_hz1_body[0] && last_rt_body[0]) {
                fprintf(tf, "{\"type\":\"telemetry\",\"rt\":%s,\"hz1\":%s}\n", last_rt_body, last_hz1_body);
            } else if (last_hz1_body[0]) {
                fprintf(tf, "{\"type\":\"telemetry\",\"hz1\":%s}\n", last_hz1_body);
            } else if (last_rt_body[0]) {
                fprintf(tf, "{\"type\":\"telemetry\",\"rt\":%s}\n", last_rt_body);
            } else {
                fputs(line, tf);
                fputc('\n', tf);
            }
            fclose(tf);
            chmod(TELEM_CACHE_TMP, 0666);
            rename(TELEM_CACHE_TMP, TELEM_CACHE_FILE);
        }
    }

    int detected_pwr = -1;
    if (strstr(line, "\"type\":\"setup_progress\"") != NULL ||
        strstr(line, "\"type\":\"selftest_progress\"") != NULL ||
        strstr(line, "\"changed\":\"power\",\"value\":true") != NULL ||
        strstr(line, "\"pwr\":true") != NULL ||
        strstr(line, "\"states\":{\"on\":true") != NULL) {
        detected_pwr = 1;
    } else if (strstr(line, "\"changed\":\"power\",\"value\":false") != NULL ||
               strstr(line, "\"pwr\":false") != NULL ||
               strstr(line, "\"states\":{\"on\":false") != NULL) {
        detected_pwr = 0;
    }

    if (detected_pwr != -1 && detected_pwr != *last_amp_pwr) {
        *last_amp_pwr = detected_pwr;
        int mode = get_stay_on_mode();
        if (detected_pwr == 1) {
            if (mode == STAY_ON_ALWAYS || mode == STAY_ON_AUTO) {
                if (fork() == 0) {
                    system("settings put system screen_off_timeout 2147483647 2>/dev/null; "
                           "settings put global stay_on_while_plugged_in 7 2>/dev/null; "
                           "svc power stayon true 2>/dev/null; "
                           "input keyevent 224 2>/dev/null; "
                           "(dumpsys activity activities 2>/dev/null | grep -E 'mResumedActivity|topResumedActivity' | grep -q 'com.jacktor.audiopanel') || "
                           "am start -n com.jacktor.audiopanel/.MainActivity --activity-single-top >/dev/null 2>&1");
                    _exit(0);
                }
            } else {
                if (fork() == 0) {
                    system("svc power stayon false 2>/dev/null; "
                           "settings put global stay_on_while_plugged_in 0 2>/dev/null; "
                           "settings put system screen_off_timeout 60000 2>/dev/null; "
                           "input keyevent 224 2>/dev/null; "
                           "(dumpsys activity activities 2>/dev/null | grep -E 'mResumedActivity|topResumedActivity' | grep -q 'com.jacktor.audiopanel') || "
                           "am start -n com.jacktor.audiopanel/.MainActivity --activity-single-top >/dev/null 2>&1");
                    _exit(0);
                }
            }
        } else {
            if (mode == STAY_ON_ALWAYS) {
                if (fork() == 0) {
                    system("settings put system screen_off_timeout 2147483647 2>/dev/null; "
                           "settings put global stay_on_while_plugged_in 7 2>/dev/null; "
                           "svc power stayon true 2>/dev/null");
                    _exit(0);
                }
            } else {
                if (fork() == 0) {
                    system("svc power stayon false 2>/dev/null; "
                           "settings put global stay_on_while_plugged_in 0 2>/dev/null; "
                           "settings put system screen_off_timeout 60000 2>/dev/null");
                    _exit(0);
                }
            }
        }
    }
}

static void write_all_nonblocking(int fd, const char *buf, size_t len) {
    if (fd < 0 || len == 0) return;
    ssize_t unused = send(fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (unused < 0 && (errno == ENOTSOCK || errno == EOPNOTSUPP)) {
        unused = write(fd, buf, len);
    }
    (void)unused;
}

static void write_serial_complete(int serial_fd, const char *buf, size_t len) {
    if (serial_fd < 0 || len == 0) return;
    size_t sent = 0;
    int retries = 0;
    while (sent < len && retries < 25) {
        ssize_t n = write(serial_fd, buf + sent, len - sent);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(1000);
            retries++;
        } else {
            break;
        }
    }
}

static void run_multiplexer_daemon(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    int lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        log_msg("Another serial-bridge daemon instance is already holding lock; exiting duplicate.");
        if (lock_fd >= 0) close(lock_fd);
        _exit(0);
    }

    char self_pid_str[32];
    snprintf(self_pid_str, sizeof(self_pid_str), "%d", (int)getpid());
    write_file(PID_FILE, self_pid_str);

    int tcp_listen_fd = -1;
    int unix_listen_fd = -1;
    int pty_master_fd = -1;
    int pty_slave_fd = -1;
    int serial_fd = -1;
    ino_t serial_ino = 0;

    if (strcmp(g_mode, "pty") != 0) {
        tcp_listen_fd = create_tcp_server(g_tcp_port);
        if (tcp_listen_fd < 0) {
            log_msg("Failed to bind TCP port; aborting daemon startup.");
            close(lock_fd);
            _exit(1);
        }
    }
    unix_listen_fd = create_unix_server(DEFAULT_UNIX_SOCK);

    if (strcmp(g_mode, "tcp") != 0) {
        pty_master_fd = create_pty_bridge(g_pty_link, &pty_slave_fd);
    }

    serial_fd = open_hw_serial_port(&serial_ino);

    int client_fds[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_fds[i] = -1;
    }

    char line_buf[LINE_BUF_SIZE];
    size_t line_len = 0;
    int last_amp_pwr = -1;
    time_t last_health_check = time(NULL);
    time_t last_rx_time = time(NULL);

    while (!g_terminate) {
        time_t now = time(NULL);
        if (now != last_health_check) {
            last_health_check = now;
            if (serial_fd >= 0) {
                if (!is_serial_fd_healthy(serial_fd, serial_ino) || (now - last_rx_time > 20)) {
                    log_msg("Hardware UART inode changed or silent >20s; reopening UART port...");
                    close(serial_fd);
                    serial_fd = -1;
                    serial_ino = 0;
                }
            }
            if (serial_fd < 0) {
                serial_fd = open_hw_serial_port(&serial_ino);
                if (serial_fd >= 0) {
                    last_rx_time = now;
                }
            }
        }

        struct pollfd pfds[4 + MAX_CLIENTS];
        int pfd_map[4 + MAX_CLIENTS];
        int nfds = 0;

        if (serial_fd >= 0) {
            pfds[nfds].fd = serial_fd;
            pfds[nfds].events = POLLIN;
            pfd_map[nfds] = -10; // serial_fd
            nfds++;
        }
        if (tcp_listen_fd >= 0) {
            pfds[nfds].fd = tcp_listen_fd;
            pfds[nfds].events = POLLIN;
            pfd_map[nfds] = -11; // tcp_listen_fd
            nfds++;
        }
        if (unix_listen_fd >= 0) {
            pfds[nfds].fd = unix_listen_fd;
            pfds[nfds].events = POLLIN;
            pfd_map[nfds] = -12; // unix_listen_fd
            nfds++;
        }
        if (pty_master_fd >= 0) {
            pfds[nfds].fd = pty_master_fd;
            pfds[nfds].events = POLLIN;
            pfd_map[nfds] = -13; // pty_master_fd
            nfds++;
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] >= 0) {
                pfds[nfds].fd = client_fds[i];
                pfds[nfds].events = POLLIN;
                pfd_map[nfds] = i;
                nfds++;
            }
        }

        int ready = poll(pfds, (nfds_t)nfds, 250);
        if (ready <= 0) continue;

        for (int idx = 0; idx < nfds; idx++) {
            short revents = pfds[idx].revents;
            if (revents == 0) continue;

            int tag = pfd_map[idx];

            // 1. Hardware Serial Port (/dev/ttyUSB0)
            if (tag == -10) {
                if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    log_msg("Hardware UART POLLERR/POLLHUP, closing fd and waiting for reconnect...");
                    close(serial_fd);
                    serial_fd = -1;
                    serial_ino = 0;
                    continue;
                }
                if (revents & POLLIN) {
                    char rx_buf[4096];
                    ssize_t n = read(serial_fd, rx_buf, sizeof(rx_buf));
                    if (n > 0) {
                        last_rx_time = time(NULL);

                        // Assemble complete lines so every client receives atomic, unbroken JSON lines
                        for (ssize_t b = 0; b < n; b++) {
                            char ch = rx_buf[b];
                            if (ch == '\n' || ch == '\r') {
                                if (line_len > 0) {
                                    line_buf[line_len] = '\n';
                                    for (int c = 0; c < MAX_CLIENTS; c++) {
                                        if (client_fds[c] >= 0) {
                                            write_all_nonblocking(client_fds[c], line_buf, line_len + 1);
                                        }
                                    }
                                    line_buf[line_len] = '\0';
                                    handle_telemetry_line(line_buf, &last_amp_pwr);
                                    line_len = 0;
                                }
                            } else {
                                if (line_len < sizeof(line_buf) - 2) {
                                    line_buf[line_len++] = ch;
                                } else {
                                    line_len = 0;
                                }
                            }
                        }
                    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        close(serial_fd);
                        serial_fd = -1;
                        serial_ino = 0;
                    }
                }
            }
            // 2. New TCP Client Connection (Port 7777)
            else if (tag == -11) {
                if (revents & POLLIN) {
                    struct sockaddr_in caddr;
                    socklen_t clen = sizeof(caddr);
                    int cfd = accept(tcp_listen_fd, (struct sockaddr *)&caddr, &clen);
                    if (cfd >= 0) {
                        set_nonblocking(cfd);
                        int nodelay = 1;
                        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
                        int keepalive = 1;
                        setsockopt(cfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

                        int placed = 0;
                        for (int c = 0; c < MAX_CLIENTS; c++) {
                            if (client_fds[c] < 0) {
                                client_fds[c] = cfd;
                                placed = 1;
                                break;
                            }
                        }
                        if (!placed) {
                            close(cfd);
                        } else {
                            char cached[4096];
                            int tf = open(TELEM_CACHE_FILE, O_RDONLY);
                            if (tf >= 0) {
                                ssize_t cn = read(tf, cached, sizeof(cached) - 1);
                                close(tf);
                                if (cn > 0) {
                                    write_all_nonblocking(cfd, cached, (size_t)cn);
                                }
                            }
                        }
                    }
                }
            }
            // 3. New Unix Socket Client Connection
            else if (tag == -12) {
                if (revents & POLLIN) {
                    int cfd = accept(unix_listen_fd, NULL, NULL);
                    if (cfd >= 0) {
                        set_nonblocking(cfd);
                        int placed = 0;
                        for (int c = 0; c < MAX_CLIENTS; c++) {
                            if (client_fds[c] < 0) {
                                client_fds[c] = cfd;
                                placed = 1;
                                break;
                            }
                        }
                        if (!placed) {
                            close(cfd);
                        }
                    }
                }
            }
            // 4. PTY Master (/dev/ttyESP) TX -> Hardware UART
            else if (tag == -13) {
                if (revents & POLLIN) {
                    char tx_buf[1024];
                    ssize_t n = read(pty_master_fd, tx_buf, sizeof(tx_buf));
                    if (n > 0) {
                        if (serial_fd < 0) {
                            serial_fd = open_hw_serial_port(&serial_ino);
                        }
                        if (serial_fd >= 0) {
                            write_serial_complete(serial_fd, tx_buf, (size_t)n);
                        }
                    }
                }
            }
            // 5. Connected TCP / Unix Client TX -> Hardware UART
            else if (tag >= 0 && tag < MAX_CLIENTS) {
                int cfd = client_fds[tag];
                if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    close(cfd);
                    client_fds[tag] = -1;
                    continue;
                }
                if (revents & POLLIN) {
                    char tx_buf[2048];
                    ssize_t n = read(cfd, tx_buf, sizeof(tx_buf) - 2);
                    if (n > 0) {
                        if (tx_buf[n - 1] != '\n') {
                            tx_buf[n++] = '\n';
                        }
                        tx_buf[n] = '\0';
                        if (serial_fd < 0) {
                            serial_fd = open_hw_serial_port(&serial_ino);
                        }
                        if (serial_fd >= 0) {
                            write_serial_complete(serial_fd, tx_buf, (size_t)n);
                        }
                        if (strstr(tx_buf, "\"ping\"") == NULL && strstr(tx_buf, "\"get_commands\"") == NULL) {
                            char lmsg[384];
                            snprintf(lmsg, sizeof(lmsg), "TX -> UART (%zd bytes): %.320s", n, tx_buf);
                            lmsg[strcspn(lmsg, "\r\n")] = '\0';
                            log_msg(lmsg);
                        }
                    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        close(cfd);
                        client_fds[tag] = -1;
                    }
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] >= 0) close(client_fds[i]);
    }
    if (serial_fd >= 0) close(serial_fd);
    if (tcp_listen_fd >= 0) close(tcp_listen_fd);
    if (unix_listen_fd >= 0) close(unix_listen_fd);
    if (pty_slave_fd >= 0) close(pty_slave_fd);
    if (pty_master_fd >= 0) close(pty_master_fd);
    if (lock_fd >= 0) close(lock_fd);
    unlink(g_pty_link);
    unlink(DEFAULT_UNIX_SOCK);
    unlink(PID_FILE);
}

static void stop_bridge(void) {
    char buf[64];
    if (read_file_trim(PID_FILE, buf, sizeof(buf))) {
        pid_t pid = atoi(buf);
        if (pid > 0 && pid != getpid()) {
            kill(pid, SIGTERM);
            usleep(200000);
            kill(pid, SIGKILL);
        }
    }
    unlink(PID_FILE);
    unlink("/data/local/tmp/serial_bridge_pty.pid");
    unlink("/data/local/tmp/serial_telemetry.pid");
    unlink("/data/local/tmp/serial_bridge_unix.pid");

    char kill_cmd[256];
    snprintf(kill_cmd, sizeof(kill_cmd),
             "ps -ef | grep -E 'serial-bridge|socat.*(7777|ttyESP|ttyUSB|jacktor_telemetry|test_socket|test_listen)' | grep -v grep | awk '$2 != %d {print $2}' | xargs -r kill -9 2>/dev/null",
             (int)getpid());
    system(kill_cmd);

    unlink(g_pty_link);
    unlink(DEFAULT_UNIX_SOCK);
    printf("STOPPED\n");
}

static int start_bridge(void) {
    if (access("/data/local/tmp/jacktor_flashing.lock", F_OK) == 0) {
        printf("MAINTENANCE_LOCKED\n");
        return 0;
    }
    if (is_running()) {
        printf("ALREADY_RUNNING\n");
        return 0;
    }

    system("ps -ef | grep -E 'socat.*(7777|ttyESP|ttyUSB|jacktor_telemetry|test_socket|test_listen)' | grep -v grep | awk '{print $2}' | xargs -r kill -9 2>/dev/null");
    unlink(PID_FILE);
    unlink(g_pty_link);

    char msg[512];
    snprintf(msg, sizeof(msg), "Starting Jacktor Native Serial Bridge (%s) on %s @ %d bps (TCP:%d)...",
             g_mode, g_hw_port, g_baud, g_tcp_port);
    log_msg(msg);

    pid_t pid = fork();
    if (pid < 0) {
        printf("START_FAILED\n");
        return 1;
    }

    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        run_multiplexer_daemon();
        _exit(0);
    }

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    write_file(PID_FILE, pid_str);
    g_bridge_pid = pid;

    usleep(250000);
    if (!is_running()) {
        printf("START_FAILED\n");
        return 1;
    }

    printf("SUCCESS:%d\n", g_bridge_pid);
    return 0;
}

static void print_status(void) {
    int running = is_running();
    int clients = 0;
    int unix_clients = 0;
    char pid_str[32] = "0";

    if (running && g_bridge_pid > 0) {
        snprintf(pid_str, sizeof(pid_str), "%d", g_bridge_pid);
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "netstat -ant 2>/dev/null | grep \":%d \" | grep -c ESTABLISHED", g_tcp_port);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char buf[32];
            if (fgets(buf, sizeof(buf), fp)) clients = atoi(buf);
            pclose(fp);
        }

        snprintf(cmd, sizeof(cmd), "ss -x 2>/dev/null | grep \"%s\" | grep -c ESTAB", DEFAULT_UNIX_SOCK);
        fp = popen(cmd, "r");
        if (fp) {
            char buf[32];
            if (fgets(buf, sizeof(buf), fp)) unix_clients = atoi(buf);
            pclose(fp);
        }
    }

    int hw_exists = access(g_hw_port, F_OK) == 0;

    char ports_json[1024] = "";
    int first = 1;
    DIR *d = opendir("/dev");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strncmp(e->d_name, "ttyUSB", 6) == 0 || strncmp(e->d_name, "ttyACM", 6) == 0) {
                char dev_path[300];
                snprintf(dev_path, sizeof(dev_path), "/dev/%s", e->d_name);

                char name[512];
                snprintf(name, sizeof(name), "%s", dev_path);

                char syspath[320];
                snprintf(syspath, sizeof(syspath), "/sys/class/tty/%s/device/../product", e->d_name);
                char prod[128] = "", mfr[128] = "";
                read_file_trim(syspath, prod, sizeof(prod));
                snprintf(syspath, sizeof(syspath), "/sys/class/tty/%s/device/../manufacturer", e->d_name);
                read_file_trim(syspath, mfr, sizeof(mfr));

                if (strlen(prod) > 0) {
                    snprintf(name, sizeof(name), "%s - %s", dev_path, prod);
                    if (strlen(mfr) > 0 && strcmp(mfr, prod) != 0) {
                        strncat(name, " (", sizeof(name) - strlen(name) - 1);
                        strncat(name, mfr, sizeof(name) - strlen(name) - 1);
                        strncat(name, ")", sizeof(name) - strlen(name) - 1);
                    }
                }

                for (char *p = name; *p; p++) if (*p == '"') *p = '\'';

                if (!first) strcat(ports_json, ",");
                first = 0;
                char entry[768];
                snprintf(entry, sizeof(entry), "{\"port\":\"%s\",\"name\":\"%s\"}", dev_path, name);
                if (strlen(ports_json) + strlen(entry) < sizeof(ports_json) - 1) {
                    strcat(ports_json, entry);
                }
            }
        }
        closedir(d);
    }
    if (strlen(ports_json) == 0) {
        strcpy(ports_json, "{\"port\":\"/dev/ttyUSB0\",\"name\":\"/dev/ttyUSB0 (Generic Serial)\"}");
    }

    printf("{\n");
    printf("  \"running\": %d,\n", running);
    printf("  \"pid\": %s,\n", pid_str);
    printf("  \"enabled\": %d,\n", g_bridge_enabled);
    printf("  \"hw_port\": \"%s\",\n", g_hw_port);
    printf("  \"hw_exists\": %d,\n", hw_exists);
    printf("  \"baud\": \"%d\",\n", g_baud);
    printf("  \"mode\": \"%s\",\n", g_mode);
    printf("  \"tcp_port\": %d,\n", g_tcp_port);
    printf("  \"pty_link\": \"%s\",\n", g_pty_link);
    printf("  \"unix_socket\": \"%s\",\n", DEFAULT_UNIX_SOCK);
    printf("  \"clients_connected\": %d,\n", clients);
    printf("  \"unix_clients_connected\": %d,\n", unix_clients);
    printf("  \"available_ports\": [%s]\n", ports_json);
    printf("}\n");
}

static void print_usage(const char *prog) {
    printf("Usage: %s {start|stop|restart|status|autostart|set-baud|set-port}\n", prog);
}

int main(int argc, char *argv[]) {
    load_config();

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "start") == 0) {
        return start_bridge() ? 1 : 0;
    } else if (strcmp(cmd, "stop") == 0) {
        stop_bridge();
        return 0;
    } else if (strcmp(cmd, "restart") == 0) {
        stop_bridge();
        usleep(300000);
        return start_bridge() ? 1 : 0;
    } else if (strcmp(cmd, "status") == 0) {
        print_status();
        return 0;
    } else if (strcmp(cmd, "autostart") == 0) {
        if (g_bridge_enabled) return start_bridge() ? 1 : 0;
        return 0;
    } else if (strcmp(cmd, "set-baud") == 0 || strcmp(cmd, "baud") == 0) {
        if (argc < 3) {
            printf("Usage: %s set-baud <baud>\n", argv[0]);
            return 1;
        }
        g_baud = atoi(argv[2]);
        save_config();
        printf("BAUD_SET:%d\n", g_baud);
        if (is_running()) {
            stop_bridge();
            usleep(300000);
            start_bridge();
        }
        return 0;
    } else if (strcmp(cmd, "set-port") == 0 || strcmp(cmd, "set-node") == 0 || strcmp(cmd, "port") == 0) {
        if (argc < 3) {
            printf("Usage: %s set-port <device_path>\n", argv[0]);
            return 1;
        }
        snprintf(g_hw_port, sizeof(g_hw_port), "%s", argv[2]);
        save_config();
        printf("PORT_SET:%s\n", g_hw_port);
        if (is_running()) {
            stop_bridge();
            usleep(300000);
            start_bridge();
        }
        return 0;
    } else {
        print_usage(argv[0]);
        return 1;
    }
}
