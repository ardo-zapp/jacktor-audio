#ifndef ESP_HEAP_CAPS_MOCK_H
#define ESP_HEAP_CAPS_MOCK_H

#include <stdlib.h>
#include <stdint.h>

#define MALLOC_CAP_DMA    (1 << 3)
#define MALLOC_CAP_32BIT  (1 << 13)

inline void* heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}

inline void heap_caps_free(void* ptr) {
    free(ptr);
}

#endif // ESP_HEAP_CAPS_MOCK_H
