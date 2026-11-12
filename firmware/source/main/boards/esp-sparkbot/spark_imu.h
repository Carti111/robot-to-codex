#pragma once
#include <driver/i2c_master.h>
#include "bmi270.h"
struct SparkMotionSample {
    float accel_x = 0, accel_y = 0, accel_z = 0;
    float gyro_x = 0, gyro_y = 0, gyro_z = 0;
};

class SparkImu {
public:
    bool Start(i2c_master_bus_handle_t bus);
    bool Read(float& x, float& y, float& z);
    bool ReadMotion(SparkMotionSample& sample);
    bool Ready() const { return ready_; }
private:
    i2c_master_dev_handle_t handle_ = nullptr;
    bmi2_dev sensor_{};
    bool ready_ = false;
    static int8_t ReadRegisters(uint8_t reg, uint8_t* data, uint32_t length, void* context);
    static int8_t WriteRegisters(uint8_t reg, const uint8_t* data, uint32_t length, void* context);
    static void Delay(uint32_t microseconds, void* context);
};
