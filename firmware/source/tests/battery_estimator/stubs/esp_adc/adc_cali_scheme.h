#pragma once
#include "adc_cali.h"
#define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 1
typedef struct { adc_unit_t unit_id; int chan; adc_atten_t atten; adc_bitwidth_t bitwidth; } adc_cali_curve_fitting_config_t;
static inline int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t* config, void** handle) { return ESP_FAIL; }
static inline int adc_cali_delete_scheme_curve_fitting(void* handle) { return ESP_OK; }
