#include "spark_features.h"
#include "spark_factory_assets.h"
#include "application.h"
#include "assets/lang_config.h"
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_log.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#ifdef ESP_PLATFORM
#include <driver/usb_serial_jtag.h>
#endif

LV_FONT_DECLARE(font_puhui_20_4);
static constexpr uint32_t Gold = 0xf1ba3b;
static uint64_t FeatureMillis() { return esp_timer_get_time() / 1000; }

static lv_obj_t* Label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                       lv_align_t align, int x, int y, uint32_t color = 0xffffff) {
    auto label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, align, x, y);
    return label;
}

const char* SparkFeatures::Name(SparkPage page) {
    static const char* names[] = {"小智聊天", "时钟", "Codex 输入", "木鱼", "骰子", "2048", "摄像头"};
    return names[static_cast<int>(page)];
}

SparkFeatures::SparkFeatures(Display* display, Esp32Camera* camera, i2c_master_bus_handle_t bus)
    : display_(display), camera_(camera) {
    imu_.Start(bus);
    dice_.Start();
    spark_game_reset(&game_);
    {
        DisplayLockGuard lock(display_);
        root_ = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(root_);
        lv_obj_set_size(root_, 240, 240);
        lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
        preview_ = lv_obj_create(lv_layer_top());
        lv_obj_set_size(preview_, 224, 130);
        lv_obj_center(preview_);
        lv_obj_set_style_bg_color(preview_, lv_color_hex(0x202020), 0);
        lv_obj_set_style_border_color(preview_, lv_color_hex(Gold), 0);
        lv_obj_set_style_border_width(preview_, 1, 0);
        lv_obj_set_style_radius(preview_, 12, 0);
        lv_obj_set_style_pad_all(preview_, 8, 0);
        lv_obj_remove_flag(preview_, LV_OBJ_FLAG_SCROLLABLE);
        preview_title_ = Label(preview_, "", &font_puhui_20_4, LV_ALIGN_TOP_MID, 0, 5, Gold);
        Label(preview_, "轻触顶部确认\n3 秒后继续听写", &font_puhui_20_4, LV_ALIGN_TOP_MID, 0, 44);
        lv_obj_add_flag(preview_, LV_OBJ_FLAG_HIDDEN);
        pairing_ = lv_obj_create(lv_layer_top());
        lv_obj_set_size(pairing_, 224, 190);
        lv_obj_center(pairing_);
        lv_obj_set_style_bg_color(pairing_, lv_color_hex(0x202020), 0);
        lv_obj_set_style_border_color(pairing_, lv_color_hex(Gold), 0);
        lv_obj_set_style_radius(pairing_, 12, 0);
        lv_obj_set_style_pad_all(pairing_, 8, 0);
        lv_obj_remove_flag(pairing_, LV_OBJ_FLAG_SCROLLABLE);
        pairing_label_ = Label(pairing_, "", &font_puhui_20_4, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(pairing_, LV_OBJ_FLAG_HIDDEN);
        Render();
    }
    if (xTaskCreate([](void* self) { static_cast<SparkFeatures*>(self)->CameraLoop(); },
                    "feature_camera", 4096, this, 2, &camera_task_) != pdPASS) {
        camera_task_ = nullptr;
        ESP_LOGW("SparkFeatures", "camera preview task unavailable");
    }
    esp_timer_create_args_t args{};
    args.name = "spark_features";
    args.arg = this;
    args.callback = [](void* context) {
        auto self = static_cast<SparkFeatures*>(context);
        if (self->tick_pending_.exchange(true)) return;
        Application::GetInstance().Schedule([self]() {
            self->Tick();
            self->tick_pending_ = false;
        });
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, 40000));
}

void SparkFeatures::ActivatePage() {
    camera_active_ = false;
    voice_chat_allowed_ = menu_.page == SparkPage::Chat && !menu_.exiting;
    Application::GetInstance().EndFeatureAudio();
    DisplayLockGuard lock(display_);
    Render();
    if (menu_.page == SparkPage::Camera && camera_task_ && camera_buffers_[0]) {
        camera_active_ = true;
        xTaskNotifyGive(camera_task_);
    }
}

void SparkFeatures::Render() {
    rendered_ = menu_.page;
    display_->SetChatVisible(menu_.page == SparkPage::Chat);
    lv_obj_clean(root_);
    body_ = detail_ = footer_ = hour_ = minute_ = image_ = nullptr;
    status_.clear();
    for (auto& row : tiles_) for (auto& tile : row) tile = nullptr;
    if (menu_.page == SparkPage::Chat) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    switch (menu_.page) {
        case SparkPage::Clock: CreateClock(); break;
        case SparkPage::Codex: CreateCodex(); break;
        case SparkPage::Muyu: CreateMuyu(); break;
        case SparkPage::Dice: CreateDice(); break;
        case SparkPage::Game: CreateGame(); break;
        case SparkPage::Camera: CreateCamera(); break;
        default: break;
    }
    ESP_LOGI("SparkFeatures", "page=%d", static_cast<int>(menu_.page));
}

void SparkFeatures::CreateClock() {
    body_ = Label(root_, "", &ui_font_OPPOSansH25, LV_ALIGN_TOP_MID, 0, 30, Gold);
    detail_ = Label(root_, "", &ui_font_OPPOSansH25, LV_ALIGN_TOP_MID, 0, 82);
    hour_ = Label(root_, "--", &ui_font_ComfortaaBold75, LV_ALIGN_BOTTOM_MID, -60, 0);
    minute_ = Label(root_, "--", &ui_font_ComfortaaBold75, LV_ALIGN_BOTTOM_MID, 60, 0, Gold);
    Label(root_, ":", &ui_font_ComfortaaBold75, LV_ALIGN_BOTTOM_MID, 0, -8);
    clock_minute_ = -1;
    RefreshClock();
}

void SparkFeatures::RefreshClock() {
    auto timestamp = std::time(nullptr);
    long long current = timestamp / 60;
    if (clock_minute_ == current) return;
    clock_minute_ = current;
    if (timestamp < 1700000000) {
        lv_label_set_text(body_, "等待网络校时");
        lv_label_set_text(detail_, "");
        return;
    }
    std::tm local{};
    localtime_r(&timestamp, &local);
    static const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    char date[32], hour[8], minute[8];
    strftime(date, sizeof(date), "%Y / %m / %d", &local);
    strftime(hour, sizeof(hour), "%H", &local);
    strftime(minute, sizeof(minute), "%M", &local);
    lv_label_set_text(body_, weekdays[local.tm_wday]);
    lv_label_set_text(detail_, date);
    lv_label_set_text(hour_, hour);
    lv_label_set_text(minute_, minute);
}

void SparkFeatures::CreateCodex() {
    Label(root_, "Codex", &ui_font_OPPOSansH25, LV_ALIGN_TOP_MID, 0, 24);
    body_ = Label(root_, "", &font_puhui_20_4, LV_ALIGN_TOP_MID, 0, 80);
    auto action = lv_obj_create(root_);
    lv_obj_remove_style_all(action);
    lv_obj_set_size(action, 174, 48);
    lv_obj_align(action, LV_ALIGN_TOP_MID, 0, 125);
    lv_obj_set_style_bg_color(action, lv_color_hex(Gold), 0);
    lv_obj_set_style_bg_opa(action, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(action, 8, 0);
    detail_ = Label(action, "", &font_puhui_20_4, LV_ALIGN_CENTER, 0, 0, 0x000000);
    footer_ = Label(root_, "", &font_puhui_20_4, LV_ALIGN_TOP_MID, 0, 196, 0xb0b0b0);
    RefreshCodex();
}

void SparkFeatures::RefreshCodex() {
    auto& app = Application::GetInstance();
    std::string text = menu_.exiting ? "正在退出听写" : app.IsVoiceInputLocked() ?
        (app.IsVoiceInputPaused() ? "听写已暂停" : "正在听写") :
        app.IsVoiceInputConnected() ? "电脑已连接" : "等待电脑连接";
    if (text == status_) return;
    status_ = text;
    lv_label_set_text(body_, text.c_str());
    lv_label_set_text(detail_, app.IsVoiceInputLocked() ?
        (app.IsVoiceInputPaused() ? "轻触顶部继续" : "说发送以提交") : "轻触顶部开始");
    lv_label_set_text(footer_, app.IsVoiceInputLocked() ? "侧边预选 顶部确认" : "长触顶部配对电脑");
}

void SparkFeatures::CreateMuyu() {
    image_ = lv_image_create(root_);
    lv_image_set_src(image_, &spark_muyu_image);
    lv_obj_align(image_, LV_ALIGN_CENTER, 0, 20);
    body_ = Label(root_, "", &ui_font_OPPOSansH18, LV_ALIGN_TOP_MID, 0, 24, Gold);
    char text[64];
    snprintf(text, sizeof(text), "今日功德 %u", merit_);
    lv_label_set_text(body_, text);
    detail_ = Label(root_, "功德+1", &ui_font_OPPOSansH25, LV_ALIGN_TOP_MID, -54, 50);
    lv_obj_set_style_text_opa(detail_, LV_OPA_TRANSP, 0);
}

void SparkFeatures::Knock() {
    ++merit_;
    Application::GetInstance().PlaySound(Lang::Sounds::OGG_SPARK_MUYU);
    DisplayLockGuard lock(display_);
    char text[64];
    snprintf(text, sizeof(text), "今日功德 %u", merit_);
    lv_label_set_text(body_, text);
    lv_anim_delete(image_, nullptr);
    lv_anim_t scale;
    lv_anim_init(&scale);
    lv_anim_set_var(&scale, image_);
    lv_anim_set_exec_cb(&scale, [](void* object, int32_t value) { lv_image_set_scale(static_cast<lv_obj_t*>(object), value); });
    lv_anim_set_values(&scale, 192, 256);
    lv_anim_set_duration(&scale, 240);
    lv_anim_set_path_cb(&scale, lv_anim_path_overshoot);
    lv_anim_start(&scale);
    lv_anim_delete(detail_, nullptr);
    lv_anim_t merit;
    lv_anim_init(&merit);
    lv_anim_set_var(&merit, detail_);
    lv_anim_set_exec_cb(&merit, [](void* object, int32_t value) {
        auto label = static_cast<lv_obj_t*>(object);
        lv_obj_set_style_text_opa(label, 255 - value * 255 / 100, 0);
        lv_obj_set_y(label, 50 - value / 5);
    });
    lv_anim_set_values(&merit, 0, 100);
    lv_anim_set_duration(&merit, 600);
    lv_anim_start(&merit);
}

void SparkFeatures::CreateDice() {
    if (dice_.Image()) {
        image_ = lv_image_create(root_);
        lv_image_set_src(image_, dice_.Image());
        lv_obj_align(image_, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    auto button = lv_obj_create(root_);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 100, 50);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, -70);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(Gold), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 2, 0);
    lv_obj_set_style_shadow_color(button, lv_color_white(), 0);
    lv_obj_set_style_shadow_ofs_x(button, 2, 0);
    lv_obj_set_style_shadow_ofs_y(button, 2, 0);
    Label(button, "掷骰子", &ui_font_OPPOSansH25, LV_ALIGN_CENTER, 0, 0);
    if (!dice_.Image()) Label(root_, "骰子图像未就绪", &font_puhui_20_4, LV_ALIGN_CENTER, 0, 35);
}

void SparkFeatures::CreateGame() {
    auto board = lv_obj_create(root_);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, 240, 200);
    lv_obj_align(board, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(board, lv_color_hex(0xb3a397), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            auto tile = lv_label_create(board);
            tiles_[x][y] = tile;
            lv_obj_set_pos(tile, 10 + x * 56, 10 + y * 46);
            lv_obj_set_size(tile, 50, 40);
            lv_obj_set_style_radius(tile, 3, 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_set_style_text_font(tile, &ui_font_OPPOSansH25, 0);
            lv_obj_set_style_text_align(tile, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(tile, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_pad_top(tile, 5, 0);
        }
    }
    detail_ = Label(root_, "", &font_puhui_20_4, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_recolor(detail_, true);
    tilt_.Reset();
    game_diagnostics_until_ = FeatureMillis() + 20000;
    game_diagnostic_count_ = 0;
    RefreshGame();
    if (!imu_.Ready()) lv_label_set_text(detail_, "运动传感器未就绪");
}

void SparkFeatures::RefreshGame() {
    static const uint32_t colors[] = {
        0xc7b9ac, 0xeee4da, 0xf7d28d, 0xf2b179, 0xf59563, 0xf67c5f,
        0xf75f3b, 0xedcf72, 0xedcc61, 0xedc850, 0xedc53f, 0xedc22e
    };
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 4; ++y) {
            unsigned exponent = game_.cells[x][y];
            char text[16] = "";
            if (exponent) snprintf(text, sizeof(text), "%u", 1U << std::min(exponent, 30U));
            lv_label_set_text(tiles_[x][y], text);
            // Font follows rendered digits: 25px digits advance ~16.9px, so three digits
            // exceed the 50px tile and used to wrap onto a second line outside the tile.
            bool compact = strlen(text) > 2;
            lv_obj_set_style_text_font(tiles_[x][y], compact ? &ui_font_OPPOSansH18 : &ui_font_OPPOSansH25, 0);
            lv_obj_set_style_pad_top(tiles_[x][y], compact ? 9 : 5, 0);
            lv_obj_set_style_bg_color(tiles_[x][y], lv_color_hex(colors[std::min(exponent, 11U)]), 0);
            lv_obj_set_style_text_color(tiles_[x][y], exponent <= 2 ? lv_color_hex(0x6c635b) : lv_color_hex(0xf8f5f0), 0);
        }
    }
    char text[72];
    snprintf(text, sizeof(text), "%s #ff0000 %u#", spark_game_over(&game_) ? "GAME OVER" : "SCORE:", static_cast<unsigned>(game_.score));
    lv_label_set_text(detail_, text);
}

void SparkFeatures::CreateCamera() {
    const auto source = camera_->GetPreviewImage();
    if (!source || !camera_task_) {
        Label(root_, "摄像头未就绪", &font_puhui_20_4, LV_ALIGN_CENTER, 0, 0);
        return;
    }
    if (!camera_buffers_[0]) {
        camera_buffers_[0] = static_cast<uint8_t*>(heap_caps_malloc(source->data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        camera_buffers_[1] = static_cast<uint8_t*>(heap_caps_malloc(source->data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!camera_buffers_[0] || !camera_buffers_[1]) {
            heap_caps_free(camera_buffers_[0]);
            heap_caps_free(camera_buffers_[1]);
            camera_buffers_[0] = camera_buffers_[1] = nullptr;
            Label(root_, "摄像头内存不足", &font_puhui_20_4, LV_ALIGN_CENTER, 0, 0);
            return;
        }
        camera_image_ = *source;
        camera_image_.data = camera_buffers_[0];
    }
    image_ = lv_image_create(root_);
    lv_obj_center(image_);
    footer_ = Label(root_, "正在打开摄像头", &font_puhui_20_4, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(footer_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(footer_, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(footer_, 3, 0);
    camera_frames_ = 0;
    camera_running_ = true;
}

void SparkFeatures::CameraLoop() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (camera_active_) {
            unsigned back = 1 - camera_front_;
            if (camera_->CapturePreview(camera_buffers_[back], camera_image_.data_size, true, true)) {
                DisplayLockGuard lock(display_);
                if (camera_active_ && image_) {
                    camera_front_ = back;
                    camera_image_.data = camera_buffers_[back];
                    lv_image_set_src(image_, &camera_image_);
                    lv_obj_invalidate(image_);
                    if (camera_frames_++ == 0) lv_label_set_text(footer_, "轻触顶部暂停");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void SparkFeatures::Side(int direction) {
    if (pairing_visible_) return;
    auto& app = Application::GetInstance();
    bool changed = menu_.Navigate(direction, app.IsVoiceInputLocked(), FeatureMillis());
    voice_chat_allowed_ = menu_.page == SparkPage::Chat && !menu_.exiting;
    if (changed) ActivatePage();
    if (menu_.preview >= 0) {
        DisplayLockGuard lock(display_);
        char text[80];
        snprintf(text, sizeof(text), "切换到%s？", Name(static_cast<SparkPage>(menu_.preview)));
        lv_label_set_text(preview_title_, text);
        lv_obj_remove_flag(preview_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool SparkFeatures::TopOnPress() {
    if (pairing_visible_) {
        ClosePairing();
        return true;
    }
    if (menu_.page != SparkPage::Muyu || menu_.preview >= 0 ||
        menu_.exiting || Application::GetInstance().IsVoiceInputLocked()) return false;
    Knock();
    return true;
}

void SparkFeatures::Top() {
    auto& app = Application::GetInstance();
    if (pairing_visible_) return;
    if (menu_.preview >= 0) {
        bool changed = menu_.Confirm(app.IsVoiceInputLocked(), FeatureMillis());
        voice_chat_allowed_ = menu_.page == SparkPage::Chat && !menu_.exiting;
        if (menu_.exiting) app.StopVoiceInputFromTouch();
        {
            DisplayLockGuard lock(display_);
            lv_obj_add_flag(preview_, LV_OBJ_FLAG_HIDDEN);
        }
        if (changed) ActivatePage();
        return;
    }
    if (menu_.exiting) return;
    switch (menu_.page) {
        case SparkPage::Chat: app.ToggleChatState(); break;
        case SparkPage::Codex:
            if (!app.IsVoiceInputLocked()) app.StartVoiceInputFromTouch();
            else if (app.IsVoiceInputPaused()) app.WakeWordInvoke("继续听写");
            break;
        case SparkPage::Muyu: Knock(); break;
        case SparkPage::Dice: Roll(FeatureMillis()); break;
        case SparkPage::Game: {
            spark_game_reset(&game_);
            tilt_.Reset();
            DisplayLockGuard lock(display_);
            RefreshGame();
            break;
        }
        case SparkPage::Camera: {
            if (!footer_ || !camera_task_ || !camera_buffers_[0]) break;
            camera_running_ = !camera_running_;
            camera_active_ = camera_running_;
            DisplayLockGuard lock(display_);
            lv_label_set_text(footer_, camera_running_ ? "轻触顶部暂停" : "已暂停 顶部继续");
            if (camera_running_) xTaskNotifyGive(camera_task_);
            break;
        }
        default: break;
    }
}

void SparkFeatures::LongTop() {
    if (Application::GetInstance().IsVoiceInputLocked()) return;
    if (menu_.page != SparkPage::Codex) return;
    if (pairing_closed_at_ && FeatureMillis() - pairing_closed_at_ < 3500) return;
    Application::GetInstance().BeginVoicePairing();
}

bool SparkFeatures::PairingNotice(const std::string& event, const std::string& code) {
    pairing_visible_ = true;
    pairing_until_ = event == "pairing" ? 0 : FeatureMillis() + 1600;
    camera_active_ = false;
    DisplayLockGuard lock(display_);
    std::string text;
    if (event == "pairing") text = "电脑配对码\n" + code + "\n\n两分钟内有效\n轻触顶部关闭";
    else text = event == "paired" ? "电脑配对成功" : event == "pair_expired" ? "配对已超时" : "配对已结束";
    lv_label_set_text(pairing_label_, text.c_str());
    lv_obj_remove_flag(pairing_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void SparkFeatures::ClosePairing() {
    pairing_visible_ = false;
    pairing_until_ = 0;
    pairing_closed_at_ = FeatureMillis();
    DisplayLockGuard lock(display_);
    lv_obj_add_flag(pairing_, LV_OBJ_FLAG_HIDDEN);
    if (menu_.page == SparkPage::Camera && camera_running_ && camera_task_ && camera_buffers_[0]) {
        camera_active_ = true;
        xTaskNotifyGive(camera_task_);
    }
}

void SparkFeatures::Roll(uint64_t now) {
    if (now < roll_until_) return;
    roll_started_ = now;
    roll_until_ = now + 900;
    for (int i = 0; i < 3; ++i) {
        roll_from_[i] = rotations_[i];
        destinations_[i] = rotations_[i] + (3 + esp_random() % 6) * 90.0f;
    }
    ESP_LOGI("SparkFeatures", "dice roll");
}

void SparkFeatures::AnimateDice(uint64_t now) {
    if (!roll_until_ || !image_) return;
    float progress = std::min(1.0f, static_cast<float>(now - roll_started_) / 900.0f);
    float remaining = 1 - progress;
    float eased = 1 - remaining * remaining * remaining;
    for (int i = 0; i < 3; ++i) rotations_[i] = roll_from_[i] + (destinations_[i] - roll_from_[i]) * eased;
    auto started = FeatureMillis();
    DisplayLockGuard lock(display_);
    dice_.Draw(rotations_[0], rotations_[1], rotations_[2]);
    lv_obj_invalidate(image_);
    if (progress >= 1) {
        roll_until_ = 0;
        for (auto& angle : rotations_) angle = std::fmod(angle, 360.0f);
    }
    unsigned duration = static_cast<unsigned>(FeatureMillis() - started);
    if (duration > 35 && now - last_frame_warning_ >= 5000) {
        last_frame_warning_ = now;
        ESP_LOGW("SparkFeatures", "dice frame duration=%u ms", duration);
    }
}

void SparkFeatures::Motion(uint64_t now) {
    if (now - last_motion_ < 40) return;
    last_motion_ = now;
    SparkMotionSample sample;
    uint64_t read_started = FeatureMillis();
    if (!imu_.ReadMotion(sample)) return;
    float magnitude = std::sqrt(sample.accel_x * sample.accel_x + sample.accel_y * sample.accel_y + sample.accel_z * sample.accel_z);
    if (menu_.page == SparkPage::Dice) {
        if (std::fabs(magnitude - 1) > 0.55f && shake_armed_) {
            Roll(now);
            shake_armed_ = false;
            shake_quiet_ = now;
        }
        if (std::fabs(magnitude - 1) < 0.2f && now - shake_quiet_ > 700) shake_armed_ = true;
    } else if (menu_.page == SparkPage::Game) {
#ifdef ESP_PLATFORM
        // Bounded, USB-only maintenance diagnostics. No text, pairing codes or network credentials.
        if (usb_serial_jtag_is_connected() && now < game_diagnostics_until_ && game_diagnostic_count_ < 20 &&
            now - last_game_diagnostic_ >= 200 &&
            std::max({std::fabs(sample.gyro_x), std::fabs(sample.gyro_y), std::fabs(sample.gyro_z)}) > 15) {
            last_game_diagnostic_ = now;
            ++game_diagnostic_count_;
            ESP_LOGI("SparkFeatures", "2048 gyro dps=%.1f %.1f %.1f read=%u ms", sample.gyro_x, sample.gyro_y, sample.gyro_z,
                     static_cast<unsigned>(FeatureMillis() - read_started));
        }
#endif
        // The official factory profile maps pitch (gyro Y) to up/down and yaw (gyro Z) to left/right.
        int direction = tilt_.Update(sample.gyro_y, sample.gyro_z, now);
        if (direction < 0) return;
        bool moved = spark_game_move(&game_, direction);
        ESP_LOGI("SparkFeatures", "2048 direction=%d moved=%d", direction, moved);
        if (moved) {
            DisplayLockGuard lock(display_);
            RefreshGame();
        }
    }
}

void SparkFeatures::Tick() {
    auto& app = Application::GetInstance();
    uint64_t now = FeatureMillis();
    if (pairing_visible_) {
        if (!pairing_until_ || now < pairing_until_) return;
        ClosePairing();
    }
    if (app.IsVoiceInputLocked() && menu_.page != SparkPage::Codex && !menu_.exiting) menu_.page = SparkPage::Codex;
    bool had_preview = menu_.preview >= 0;
    menu_.Tick(app.IsVoiceInputLocked(), now);
    voice_chat_allowed_ = menu_.page == SparkPage::Chat && !menu_.exiting;
    if (rendered_ != menu_.page) ActivatePage();
    if (had_preview && menu_.preview < 0) {
        DisplayLockGuard lock(display_);
        lv_obj_add_flag(preview_, LV_OBJ_FLAG_HIDDEN);
    }
    if (menu_.page == SparkPage::Dice || menu_.page == SparkPage::Game) Motion(now);
    if (menu_.page == SparkPage::Dice) AnimateDice(now);
    if (menu_.page == SparkPage::Clock && clock_minute_ != std::time(nullptr) / 60) {
        DisplayLockGuard lock(display_);
        RefreshClock();
    }
    if (menu_.page == SparkPage::Codex) {
        DisplayLockGuard lock(display_);
        RefreshCodex();
    }
}
