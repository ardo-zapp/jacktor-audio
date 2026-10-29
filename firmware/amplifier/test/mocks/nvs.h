#pragma once
#include <cstdint>
#include <cstddef>
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef uint32_t nvs_handle_t;
typedef uint32_t nvs_handle;
inline int nvs_open(const char*, int, nvs_handle_t*) { return 1; }
inline int nvs_get_str(nvs_handle_t, const char*, char*, size_t*) { return 1; }
inline int nvs_get_u8(nvs_handle_t, const char*, uint8_t*) { return 1; }
inline int nvs_get_u16(nvs_handle_t, const char*, uint16_t*) { return 1; }
inline int nvs_get_u32(nvs_handle_t, const char*, uint32_t*) { return 1; }
inline void nvs_close(nvs_handle_t) {}
inline int nvs_set_str(nvs_handle_t, const char*, const char*) { return 0; }
inline int nvs_set_u8(nvs_handle_t, const char*, uint8_t) { return 0; }
inline int nvs_set_u16(nvs_handle_t, const char*, uint16_t) { return 0; }
inline int nvs_set_u32(nvs_handle_t, const char*, uint32_t) { return 0; }
inline int nvs_commit(nvs_handle_t) { return 0; }
