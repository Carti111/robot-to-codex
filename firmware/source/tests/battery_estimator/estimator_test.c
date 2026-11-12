// Link the unmodified official component against deterministic ADC/time fixtures.
#include <assert.h>
#ifdef NDEBUG
#error Battery regression assertions must remain enabled
#endif
#include <stdio.h>
#include "adc_battery_estimation.h"
int64_t test_time_us = 0;
int test_pin_mv = 0;
int test_read_count = 0;
bool test_adc_failure = false;
static bool charging = false;
static bool charging_state(void* context) { return charging; }
static void* create(bool explicit_charging) {
    adc_battery_estimation_t config = {0};
    config.external.adc_handle = (void*)(uintptr_t)0x1000;
    config.external.adc_cali_handle = (void*)(uintptr_t)0x2000;
    config.adc_channel = ADC_CHANNEL_3;
    config.upper_resistor = config.lower_resistor = 100000;
    if (explicit_charging) config.charging_detect_cb = charging_state;
    void* handle = adc_battery_estimation_create(&config);
    assert(handle);
    return handle;
}
static float capacity(void* handle, int battery_mv) {
    test_pin_mv = battery_mv / 2;
    float result = -1;
    assert(adc_battery_estimation_get_capacity(handle, &result) == ESP_OK);
    return result;
}
int main(void) {
    const int voltages[] = {4160, 4070, 3720};
    const float expected[] = {100, 90, 50};
    for (int i = 0; i < 3; ++i) {
        void* handle = create(true);
        float result = capacity(handle, voltages[i]);
        assert(result > expected[i] - 0.01f && result < expected[i] + 0.01f);
        assert(adc_battery_estimation_destroy(handle) == ESP_OK);
    }
    assert(test_read_count == 30); // Official ten-reading filter per estimation call.
    void* handle = create(true);
    float initial = capacity(handle, 3900);
    assert(initial > 69.9f && initial < 70.1f);
    float held = capacity(handle, 4200);
    assert(held == initial); // Official discharge estimates cannot increase.
    charging = true;
    float risen = capacity(handle, 4200);
    assert(risen > 75.9f && risen < 76.1f); // Official 0.2 low-pass capacity filter.
    assert(capacity(handle, 3700) == risen); // Official charge estimates cannot decrease.
    test_adc_failure = true;
    float unavailable = -1;
    assert(adc_battery_estimation_get_capacity(handle, &unavailable) != ESP_OK);
    assert(unavailable == -1);
    test_adc_failure = false;
    assert(adc_battery_estimation_destroy(handle) == ESP_OK);

    test_time_us = 0;
    handle = create(false);
    assert(capacity(handle, 0) == 0);
    for (int second = 1; second < 200; ++second) {
        test_time_us = (int64_t)second * 1000000;
        assert(capacity(handle, second <= 20 ? 0 : 4200) == 0);
    }
    test_time_us = 200000000;
    assert(capacity(handle, 4200) > 0); // Reproduces the original 200-second charging-classifier latch.
    assert(adc_battery_estimation_destroy(handle) == ESP_OK);
    puts("PASS unmodified official OCV curve, ten-read filter, LPF, direction clamp, ADC failure and 200-second latch reproduction");
    return 0;
}
