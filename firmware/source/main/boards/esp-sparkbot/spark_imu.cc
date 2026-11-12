#include "spark_imu.h"
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <vector>

extern const uint8_t bmi260_config_start[] asm("_binary_bmi260_init_data_fw_start");
extern const uint8_t bmi260_config_end[] asm("_binary_bmi260_init_data_fw_end");
int8_t SparkImu::ReadRegisters(uint8_t reg, uint8_t* data, uint32_t length, void* context) {
    auto self = static_cast<SparkImu*>(context);
    return i2c_master_transmit_receive(self->handle_, &reg, 1, data, length, 80) == ESP_OK ? 0 : -1;
}
int8_t SparkImu::WriteRegisters(uint8_t reg, const uint8_t* data, uint32_t length, void* context) {
    auto self = static_cast<SparkImu*>(context);
    std::vector<uint8_t> bytes(length + 1); bytes[0] = reg;
    memcpy(bytes.data() + 1, data, length);
    return i2c_master_transmit(self->handle_, bytes.data(), bytes.size(), 80) == ESP_OK ? 0 : -1;
}
void SparkImu::Delay(uint32_t microseconds, void*) {
    if (microseconds >= 1000) {
        TickType_t ticks = pdMS_TO_TICKS((microseconds + 999) / 1000);
        vTaskDelay(ticks > 0 ? ticks : 1);
    } else {
        esp_rom_delay_us(microseconds);
    }
}
bool SparkImu::Start(i2c_master_bus_handle_t bus) {
    for (uint16_t address : {0x68, 0x69}) {
        if (i2c_master_probe(bus, address, 30) != ESP_OK) continue;
        i2c_device_config_t config{}; config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = address; config.scl_speed_hz = 400000;
        if (i2c_master_bus_add_device(bus, &config, &handle_) != ESP_OK) continue;
        uint8_t chip = 0; ReadRegisters(0, &chip, 1, this);
        ESP_LOGI("SparkImu", "address=0x%02x chip=0x%02x", address, chip);
        sensor_.intf = BMI2_I2C_INTF; sensor_.intf_ptr = this;
        sensor_.read = ReadRegisters; sensor_.write = WriteRegisters; sensor_.delay_us = Delay;
        sensor_.read_write_len = 32; sensor_.config_file_ptr = nullptr;
        int8_t result = BMI2_E_DEV_NOT_FOUND;
        if (chip == BMI270_CHIP_ID) {
            result = bmi270_init(&sensor_);
        } else if (chip == 0x27) {
            // BMI260 shares the basic accelerometer registers, but needs its own configuration image.
            sensor_.chip_id = chip;
            sensor_.config_file_ptr = bmi260_config_start;
            sensor_.config_size = static_cast<uint16_t>(bmi260_config_end - bmi260_config_start);
            sensor_.variant_feature = 0;
            result = bmi2_sec_init(&sensor_);
        }
        if (result == BMI2_OK) {
            // Reuse the factory/Bosch accel + gyro configuration for pitch and yaw gestures.
            bmi2_sens_config config[2]{};
            config[0].type = BMI2_ACCEL;
            config[1].type = BMI2_GYRO;
            result = bmi2_get_sensor_config(config, 2, &sensor_);
            if (result == BMI2_OK) {
                config[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
                config[0].cfg.acc.range = BMI2_ACC_RANGE_2G;
                config[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
                config[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
                config[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
                config[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
                config[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
                config[1].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
                config[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
                result = bmi2_set_sensor_config(config, 2, &sensor_);
            }
            uint8_t enabled[] = {BMI2_ACCEL, BMI2_GYRO};
            if (result == BMI2_OK) result = bmi2_sensor_enable(enabled, 2, &sensor_);
        }
        if (result == BMI2_OK) {
            ready_ = true;
            ESP_LOGI("SparkImu", "accelerometer and gyroscope ready");
            Delay(100000, this);
            SparkMotionSample sample;
            if (ReadMotion(sample)) {
                ESP_LOGI("SparkImu", "initial acceleration g: %.3f %.3f %.3f", sample.accel_x, sample.accel_y, sample.accel_z);
                ESP_LOGI("SparkImu", "initial angular rate dps: %.2f %.2f %.2f", sample.gyro_x, sample.gyro_y, sample.gyro_z);
            }
            return true;
        }
        ESP_LOGW("SparkImu", "sensor initialization rejected: %d", result);
        i2c_master_bus_rm_device(handle_); handle_ = nullptr;
    }
    ESP_LOGW("SparkImu", "motion input unavailable"); return false;
}
bool SparkImu::ReadMotion(SparkMotionSample& sample) {
    if (!ready_) return false;
    bmi2_sens_data data{};
    if (bmi2_get_sensor_data(&data, &sensor_) != BMI2_OK) return false;
    if (!(data.status & BMI2_DRDY_ACC) || !(data.status & BMI2_DRDY_GYR)) return false;
    sample.accel_x = data.acc.x / 16384.0f;
    sample.accel_y = data.acc.y / 16384.0f;
    sample.accel_z = data.acc.z / 16384.0f;
    constexpr float GyroScale = 2000.0f / 32768.0f;
    sample.gyro_x = data.gyr.x * GyroScale;
    sample.gyro_y = data.gyr.y * GyroScale;
    sample.gyro_z = data.gyr.z * GyroScale;
    return true;
}

bool SparkImu::Read(float& x, float& y, float& z) {
    SparkMotionSample sample;
    if (!ReadMotion(sample)) return false;
    x = sample.accel_x;
    y = sample.accel_y;
    z = sample.accel_z;
    return true;
}
