#pragma once
#include "adc_oneshot.h"
typedef void* adc_cali_handle_t;
static inline int adc_cali_raw_to_voltage(void* handle, int raw, int* mv) { *mv = raw; return ESP_OK; }
