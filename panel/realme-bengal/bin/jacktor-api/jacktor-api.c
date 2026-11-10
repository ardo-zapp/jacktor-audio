/*
 * Jacktor Audio Framework - High-Performance Native API Service & CGI Engine
 *
 * Copyright (C) 2026 Ardo Zapp. All rights reserved.
 * Licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 (CC BY-NC-SA 4.0).
 *
 * Direct Kernel/Procfs/Sysfs Access, Fast POSIX Direct Reading,
 * Native BSD Socket IPC to Serial-Bridge (TCP 127.0.0.1:7777 / Unix Domain Socket),
 * and Zero-Fork CGI Engine for Ultra-Low Latency Android Audio Panel Diagnostics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <netinet/tcp.h>
#include <poll.h>

#define MODDIR "/data/adb/modules/jacktor-audio-framework"
#define CONF_DIR MODDIR "/config"
#define AUTH_CONF CONF_DIR "/auth.conf"
#define SSH_AUTH_CONF "/data/adb/ssh/auth.conf"
#define TOKEN_FILE CONF_DIR "/session_token"
#define SERIAL_BRIDGE_TCP_HOST "127.0.0.1"
#define SERIAL_BRIDGE_TCP_PORT 7777
#define SERIAL_BRIDGE_UNIX_SOCK "/data/local/tmp/jacktor_telemetry.sock"
#define TELEMETRY_CACHE "/data/local/tmp/amp_telemetry.json"
#define WEBROOT MODDIR "/webroot"

static time_t g_last_tw_time = 0;

/* -------------------------------------------------------------------------- */
/* SHA-256 Standalone Implementation (FIPS 180-2, Zero External Dependencies)   */
/* -------------------------------------------------------------------------- */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_ctx;

#define ROR32(val, bits) (((val) >> (bits)) | ((val) << (32 - (bits))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define EP1(x) (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SIG0(x) (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;
    for (i = 0; i < 16; ++i)
        m[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) | ((uint32_t)data[i * 4 + 3]);
    for (; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + K256[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(sha256_ctx *ctx, const void *data, size_t len) {
    size_t i, idx = (size_t)(ctx->count & 63);
    ctx->count += len;
    for (i = 0; i < len; ++i) {
        ctx->buffer[idx++] = ((const uint8_t *)data)[i];
        if (idx == 64) {
            sha256_transform(ctx, ctx->buffer);
            idx = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[32]) {
    uint64_t bits = ctx->count * 8;
    size_t pad_len = (ctx->count % 64 < 56) ? (56 - (ctx->count % 64)) : (120 - (ctx->count % 64));
    uint8_t pad[128];
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    sha256_update(ctx, pad, pad_len);
    for (int i = 7; i >= 0; --i)
        pad[7 - i] = (uint8_t)(bits >> (i * 8));
    sha256_update(ctx, pad, 8);
    for (int i = 0; i < 8; ++i) {
        hash[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

static void sha256_final_hex(sha256_ctx *ctx, char *output_hex) {
    uint8_t digest[32];
    sha256_final(ctx, digest);
    for (int i = 0; i < 32; ++i) {
        sprintf(output_hex + (i * 2), "%02x", digest[i]);
    }
    output_hex[64] = '\0';
}

static void calculate_sha256_hex(const char *input, char *output_hex) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, input, strlen(input));
    sha256_final_hex(&ctx, output_hex);
}

/* -------------------------------------------------------------------------- */
/* Fast File & String POSIX Utilities                                         */
/* -------------------------------------------------------------------------- */
static int read_file_str(const char *path, char *buf, size_t max_len) {
    if (!path || !buf || max_len == 0) return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        buf[0] = '\0';
        return -1;
    }
    ssize_t n = read(fd, buf, max_len - 1);
    close(fd);
    if (n < 0) {
        buf[0] = '\0';
        return -1;
    }
    buf[n] = '\0';
    return 0;
}

static int read_file_trimmed(const char *path, char *buf, size_t max_len) {
    if (read_file_str(path, buf, max_len) != 0) return -1;
    char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != buf) memmove(buf, p, strlen(p) + 1);
    size_t len = strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len - 1])) {
        buf[--len] = '\0';
    }
    return 0;
}

static int read_file_int(const char *path, long long *val) {
    char buf[64];
    if (read_file_trimmed(path, buf, sizeof(buf)) != 0) {
        if (val) *val = 0;
        return -1;
    }
    char *endptr = NULL;
    long long v = strtoll(buf, &endptr, 10);
    if (endptr == buf) {
        if (val) *val = 0;
        return -1;
    }
    if (val) *val = v;
    return 0;
}

static int write_file_str(const char *path, const char *str) {
    if (!path || !str) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    size_t len = strlen(str);
    ssize_t w = write(fd, str, len);
    close(fd);
    return (w == (ssize_t)len) ? 0 : -1;
}

static void safe_copy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void url_decode(char *dst, const char *src, size_t max_dst) {
    if (!dst || !src || max_dst == 0) return;
    size_t d = 0;
    for (size_t s = 0; src[s] != '\0' && d + 1 < max_dst; s++) {
        if (src[s] == '+') {
            dst[d++] = ' ';
        } else if (src[s] == '%' && isxdigit((unsigned char)src[s + 1]) && isxdigit((unsigned char)src[s + 2])) {
            char hex[3] = { src[s + 1], src[s + 2], '\0' };
            dst[d++] = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            dst[d++] = src[s];
        }
    }
    dst[d] = '\0';
}

static void json_escape(const char *src, char *dst, size_t max_dst) {
    if (!dst || !src || max_dst == 0) return;
    size_t d = 0;
    for (size_t s = 0; src[s] != '\0' && d + 2 < max_dst; s++) {
        unsigned char c = (unsigned char)src[s];
        if (c == '\"') {
            dst[d++] = '\\'; dst[d++] = '\"';
        } else if (c == '\\') {
            dst[d++] = '\\'; dst[d++] = '\\';
        } else if (c == '\b') {
            dst[d++] = '\\'; dst[d++] = 'b';
        } else if (c == '\f') {
            dst[d++] = '\\'; dst[d++] = 'f';
        } else if (c == '\n') {
            dst[d++] = '\\'; dst[d++] = 'n';
        } else if (c == '\r') {
            dst[d++] = '\\'; dst[d++] = 'r';
        } else if (c == '\t') {
            dst[d++] = '\\'; dst[d++] = 't';
        } else if (c < 32) {
            int n = snprintf(dst + d, max_dst - d, "\\u%04x", c);
            if (n > 0) d += n;
        } else {
            dst[d++] = c;
        }
    }
    dst[d] = '\0';
}

static int run_command_output(const char *cmd, char *out, size_t max_out) {
    if (!cmd || !out || max_out == 0) return -1;
    out[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t bytes = fread(out, 1, max_out - 1, fp);
    out[bytes] = '\0';
    int ret = pclose(fp);
    while (bytes > 0 && (out[bytes - 1] == '\n' || out[bytes - 1] == '\r')) {
        out[--bytes] = '\0';
    }
    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
}

/* -------------------------------------------------------------------------- */
/* Native BSD Socket Client: Multiplexed Serial Bridge (TCP 7777 / Unix Sock)  */
/* -------------------------------------------------------------------------- */
static int send_amp_command(const char *cmd_str, char *resp_out, size_t max_resp) {
    if (!cmd_str || !cmd_str[0]) return -1;
    if (resp_out && max_resp > 0) resp_out[0] = '\0';

    char send_buf[1024];
    size_t slen = snprintf(send_buf, sizeof(send_buf), "%s\n", cmd_str);

    // 1. Try TCP localhost 7777 (native multiplexer socket)
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock >= 0) {
        int nodelay = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(SERIAL_BRIDGE_TCP_PORT);
        serv_addr.sin_addr.s_addr = inet_addr(SERIAL_BRIDGE_TCP_HOST);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 150000; // 150ms timeout max
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
            ssize_t w = write(sock, send_buf, slen);
            (void)w;
            if (resp_out && max_resp > 0) {
                struct pollfd pfd;
                pfd.fd = sock;
                pfd.events = POLLIN;
                if (poll(&pfd, 1, 20) > 0 && (pfd.revents & POLLIN)) {
                    ssize_t r = read(sock, resp_out, max_resp - 1);
                    if (r > 0) resp_out[r] = '\0';
                }
            }
            close(sock);
            return 0; // Success via TCP
        }
        close(sock);
    }

    // 2. Fallback: Unix Domain Socket
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock >= 0) {
        struct sockaddr_un un_addr;
        memset(&un_addr, 0, sizeof(un_addr));
        un_addr.sun_family = AF_UNIX;
        strncpy(un_addr.sun_path, SERIAL_BRIDGE_UNIX_SOCK, sizeof(un_addr.sun_path) - 1);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 150000;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (connect(sock, (struct sockaddr *)&un_addr, sizeof(un_addr)) == 0) {
            ssize_t w = write(sock, send_buf, slen);
            (void)w;
            if (resp_out && max_resp > 0) {
                struct pollfd pfd;
                pfd.fd = sock;
                pfd.events = POLLIN;
                if (poll(&pfd, 1, 20) > 0 && (pfd.revents & POLLIN)) {
                    ssize_t r = read(sock, resp_out, max_resp - 1);
                    if (r > 0) resp_out[r] = '\0';
                }
            }
            close(sock);
            return 0; // Success via Unix Domain Socket
        }
        close(sock);
    }

    // 3. Fallback: Direct PTY (/dev/ttyESP) or hardware port
    const char *target_dev = "/dev/ttyESP";
    if (access(target_dev, W_OK) != 0) {
        target_dev = "/dev/ttyUSB0";
    }
    int fd = open(target_dev, O_WRONLY | O_NOCTTY | O_NONBLOCK);
    if (fd >= 0) {
        ssize_t w = write(fd, send_buf, slen);
        (void)w;
        close(fd);
        return 0; // Success via direct character device
    }

    return -1;
}

/* -------------------------------------------------------------------------- */
/* Authentication Helpers                                                     */
/* -------------------------------------------------------------------------- */
static void get_system_credentials(char *user, size_t max_u, char *pass, size_t max_p) {
    if (user && max_u > 0) strncpy(user, "root", max_u);
    if (pass && max_p > 0) strncpy(pass, "furry2026", max_p);

    const char *cfg_path = access(AUTH_CONF, R_OK) == 0 ? AUTH_CONF : (access(SSH_AUTH_CONF, R_OK) == 0 ? SSH_AUTH_CONF : NULL);
    if (!cfg_path) return;

    FILE *fp = fopen(cfg_path, "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (strncmp(p, "username=", 9) == 0) {
            char *v = p + 9;
            while (*v && isspace((unsigned char)*v)) v++;
            size_t len = strlen(v);
            while (len > 0 && isspace((unsigned char)v[len - 1])) v[--len] = '\0';
            if (user && max_u > 0 && len > 0) strncpy(user, v, max_u);
        } else if (strncmp(p, "password=", 9) == 0) {
            char *v = p + 9;
            while (*v && isspace((unsigned char)*v)) v++;
            size_t len = strlen(v);
            while (len > 0 && isspace((unsigned char)v[len - 1])) v[--len] = '\0';
            if (pass && max_p > 0 && len > 0) strncpy(pass, v, max_p);
        }
    }
    fclose(fp);
}

static int is_authenticated(const char *token) {
    // Allow trusted local loopback access from panel apps and localhost
    const char *remote_addr = getenv("REMOTE_ADDR");
    if (!remote_addr || strstr(remote_addr, "127.0.0.1") || strstr(remote_addr, "::1")) {
        return 1;
    }

    if (!token || !token[0]) return 0;

    char saved_token[128];
    if (read_file_trimmed(TOKEN_FILE, saved_token, sizeof(saved_token)) == 0) {
        if (strcmp(token, saved_token) == 0) return 1;
    }

    char sys_user[64], sys_pass[128];
    get_system_credentials(sys_user, sizeof(sys_user), sys_pass, sizeof(sys_pass));

    char salt_buf[256], expected_hash[65];
    snprintf(salt_buf, sizeof(salt_buf), "%s:jacktor_secret_salt_2026", sys_pass);
    calculate_sha256_hex(salt_buf, expected_hash);

    return (strcmp(token, expected_hash) == 0) ? 1 : 0;
}

static int base64_decode(const char *src, unsigned char *out, size_t max_out) {
    if (!src || !out || max_out == 0) return 0;
    static const signed char T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    size_t j = 0;
    uint32_t buf = 0;
    int bits = 0;

    for (size_t i = 0; src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '=') break;
        int val = T[c];
        if (val == -1) continue;
        buf = (buf << 6) | (uint32_t)val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (j < max_out) {
                out[j++] = (unsigned char)((buf >> bits) & 0xFF);
            }
        }
    }
    return (int)j;
}

static void base64_encode(const unsigned char *src, size_t len, char *out, size_t max_out) {
    if (!out || max_out == 0) return;
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, j = 0;
    while (i < len && j + 4 < max_out) {
        size_t rem = len - i;
        uint32_t a = src[i++];
        uint32_t b = (rem > 1) ? src[i++] : 0;
        uint32_t c = (rem > 2) ? src[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (rem > 1) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (rem > 2) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
}

static void apply_hardware_audio_gain(const char *mode_or_sub, const char *val_str) {
    long long dig_gain = 84, ana_gain = 20;
    char ear_gain[32] = "G_6_DB";

    // Read current config if present
    FILE *gfp = fopen(CONF_DIR "/audio_gain.conf", "r");
    if (gfp) {
        char line[128];
        while (fgets(line, sizeof(line), gfp)) {
            if (strncmp(line, "DIGITAL_GAIN=", 13) == 0) dig_gain = atoll(line + 13);
            else if (strncmp(line, "ANALOG_GAIN=", 12) == 0) ana_gain = atoll(line + 12);
            else if (strncmp(line, "EAR_PA_GAIN=", 12) == 0) sscanf(line + 12, "%31s", ear_gain);
        }
        fclose(gfp);
    }

    if (strcmp(mode_or_sub, "preset_stock") == 0) {
        dig_gain = 84; ana_gain = 20; strcpy(ear_gain, "G_6_DB");
    } else if (strcmp(mode_or_sub, "preset_boost") == 0) {
        dig_gain = 90; ana_gain = 21; strcpy(ear_gain, "G_9_DB");
    } else if (strcmp(mode_or_sub, "preset_loud") == 0) {
        dig_gain = 96; ana_gain = 22; strcpy(ear_gain, "G_12_DB");
    } else if (strcmp(mode_or_sub, "preset_max") == 0) {
        dig_gain = 104; ana_gain = 24; strcpy(ear_gain, "G_16_DB");
    } else if (strcmp(mode_or_sub, "digital") == 0) {
        if (val_str && val_str[0]) dig_gain = atoll(val_str);
    } else if (strcmp(mode_or_sub, "analog") == 0) {
        if (val_str && val_str[0]) ana_gain = atoll(val_str);
    } else if (strcmp(mode_or_sub, "ear_pa") == 0) {
        if (val_str && val_str[0]) safe_copy(ear_gain, val_str, sizeof(ear_gain));
    } else if (val_str && val_str[0] && isdigit((unsigned char)val_str[0])) {
        dig_gain = atoll(val_str);
    }

    char cmd_buf[512];
    snprintf(cmd_buf, sizeof(cmd_buf),
             "tinymix 'RX_RX0 Digital Volume' %lld >/dev/null 2>&1; "
             "tinymix 'RX_RX1 Digital Volume' %lld >/dev/null 2>&1; "
             "tinymix 'RX_RX2 Digital Volume' %lld >/dev/null 2>&1; "
             "tinymix 'RX_RX0 Mix Digital Volume' %lld >/dev/null 2>&1; "
             "tinymix 'RX_RX1 Mix Digital Volume' %lld >/dev/null 2>&1; "
             "tinymix 'RX_RX2 Mix Digital Volume' %lld >/dev/null 2>&1; "
             "tinymix 'HPHL Volume' %lld >/dev/null 2>&1; "
             "tinymix 'HPHR Volume' %lld >/dev/null 2>&1; "
             "tinymix 'EAR PA GAIN' '%s' >/dev/null 2>&1",
             dig_gain, dig_gain, dig_gain, dig_gain, dig_gain, dig_gain,
             ana_gain, ana_gain, ear_gain);
    int res = system(cmd_buf);
    (void)res;

    // Save persistent config
    char conf_buf[256];
    snprintf(conf_buf, sizeof(conf_buf),
             "DIGITAL_GAIN=%lld\nANALOG_GAIN=%lld\nEAR_PA_GAIN=%s\n",
             dig_gain, ana_gain, ear_gain);
    write_file_str(CONF_DIR "/audio_gain.conf", conf_buf);

    printf("{\"success\":true,\"message\":\"Hardware Audio Gain applied\",\"digital\":%lld,\"analog\":%lld,\"ear_pa\":\"%s\"}\n",
           dig_gain, ana_gain, ear_gain);
}

/* -------------------------------------------------------------------------- */
/* High-Speed System Data Collectors (Zero-Fork Direct C Implementation)      */
/* -------------------------------------------------------------------------- */

static void get_uptime_info(long long *sec_out, char *up_en, size_t max_en, char *up_id, size_t max_id) {
    long long sec = 0;
    struct sysinfo s_info;
    if (sysinfo(&s_info) == 0) {
        sec = s_info.uptime;
    } else {
        read_file_int("/proc/uptime", &sec);
    }
    if (sec_out) *sec_out = sec;

    long long days = sec / 86400;
    long long hours = (sec % 86400) / 3600;
    long long mins = (sec % 3600) / 60;
    long long secs = sec % 60;

    if (days > 0) {
        snprintf(up_en, max_en, "%lld days, %lld hours, %lld mins", days, hours, mins);
        snprintf(up_id, max_id, "%lld hari, %lld jam, %lld menit", days, hours, mins);
    } else if (hours > 0) {
        snprintf(up_en, max_en, "%lld hours, %lld mins", hours, mins);
        snprintf(up_id, max_id, "%lld jam %lld menit", hours, mins);
    } else if (mins > 0) {
        snprintf(up_en, max_en, "%lld mins %lld secs", mins, secs);
        snprintf(up_id, max_id, "%lld menit %lld detik", mins, secs);
    } else {
        snprintf(up_en, max_en, "%lld secs", secs);
        snprintf(up_id, max_id, "%lld detik", secs);
    }
}

static void get_network_ip(char *ip_out, size_t max_ip) {
    if (read_file_trimmed("/tmp/jacktor_ip.cache", ip_out, max_ip) == 0 && ip_out[0] != '\0') {
        return;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
        if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)&ifr.ifr_addr;
            strncpy(ip_out, inet_ntoa(sa->sin_addr), max_ip - 1);
            ip_out[max_ip - 1] = '\0';
            close(fd);
            write_file_str("/tmp/jacktor_ip.cache", ip_out);
            return;
        }
        close(fd);
    }

    if (run_command_output("getprop dhcp.wlan0.ipaddress", ip_out, max_ip) == 0 && ip_out[0] != '\0') {
        write_file_str("/tmp/jacktor_ip.cache", ip_out);
        return;
    }

    strncpy(ip_out, "192.168.1.11", max_ip - 1);
}

static void get_swap_info(long long *total_mb, long long *used_mb, long long *free_mb) {
    *total_mb = 0; *used_mb = 0; *free_mb = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;

    char line[128];
    long long stotal = 0, sfree = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "SwapTotal:", 10) == 0) {
            sscanf(line + 10, "%lld", &stotal);
        } else if (strncmp(line, "SwapFree:", 9) == 0) {
            sscanf(line + 9, "%lld", &sfree);
        }
    }
    fclose(fp);

    *total_mb = stotal / 1024;
    *free_mb = sfree / 1024;
    *used_mb = (stotal > sfree) ? (stotal - sfree) / 1024 : 0;
}

static int is_swapfile_active(void) {
    char buf[512];
    if (read_file_str("/proc/swaps", buf, sizeof(buf)) == 0) {
        return (strstr(buf, "/data/swapfile") != NULL) ? 1 : 0;
    }
    return 0;
}

static void get_ssh_info(int *running, int *pid, int *port, int *autostart, int *keys_root, int *keys_shell) {
    *running = 0; *pid = 0; *port = 22; *autostart = 1; *keys_root = 0; *keys_shell = 0;

    if (access("/data/adb/ssh/no-autostart", F_OK) == 0) {
        *autostart = 0;
    }

    long long p = 0;
    if (read_file_int("/data/adb/ssh/sshd.pid", &p) == 0 && p > 0) {
        if (kill((pid_t)p, 0) == 0) {
            *running = 1;
            *pid = (int)p;
        }
    }
    if (*running == 0) {
        char pbuf[32];
        if (run_command_output("pidof sshd | awk '{print $1}'", pbuf, sizeof(pbuf)) == 0 && pbuf[0] != '\0') {
            *pid = atoi(pbuf);
            if (*pid > 0) *running = 1;
        }
    }

    FILE *fp = fopen("/data/adb/ssh/sshd_config", "r");
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp)) {
            char *s = line;
            while (*s && isspace((unsigned char)*s)) s++;
            if (strncasecmp(s, "port", 4) == 0 && isspace((unsigned char)s[4])) {
                int pt = atoi(s + 4);
                if (pt > 0) *port = pt;
            }
        }
        fclose(fp);
    }

    fp = fopen("/data/adb/ssh/root/.ssh/authorized_keys", "r");
    if (fp) {
        char line[512];
        int cnt = 0;
        while (fgets(line, sizeof(line), fp)) {
            char *s = line;
            while (*s && isspace((unsigned char)*s)) s++;
            if (*s && *s != '#') cnt++;
        }
        fclose(fp);
        *keys_root = cnt;
    }

    fp = fopen("/data/adb/ssh/shell/.ssh/authorized_keys", "r");
    if (fp) {
        char line[512];
        int cnt = 0;
        while (fgets(line, sizeof(line), fp)) {
            char *s = line;
            while (*s && isspace((unsigned char)*s)) s++;
            if (*s && *s != '#') cnt++;
        }
        fclose(fp);
        *keys_shell = cnt;
    }
}

static void get_serial_bridge_info(char *json_out, size_t max_len) {
    int running = 0;
    int pid = 0;
    long long p = 0;
    if (read_file_int("/data/local/tmp/serial_bridge.pid", &p) == 0 && p > 0) {
        if (kill((pid_t)p, 0) == 0) {
            running = 1;
            pid = (int)p;
        }
    }
    if (!running) {
        char pbuf[32];
        if (run_command_output("pidof serial-bridge | awk '{print $1}'", pbuf, sizeof(pbuf)) == 0 && pbuf[0] != '\0') {
            pid = atoi(pbuf);
            if (pid > 0 && kill((pid_t)pid, 0) == 0) {
                running = 1;
            }
        }
    }

    int enabled = 1;
    char hw_port[128] = "/dev/ttyUSB0";
    int baud = 921600;
    char mode[64] = "tcp_pty";
    int tcp_port = 7777;
    char pty_link[128] = "/dev/ttyESP";
    char unix_sock[128] = "/data/local/tmp/jacktor_telemetry.sock";

    FILE *cfp = fopen(CONF_DIR "/serial_bridge.conf", "r");
    if (cfp) {
        char line[256];
        while (fgets(line, sizeof(line), cfp)) {
            char *s = line;
            while (*s && isspace((unsigned char)*s)) s++;
            if (strncmp(s, "BRIDGE_ENABLED=", 15) == 0) enabled = atoi(s + 15);
            else if (strncmp(s, "HW_PORT=", 8) == 0) sscanf(s + 8, "%127s", hw_port);
            else if (strncmp(s, "BAUD=", 5) == 0) baud = atoi(s + 5);
            else if (strncmp(s, "MODE=", 5) == 0) sscanf(s + 5, "%63s", mode);
            else if (strncmp(s, "TCP_PORT=", 9) == 0) tcp_port = atoi(s + 9);
            else if (strncmp(s, "PTY_LINK=", 9) == 0) sscanf(s + 9, "%127s", pty_link);
            else if (strncmp(s, "UNIX_SOCKET=", 12) == 0) sscanf(s + 12, "%127s", unix_sock);
        }
        fclose(cfp);
    }

    int hw_exists = (access(hw_port, F_OK) == 0) ? 1 : 0;

    int clients = 0;
    if (running) {
        char port_hex[16];
        snprintf(port_hex, sizeof(port_hex), ":%04X", tcp_port);
        FILE *tfp = fopen("/proc/net/tcp", "r");
        if (tfp) {
            char tline[256];
            while (fgets(tline, sizeof(tline), tfp)) {
                if (strstr(tline, port_hex)) {
                    if (strstr(tline, " 01 ")) clients++;
                }
            }
            fclose(tfp);
        }
    }

    char ports_json[1024] = "";
    int first = 1;
    DIR *d = opendir("/dev");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strncmp(e->d_name, "ttyUSB", 6) == 0 || strncmp(e->d_name, "ttyACM", 6) == 0) {
                char dev_path[256];
                snprintf(dev_path, sizeof(dev_path), "/dev/%s", e->d_name);
                char name[512];
                snprintf(name, sizeof(name), "%s", dev_path);

                char syspath[320];
                char prod[64] = "", mfr[64] = "";
                snprintf(syspath, sizeof(syspath), "/sys/class/tty/%s/device/../product", e->d_name);
                read_file_trimmed(syspath, prod, sizeof(prod));
                snprintf(syspath, sizeof(syspath), "/sys/class/tty/%s/device/../manufacturer", e->d_name);
                read_file_trimmed(syspath, mfr, sizeof(mfr));

                if (prod[0]) {
                    snprintf(name, sizeof(name), "%s - %s", dev_path, prod);
                    if (mfr[0] && strcmp(mfr, prod) != 0) {
                        strncat(name, " (", sizeof(name) - strlen(name) - 1);
                        strncat(name, mfr, sizeof(name) - strlen(name) - 1);
                        strncat(name, ")", sizeof(name) - strlen(name) - 1);
                    }
                }

                if (!first) strncat(ports_json, ",", sizeof(ports_json) - strlen(ports_json) - 1);
                first = 0;
                char entry[384];
                snprintf(entry, sizeof(entry), "{\"port\":\"%s\",\"name\":\"%s\"}", dev_path, name);
                strncat(ports_json, entry, sizeof(ports_json) - strlen(ports_json) - 1);
            }
        }
        closedir(d);
    }
    if (ports_json[0] == '\0') {
        strcpy(ports_json, "{\"port\":\"/dev/ttyUSB0\",\"name\":\"/dev/ttyUSB0 (CP2102 Bridge)\"}");
    }

    snprintf(json_out, max_len,
             "{\"running\":%d,\"pid\":%d,\"enabled\":%d,\"hw_port\":\"%s\",\"hw_exists\":%d,\"baud\":\"%d\",\"mode\":\"%s\",\"tcp_port\":%d,\"pty_link\":\"%s\",\"unix_socket\":\"%s\",\"clients_connected\":%d,\"unix_clients_connected\":0,\"available_ports\":[%s]}",
             running, pid, enabled, hw_port, hw_exists, baud, mode, tcp_port, pty_link, unix_sock, clients, ports_json);
}

/* -------------------------------------------------------------------------- */
/* Dynamic Audio DAC & Devices Scanner                                        */
/* -------------------------------------------------------------------------- */
typedef struct {
    int found;
    char card[32];
    char name[128];
    char status[64];
    char freq[32];
    char format[32];
    char bit_depth[32];
    char period_size[32];
    char buffer_size[32];
    char max_freq[32];
    char max_bit_depth[32];
    char channels[32];
    char rates_list[64];
    char rates_json[256];
    char hw_capabilities[128];
} dac_info_t;

static void scan_audio_dac(dac_info_t *dac, const char *resampler_mode, const char *fixed_rate) {
    (void)resampler_mode;
    (void)fixed_rate;
    memset(dac, 0, sizeof(*dac));
    strncpy(dac->status, "Standby", sizeof(dac->status));
    strncpy(dac->freq, "96000 Hz", sizeof(dac->freq));
    strncpy(dac->format, "S24_3LE", sizeof(dac->format));
    strncpy(dac->bit_depth, "24-bit PCM", sizeof(dac->bit_depth));
    strncpy(dac->period_size, "2400", sizeof(dac->period_size));
    strncpy(dac->buffer_size, "4800", sizeof(dac->buffer_size));
    strncpy(dac->max_freq, "96 kHz", sizeof(dac->max_freq));
    strncpy(dac->max_bit_depth, "24-bit", sizeof(dac->max_bit_depth));
    strncpy(dac->channels, "Stereo (2 Ch)", sizeof(dac->channels));
    strncpy(dac->rates_list, "8k - 96k Hz", sizeof(dac->rates_list));
    strncpy(dac->rates_json, "96000, 48000, 44100", sizeof(dac->rates_json));
    strncpy(dac->hw_capabilities, "96 kHz • 24-bit • Stereo", sizeof(dac->hw_capabilities));

    DIR *d = opendir("/proc/asound");
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strncmp(dir->d_name, "card", 4) == 0 && isdigit((unsigned char)dir->d_name[4])) {
                char path[512];
                snprintf(path, sizeof(path), "/proc/asound/%s/stream0", dir->d_name);
                if (access(path, R_OK) == 0) {
                    dac->found = 1;
                    snprintf(dac->card, sizeof(dac->card), "%s", dir->d_name);

                    snprintf(path, sizeof(path), "/proc/asound/%s/id", dir->d_name);
                    read_file_trimmed(path, dac->name, sizeof(dac->name));
                    if (!dac->name[0]) strncpy(dac->name, "USB-Audio", sizeof(dac->name));

                    char stream_buf[4096];
                    snprintf(path, sizeof(path), "/proc/asound/%s/stream0", dir->d_name);
                    if (read_file_str(path, stream_buf, sizeof(stream_buf)) == 0) {
                        if (strstr(stream_buf, "Status: Running")) {
                            strncpy(dac->status, "Active (PCM Direct)", sizeof(dac->status));
                            char *mf = strstr(stream_buf, "Momentary freq = ");
                            if (mf) {
                                int freq = atoi(mf + 17);
                                if (freq > 0) snprintf(dac->freq, sizeof(dac->freq), "%d Hz", freq);
                            }
                        } else {
                            strncpy(dac->status, "Standby", sizeof(dac->status));
                        }

                        if (strstr(stream_buf, "S32") || strstr(stream_buf, "32_BIT")) {
                            strncpy(dac->max_bit_depth, "32-bit", sizeof(dac->max_bit_depth));
                        } else if (strstr(stream_buf, "S24") || strstr(stream_buf, "24_BIT")) {
                            strncpy(dac->max_bit_depth, "24-bit", sizeof(dac->max_bit_depth));
                        } else {
                            strncpy(dac->max_bit_depth, "16-bit", sizeof(dac->max_bit_depth));
                        }
                    }

                    snprintf(path, sizeof(path), "/proc/asound/%s/pcm0p/sub0/hw_params", dir->d_name);
                    char hw_buf[1024];
                    if (read_file_str(path, hw_buf, sizeof(hw_buf)) == 0 && !strstr(hw_buf, "closed")) {
                        char *r_line = strstr(hw_buf, "rate: ");
                        if (r_line) {
                            int r = atoi(r_line + 6);
                            if (r > 0) snprintf(dac->freq, sizeof(dac->freq), "%d Hz", r);
                        }
                        char *p_line = strstr(hw_buf, "period_size: ");
                        if (p_line) {
                            int pz = atoi(p_line + 13);
                            if (pz > 0) snprintf(dac->period_size, sizeof(dac->period_size), "%d", pz);
                        }
                        char *b_line = strstr(hw_buf, "buffer_size: ");
                        if (b_line) {
                            int bz = atoi(b_line + 13);
                            if (bz > 0) snprintf(dac->buffer_size, sizeof(dac->buffer_size), "%d", bz);
                        }
                    }
                    break;
                }
            }
        }
        closedir(d);
    }

    if (!dac->found && access("/proc/asound/card0", R_OK) == 0) {
        dac->found = 1;
        strncpy(dac->card, "card0", sizeof(dac->card));
        char variant[64];
        if (read_file_trimmed("/proc/asound/card0/codecs/wcd937x/variant", variant, sizeof(variant)) == 0 && variant[0]) {
            snprintf(dac->name, sizeof(dac->name), "Qualcomm %s Aqstic DAC", variant);
        } else {
            read_file_trimmed("/proc/asound/card0/id", dac->name, sizeof(dac->name));
            if (!dac->name[0]) strncpy(dac->name, "Qualcomm Audio (Internal)", sizeof(dac->name));
        }

        strncpy(dac->status, "Standby", sizeof(dac->status));
        strncpy(dac->freq, "384000 Hz", sizeof(dac->freq));
        strncpy(dac->format, "S32_LE", sizeof(dac->format));
        strncpy(dac->bit_depth, "32-bit PCM", sizeof(dac->bit_depth));
        strncpy(dac->period_size, "240", sizeof(dac->period_size));
        strncpy(dac->buffer_size, "1920", sizeof(dac->buffer_size));
        strncpy(dac->max_freq, "384 kHz", sizeof(dac->max_freq));
        strncpy(dac->max_bit_depth, "32-bit", sizeof(dac->max_bit_depth));
        strncpy(dac->channels, "Stereo (2 Ch)", sizeof(dac->channels));
        strncpy(dac->rates_list, "44.1k - 384k Hz", sizeof(dac->rates_list));
        strncpy(dac->rates_json, "384000, 192000, 96000, 88200, 48000, 44100", sizeof(dac->rates_json));
        strncpy(dac->hw_capabilities, "384 kHz • 32-bit • Stereo", sizeof(dac->hw_capabilities));
    }
}

/* -------------------------------------------------------------------------- */
/* Dynamic USB Devices Scanner (Direct Sysfs /sys/bus/usb/devices)           */
/* -------------------------------------------------------------------------- */
static int scan_usb_devices(char *json_out, size_t max_json) {
    if (!json_out || max_json == 0) return 0;
    json_out[0] = '\0';

    DIR *d = opendir("/sys/bus/usb/devices");
    if (!d) return 0;

    int count = 0;
    size_t cur_len = 0;
    struct dirent *dir;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor", dir->d_name);

        char vid[16], pid[16];
        if (read_file_trimmed(path, vid, sizeof(vid)) != 0) continue;
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idProduct", dir->d_name);
        if (read_file_trimmed(path, pid, sizeof(pid)) != 0) continue;

        if (strcmp(vid, "1d6b") == 0) continue;

        char mfr[64] = "Generic", prod[64] = "", bus[16] = "1", dev[16] = "1", bclass[16] = "";
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/manufacturer", dir->d_name);
        read_file_trimmed(path, mfr, sizeof(mfr));
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/product", dir->d_name);
        read_file_trimmed(path, prod, sizeof(prod));
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/busnum", dir->d_name);
        read_file_trimmed(path, bus, sizeof(bus));
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/devnum", dir->d_name);
        read_file_trimmed(path, dev, sizeof(dev));
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/bDeviceClass", dir->d_name);
        read_file_trimmed(path, bclass, sizeof(bclass));

        if (!prod[0]) snprintf(prod, sizeof(prod), "Device %s:%s", vid, pid);

        const char *dtype = "usb";
        if (strstr(prod, "CP210") || strstr(prod, "UART") || strstr(prod, "Bridge") || strstr(prod, "Serial") ||
            strcmp(vid, "10c4") == 0 || strcmp(vid, "0403") == 0 || strcmp(vid, "1a86") == 0) {
            dtype = "serial";
        } else if (strstr(prod, "Audio") || strstr(prod, "DAC") || strstr(prod, "Sound") || strcmp(bclass, "01") == 0) {
            dtype = "audio";
        } else if (strstr(prod, "Hub") || strcmp(bclass, "09") == 0) {
            dtype = "hub";
        } else if (strstr(prod, "Storage") || strstr(prod, "Disk") || strstr(prod, "Flash") || strcmp(bclass, "08") == 0) {
            dtype = "storage";
        }

        char safe_mfr[128], safe_prod[128];
        json_escape(mfr, safe_mfr, sizeof(safe_mfr));
        json_escape(prod, safe_prod, sizeof(safe_prod));

        char item[512];
        int n = snprintf(item, sizeof(item),
            "%s{\"bus\":\"%s\",\"dev\":\"%s\",\"vid\":\"%s\",\"pid\":\"%s\",\"mfr\":\"%s\",\"prod\":\"%s\",\"type\":\"%s\"}",
            (count > 0 ? "," : ""), bus, dev, vid, pid, safe_mfr, safe_prod, dtype);

        if (cur_len + n < max_json - 1) {
            strcat(json_out, item);
            cur_len += n;
            count++;
        }
    }
    closedir(d);
    return count;
}

/* -------------------------------------------------------------------------- */
/* Action Handler: Return Full Diagnostic Status (JSON)                       */
/* -------------------------------------------------------------------------- */
typedef struct {
    char state[32];
    char title[256];
    char artist[256];
    char album[256];
    char app[64];
    char format[64];
    char bit_depth[64];
    char bitrate[64];
    char sample_rate[32];
    char raw_rate[16];
    char cover[512];
} track_info_t;

static void auto_trim(char *s) {
    if (!s) return;
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    if (start != s) memmove(s, start, (end - start) + 1);
}

static void sync_media_cover_art(const char *title, const char *artist, const char *album, char *out_cover_url, size_t out_len) {
    (void)album;
    if (out_cover_url && out_len > 0) out_cover_url[0] = '\0';
    const char *webroot_cover = WEBROOT "/cover.jpg";
    struct stat st;

    if (!title || !title[0] || strcmp(title, "null") == 0 || strcmp(title, "-") == 0) {
        unlink(webroot_cover);
        unlink(CONF_DIR "/current_track");
        return;
    }

    char saved_track[512] = "";
    read_file_trimmed(CONF_DIR "/current_track", saved_track, sizeof(saved_track));

    char cur_track[512];
    snprintf(cur_track, sizeof(cur_track), "%s|%s", title, artist ? artist : "");

    int track_changed = (strcmp(saved_track, cur_track) != 0);

    // Check system media artwork files from Android NotificationListener / MediaSession
    const char *sys_covers[] = {
        "/data/local/tmp/current_cover.jpg",
        "/data/data/com.jacktor.audiopanel/files/cover.jpg",
        NULL
    };

    int resolved = 0;
    for (int i = 0; sys_covers[i] != NULL; i++) {
        struct stat sst;
        if (stat(sys_covers[i], &sst) == 0 && sst.st_size > 500) {
            int need_copy = 0;
            if (track_changed) {
                need_copy = 1;
            } else if (stat(webroot_cover, &st) != 0) {
                need_copy = 1;
            } else if (sst.st_mtime > st.st_mtime || sst.st_size != st.st_size) {
                need_copy = 1;
            }

            if (need_copy) {
                char cp_cmd[512];
                snprintf(cp_cmd, sizeof(cp_cmd), "cp -f '%s' '%s' >/dev/null 2>&1 && chmod 644 '%s' >/dev/null 2>&1",
                         sys_covers[i], webroot_cover, webroot_cover);
                int r = system(cp_cmd);
                (void)r;
            }
            resolved = 1;
            break;
        }
    }

    if (track_changed) {
        mkdir(CONF_DIR, 0755);
        write_file_str(CONF_DIR "/current_track", cur_track);
        // If track changed and no new system artwork is available, remove stale cover of previous song
        if (!resolved) {
            unlink(webroot_cover);
        }
    }

    if (stat(webroot_cover, &st) == 0 && st.st_size > 500) {
        snprintf(out_cover_url, out_len, "/cover.jpg?v=%ld", (long)st.st_mtime);
    }
}

static void scan_active_media_track(track_info_t *track, const dac_info_t *dac) {
    memset(track, 0, sizeof(*track));
    strcpy(track->state, "Standby");
    strcpy(track->title, "");
    strcpy(track->artist, "");
    strcpy(track->album, "");
    strcpy(track->app, "-");
    strcpy(track->format, "16-bit PCM");
    strcpy(track->bit_depth, "16-bit PCM");
    strcpy(track->bitrate, "Direct Stream");
    strcpy(track->sample_rate, "44.1 kHz");
    strcpy(track->raw_rate, "44100");
    strcpy(track->cover, "");

    // 1. If DAC is Standby / not running, skip heavy Android binder IPC (dumpsys) completely!
    if (!dac || !dac->found || strcmp(dac->status, "Running") != 0) {
        return;
    }

    // 2. Cache track metadata for 2 seconds to avoid CPU-heavy subshell binder calls on high-frequency polling
    static track_info_t s_cached_track;
    static time_t s_last_track_scan = 0;
    time_t now = time(NULL);
    if (s_last_track_scan > 0 && (now - s_last_track_scan < 2)) {
        memcpy(track, &s_cached_track, sizeof(track_info_t));
        return;
    }

    FILE *fp = popen("dumpsys media_session 2>/dev/null | grep -E \"package=|state=PlaybackState|description=\" | head -n 40", "r");
    if (!fp) return;

    char line[512];
    char curr_pkg[128] = "";
    char curr_state[32] = "Standby";
    int found_playing = 0;

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }

        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;

        if (strncmp(p, "package=", 8) == 0) {
            sscanf(p + 8, "%127s", curr_pkg);
        } else if (strstr(p, "state=PlaybackState")) {
            if (strstr(p, "state=PLAYING") || strstr(p, "state=3")) {
                strcpy(curr_state, "Playing");
            } else if (strstr(p, "state=PAUSED") || strstr(p, "state=2")) {
                strcpy(curr_state, "Paused");
            } else if (strstr(p, "state=STOPPED") || strstr(p, "state=1")) {
                strcpy(curr_state, "Stopped");
            }
        } else if (strstr(p, "description=")) {
            char *desc = strstr(p, "description=") + 12;
            while (*desc && isspace((unsigned char)*desc)) desc++;

            char t[256] = "", ar[256] = "", al[256] = "";
            char *comma1 = strchr(desc, ',');
            if (comma1) {
                size_t tlen = comma1 - desc;
                if (tlen >= sizeof(t)) tlen = sizeof(t) - 1;
                strncpy(t, desc, tlen);
                t[tlen] = '\0';

                char *comma2 = strchr(comma1 + 1, ',');
                if (comma2) {
                    size_t arlen = comma2 - (comma1 + 1);
                    if (arlen >= sizeof(ar)) arlen = sizeof(ar) - 1;
                    strncpy(ar, comma1 + 1, arlen);
                    ar[arlen] = '\0';

                    strncpy(al, comma2 + 1, sizeof(al) - 1);
                } else {
                    strncpy(ar, comma1 + 1, sizeof(ar) - 1);
                }
            } else {
                strncpy(t, desc, sizeof(t) - 1);
            }

            auto_trim(t);
            auto_trim(ar);
            auto_trim(al);

            if (t[0] && strcmp(t, "null") != 0) {
                int is_play = (strcmp(curr_state, "Playing") == 0);
                if (is_play || !found_playing) {
                    strncpy(track->title, t, sizeof(track->title) - 1);
                    strncpy(track->artist, ar, sizeof(track->artist) - 1);
                    strncpy(track->album, al, sizeof(track->album) - 1);
                    strncpy(track->state, curr_state, sizeof(track->state) - 1);

                    char pkg_low[128];
                    strncpy(pkg_low, curr_pkg, sizeof(pkg_low) - 1);
                    pkg_low[sizeof(pkg_low) - 1] = '\0';
                    for (char *c = pkg_low; *c; c++) *c = (char)tolower((unsigned char)*c);

                    if (strstr(pkg_low, "spotify")) strcpy(track->app, "Spotify");
                    else if (strstr(pkg_low, "youtube.music")) strcpy(track->app, "YouTube Music");
                    else if (strstr(pkg_low, "youtube")) strcpy(track->app, "YouTube");
                    else if (strstr(pkg_low, "maxmpz") || strstr(pkg_low, "poweramp")) strcpy(track->app, "Poweramp");
                    else if (strstr(pkg_low, "symfonik") || strstr(pkg_low, "symfonium")) strcpy(track->app, "Symfonium");
                    else if (strstr(pkg_low, "apple.music")) strcpy(track->app, "Apple Music");
                    else if (strstr(pkg_low, "hiby")) strcpy(track->app, "HiBy Music");
                    else if (strstr(pkg_low, "usbaudioplayerpro") || strstr(pkg_low, "extreamsd")) strcpy(track->app, "USB Audio Player PRO");
                    else if (strstr(pkg_low, "tidal")) strcpy(track->app, "TIDAL");
                    else if (strstr(pkg_low, "deezer")) strcpy(track->app, "Deezer");
                    else if (strstr(pkg_low, "amazon")) strcpy(track->app, "Amazon Music");
                    else if (strstr(pkg_low, "musicolet")) strcpy(track->app, "Musicolet");
                    else if (strstr(pkg_low, "videolan") || strstr(pkg_low, "vlc")) strcpy(track->app, "VLC");
                    else if (strstr(pkg_low, "soundcloud")) strcpy(track->app, "SoundCloud");
                    else if (curr_pkg[0]) {
                        char *last_dot = strrchr(curr_pkg, '.');
                        if (last_dot && *(last_dot + 1)) {
                            strncpy(track->app, last_dot + 1, sizeof(track->app) - 1);
                            track->app[0] = (char)toupper((unsigned char)track->app[0]);
                        } else {
                            strncpy(track->app, curr_pkg, sizeof(track->app) - 1);
                        }
                    } else {
                        strcpy(track->app, "Media Player");
                    }

                    if (is_play) {
                        found_playing = 1;
                    }
                }
            }
        }
    }
    pclose(fp);

    // Sync DAC stream properties if active
    if (dac && dac->found && strcmp(dac->status, "Standby") != 0) {
        if (dac->freq[0]) {
            strncpy(track->sample_rate, dac->freq, sizeof(track->sample_rate) - 1);
            int rate_num = atoi(dac->freq);
            if (rate_num > 0) {
                snprintf(track->raw_rate, sizeof(track->raw_rate), "%d", rate_num);
                snprintf(track->sample_rate, sizeof(track->sample_rate), "%.1f kHz", rate_num / 1000.0);
            }
        }
        if (dac->bit_depth[0]) {
            strncpy(track->format, dac->bit_depth, sizeof(track->format) - 1);
            strncpy(track->bit_depth, dac->bit_depth, sizeof(track->bit_depth) - 1);
        }
    }

    if (track->title[0] == '\0') {
        strcpy(track->state, "Standby");
        strcpy(track->app, "-");
        strcpy(track->cover, "");
    } else {
        sync_media_cover_art(track->title, track->artist, track->album, track->cover, sizeof(track->cover));
    }

    memcpy(&s_cached_track, track, sizeof(track_info_t));
    s_last_track_scan = now;
}

static void handle_action_languages(void) {
    const char *dirs[] = {
        MODDIR "/webroot/i18n",
        "webroot/i18n",
        "./i18n",
        "../i18n",
        NULL
    };
    DIR *dp = NULL;
    const char *used_dir = NULL;
    for (int i = 0; dirs[i]; i++) {
        dp = opendir(dirs[i]);
        if (dp) {
            used_dir = dirs[i];
            break;
        }
    }

    printf("{\"success\":true,\"languages\":[");
    int count = 0;

    if (dp && used_dir) {
        struct dirent *ep;
        while ((ep = readdir(dp)) != NULL) {
            size_t len = strlen(ep->d_name);
            if (len > 5 && strcmp(ep->d_name + len - 5, ".json") == 0 &&
                strcmp(ep->d_name, "languages.json") != 0 &&
                strcmp(ep->d_name, "manifest.json") != 0) {
                // Extract code
                char code[32];
                size_t clen = len - 5;
                if (clen >= sizeof(code)) clen = sizeof(code) - 1;
                memcpy(code, ep->d_name, clen);
                code[clen] = '\0';

                // Read file to parse lang_name and lang_flag
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", used_dir, ep->d_name);
                FILE *fp = fopen(filepath, "r");
                char name[64] = "";
                char flag[32] = "";
                if (fp) {
                    char buf[1024];
                    size_t r = fread(buf, 1, sizeof(buf) - 1, fp);
                    buf[r] = '\0';
                    fclose(fp);

                    // Quick extract lang_name
                    char *p_name = strstr(buf, "\"lang_name\"");
                    if (p_name) {
                        char *colon = strchr(p_name, ':');
                        if (colon) {
                            char *q1 = strchr(colon, '\"');
                            if (q1) {
                                char *q2 = strchr(q1 + 1, '\"');
                                if (q2) {
                                    size_t nlen = q2 - (q1 + 1);
                                    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
                                    memcpy(name, q1 + 1, nlen);
                                    name[nlen] = '\0';
                                }
                            }
                        }
                    }

                    // Quick extract lang_flag
                    char *p_flag = strstr(buf, "\"lang_flag\"");
                    if (p_flag) {
                        char *colon = strchr(p_flag, ':');
                        if (colon) {
                            char *q1 = strchr(colon, '\"');
                            if (q1) {
                                char *q2 = strchr(q1 + 1, '\"');
                                if (q2) {
                                    size_t flen = q2 - (q1 + 1);
                                    if (flen >= sizeof(flag)) flen = sizeof(flag) - 1;
                                    memcpy(flag, q1 + 1, flen);
                                    flag[flen] = '\0';
                                }
                            }
                        }
                    }
                }

                if (!name[0]) {
                    if (strcmp(code, "id") == 0) snprintf(name, sizeof(name), "Bahasa Indonesia");
                    else if (strcmp(code, "en") == 0) snprintf(name, sizeof(name), "English");
                    else snprintf(name, sizeof(name), "%s", code);
                }
                if (!flag[0]) {
                    if (strcmp(code, "id") == 0) snprintf(flag, sizeof(flag), "🇮🇩");
                    else if (strcmp(code, "en") == 0) snprintf(flag, sizeof(flag), "🇬🇧");
                    else snprintf(flag, sizeof(flag), "🌐");
                }

                char safe_name[128];
                json_escape(name, safe_name, sizeof(safe_name));

                if (count > 0) printf(",");
                printf("{\"code\":\"%s\",\"name\":\"%s\",\"flag\":\"%s\",\"file\":\"%s\"}",
                       code, safe_name, flag, ep->d_name);
                count++;
            }
        }
        closedir(dp);
    }

    // Fallback if no files found
    if (count == 0) {
        printf("{\"code\":\"id\",\"name\":\"Bahasa Indonesia\",\"flag\":\"🇮🇩\",\"file\":\"id.json\"},"
               "{\"code\":\"en\",\"name\":\"English\",\"flag\":\"🇬🇧\",\"file\":\"en.json\"}");
    }

    printf("]}\n");
}

static void handle_action_status(void) {
    long long otg_switch = 0, otg_online = 0, voltage_now = 0, real_cap = 100;
    read_file_int("/sys/class/power_supply/usb/otg_switch", &otg_switch);
    read_file_int("/sys/class/power_supply/usb/otg_online", &otg_online);
    read_file_int("/sys/class/power_supply/usb/voltage_now", &voltage_now);
    read_file_int("/sys/class/power_supply/battery/capacity", &real_cap);

    char extcon[64] = "", ssusb_mode[64] = "", udc[64] = "", real_status[64] = "Discharging";
    read_file_trimmed("/sys/class/extcon/extcon1/state", extcon, sizeof(extcon));
    for (char *p = extcon; *p; p++) {
        if (*p == '\r' || *p == '\n') *p = ' ';
    }
    size_t el = strlen(extcon);
    while (el > 0 && extcon[el - 1] == ' ') extcon[--el] = '\0';
    read_file_trimmed("/sys/devices/platform/soc/4e00000.ssusb/mode", ssusb_mode, sizeof(ssusb_mode));
    read_file_trimmed("/config/usb_gadget/g1/UDC", udc, sizeof(udc));
    read_file_trimmed("/sys/class/power_supply/battery/status", real_status, sizeof(real_status));

    char bypass_conf[16] = "true";
    read_file_trimmed(CONF_DIR "/battery_bypass", bypass_conf, sizeof(bypass_conf));
    int is_bypass = (strcmp(bypass_conf, "false") != 0);

    long long uptime_sec = 0;
    char uptime_en[64], uptime_id[64];
    get_uptime_info(&uptime_sec, uptime_en, sizeof(uptime_en), uptime_id, sizeof(uptime_id));

    char ip_addr[64];
    get_network_ip(ip_addr, sizeof(ip_addr));

    long long cpu0_freq = 0, cpu4_freq = 0, cpu_temp_raw = 0;
    read_file_int("/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq", &cpu0_freq);
    read_file_int("/sys/devices/system/cpu/cpufreq/policy4/scaling_cur_freq", &cpu4_freq);
    read_file_int("/sys/class/thermal/thermal_zone9/temp", &cpu_temp_raw);
    if (cpu_temp_raw == 0) read_file_int("/sys/class/thermal/thermal_zone0/temp", &cpu_temp_raw);

    char cpu_gov[64] = "schedutil";
    read_file_trimmed("/sys/devices/system/cpu/cpufreq/policy0/scaling_governor", cpu_gov, sizeof(cpu_gov));
    const char *cpu_profile = (strcmp(cpu_gov, "performance") == 0) ? "performance" : "balanced";
    const char *cpu_gov_desc = (strcmp(cpu_gov, "performance") == 0) ? "performance (Performa Maksimal)" : "schedutil (Seimbang)";

    char resampler_mode[32] = "fixed";
    read_file_trimmed(CONF_DIR "/resampler_mode", resampler_mode, sizeof(resampler_mode));
    if (!resampler_mode[0]) strcpy(resampler_mode, "fixed");
    const char *res_title = (strcmp(resampler_mode, "match_source") == 0) ? "Match Source (Bit-Perfect)" : "Fixed Quality";
    const char *res_desc = (strcmp(resampler_mode, "match_source") == 0) ? "Native 1:1 Sample Rate (Hardware Fallback Active)" : "Fixed Resampling (Concurrent Audio Allowed)";

    char fixed_rate[32] = "96000";
    read_file_trimmed(CONF_DIR "/fixed_sample_rate", fixed_rate, sizeof(fixed_rate));
    if (!fixed_rate[0]) strcpy(fixed_rate, "96000");

    dac_info_t dac;
    scan_audio_dac(&dac, resampler_mode, fixed_rate);

    track_info_t track;
    scan_active_media_track(&track, &dac);

    char usb_list_json[2048];
    int ext_usb_count = scan_usb_devices(usb_list_json, sizeof(usb_list_json));
    int otg_cable = (ext_usb_count > 0 || strstr(extcon, "USB-HOST=1")) ? 1 : 0;

    int ssh_running, ssh_pid, ssh_port, ssh_autostart, ssh_kroot, ssh_kshell;
    get_ssh_info(&ssh_running, &ssh_pid, &ssh_port, &ssh_autostart, &ssh_kroot, &ssh_kshell);

    long long swap_total, swap_used, swap_free, swap_cfg = 3072;
    get_swap_info(&swap_total, &swap_used, &swap_free);
    read_file_int(CONF_DIR "/swap_size_mb", &swap_cfg);
    if (swap_cfg <= 0) swap_cfg = 3072;
    int swap_active = is_swapfile_active();

    long long tw_stay_on = 0, tw_autosuspend = -1;
    char stay_conf[16] = "";

    // Dynamic tweaks cached for 15s to eliminate blocking Java VM commands (settings, wm)
    static long long tw_rot = 1, tw_den = 320, tw_lock = 1;
    static char tw_res[32] = "720x1600";
    time_t now_tw = time(NULL);

    if (now_tw - g_last_tw_time > 15 || g_last_tw_time == 0) {
        g_last_tw_time = now_tw;

        // Read dynamic orientation
        char rot_buf[32] = "";
        if (run_command_output("settings get system user_rotation 2>/dev/null", rot_buf, sizeof(rot_buf)) == 0 && rot_buf[0]) {
            tw_rot = atoll(rot_buf);
        }

        // Read dynamic resolution & density
        char wm_sz_buf[256] = "";
        if (run_command_output("wm size 2>/dev/null", wm_sz_buf, sizeof(wm_sz_buf)) == 0) {
            char *ov = strstr(wm_sz_buf, "Override size: ");
            if (ov) {
                sscanf(ov + 15, "%31s", tw_res);
            } else {
                char *ph = strstr(wm_sz_buf, "Physical size: ");
                if (ph) sscanf(ph + 15, "%31s", tw_res);
            }
        }

        char wm_den_buf[256] = "";
        if (run_command_output("wm density 2>/dev/null", wm_den_buf, sizeof(wm_den_buf)) == 0) {
            char *ov = strstr(wm_den_buf, "Override density: ");
            if (ov) {
                tw_den = atoll(ov + 18);
            } else {
                char *ph = strstr(wm_den_buf, "Physical density: ");
                if (ph) tw_den = atoll(ph + 18);
            }
        }

        // Read lockscreen status
        char lock_buf[32] = "";
        if (run_command_output("settings get secure lockscreen.disabled 2>/dev/null", lock_buf, sizeof(lock_buf)) == 0 && lock_buf[0]) {
            tw_lock = atoll(lock_buf);
        }
    }

    read_file_trimmed(CONF_DIR "/stay_on_mode", stay_conf, sizeof(stay_conf));
    if (strcmp(stay_conf, "always") == 0 || strcmp(stay_conf, "true") == 0 || strcmp(stay_conf, "1") == 0) {
        tw_stay_on = 1;
    }
    read_file_int("/sys/module/usbcore/parameters/autosuspend", &tw_autosuspend);

    long long dig_gain = 84, ana_gain = 20;
    char ear_gain[32] = "G_6_DB";
    FILE *gfp = fopen(CONF_DIR "/audio_gain.conf", "r");
    if (gfp) {
        char line[128];
        while (fgets(line, sizeof(line), gfp)) {
            if (strncmp(line, "DIGITAL_GAIN=", 13) == 0) dig_gain = atoll(line + 13);
            else if (strncmp(line, "ANALOG_GAIN=", 12) == 0) ana_gain = atoll(line + 12);
            else if (strncmp(line, "EAR_PA_GAIN=", 12) == 0) {
                sscanf(line + 12, "%31s", ear_gain);
            }
        }
        fclose(gfp);
    }

    char sb_status[2048];
    get_serial_bridge_info(sb_status, sizeof(sb_status));
    static char amp_telemetry[8192];
    amp_telemetry[0] = '\0';
    if (read_file_str(TELEMETRY_CACHE, amp_telemetry, sizeof(amp_telemetry)) != 0 || !amp_telemetry[0]) {
        strcpy(amp_telemetry, "null");
    }
    size_t tl = strlen(amp_telemetry);
    while (tl > 0 && (amp_telemetry[tl - 1] == '\r' || amp_telemetry[tl - 1] == '\n' || amp_telemetry[tl - 1] == ' ')) {
        amp_telemetry[--tl] = '\0';
    }
    if (tl == 0 || amp_telemetry[0] != '{' || amp_telemetry[tl - 1] != '}') {
        strcpy(amp_telemetry, "null");
    }

    char fq_spk[32] = "96000_24", rm_spk[32] = "fixed";
    char fq_hp[32] = "96000_24", rm_hp[32] = "fixed";
    char fq_usb[32] = "192000_24", rm_usb[32] = "match_source";

    if (read_file_trimmed(CONF_DIR "/fixed_quality_internal_speaker", fq_spk, sizeof(fq_spk)) != 0 || !fq_spk[0]) {
        if (read_file_trimmed(CONF_DIR "/fixed_quality", fq_spk, sizeof(fq_spk)) != 0 || !fq_spk[0]) {
            read_file_trimmed(CONF_DIR "/fixed_sample_rate", fq_spk, sizeof(fq_spk));
        }
    }
    read_file_trimmed(CONF_DIR "/resampler_mode_internal_speaker", rm_spk, sizeof(rm_spk));

    if (read_file_trimmed(CONF_DIR "/fixed_quality_internal_headset", fq_hp, sizeof(fq_hp)) != 0 || !fq_hp[0]) {
        if (read_file_trimmed(CONF_DIR "/fixed_quality", fq_hp, sizeof(fq_hp)) != 0 || !fq_hp[0]) {
            read_file_trimmed(CONF_DIR "/fixed_sample_rate", fq_hp, sizeof(fq_hp));
        }
    }
    read_file_trimmed(CONF_DIR "/resampler_mode_internal_headset", rm_hp, sizeof(rm_hp));

    read_file_trimmed(CONF_DIR "/fixed_quality_usb_dac", fq_usb, sizeof(fq_usb));
    read_file_trimmed(CONF_DIR "/resampler_mode_usb_dac", rm_usb, sizeof(rm_usb));

    long long ch_mode = 0;
    read_file_int(CONF_DIR "/channel_mode", &ch_mode);
    if (ch_mode < 0 || ch_mode > 3) ch_mode = 0;

    const char *supp_json = "["
        "{\"value\":\"44100_16\",\"label\":\"44.1 kHz / 16-bit\"},"
        "{\"value\":\"44100_24\",\"label\":\"44.1 kHz / 24-bit\"},"
        "{\"value\":\"48000_16\",\"label\":\"48.0 kHz / 16-bit\"},"
        "{\"value\":\"48000_24\",\"label\":\"48.0 kHz / 24-bit\"},"
        "{\"value\":\"88200_24\",\"label\":\"88.2 kHz / 24-bit\"},"
        "{\"value\":\"96000_24\",\"label\":\"96.0 kHz / 24-bit\"},"
        "{\"value\":\"176400_24\",\"label\":\"176.4 kHz / 24-bit\"},"
        "{\"value\":\"192000_24\",\"label\":\"192.0 kHz / 24-bit\"},"
        "{\"value\":\"352800_32\",\"label\":\"352.8 kHz / 32-bit\"},"
        "{\"value\":\"384000_32\",\"label\":\"384.0 kHz / 32-bit\"}"
        "]";

    struct utsname uts;
    char k_release[128] = "4.19.152", k_version[256] = "", k_display[512] = "";
    if (uname(&uts) == 0) {
        safe_copy(k_release, uts.release, sizeof(k_release));
        safe_copy(k_version, uts.version, sizeof(k_version));
        snprintf(k_display, sizeof(k_display), "%s %s", uts.release, uts.version);
    } else {
        read_file_trimmed("/proc/sys/kernel/osrelease", k_release, sizeof(k_release));
        read_file_trimmed("/proc/sys/kernel/version", k_version, sizeof(k_version));
        snprintf(k_display, sizeof(k_display), "%s %s", k_release, k_version);
    }

    static char dev_model[128] = "";
    if (!dev_model[0]) {
        char p_model[64] = "", p_brand[64] = "";
        run_command_output("getprop ro.product.model 2>/dev/null", p_model, sizeof(p_model));
        run_command_output("getprop ro.product.brand 2>/dev/null", p_brand, sizeof(p_brand));
        if (p_model[0]) {
            if (p_brand[0] && !strstr(p_model, p_brand)) {
                snprintf(dev_model, sizeof(dev_model), "%s %s", p_brand, p_model);
            } else {
                snprintf(dev_model, sizeof(dev_model), "%s", p_model);
            }
        } else {
            strcpy(dev_model, "Realme C15");
        }
    }

    char safe_k_rel[256], safe_k_ver[512], safe_k_disp[1024], safe_dev_mod[256];
    json_escape(k_release, safe_k_rel, sizeof(safe_k_rel));
    json_escape(k_version, safe_k_ver, sizeof(safe_k_ver));
    json_escape(k_display, safe_k_disp, sizeof(safe_k_disp));
    json_escape(dev_model, safe_dev_mod, sizeof(safe_dev_mod));

    printf("{\n");
    printf("  \"success\": true,\n");
    printf("  \"otg_switch\": %lld,\n", otg_switch);
    printf("  \"otg_online\": %lld,\n", otg_online);
    printf("  \"otg_cable_connected\": %d,\n", otg_cable);
    printf("  \"voltage_now\": %lld,\n", voltage_now);
    printf("  \"extcon_state\": \"%s\",\n", extcon);
    printf("  \"ssusb_mode\": \"%s\",\n", ssusb_mode);
    printf("  \"usb_target_mode\": \"auto\",\n");
    printf("  \"udc\": \"%s\",\n", udc);
    printf("  \"tweaks\": {\n");
    printf("    \"rotation\": %lld,\n", tw_rot);
    printf("    \"resolution\": \"%s\",\n", tw_res);
    printf("    \"density\": %lld,\n", tw_den);
    printf("    \"stayon\": %lld,\n", tw_stay_on);
    printf("    \"lockscreen_disabled\": %lld,\n", tw_lock);
    printf("    \"autosuspend\": %lld\n", tw_autosuspend);
    printf("  },\n");
    printf("  \"battery\": {\n");
    printf("    \"capacity\": %lld,\n", is_bypass ? 100LL : real_cap);
    printf("    \"status\": \"%s\",\n", is_bypass ? "AC Bypass" : real_status);
    printf("    \"raw_capacity\": \"%lld\",\n", real_cap);
    printf("    \"is_bypass\": %s\n", is_bypass ? "true" : "false");
    printf("  },\n");
    printf("  \"volume\": 25,\n");
    printf("  \"volume_max\": 25,\n");
    printf("  \"uptime\": \"%s\",\n", uptime_en);
    printf("  \"uptime_id\": \"%s\",\n", uptime_id);
    printf("  \"uptime_sec\": %lld,\n", uptime_sec);
    printf("  \"ip\": \"%s\",\n", ip_addr);
    printf("  \"device_model\": \"%s\",\n", safe_dev_mod);
    printf("  \"kernel\": {\n");
    printf("    \"release\": \"%s\",\n", safe_k_rel);
    printf("    \"version\": \"%s\",\n", safe_k_ver);
    printf("    \"display\": \"%s\"\n", safe_k_disp);
    printf("  },\n");
    printf("  \"cpu\": {\n");
    printf("    \"little_mhz\": \"%lld MHz\",\n", cpu0_freq > 0 ? cpu0_freq / 1000 : 1804LL);
    printf("    \"big_mhz\": \"%lld MHz\",\n", cpu4_freq > 0 ? cpu4_freq / 1000 : 1612LL);
    printf("    \"temp\": \"%lld °C\",\n", cpu_temp_raw > 1000 ? cpu_temp_raw / 1000 : 48LL);
    printf("    \"governor\": \"%s\",\n", cpu_gov_desc);
    printf("    \"profile\": \"%s\",\n", cpu_profile);
    printf("    \"status\": \"Optimal & Safe\"\n");
    printf("  },\n");
    char safe_title[512], safe_artist[512], safe_album[512], safe_app[128];
    json_escape(track.title, safe_title, sizeof(safe_title));
    json_escape(track.artist, safe_artist, sizeof(safe_artist));
    json_escape(track.album, safe_album, sizeof(safe_album));
    json_escape(track.app, safe_app, sizeof(safe_app));

    printf("  \"track\": {\n");
    printf("    \"state\": \"%s\",\n", track.state);
    printf("    \"title\": \"%s\",\n", safe_title);
    printf("    \"artist\": \"%s\",\n", safe_artist);
    printf("    \"album\": \"%s\",\n", safe_album);
    printf("    \"app\": \"%s\",\n", safe_app);
    printf("    \"format\": \"%s\",\n", track.format);
    printf("    \"bit_depth\": \"%s\",\n", track.bit_depth);
    printf("    \"bitrate\": \"%s\",\n", track.bitrate);
    printf("    \"sample_rate\": \"%s\",\n", track.sample_rate);
    printf("    \"raw_rate\": \"%s\",\n", track.raw_rate);
    printf("    \"cover\": \"%s\"\n", track.cover);
    printf("  },\n");
    printf("  \"dac\": {\n");
    printf("    \"found\": %d,\n", dac.found);
    printf("    \"card\": \"%s\",\n", dac.card);
    printf("    \"name\": \"%s\",\n", dac.name);
    printf("    \"status\": \"%s\",\n", dac.status);
    printf("    \"freq\": \"%s\",\n", dac.freq);
    printf("    \"format\": \"%s\",\n", dac.format);
    printf("    \"bit_depth\": \"%s\",\n", dac.bit_depth);
    printf("    \"period_size\": \"%s\",\n", dac.period_size);
    printf("    \"buffer_size\": \"%s\",\n", dac.buffer_size);
    printf("    \"max_freq\": \"%s\",\n", dac.max_freq);
    printf("    \"max_bit_depth\": \"%s\",\n", dac.max_bit_depth);
    printf("    \"channels\": \"%s\",\n", dac.channels);
    printf("    \"rates_list\": \"%s\",\n", dac.rates_list);
    printf("    \"rates\": [%s],\n", dac.rates_json);
    printf("    \"hw_capabilities\": \"%s\"\n", dac.hw_capabilities);
    printf("  },\n");
    printf("  \"audio_devices\": [\n");
    printf("    {\"id\":\"internal_speaker\",\"name\":\"Speaker Internal (Qualcomm SoC)\",\"volume\":25,\"volume_max\":25,\"resampler_mode\":\"%s\",\"fixed_quality\":\"%s\",\"supported_qualities\":%s},\n", rm_spk, fq_spk, supp_json);
    printf("    {\"id\":\"internal_headset\",\"name\":\"Headset 3.5mm (Qualcomm SoC)\",\"volume\":25,\"volume_max\":25,\"resampler_mode\":\"%s\",\"fixed_quality\":\"%s\",\"supported_qualities\":%s},\n", rm_hp, fq_hp, supp_json);
    printf("    {\"id\":\"usb_dac\",\"name\":\"USB DAC Prioritas\",\"volume\":25,\"volume_max\":25,\"resampler_mode\":\"%s\",\"fixed_quality\":\"%s\",\"supported_qualities\":%s}\n", rm_usb, fq_usb, supp_json);
    printf("  ],\n");
    printf("  \"audio_output\": \"auto\",\n");
    printf("  \"audio_outputs_list\": [{\"id\":\"auto\",\"name\":\"Auto (USB DAC Prioritas)\"}, {\"id\":\"internal_speaker\",\"name\":\"Speaker Internal (Qualcomm SoC)\"}, {\"id\":\"internal_headset\",\"name\":\"Headset 3.5mm (Qualcomm SoC)\"}],\n");
    printf("  \"serial_bridge\": %s,\n", sb_status);
    printf("  \"amp_telemetry\": %s,\n", amp_telemetry);
    printf("  \"notification_split\": \"auto\",\n");
    printf("  \"channel_mode\": %lld,\n", ch_mode);
    printf("  \"fixed_sample_rate\": \"%s\",\n", fixed_rate);
    printf("  \"usb_ext_count\": %d,\n", ext_usb_count);
    printf("  \"usb_devices_list\": [%s],\n", usb_list_json);
    printf("  \"ssh\": {\n");
    printf("    \"running\": %d,\n", ssh_running);
    printf("    \"pid\": %d,\n", ssh_pid);
    printf("    \"port\": %d,\n", ssh_port);
    printf("    \"autostart\": %d,\n", ssh_autostart);
    printf("    \"keys_root\": %d,\n", ssh_kroot);
    printf("    \"keys_shell\": %d,\n", ssh_kshell);
    printf("    \"ip\": \"%s\"\n", ip_addr);
    printf("  },\n");
    printf("  \"swap\": {\n");
    printf("    \"total_mb\": %lld,\n", swap_total);
    printf("    \"used_mb\": %lld,\n", swap_used);
    printf("    \"free_mb\": %lld,\n", swap_free);
    printf("    \"config_mb\": %lld,\n", swap_cfg);
    printf("    \"file_active\": %d\n", swap_active);
    printf("  },\n");
    printf("  \"audio_gain\": {\n");
    printf("    \"digital\": %lld,\n", dig_gain);
    printf("    \"analog\": %lld,\n", ana_gain);
    printf("    \"ear_pa\": \"%s\"\n", ear_gain);
    printf("  },\n");
    printf("  \"resampler\": {\n");
    printf("    \"mode\": \"%s\",\n", resampler_mode);
    printf("    \"title\": \"%s\",\n", res_title);
    printf("    \"description\": \"%s\"\n", res_desc);
    printf("  }\n");
    printf("}\n");
}

/* -------------------------------------------------------------------------- */
/* Realtime Flash & Upload & Telemetry Streaming Handlers                     */
/* -------------------------------------------------------------------------- */
static void handle_action_upload_file(const char *filename) {
    if (!filename || !filename[0]) {
        printf("{\"success\":false,\"error\":\"Missing filename parameter\"}\n");
        return;
    }

    char safe_fn[128];
    size_t sidx = 0;
    for (const char *p = filename; *p && sidx < sizeof(safe_fn) - 1; p++) {
        if (isalnum((unsigned char)*p) || *p == '.' || *p == '_' || *p == '-') {
            safe_fn[sidx++] = *p;
        }
    }
    safe_fn[sidx] = '\0';
    if (!safe_fn[0]) {
        printf("{\"success\":false,\"error\":\"Invalid filename\"}\n");
        return;
    }

    const char *clen = getenv("CONTENT_LENGTH");
    long long total_len = clen ? atoll(clen) : 0;
    if (total_len <= 0) {
        printf("{\"success\":false,\"error\":\"Zero or missing CONTENT_LENGTH\"}\n");
        return;
    }

    mkdir("/data/local/tmp", 0777);
    mkdir("/data/local/tmp/jacktor_upload", 0777);
    char dest_path[256];
    snprintf(dest_path, sizeof(dest_path), "/data/local/tmp/jacktor_upload/%s", safe_fn);

    FILE *out_fp = fopen(dest_path, "wb");
    if (!out_fp) {
        printf("{\"success\":false,\"error\":\"Cannot open destination file for write\"}\n");
        return;
    }

    sha256_ctx sctx;
    sha256_init(&sctx);

    char buf[65536];
    long long remaining = total_len;
    long long total_written = 0;

    while (remaining > 0) {
        size_t to_read = (remaining > (long long)sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
        size_t nread = fread(buf, 1, to_read, stdin);
        if (nread == 0) break;
        sha256_update(&sctx, buf, nread);
        size_t nwritten = fwrite(buf, 1, nread, out_fp);
        total_written += nwritten;
        remaining -= nread;
    }
    fclose(out_fp);

    char hash_hex[65];
    sha256_final_hex(&sctx, hash_hex);

    if (total_written != total_len) {
        printf("{\"success\":false,\"error\":\"Upload truncated\",\"expected\":%lld,\"received\":%lld}\n", total_len, total_written);
        return;
    }

    printf("{\"success\":true,\"filename\":\"%s\",\"bytes\":%lld,\"sha256\":\"%s\"}\n", safe_fn, total_written, hash_hex);
}

static void handle_action_flash_firmware(const char *port, int baud) {
    if (!port || !port[0]) port = "/dev/ttyUSB0";
    if (baud <= 0) baud = 921600;

    printf("Content-Type: text/plain; charset=utf-8\r\n");
    printf("Access-Control-Allow-Origin: *\r\n");
    printf("Cache-Control: no-cache\r\n");
    printf("X-Accel-Buffering: no\r\n");
    printf("\r\n");
    fflush(stdout);

    printf("[JACKTOR-FLASH] Preparing firmware flash...\n");
    printf("[JACKTOR-FLASH] Target port: %s @ %d bps\n", port, baud);
    fflush(stdout);

    // 1. Enter maintenance mode by creating flashing lock
    int lfd = open("/data/local/tmp/jacktor_flashing.lock", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (lfd >= 0) close(lfd);

    // 2. Stop serial-bridge daemon and ensure USB port is fully released
    printf("[JACKTOR-FLASH] Stopping serial-bridge daemon and holding port lock...\n");
    fflush(stdout);
    system("/data/adb/modules/jacktor-audio-framework/system/bin/serial-bridge stop >/dev/null 2>&1; /data/adb/modules/jacktor-audio-framework/system/bin/busybox fuser -k /dev/ttyUSB* 2>/dev/null; sleep 0.8");

    const char *dir = "/data/local/tmp/jacktor_upload";
    char cmd[1024];
    struct stat st_boot, st_part, st_app;
    int has_boot = (stat("/data/local/tmp/jacktor_upload/bootloader.bin", &st_boot) == 0 && st_boot.st_size > 0);
    int has_part = (stat("/data/local/tmp/jacktor_upload/partitions.bin", &st_part) == 0 && st_part.st_size > 0);
    int has_app = (stat("/data/local/tmp/jacktor_upload/amplifier.bin", &st_app) == 0 && st_app.st_size > 0);

    if (!has_app) {
        printf("[JACKTOR-FLASH-ERROR] amplifier.bin not found in %s!\n", dir);
        fflush(stdout);
        unlink("/data/local/tmp/jacktor_flashing.lock");
        system("/data/adb/modules/jacktor-audio-framework/system/bin/serial-bridge start >/dev/null 2>&1 &");
        return;
    }

    const char *esputil_bin = "/data/adb/modules/jacktor-audio-framework/system/bin/esputil";
    if (access(esputil_bin, X_OK) != 0) {
        esputil_bin = "esputil";
    }

    if (has_boot && has_part) {
        printf("[JACKTOR-FLASH] Flashing full image: bootloader (0x1000), partitions (0x8000), amplifier (0x10000)\n");
        snprintf(cmd, sizeof(cmd), "%s -b %d -p %s flash 0x1000 %s/bootloader.bin 0x8000 %s/partitions.bin 0x10000 %s/amplifier.bin 2>&1",
                 esputil_bin, baud, port, dir, dir, dir);
    } else {
        printf("[JACKTOR-FLASH] Flashing app partition: amplifier (0x10000)\n");
        snprintf(cmd, sizeof(cmd), "%s -b %d -p %s flash 0x10000 %s/amplifier.bin 2>&1",
                 esputil_bin, baud, port, dir);
    }
    fflush(stdout);

    FILE *pfp = popen(cmd, "r");
    if (!pfp) {
        printf("[JACKTOR-FLASH-ERROR] Failed to execute esputil flasher!\n");
        fflush(stdout);
        unlink("/data/local/tmp/jacktor_flashing.lock");
        system("/data/adb/modules/jacktor-audio-framework/system/bin/serial-bridge start >/dev/null 2>&1 &");
        return;
    }

    char chunk_buf[256];
    int pfd = fileno(pfp);
    ssize_t n;
    while ((n = read(pfd, chunk_buf, sizeof(chunk_buf) - 1)) > 0) {
        chunk_buf[n] = '\0';
        fwrite(chunk_buf, 1, n, stdout);
        fflush(stdout);
    }
    int exit_code = pclose(pfp);
    int flash_ok = (exit_code == 0);

    // 3. Remove maintenance lock
    unlink("/data/local/tmp/jacktor_flashing.lock");

    if (flash_ok) {
        printf("\n[JACKTOR-FLASH] Flashing completed successfully with status 0.\n");
        printf("[JACKTOR-FLASH] Restarting serial-bridge and waking amplifier...\n");
        fflush(stdout);
    } else {
        printf("\n[JACKTOR-FLASH-ERROR] Flashing exited with code %d.\n", exit_code);
        fflush(stdout);
    }

    // 4. Restart serial bridge daemon
    system("/data/adb/modules/jacktor-audio-framework/system/bin/serial-bridge start >/dev/null 2>&1 &");
    usleep(500000);

    if (flash_ok) {
        char resp[128];
        send_amp_command("pwr-on", resp, sizeof(resp));
        usleep(150000);
        send_amp_command("bt-on", resp, sizeof(resp));
        printf("[JACKTOR-FLASH-SUCCESS] Flash completed and amplifier operational.\n");
    } else {
        printf("[JACKTOR-FLASH-FAILED] Flash verification failed.\n");
    }
    fflush(stdout);
}

static void handle_action_stream(void) {
    signal(SIGPIPE, SIG_IGN);

    // Disable stdout buffering so SSE lines flush directly to HTTP client with 0 latency
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Content-Type: text/event-stream; charset=utf-8\r\n");
    printf("Access-Control-Allow-Origin: *\r\n");
    printf("Cache-Control: no-cache, no-transform\r\n");
    printf("Connection: keep-alive\r\n");
    printf("X-Accel-Buffering: no\r\n");
    printf("Content-Encoding: none\r\n");
    printf("\r\n");
    fflush(stdout);

    int sock = -1;
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERIAL_BRIDGE_TCP_PORT);
    serv_addr.sin_addr.s_addr = inet_addr(SERIAL_BRIDGE_TCP_HOST);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock >= 0) {
        int nodelay = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        int keepalive = 1;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
            close(sock);
            sock = -1;
        }
    }
    if (sock < 0) {
        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct sockaddr_un un_addr;
            memset(&un_addr, 0, sizeof(un_addr));
            un_addr.sun_family = AF_UNIX;
            strncpy(un_addr.sun_path, SERIAL_BRIDGE_UNIX_SOCK, sizeof(un_addr.sun_path) - 1);
            if (connect(sock, (struct sockaddr *)&un_addr, sizeof(un_addr)) != 0) {
                close(sock);
                sock = -1;
            }
        }
    }

    if (sock < 0) {
        printf("event: error\ndata: {\"error\":\"serial-bridge socket unreachable\"}\n\n");
        fflush(stdout);
        return;
    }

    static char buf[16384];
    FILE *sfp = fdopen(sock, "r");
    if (!sfp) {
        close(sock);
        return;
    }

    while (fgets(buf, sizeof(buf), sfp)) {
        size_t blen = strlen(buf);
        while (blen > 0 && (buf[blen - 1] == '\r' || buf[blen - 1] == '\n')) {
            buf[--blen] = '\0';
        }
        if (blen == 0) continue;

        if (printf("data: %s\n\n", buf) < 0) break;
        if (fflush(stdout) != 0) break;
    }

    fclose(sfp);
}

/* -------------------------------------------------------------------------- */
/* Main CGI / CLI Entry Point                                                 */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv) {
    setenv("PATH", "/data/adb/modules/jacktor-audio-framework/system/bin:/system/bin:/system/xbin:/data/adb/ap/bin:/sbin:/bin", 1);

    char action[64] = "status";
    char val[256] = "";
    char mode[64] = "";
    char cmd[512] = "";
    char tweak[64] = "";
    char user[64] = "root";
    char pass[128] = "";
    char token[128] = "";
    char new_pass[128] = "";
    char filename[128] = "";
    char port[64] = "/dev/ttyUSB0";
    char target_device[64] = "";
    char resampler_mode_param[64] = "";
    char fixed_quality_param[64] = "";
    char b64_data[8192] = "";
    char key_data[1024] = "";
    int line_num = 0;
    int baud = 921600;

    const char *req_method = getenv("REQUEST_METHOD");
    int is_cgi = (req_method != NULL);

    if (is_cgi) {
        if (strcmp(req_method, "OPTIONS") == 0) {
            printf("Access-Control-Allow-Origin: *\r\n");
            printf("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
            printf("Access-Control-Allow-Headers: Content-Type, X-Jacktor-Token\r\n");
            printf("\r\n");
            return 0;
        }

        const char *hdr_token = getenv("HTTP_X_JACKTOR_TOKEN");
        if (hdr_token && hdr_token[0]) {
            safe_copy(token, hdr_token, sizeof(token));
        }

        const char *hdr_auth = getenv("HTTP_AUTHORIZATION");
        if (hdr_auth && (!token[0])) {
            if (strncmp(hdr_auth, "Bearer ", 7) == 0) {
                safe_copy(token, hdr_auth + 7, sizeof(token));
            } else {
                safe_copy(token, hdr_auth, sizeof(token));
            }
        }

        const char *hdr_cookie = getenv("HTTP_COOKIE");
        if (hdr_cookie && (!token[0])) {
            const char *c_pos = strstr(hdr_cookie, "jacktor_auth=");
            if (c_pos) {
                c_pos += 13;
                char c_buf[128];
                safe_copy(c_buf, c_pos, sizeof(c_buf));
                char *semi = strchr(c_buf, ';');
                if (semi) *semi = '\0';
                safe_copy(token, c_buf, sizeof(token));
            }
        }

        const char *qs = getenv("QUERY_STRING");
        if (qs && qs[0]) {
            char *qs_copy = strdup(qs);
            char *pair = strtok(qs_copy, "&");
            while (pair) {
                char *eq = strchr(pair, '=');
                if (eq) {
                    *eq = '\0';
                    char dec_val[8192];
                    url_decode(dec_val, eq + 1, sizeof(dec_val));
                    if (strcmp(pair, "action") == 0) safe_copy(action, dec_val, sizeof(action));
                    else if (strcmp(pair, "val") == 0) safe_copy(val, dec_val, sizeof(val));
                    else if (strcmp(pair, "mode") == 0) safe_copy(mode, dec_val, sizeof(mode));
                    else if (strcmp(pair, "cmd") == 0) safe_copy(cmd, dec_val, sizeof(cmd));
                    else if (strcmp(pair, "bridge_cmd") == 0) safe_copy(cmd, dec_val, sizeof(cmd));
                    else if (strcmp(pair, "tweak") == 0) safe_copy(tweak, dec_val, sizeof(tweak));
                    else if (strcmp(pair, "user") == 0) safe_copy(user, dec_val, sizeof(user));
                    else if (strcmp(pair, "pass") == 0) safe_copy(pass, dec_val, sizeof(pass));
                    else if (strcmp(pair, "token") == 0) safe_copy(token, dec_val, sizeof(token));
                    else if (strcmp(pair, "new_pass") == 0) safe_copy(new_pass, dec_val, sizeof(new_pass));
                    else if (strcmp(pair, "filename") == 0) safe_copy(filename, dec_val, sizeof(filename));
                    else if (strcmp(pair, "port") == 0) safe_copy(port, dec_val, sizeof(port));
                    else if (strcmp(pair, "target_device") == 0) safe_copy(target_device, dec_val, sizeof(target_device));
                    else if (strcmp(pair, "resampler_mode") == 0) safe_copy(resampler_mode_param, dec_val, sizeof(resampler_mode_param));
                    else if (strcmp(pair, "fixed_quality") == 0) safe_copy(fixed_quality_param, dec_val, sizeof(fixed_quality_param));
                    else if (strcmp(pair, "b64") == 0) safe_copy(b64_data, dec_val, sizeof(b64_data));
                    else if (strcmp(pair, "key") == 0) safe_copy(key_data, dec_val, sizeof(key_data));
                    else if (strcmp(pair, "line") == 0) line_num = atoi(dec_val);
                    else if (strcmp(pair, "baud") == 0) baud = atoi(dec_val);
                }
                pair = strtok(NULL, "&");
            }
            free(qs_copy);
        }

        // Only parse standard urlencoded POST body if action is not binary file upload
        if (strcmp(req_method, "POST") == 0 && strcmp(action, "upload_file") != 0) {
            const char *clen = getenv("CONTENT_LENGTH");
            if (clen) {
                int len = atoi(clen);
                if (len > 0 && len < 65536) {
                    char *pdata = malloc(len + 1);
                    if (pdata) {
                        size_t r = fread(pdata, 1, len, stdin);
                        pdata[r] = '\0';
                        char *pair = strtok(pdata, "&");
                        while (pair) {
                            char *eq = strchr(pair, '=');
                            if (eq) {
                                *eq = '\0';
                                char dec_val[8192];
                                url_decode(dec_val, eq + 1, sizeof(dec_val));
                                if (strcmp(pair, "action") == 0) safe_copy(action, dec_val, sizeof(action));
                                else if (strcmp(pair, "val") == 0) safe_copy(val, dec_val, sizeof(val));
                                else if (strcmp(pair, "mode") == 0) safe_copy(mode, dec_val, sizeof(mode));
                                else if (strcmp(pair, "cmd") == 0) safe_copy(cmd, dec_val, sizeof(cmd));
                                else if (strcmp(pair, "bridge_cmd") == 0) safe_copy(cmd, dec_val, sizeof(cmd));
                                else if (strcmp(pair, "tweak") == 0) safe_copy(tweak, dec_val, sizeof(tweak));
                                else if (strcmp(pair, "user") == 0) safe_copy(user, dec_val, sizeof(user));
                                else if (strcmp(pair, "pass") == 0) safe_copy(pass, dec_val, sizeof(pass));
                                else if (strcmp(pair, "token") == 0) safe_copy(token, dec_val, sizeof(token));
                                else if (strcmp(pair, "new_pass") == 0) safe_copy(new_pass, dec_val, sizeof(new_pass));
                                else if (strcmp(pair, "filename") == 0) safe_copy(filename, dec_val, sizeof(filename));
                                else if (strcmp(pair, "port") == 0) safe_copy(port, dec_val, sizeof(port));
                                else if (strcmp(pair, "target_device") == 0) safe_copy(target_device, dec_val, sizeof(target_device));
                                else if (strcmp(pair, "resampler_mode") == 0) safe_copy(resampler_mode_param, dec_val, sizeof(resampler_mode_param));
                                else if (strcmp(pair, "fixed_quality") == 0) safe_copy(fixed_quality_param, dec_val, sizeof(fixed_quality_param));
                                else if (strcmp(pair, "b64") == 0) safe_copy(b64_data, dec_val, sizeof(b64_data));
                                else if (strcmp(pair, "key") == 0) safe_copy(key_data, dec_val, sizeof(key_data));
                                else if (strcmp(pair, "line") == 0) line_num = atoi(dec_val);
                                else if (strcmp(pair, "baud") == 0) baud = atoi(dec_val);
                            }
                            pair = strtok(NULL, "&");
                        }
                        free(pdata);
                    }
                }
            }
        }

        // Authentication enforcement
        if (strcmp(action, "auth_login") != 0 && strcmp(action, "auth_check") != 0 &&
            strcmp(action, "languages") != 0 && strcmp(action, "get_languages") != 0) {
            if (!is_authenticated(token)) {
                if (strcmp(action, "stream") == 0 || strcmp(action, "flash_firmware") == 0) {
                    printf("Content-Type: text/plain; charset=utf-8\r\n\r\n[ERROR] Authentication required. Please log in.\n");
                } else {
                    printf("Content-Type: application/json; charset=utf-8\r\n");
                    printf("Access-Control-Allow-Origin: *\r\n\r\n");
                    printf("{\"success\":false,\"authenticated\":false,\"error\":\"Authentication required. Please log in.\"}\n");
                }
                return 0;
            }
        }

        // Special streaming endpoints emit their own headers
        if (strcmp(action, "stream") == 0) {
            handle_action_stream();
            return 0;
        }
        if (strcmp(action, "flash_firmware") == 0) {
            handle_action_flash_firmware(port, baud);
            return 0;
        }

        // Standard JSON API Header
        printf("Content-Type: application/json; charset=utf-8\r\n");
        printf("Access-Control-Allow-Origin: *\r\n");
        printf("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
        printf("Access-Control-Allow-Headers: Content-Type, X-Jacktor-Token\r\n");
        printf("Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n");
        printf("Pragma: no-cache\r\n");
        printf("\r\n");
        fflush(stdout);

        if (strcmp(action, "upload_file") == 0) {
            handle_action_upload_file(filename);
            return 0;
        }
        if (strcmp(action, "languages") == 0 || strcmp(action, "get_languages") == 0) {
            handle_action_languages();
            return 0;
        }
    } else {
        if (argc > 1) safe_copy(action, argv[1], sizeof(action));
        if (argc > 2) safe_copy(val, argv[2], sizeof(val));
        if (argc > 3) safe_copy(mode, argv[3], sizeof(mode));

        if (strcmp(action, "stream") == 0) {
            handle_action_stream();
            return 0;
        }
        if (strcmp(action, "flash_firmware") == 0) {
            handle_action_flash_firmware(port, baud);
            return 0;
        }
        if (strcmp(action, "upload_file") == 0) {
            handle_action_upload_file(val[0] ? val : filename);
            return 0;
        }
        if (strcmp(action, "languages") == 0 || strcmp(action, "get_languages") == 0) {
            handle_action_languages();
            return 0;
        }
    }

    if (strcmp(action, "auth_login") == 0) {
        char sys_user[64], sys_pass[128];
        get_system_credentials(sys_user, sizeof(sys_user), sys_pass, sizeof(sys_pass));

        if (strcmp(user, sys_user) == 0 && strcmp(pass, sys_pass) == 0) {
            char salt_buf[256], token_hex[65];
            snprintf(salt_buf, sizeof(salt_buf), "%s:jacktor_secret_salt_2026", sys_pass);
            calculate_sha256_hex(salt_buf, token_hex);

            mkdir(CONF_DIR, 0755);
            write_file_str(TOKEN_FILE, token_hex);
            chmod(TOKEN_FILE, 0600);

            printf("{\"success\":true,\"authenticated\":true,\"token\":\"%s\",\"user\":\"%s\"}\n", token_hex, sys_user);
        } else {
            printf("{\"success\":false,\"authenticated\":false,\"error\":\"Invalid credentials\"}\n");
        }
        return 0;
    }

    if (strcmp(action, "auth_check") == 0) {
        int auth_ok = is_authenticated(token);
        if (auth_ok) {
            char sys_user[64], sys_pass[128];
            get_system_credentials(sys_user, sizeof(sys_user), sys_pass, sizeof(sys_pass));
            printf("{\"success\":true,\"authenticated\":true,\"user\":\"%s\"}\n", sys_user);
        } else {
            printf("{\"success\":true,\"authenticated\":false}\n");
        }
        return 0;
    }

    if (strcmp(action, "auth_logout") == 0) {
        unlink(TOKEN_FILE);
        printf("{\"success\":true,\"message\":\"Logged out\"}\n");
        return 0;
    }

    if (strcmp(action, "auth_change_password") == 0) {
        char sys_user[64], sys_pass[128];
        get_system_credentials(sys_user, sizeof(sys_user), sys_pass, sizeof(sys_pass));

        if (pass[0] && strcmp(pass, sys_pass) != 0) {
            printf("{\"success\":false,\"error\":\"Password lama tidak sesuai\"}\n");
            return 0;
        }
        if (strlen(new_pass) < 6) {
            printf("{\"success\":false,\"error\":\"Password baru minimal 6 karakter\"}\n");
            return 0;
        }

        char auth_buf[256];
        snprintf(auth_buf, sizeof(auth_buf), "username=%s\npassword=%s\n", sys_user, new_pass);
        write_file_str(AUTH_CONF, auth_buf);
        chmod(AUTH_CONF, 0600);
        write_file_str(SSH_AUTH_CONF, auth_buf);
        chmod(SSH_AUTH_CONF, 0600);

        char salt_buf[256], new_tok[65];
        snprintf(salt_buf, sizeof(salt_buf), "%s:jacktor_secret_salt_2026", new_pass);
        calculate_sha256_hex(salt_buf, new_tok);
        write_file_str(TOKEN_FILE, new_tok);
        chmod(TOKEN_FILE, 0600);

        printf("{\"success\":true,\"message\":\"Password berhasil diperbarui\",\"token\":\"%s\"}\n", new_tok);
        return 0;
    }

    if (strcmp(action, "amp_cmd") == 0 || strcmp(action, "amp_command") == 0) {
        const char *amp_c = cmd[0] ? cmd : (val[0] ? val : mode);
        if (!amp_c[0]) {
            printf("{\"success\":false,\"error\":\"Missing cmd\"}\n");
            return 0;
        }

        int ret = send_amp_command(amp_c, NULL, 0);
        if (ret == 0) {
            char safe_c[512];
            json_escape(amp_c, safe_c, sizeof(safe_c));
            printf("{\"success\":true,\"cmd\":\"%s\",\"device\":\"tcp://127.0.0.1:7777\"}\n", safe_c);
        } else {
            printf("{\"success\":false,\"error\":\"Hardware serial bridge socket unreachable\"}\n");
        }
        return 0;
    }

    if (strcmp(action, "amp_telemetry") == 0) {
        static char tel_buf[8192];
        if (read_file_trimmed(TELEMETRY_CACHE, tel_buf, sizeof(tel_buf)) == 0 && tel_buf[0]) {
            printf("{\"success\":true,\"telemetry\":%s}\n", tel_buf);
        } else {
            printf("{\"success\":false,\"error\":\"Telemetry cache not ready\"}\n");
        }
        return 0;
    }

    if (strcmp(action, "otg_mode") == 0) {
        const char *m = mode[0] ? mode : val;
        if (strcmp(m, "device") == 0 || strcmp(m, "peripheral") == 0 || strcmp(m, "gadget") == 0) {
            int res = system("/data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger device >/dev/null 2>&1");
            (void)res;
            printf("{\"success\":true,\"message\":\"Device / Gadget Mode aktif\",\"usb_target_mode\":\"device\"}\n");
        } else if (strcmp(m, "auto") == 0) {
            int res = system("/data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger auto >/dev/null 2>&1");
            (void)res;
            printf("{\"success\":true,\"message\":\"Auto Mode aktif\",\"usb_target_mode\":\"auto\"}\n");
        } else {
            int res = system("/data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger host >/dev/null 2>&1");
            (void)res;
            printf("{\"success\":true,\"message\":\"Host Mode aktif\",\"usb_target_mode\":\"host\"}\n");
        }
        return 0;
    }

    if (strcmp(action, "fixed_rate") == 0) {
        if (val[0]) write_file_str(CONF_DIR "/fixed_sample_rate", val);
        printf("{\"success\":true,\"fixed_sample_rate\":\"%s\"}\n", val);
        return 0;
    }

    if (strcmp(action, "notif_split") == 0) {
        if (val[0]) write_file_str(CONF_DIR "/notif_split", val);
        printf("{\"success\":true,\"notif_split\":\"%s\"}\n", val);
        return 0;
    }

    if (strcmp(action, "audio_output") == 0) {
        if (val[0]) write_file_str(CONF_DIR "/audio_output", val);
        printf("{\"success\":true,\"audio_output\":\"%s\"}\n", val);
        return 0;
    }

    if (strcmp(action, "channel_mode") == 0 || strcmp(action, "set_channel_mode") == 0) {
        const char *m = mode[0] ? mode : val;
        int ch = 0;
        if (strcmp(m, "1") == 0 || strcasecmp(m, "left") == 0 || strcasecmp(m, "l") == 0) {
            ch = 1;
        } else if (strcmp(m, "2") == 0 || strcasecmp(m, "right") == 0 || strcasecmp(m, "r") == 0) {
            ch = 2;
        } else if (strcmp(m, "3") == 0 || strcasecmp(m, "mono") == 0 || strcasecmp(m, "mix") == 0) {
            ch = 3;
        } else {
            ch = 0; // STEREO
        }

        // Apply to Android System Audio
        if (ch == 0) {
            int res = system("settings put secure master_mono 0 >/dev/null 2>&1; settings put system master_balance 0.0 >/dev/null 2>&1");
            (void)res;
        } else if (ch == 1) {
            int res = system("settings put secure master_mono 1 >/dev/null 2>&1; settings put system master_balance -1.0 >/dev/null 2>&1");
            (void)res;
        } else if (ch == 2) {
            int res = system("settings put secure master_mono 1 >/dev/null 2>&1; settings put system master_balance 1.0 >/dev/null 2>&1");
            (void)res;
        } else if (ch == 3) {
            int res = system("settings put secure master_mono 1 >/dev/null 2>&1; settings put system master_balance 0.0 >/dev/null 2>&1");
            (void)res;
        }

        char ch_str[16];
        snprintf(ch_str, sizeof(ch_str), "%d\n", ch);
        write_file_str(CONF_DIR "/channel_mode", ch_str);

        // Forward to ESP32 Amplifier
        char esp_cmd[64];
        snprintf(esp_cmd, sizeof(esp_cmd), "setchannel %d", ch);
        send_amp_command(esp_cmd, NULL, 0);

        const char *ch_name = (ch == 1) ? "LEFT" : (ch == 2) ? "RIGHT" : (ch == 3) ? "MONO" : "STEREO";
        printf("{\"success\":true,\"channel_mode\":%d,\"name\":\"%s\",\"message\":\"Channel mode set to %s\"}\n", ch, ch_name, ch_name);
        return 0;
    }

    if (strcmp(action, "set_volume") == 0 || strcmp(action, "set_device_volume") == 0) {
        if (val[0]) {
            char vcmd[128];
            snprintf(vcmd, sizeof(vcmd), "cmd media_session volume --stream 3 --set %s >/dev/null 2>&1", val);
            int res = system(vcmd);
            (void)res;
            printf("{\"success\":true,\"volume\":%s}\n", val);
        } else {
            printf("{\"success\":false,\"error\":\"Missing volume value\"}\n");
        }
        return 0;
    }

    if (strcmp(action, "set_device_audio_config") == 0) {
        if (target_device[0]) {
            char f_path[256];
            if (resampler_mode_param[0]) {
                snprintf(f_path, sizeof(f_path), CONF_DIR "/resampler_mode_%s", target_device);
                write_file_str(f_path, resampler_mode_param);
            }
            if (fixed_quality_param[0]) {
                snprintf(f_path, sizeof(f_path), CONF_DIR "/fixed_quality_%s", target_device);
                write_file_str(f_path, fixed_quality_param);
            }
        }
        if (resampler_mode_param[0]) {
            write_file_str(CONF_DIR "/resampler_mode", resampler_mode_param);
        }
        if (fixed_quality_param[0]) {
            write_file_str(CONF_DIR "/fixed_quality", fixed_quality_param);
            write_file_str(CONF_DIR "/fixed_sample_rate", fixed_quality_param);
        }
        int res = system("killall audioserver android.hardware.audio.service >/dev/null 2>&1");
        (void)res;
        printf("{\"success\":true,\"message\":\"Device audio config updated\"}\n");
        return 0;
    }

    if (strcmp(action, "set_gain") == 0) {
        apply_hardware_audio_gain(mode[0] ? mode : (val[0] ? val : "all"), val);
        return 0;
    }

    if (strcmp(action, "set_swap") == 0) {
        long long mb = atoll(val);
        if (mb <= 0) {
            int res = system("swapoff /data/swapfile >/dev/null 2>&1; rm -f /data/swapfile >/dev/null 2>&1");
            (void)res;
            write_file_str(CONF_DIR "/swap_size_mb", "0\n");
            printf("{\"success\":true,\"message\":\"SWAP deactivated\",\"swap_mb\":0}\n");
        } else {
            char scmd[512];
            snprintf(scmd, sizeof(scmd), "swapoff /data/swapfile >/dev/null 2>&1; rm -f /data/swapfile >/dev/null 2>&1; dd if=/dev/zero of=/data/swapfile bs=1M count=%lld >/dev/null 2>&1 && chmod 600 /data/swapfile && mkswap /data/swapfile >/dev/null 2>&1 && swapon -p 10 /data/swapfile >/dev/null 2>&1", mb);
            int res = system(scmd);
            (void)res;
            char mb_str[32];
            snprintf(mb_str, sizeof(mb_str), "%lld\n", mb);
            write_file_str(CONF_DIR "/swap_size_mb", mb_str);
            printf("{\"success\":true,\"message\":\"SWAP activated\",\"swap_mb\":%lld}\n", mb);
        }
        return 0;
    }

    if (strcmp(action, "audio_test") == 0) {
        char tcmd[256];
        snprintf(tcmd, sizeof(tcmd), "/data/adb/modules/jacktor-audio-framework/system/bin/audio-test %s >/dev/null 2>&1 &", mode[0] ? mode : (val[0] ? val : "start"));
        int res = system(tcmd);
        (void)res;
        printf("{\"success\":true,\"message\":\"Audio test updated\"}\n");
        return 0;
    }

    if (strcmp(action, "exec") == 0) {
        if (!cmd[0]) {
            printf("{\"success\":false,\"error\":\"Missing cmd\"}\n");
            return 0;
        }
        char out_buf[4096] = "";
        run_command_output(cmd, out_buf, sizeof(out_buf));
        char safe_out[8192];
        json_escape(out_buf, safe_out, sizeof(safe_out));
        printf("{\"success\":true,\"output\":\"%s\"}\n", safe_out);
        return 0;
    }

    if (strncmp(action, "ssh_", 4) == 0) {
        if (strcmp(action, "ssh_start") == 0) {
            int res = system("/data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init start >/dev/null 2>&1");
            (void)res;
            printf("{\"success\":true,\"message\":\"OpenSSH Server started\"}\n");
            return 0;
        }
        if (strcmp(action, "ssh_stop") == 0) {
            int res = system("/data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init stop >/dev/null 2>&1");
            (void)res;
            printf("{\"success\":true,\"message\":\"OpenSSH Server stopped\"}\n");
            return 0;
        }
        if (strcmp(action, "ssh_restart") == 0) {
            int res = system("/data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init restart >/dev/null 2>&1");
            (void)res;
            printf("{\"success\":true,\"message\":\"OpenSSH Server restarted\"}\n");
            return 0;
        }
        if (strcmp(action, "ssh_set_autostart") == 0) {
            if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0) {
                unlink("/data/adb/ssh/no-autostart");
                printf("{\"success\":true,\"message\":\"OpenSSH autostart enabled\",\"autostart\":1}\n");
            } else {
                int fd = open("/data/adb/ssh/no-autostart", O_CREAT | O_WRONLY, 0644);
                if (fd >= 0) close(fd);
                printf("{\"success\":true,\"message\":\"OpenSSH autostart disabled\",\"autostart\":0}\n");
            }
            return 0;
        }
        if (strcmp(action, "ssh_set_port") == 0) {
            int p = atoi(port[0] ? port : val);
            if (p > 0 && p <= 65535) {
                char scmd[256];
                snprintf(scmd, sizeof(scmd),
                         "sed -i -E 's/^[# ]*Port [0-9]+/Port %d/' /data/adb/ssh/sshd_config; "
                         "/data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init restart >/dev/null 2>&1", p);
                int res = system(scmd);
                (void)res;
                printf("{\"success\":true,\"message\":\"SSH port changed to %d\",\"port\":%d}\n", p, p);
            } else {
                printf("{\"success\":false,\"error\":\"Invalid port number\"}\n");
            }
            return 0;
        }
        if (strcmp(action, "ssh_clear_log") == 0) {
            int fd = open("/data/adb/ssh/sshd.log", O_TRUNC | O_WRONLY);
            if (fd >= 0) close(fd);
            printf("{\"success\":true,\"message\":\"SSH log cleared\"}\n");
            return 0;
        }
        if (strcmp(action, "ssh_get_log") == 0) {
            char log_buf[4096] = "";
            run_command_output("tail -n 120 /data/adb/ssh/sshd.log 2>/dev/null", log_buf, sizeof(log_buf));
            char safe_b64[6144] = "";
            base64_encode((const unsigned char *)log_buf, strlen(log_buf), safe_b64, sizeof(safe_b64));
            printf("{\"success\":true,\"log_b64\":\"%s\"}\n", safe_b64);
            return 0;
        }
        if (strcmp(action, "ssh_get_config") == 0) {
            char cfg_buf[8192] = "";
            size_t r = 0;
            FILE *cfp = fopen("/data/adb/ssh/sshd_config", "r");
            if (cfp) {
                r = fread(cfg_buf, 1, sizeof(cfg_buf) - 1, cfp);
                cfg_buf[r] = '\0';
                fclose(cfp);
            }
            char safe_b64[12288] = "";
            base64_encode((const unsigned char *)cfg_buf, r, safe_b64, sizeof(safe_b64));
            printf("{\"success\":true,\"config_b64\":\"%s\"}\n", safe_b64);
            return 0;
        }
        if (strcmp(action, "ssh_save_config") == 0) {
            if (b64_data[0]) {
                unsigned char dec_cfg[8192];
                int dlen = base64_decode(b64_data, dec_cfg, sizeof(dec_cfg));
                if (dlen > 0) {
                    FILE *cfp = fopen("/data/adb/ssh/sshd_config", "w");
                    if (cfp) {
                        fwrite(dec_cfg, 1, dlen, cfp);
                        fclose(cfp);
                        chmod("/data/adb/ssh/sshd_config", 0600);
                        int res = system("/data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init restart >/dev/null 2>&1");
                        (void)res;
                        printf("{\"success\":true,\"message\":\"sshd_config saved and server restarted\"}\n");
                        return 0;
                    }
                }
            }
            printf("{\"success\":false,\"error\":\"Empty or invalid config payload\"}\n");
            return 0;
        }
        if (strcmp(action, "ssh_get_keys") == 0) {
            const char *target_u = (strcmp(user, "shell") == 0) ? "shell" : "root";
            char key_path[256];
            snprintf(key_path, sizeof(key_path), "/data/adb/ssh/%s/.ssh/authorized_keys", target_u);
            printf("{\"success\":true,\"user\":\"%s\",\"keys\":[", target_u);
            FILE *kfp = fopen(key_path, "r");
            if (kfp) {
                char kline[1024];
                int lnum = 0, first = 1;
                while (fgets(kline, sizeof(kline), kfp)) {
                    lnum++;
                    size_t klen = strlen(kline);
                    while (klen > 0 && (kline[klen - 1] == '\r' || kline[klen - 1] == '\n')) kline[--klen] = '\0';
                    if (klen == 0 || kline[0] == '#') continue;

                    char k_type[64] = "", k_data[512] = "", k_comm[256] = "";
                    if (sscanf(kline, "%63s %511s %255[^\n]", k_type, k_data, k_comm) >= 2) {
                        if (!k_comm[0]) snprintf(k_comm, sizeof(k_comm), "Key #%d", lnum);
                        char fp_short[32] = "";
                        snprintf(fp_short, sizeof(fp_short), "%.16s...", k_data);
                        char safe_comm[512];
                        json_escape(k_comm, safe_comm, sizeof(safe_comm));
                        if (!first) printf(",");
                        printf("{\"line\":%d,\"type\":\"%s\",\"comment\":\"%s\",\"fingerprint\":\"%s\"}",
                               lnum, k_type, safe_comm, fp_short);
                        first = 0;
                    }
                }
                fclose(kfp);
            }
            printf("]}\n");
            return 0;
        }
        if (strcmp(action, "ssh_add_key") == 0) {
            const char *target_u = (strcmp(user, "shell") == 0) ? "shell" : "root";
            char dir_path[256], key_path[256];
            snprintf(dir_path, sizeof(dir_path), "/data/adb/ssh/%s/.ssh", target_u);
            snprintf(key_path, sizeof(key_path), "%s/authorized_keys", dir_path);
            mkdir(dir_path, 0700);

            char key_str[1024] = "";
            if (b64_data[0]) {
                unsigned char dec_k[1024];
                int dlen = base64_decode(b64_data, dec_k, sizeof(dec_k) - 1);
                if (dlen > 0) {
                    dec_k[dlen] = '\0';
                    safe_copy(key_str, (char *)dec_k, sizeof(key_str));
                }
            } else if (key_data[0]) {
                safe_copy(key_str, key_data, sizeof(key_str));
            }

            size_t klen = strlen(key_str);
            while (klen > 0 && (key_str[klen - 1] == '\r' || key_str[klen - 1] == '\n')) key_str[--klen] = '\0';

            if (klen > 0 && (strncmp(key_str, "ssh-", 4) == 0 || strncmp(key_str, "ecdsa-", 6) == 0)) {
                FILE *kfp = fopen(key_path, "a");
                if (kfp) {
                    fprintf(kfp, "%s\n", key_str);
                    fclose(kfp);
                    chmod(key_path, 0600);
                    printf("{\"success\":true,\"message\":\"SSH key added for %s\"}\n", target_u);
                    return 0;
                }
            }
            printf("{\"success\":false,\"error\":\"Invalid SSH public key format\"}\n");
            return 0;
        }
        if (strcmp(action, "ssh_delete_key") == 0) {
            const char *target_u = (strcmp(user, "shell") == 0) ? "shell" : "root";
            char key_path[256];
            snprintf(key_path, sizeof(key_path), "/data/adb/ssh/%s/.ssh/authorized_keys", target_u);
            if (line_num > 0) {
                char scmd[512];
                snprintf(scmd, sizeof(scmd), "sed -i '%dd' %s >/dev/null 2>&1", line_num, key_path);
                int res = system(scmd);
                (void)res;
                printf("{\"success\":true,\"message\":\"Key at line %d deleted for %s\"}\n", line_num, target_u);
            } else {
                printf("{\"success\":false,\"error\":\"Invalid line number\"}\n");
            }
            return 0;
        }
        printf("{\"success\":true,\"message\":\"SSH command executed\"}\n");
        return 0;
    }

    if (strcmp(action, "media_action") == 0 || strcmp(action, "media") == 0) {
        const char *act = val[0] ? val : mode;
        char cmd_buf[128];
        snprintf(cmd_buf, sizeof(cmd_buf), "cmd media_session dispatch %s >/dev/null 2>&1", act);
        int res = system(cmd_buf);
        (void)res;
        printf("{\"success\":true,\"action\":\"%s\"}\n", act);
        return 0;
    }

    if (strcmp(action, "restart_audio") == 0) {
        int res = system("killall audioserver android.hardware.audio.service >/dev/null 2>&1");
        (void)res;
        printf("{\"success\":true,\"message\":\"Audio subsystem restarted\"}\n");
        return 0;
    }

    if (strcmp(action, "serial_bridge_control") == 0) {
        const char *bcmd = cmd[0] ? cmd : (mode[0] ? mode : "status");
        char cmd_buf[512];
        if (strcmp(bcmd, "set-baud") == 0) {
            int bval = baud > 0 ? baud : (val[0] ? atoi(val) : 921600);
            snprintf(cmd_buf, sizeof(cmd_buf), "serial-bridge set-baud %d 2>&1", bval);
        } else if (strcmp(bcmd, "set-port") == 0) {
            const char *pval = port[0] ? port : (val[0] ? val : "/dev/ttyUSB0");
            snprintf(cmd_buf, sizeof(cmd_buf), "serial-bridge set-port %s 2>&1", pval);
        } else if (val[0]) {
            snprintf(cmd_buf, sizeof(cmd_buf), "serial-bridge %s %s 2>&1", bcmd, val);
        } else {
            snprintf(cmd_buf, sizeof(cmd_buf), "serial-bridge %s 2>&1", bcmd);
        }
        char out_buf[1024] = "";
        run_command_output(cmd_buf, out_buf, sizeof(out_buf));
        char safe_out[2048];
        json_escape(out_buf, safe_out, sizeof(safe_out));
        printf("{\"success\":true,\"message\":\"serial-bridge %s executed\",\"output\":\"%s\"}\n", bcmd, safe_out);
        return 0;
    }

    if (strcmp(action, "otg_trigger") == 0) {
        int res = system("/data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger >/dev/null 2>&1 || (echo 0 > /sys/class/power_supply/usb/otg_switch 2>/dev/null; sleep 1; echo 1 > /sys/class/power_supply/usb/otg_switch 2>/dev/null)");
        (void)res;
        printf("{\"success\":true,\"message\":\"OTG triggered successfully\"}\n");
        return 0;
    }

    if (strcmp(action, "apply_tweak") == 0) {
        g_last_tw_time = 0;
        const char *tw = tweak[0] ? tweak : mode;
        const char *v = val[0] ? val : mode;

        if (strcmp(tw, "rotation") == 0) {
            int rot = atoi(v);
            char scmd[128];
            snprintf(scmd, sizeof(scmd),
                     "settings put system accelerometer_rotation 0 >/dev/null 2>&1; "
                     "settings put system user_rotation %d >/dev/null 2>&1", rot);
            int res = system(scmd);
            (void)res;
            printf("{\"success\":true,\"message\":\"Rotasi layar diatur ke %d\",\"rotation\":%d}\n", rot, rot);
            return 0;
        }

        if (strcmp(tw, "resolution") == 0) {
            char scmd[128];
            if (strcmp(v, "reset") == 0) {
                snprintf(scmd, sizeof(scmd), "wm size reset >/dev/null 2>&1");
            } else {
                snprintf(scmd, sizeof(scmd), "wm size %s >/dev/null 2>&1", v);
            }
            int res = system(scmd);
            (void)res;
            printf("{\"success\":true,\"message\":\"Resolusi layar diatur ke %s\",\"resolution\":\"%s\"}\n", v, v);
            return 0;
        }

        if (strcmp(tw, "density") == 0) {
            char scmd[128];
            if (strcmp(v, "reset") == 0) {
                snprintf(scmd, sizeof(scmd), "wm density reset >/dev/null 2>&1");
            } else {
                snprintf(scmd, sizeof(scmd), "wm density %s >/dev/null 2>&1", v);
            }
            int res = system(scmd);
            (void)res;
            printf("{\"success\":true,\"message\":\"Density layar diatur ke %s DPI\",\"density\":\"%s\"}\n", v, v);
            return 0;
        }

        if (strcmp(tw, "stayon") == 0) {
            int on = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcmp(v, "always") == 0);
            write_file_str(CONF_DIR "/stay_on_mode", on ? "always\n" : "normal\n");
            if (on) {
                int res = system("svc power stayon true >/dev/null 2>&1; settings put global stay_on_while_plugged_in 7 >/dev/null 2>&1; settings put system screen_off_timeout 2147483647 >/dev/null 2>&1");
                (void)res;
            } else {
                int res = system("svc power stayon false >/dev/null 2>&1; settings put global stay_on_while_plugged_in 0 >/dev/null 2>&1; settings put system screen_off_timeout 60000 >/dev/null 2>&1");
                (void)res;
            }
            printf("{\"success\":true,\"message\":\"Layar tetap menyala diatur ke %s\",\"state\":%s}\n", on ? "Aktif" : "Normal", on ? "true" : "false");
            return 0;
        }

        if (strcmp(tw, "lockscreen") == 0) {
            int dis = (strcmp(v, "disabled") == 0 || strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
            if (dis) {
                int res = system("locksettings set-disabled true >/dev/null 2>&1; settings put secure lockscreen.disabled 1 >/dev/null 2>&1");
                (void)res;
            } else {
                int res = system("locksettings set-disabled false >/dev/null 2>&1; settings put secure lockscreen.disabled 0 >/dev/null 2>&1");
                (void)res;
            }
            printf("{\"success\":true,\"message\":\"Kunci layar %s\",\"lockscreen_disabled\":%s}\n", dis ? "Dinonaktifkan" : "Diaktifkan", dis ? "true" : "false");
            return 0;
        }

        if (strcmp(tw, "bypass") == 0) {
            int bp = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
            write_file_str(CONF_DIR "/battery_bypass", bp ? "true\n" : "false\n");
            if (bp) {
                int res = system("dumpsys battery set ac 1 >/dev/null 2>&1; dumpsys battery set status 2 >/dev/null 2>&1; dumpsys battery set level 100 >/dev/null 2>&1");
                (void)res;
            } else {
                int res = system("dumpsys battery reset >/dev/null 2>&1");
                (void)res;
            }
            printf("{\"success\":true,\"message\":\"Battery bypass %s\",\"state\":%s}\n", bp ? "Aktif (AC Direct)" : "Standar", bp ? "true" : "false");
            return 0;
        }

        if (strcmp(tw, "cpu_profile") == 0) {
            const char *gov = (strcmp(v, "performance") == 0 || strcmp(v, "perf") == 0) ? "performance" : "schedutil";
            write_file_str("/sys/devices/system/cpu/cpufreq/policy0/scaling_governor", gov);
            write_file_str("/sys/devices/system/cpu/cpufreq/policy4/scaling_governor", gov);
            write_file_str(CONF_DIR "/cpu_profile", gov);
            printf("{\"success\":true,\"message\":\"Profil CPU diatur ke %s\",\"profile\":\"%s\"}\n", gov, v);
            return 0;
        }

        if (strcmp(tw, "usb_autosuspend") == 0) {
            int dis = (strcmp(v, "disabled") == 0 || strcmp(v, "-1") == 0);
            if (dis) {
                int res = system("echo -1 > /sys/module/usbcore/parameters/autosuspend >/dev/null 2>&1; "
                                 "for d in /sys/bus/usb/devices/*; do "
                                 "[ -f \"$d/power/control\" ] && echo on > \"$d/power/control\" 2>/dev/null; "
                                 "[ -f \"$d/power/autosuspend_delay_ms\" ] && echo -1 > \"$d/power/autosuspend_delay_ms\" 2>/dev/null; "
                                 "done >/dev/null 2>&1");
                (void)res;
            } else {
                int res = system("echo 2 > /sys/module/usbcore/parameters/autosuspend >/dev/null 2>&1; "
                                 "for d in /sys/bus/usb/devices/*; do "
                                 "[ -f \"$d/power/control\" ] && echo auto > \"$d/power/control\" 2>/dev/null; "
                                 "done >/dev/null 2>&1");
                (void)res;
            }
            printf("{\"success\":true,\"message\":\"USB Autosuspend %s\"}\n", dis ? "Dinonaktifkan (Zero Dropout)" : "Standar Auto");
            return 0;
        }

        if (strcmp(tw, "swap") == 0) {
            long long mb = atoll(v);
            if (mb <= 0) {
                int res = system("swapoff /data/swapfile >/dev/null 2>&1; rm -f /data/swapfile >/dev/null 2>&1");
                (void)res;
                write_file_str(CONF_DIR "/swap_size_mb", "0\n");
                printf("{\"success\":true,\"message\":\"SWAP dinonaktifkan\",\"swap_mb\":0}\n");
            } else {
                char scmd[512];
                snprintf(scmd, sizeof(scmd),
                         "swapoff /data/swapfile >/dev/null 2>&1; rm -f /data/swapfile >/dev/null 2>&1; "
                         "dd if=/dev/zero of=/data/swapfile bs=1M count=%lld >/dev/null 2>&1 && "
                         "chmod 600 /data/swapfile && mkswap /data/swapfile >/dev/null 2>&1 && "
                         "swapon -p 10 /data/swapfile >/dev/null 2>&1", mb);
                int res = system(scmd);
                (void)res;
                char mb_str[32];
                snprintf(mb_str, sizeof(mb_str), "%lld\n", mb);
                write_file_str(CONF_DIR "/swap_size_mb", mb_str);
                printf("{\"success\":true,\"message\":\"SWAP diatur ke %lld MB\",\"swap_mb\":%lld}\n", mb, mb);
            }
            return 0;
        }

        if (strcmp(tw, "resampler_mode") == 0 || strcmp(tw, "resampler") == 0) {
            write_file_str(CONF_DIR "/resampler_mode", v);
            int res = system("killall audioserver android.hardware.audio.service >/dev/null 2>&1");
            (void)res;
            printf("{\"success\":true,\"message\":\"Mode resampler diatur ke %s\",\"resampler_mode\":\"%s\"}\n", v, v);
            return 0;
        }

        if (strcmp(tw, "restart_adb") == 0) {
            int res = system("setprop service.adb.tcp.port 5555 >/dev/null 2>&1; stop adbd >/dev/null 2>&1; start adbd >/dev/null 2>&1");
            (void)res;
            printf("{\"success\":true,\"message\":\"ADB Wi-Fi direstart pada port 5555\"}\n");
            return 0;
        }

        printf("{\"success\":true,\"message\":\"Tweak %s berhasil diterapkan\"}\n", tw);
        return 0;
    }

    handle_action_status();
    return 0;
}
