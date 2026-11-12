#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define CONFIG_OCV_SOC_MODEL_1 1
#define CONFIG_ADC_FILTER_WINDOW_SIZE 10
#define CONFIG_BATTERY_CAPACITY_LPF_COEFFICIENT 2
#define CONFIG_BATTERY_STATE_SOFTWARE_ESTIMATION 1
#define CONFIG_SOFTWARE_ESTIMATION_SAMPLE_COUNT 10
#define CONFIG_SOFTWARE_ESTIMATION_SAMPLE_INTERVAL 20000
#define SOC_ADC_PERIPH_NUM 2
#define SOC_ADC_RTC_MIN_BITWIDTH 12
#define SOC_ADC_RTC_MAX_BITWIDTH 12
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ADC_UNIT_2 1
#define ADC_CHANNEL_3 3
#define ADC_BITWIDTH_DEFAULT 0
#define ADC_ATTEN_DB_12 3
typedef int esp_err_t;
typedef int adc_unit_t;
typedef int adc_channel_t;
typedef int adc_bitwidth_t;
typedef int adc_atten_t;
typedef void* adc_oneshot_unit_handle_t;
typedef struct { adc_unit_t unit_id; } adc_oneshot_unit_init_cfg_t;
typedef struct { adc_atten_t atten; adc_bitwidth_t bitwidth; } adc_oneshot_chan_cfg_t;
extern int test_pin_mv;
extern int test_read_count;
extern bool test_adc_failure;
static inline int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t* config, void** handle) { return ESP_FAIL; }
static inline int adc_oneshot_config_channel(void* handle, int channel, const adc_oneshot_chan_cfg_t* config) { return ESP_FAIL; }
static inline int adc_oneshot_read(void* handle, int channel, int* value) {
    ++test_read_count;
    if (test_adc_failure) return ESP_FAIL;
    *value = test_pin_mv;
    return ESP_OK;
}
static inline int adc_oneshot_del_unit(void* handle) { return ESP_OK; }
