// 桌面桩：esp_heap_caps.h
#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT     0
inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void* heap_caps_calloc(size_t n, size_t s, int) { return calloc(n, s); }
inline void  heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(int) { return 0; }
