/*
 * Memory allocator for TinyGL
 */
#include "zgl.h"
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

/* modify these functions so that they suit your needs */

void gl_free(void *p)
{
    free(p);
}

void *gl_malloc(int size)
{
#ifdef ESP_PLATFORM
    /* This CPU-only renderer does not need internal/DMA-capable memory. */
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

void *gl_zalloc(int size)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, size);
#endif
}
