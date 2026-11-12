#pragma once
#include "display/lcd_display.h"
#include "board.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <esp_log.h>

// SparkBot keeps the shared chat UI and adds its compact battery percentage.
class SparkDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void InitializeHomepage() {
        SetTheme("dark");
        DisplayLockGuard lock(this);
        percentage_ = lv_label_create(status_bar_);
        lv_obj_set_style_text_font(percentage_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(percentage_, current_theme_.text, 0);
        lv_obj_set_style_margin_left(percentage_, 4, 0);
        lv_label_set_text(percentage_, "--%");
        lv_obj_set_flex_align(status_bar_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    void SetTheme(const std::string& theme) override {
        SpiLcdDisplay::SetTheme(theme);
        DisplayLockGuard lock(this);
        if (current_theme_name_ == "dark" || current_theme_name_ == "DARK") {
            current_theme_.background = lv_color_black();
            current_theme_.chat_background = lv_color_black();
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
            lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
            lv_obj_set_style_bg_color(status_bar_, lv_color_black(), 0);
            lv_obj_set_style_bg_color(content_, lv_color_black(), 0);
        }
        if (percentage_) lv_obj_set_style_text_color(percentage_, current_theme_.text, 0);
    }

    void UpdateStatusBar(bool update_all = false) override {
        SpiLcdDisplay::UpdateStatusBar(update_all);
        int level = 0;
        bool charging = false, discharging = false;
        char text[8] = "--%";
        bool available = Board::GetInstance().GetBatteryLevel(level, charging, discharging);
        if (available) {
            snprintf(text, sizeof(text), "%d%%", std::clamp(level, 0, 100));
        }
        if (!percentage_) return;
        DisplayLockGuard lock(this);
        if (!available) {
            if (battery_icon_) {
                lv_label_set_text(battery_label_, "");
                battery_icon_ = nullptr;
            }
            if (low_battery_popup_) lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
        }
        if (last_percentage_ == text) return;
        lv_label_set_text(percentage_, text);
        last_percentage_ = text;
        ESP_LOGI("SparkDisplay", "battery=%s", text);
    }

private:
    lv_obj_t* percentage_ = nullptr;
    std::string last_percentage_;
};
