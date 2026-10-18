#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "memguard.h"

void *mem_alloc(int size, bool prefer_fast_memory) {
    void* ptr = NULL;

    if (!prefer_fast_memory) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    }

    if (!ptr) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }

    if (ptr) {
        memset(ptr, 0, size);
    }

    return ptr;
}

void *nes_mem_calloc(size_t nmemb, size_t size) {
    return mem_alloc(nmemb * size, false);
}

void *nes_mem_malloc(size_t size) {
    return mem_alloc(size, false);
}

void nes_mem_free(void *ptr) {
    heap_caps_free(ptr);
}
