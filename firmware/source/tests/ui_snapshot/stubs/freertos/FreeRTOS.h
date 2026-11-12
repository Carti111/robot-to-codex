#pragma once
#include <stdint.h>
using TaskHandle_t = void*;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffff
#define pdMS_TO_TICKS(value) (value)
