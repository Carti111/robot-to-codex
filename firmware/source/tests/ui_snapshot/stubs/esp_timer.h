#pragma once
#include <stdint.h>
#include <cassert>
inline uint64_t snapshot_milliseconds = 0;
using esp_timer_handle_t = void*;
struct esp_timer_create_args_t { void (*callback)(void*); void* arg; const char* name; };
inline int64_t esp_timer_get_time() { return snapshot_milliseconds * 1000; }
inline int esp_timer_create(esp_timer_create_args_t*, void**) { return 0; }
inline int esp_timer_start_periodic(void*, int) { return 0; }
#define ESP_ERROR_CHECK(value) assert((value) == 0)
