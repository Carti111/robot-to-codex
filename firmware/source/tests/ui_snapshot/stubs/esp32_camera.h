#pragma once
#include <lvgl.h>
class Esp32Camera {
public:
 const lv_image_dsc_t* GetPreviewImage() const { return nullptr; }
 bool CapturePreview(uint8_t*, size_t, bool = false, bool = false) { return false; }
};
