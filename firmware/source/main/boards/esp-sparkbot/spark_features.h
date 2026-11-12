#pragma once
#include "spark_menu_state.h"
#include "spark_game_2048.h"
#include "spark_imu.h"
#include "spark_dice.h"
#include "spark_tilt_input.h"
#include "display.h"
#include "esp32_camera.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <atomic>
#include <string>

class SparkFeatures {
public:
    SparkFeatures(Display* display, Esp32Camera* camera, i2c_master_bus_handle_t bus);
    void Side(int direction);
    void Top();
    bool TopOnPress();
    void LongTop();
    void Tick();
    bool AllowVoiceChat() const { return voice_chat_allowed_.load(); }
    bool PairingNotice(const std::string& event, const std::string& code);
private:
    Display* display_;
    Esp32Camera* camera_;
    SparkImu imu_;
    SparkDice dice_;
    SparkTiltInput tilt_;
    SparkMenuState menu_;
    SparkPage rendered_ = SparkPage::Count;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* detail_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    lv_obj_t* hour_ = nullptr;
    lv_obj_t* minute_ = nullptr;
    lv_obj_t* preview_ = nullptr;
    lv_obj_t* preview_title_ = nullptr;
    lv_obj_t* pairing_ = nullptr;
    lv_obj_t* pairing_label_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* tiles_[4][4]{};
    esp_timer_handle_t timer_ = nullptr;
    std::atomic<bool> tick_pending_{false};
    std::atomic<bool> voice_chat_allowed_{true};
    std::atomic<bool> camera_active_{false};
    TaskHandle_t camera_task_ = nullptr;
    uint8_t* camera_buffers_[2]{};
    lv_image_dsc_t camera_image_{};
    unsigned camera_front_ = 0;
    unsigned camera_frames_ = 0;
    bool camera_running_ = true;
    bool pairing_visible_ = false;
    uint64_t pairing_until_ = 0;
    uint64_t pairing_closed_at_ = 0;
    SparkGame2048 game_{};
    unsigned merit_ = 0;
    bool shake_armed_ = true;
    uint64_t last_motion_ = 0;
    uint64_t game_diagnostics_until_ = 0;
    uint64_t last_game_diagnostic_ = 0;
    unsigned game_diagnostic_count_ = 0;
    uint64_t roll_started_ = 0;
    uint64_t roll_until_ = 0;
    uint64_t shake_quiet_ = 0;
    uint64_t last_frame_warning_ = 0;
    long long clock_minute_ = -1;
    float rotations_[3]{};
    float roll_from_[3]{};
    float destinations_[3]{};
    std::string status_;
    void ActivatePage();
    void Render();
    void CreateClock();
    void CreateCodex();
    void CreateMuyu();
    void CreateDice();
    void CreateGame();
    void CreateCamera();
    void RefreshClock();
    void RefreshCodex();
    void RefreshGame();
    void Knock();
    void ClosePairing();
    void Motion(uint64_t now);
    void Roll(uint64_t now);
    void AnimateDice(uint64_t now);
    void CameraLoop();
    static const char* Name(SparkPage page);
};
