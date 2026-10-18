#pragma once
#include <cstdint>
typedef void* TaskHandle_t;
#define pdMS_TO_TICKS(ms) (ms)
inline void vTaskDelay(uint32_t) {}
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int) { return 0; }
inline void vTaskDelete(TaskHandle_t) {}
