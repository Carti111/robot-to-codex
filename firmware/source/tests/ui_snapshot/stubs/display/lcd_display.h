#pragma once
#include "../display.h"
#include "board.h"
LV_FONT_DECLARE(font_puhui_20_4);
// Platform fixture provides the shared chat containers; SparkDisplay applies the production styling.
class SpiLcdDisplay : public Display {
public:
    SpiLcdDisplay() {
        container_ = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(container_);
        lv_obj_set_size(container_, 240, 240);
        status_bar_ = lv_obj_create(container_);
        lv_obj_remove_style_all(status_bar_);
        lv_obj_set_size(status_bar_, 240, 26);
        lv_obj_set_style_pad_hor(status_bar_, 2, 0);
        lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
        auto network = lv_label_create(status_bar_);
        lv_label_set_text(network, LV_SYMBOL_WIFI);
        auto status = lv_label_create(status_bar_);
        lv_label_set_text(status, "待命");
        lv_obj_set_style_text_font(status, &font_puhui_20_4, 0);
        lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_flex_grow(status, 1);
        battery_label_ = lv_label_create(status_bar_);
        lv_label_set_text(battery_label_, LV_SYMBOL_BATTERY_FULL);
        content_ = lv_obj_create(container_);
        lv_obj_remove_style_all(content_);
        lv_obj_set_pos(content_, 0, 26);
        lv_obj_set_size(content_, 240, 214);
        low_battery_popup_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(low_battery_popup_, 216, 40);
        lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
    }
    void SetTheme(const std::string& theme) override {
        current_theme_name_ = theme;
        current_theme_.text = theme == "dark" ? lv_color_white() : lv_color_black();
        current_theme_.background = theme == "dark" ? lv_color_hex(0x121212) : lv_color_white();
        current_theme_.chat_background = current_theme_.background;
        lv_obj_set_style_text_color(container_, current_theme_.text, 0);
    }
    void UpdateStatusBar(bool = false) override {
        int level; bool charging, discharging;
        bool available = Board::GetInstance().GetBatteryLevel(level, charging, discharging);
        if (!available) return; // Shared display preserves its old icon/alert until SparkDisplay clears unavailable data.
        const char* icon = level <= 20 ? LV_SYMBOL_BATTERY_EMPTY : LV_SYMBOL_BATTERY_FULL;
        battery_icon_ = icon;
        if (level <= 20) lv_obj_remove_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
        if (std::string(lv_label_get_text(battery_label_)) != icon) lv_label_set_text(battery_label_, icon);
    }
protected:
    lv_obj_t* container_;
    lv_obj_t* status_bar_;
    lv_obj_t* content_;
    lv_obj_t* battery_label_;
    lv_obj_t* low_battery_popup_ = nullptr;
    const char* battery_icon_ = nullptr;
    struct { lv_color_t text, background, chat_background; } current_theme_;
    std::string current_theme_name_;
};
