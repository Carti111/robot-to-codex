#ifndef ESP32_CAMERA_H
#define ESP32_CAMERA_H

#include <esp_camera.h>
#include <lvgl.h>
#include <thread>
#include <memory>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class Esp32Camera : public Camera {
private:
    camera_fb_t* fb_ = nullptr;
    std::mutex capture_mutex_;
    bool chat_preview_ = true;
    lv_img_dsc_t preview_image_{};
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;

public:
    Esp32Camera(const camera_config_t& config);
    ~Esp32Camera();

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    bool CapturePreview(uint8_t* destination, size_t capacity, bool flip_vertical = false, bool mirror_horizontal = false);
    const lv_image_dsc_t* GetPreviewImage() const { return preview_image_.data ? &preview_image_ : nullptr; }
    void SetChatPreview(bool enabled) { chat_preview_ = enabled; }
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);
};

#endif // ESP32_CAMERA_H