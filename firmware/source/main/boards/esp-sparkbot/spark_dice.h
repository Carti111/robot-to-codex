#pragma once
#include <lvgl.h>
extern "C" {
#include "zbuffer.h"
#include <GL/gl.h>
}

// LVGL 9 image wrapper around the factory TinyGL renderer and original textures.
class SparkDice {
public:
    ~SparkDice();
    bool Start();
    void Draw(float x, float y, float z);
    const lv_image_dsc_t* Image() const { return buffer_ ? &image_ : nullptr; }
private:
    ZBuffer* buffer_ = nullptr;
    uint16_t* pixels_ = nullptr;
    GLuint textures_[6]{};
    lv_image_dsc_t image_{};
};
