#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t bytes, unsigned) { return malloc(bytes); }
inline void heap_caps_free(void* value) { free(value); }
