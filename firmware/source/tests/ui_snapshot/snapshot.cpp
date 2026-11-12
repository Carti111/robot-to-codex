// Compile the production pages and assets against the actual LVGL software renderer.
#include "spark_features.h"
#include "spark_display.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

bool SparkImu::Start(i2c_master_bus_handle_t) { ready_ = true; return true; }
bool SparkImu::Read(float& x, float& y, float& z) { x = y = 0; z = 1; return true; }
bool SparkImu::ReadMotion(SparkMotionSample& sample) { sample = {}; sample.accel_z = 1; return true; }
static uint16_t display_pixels[240 * 240];
static uint16_t draw_pixels[240 * 240];
static unsigned flushes = 0;

static void Flush(lv_display_t* display, const lv_area_t* area, uint8_t* bytes) {
    auto pixels = reinterpret_cast<uint16_t*>(bytes);
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) display_pixels[y * 240 + x] = *pixels++;
    }
    ++flushes;
    lv_display_flush_ready(display);
}

static void Snapshot(const char* name) {
    lv_refr_now(nullptr);
    auto output = fopen(name, "wb");
    if (!output) std::abort();
    fprintf(output, "P6\n240 240\n255\n");
    for (auto pixel : display_pixels) {
        uint8_t rgb[] = {static_cast<uint8_t>((pixel >> 11) * 255 / 31),
                         static_cast<uint8_t>(((pixel >> 5) & 63) * 255 / 63),
                         static_cast<uint8_t>((pixel & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, output);
    }
    fclose(output);
    printf("Rendered %s\n", name);
}

int main() {
    lv_init();
    auto display = lv_display_create(240, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_pixels, nullptr, sizeof(draw_pixels), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, Flush);
    {
        SparkDisplay home;
        home.InitializeHomepage();
        home.UpdateStatusBar();
        auto container = lv_obj_get_child(lv_screen_active(), 0);
        auto bar = lv_obj_get_child(container, 0);
        auto percentage = lv_obj_get_child(bar, -1);
        if (strcmp(lv_label_get_text(percentage), "75%") != 0) return 10;
        if (lv_color_to_u32(lv_obj_get_style_bg_color(container, 0)) != lv_color_to_u32(lv_color_black())) return 11;
        Snapshot("chat-home.ppm");
        lv_area_t bounds;
        lv_obj_get_coords(percentage, &bounds);
        if (bounds.x1 < 180 || bounds.x2 > 239 || bounds.y2 > 25) return 12;
        unsigned before = flushes;
        home.UpdateStatusBar();
        lv_refr_now(nullptr);
        if (flushes != before) return 13;
        Board::GetInstance().battery_level = 8;
        home.UpdateStatusBar();
        if (strcmp(lv_label_get_text(percentage), "8%") != 0) return 14;
        Snapshot("chat-home-low-battery.ppm");
        Board::GetInstance().battery_available = false;
        home.UpdateStatusBar();
        if (strcmp(lv_label_get_text(percentage), "--%") != 0) return 15;
        if (strlen(lv_label_get_text(lv_obj_get_child(bar, -2))) != 0) return 18;
        if (!lv_obj_has_flag(lv_obj_get_child(lv_screen_active(), -1), LV_OBJ_FLAG_HIDDEN)) return 19;
        Snapshot("chat-home-battery-unavailable.ppm");
        Board::GetInstance().battery_available = true;
        Board::GetInstance().battery_level = 100;
        home.UpdateStatusBar();
        if (strcmp(lv_label_get_text(percentage), "100%") != 0) return 16;
        Snapshot("chat-home-full-battery.ppm");
        lv_obj_get_coords(percentage, &bounds);
        if (bounds.x1 < 180 || bounds.x2 > 239 || bounds.y2 > 25) return 17;
    }
    lv_obj_clean(lv_screen_active());
    Display wrapper;
    Esp32Camera camera;
    SparkFeatures pages(&wrapper, &camera, nullptr);
    pages.Side(1);
    Snapshot("clock.ppm");
    pages.Side(1);
    Snapshot("codex.ppm");
    if (pages.TopOnPress()) return 5; // A long pairing touch must not start dictation.
    pages.Side(1);
    Snapshot("muyu.ppm");
    if (!pages.TopOnPress()) return 6;
    for (int frame = 0; frame < 20; ++frame) {
        snapshot_milliseconds += 40;
        lv_tick_inc(40);
        lv_timer_handler();
        pages.Tick();
    }
    Snapshot("muyu-knocked.ppm");
    unsigned before = flushes;
    for (int frame = 0; frame < 50; ++frame) {
        snapshot_milliseconds += 40;
        lv_tick_inc(40);
        lv_timer_handler();
        pages.Tick();
    }
    printf("Resting wooden-fish redraws in 2 seconds: %u\n", flushes - before);
    if (flushes != before) return 3;
    pages.Side(1);
    Snapshot("dice.ppm");
    pages.Top();
    for (int frame = 0; frame < 10; ++frame) {
        snapshot_milliseconds += 40;
        lv_tick_inc(40);
        lv_timer_handler();
        pages.Tick();
    }
    Snapshot("dice-rolling.ppm");
    for (int frame = 0; frame < 30; ++frame) {
        snapshot_milliseconds += 40;
        lv_tick_inc(40);
        lv_timer_handler();
        pages.Tick();
    }
    Snapshot("dice-settled.ppm");
    before = flushes;
    for (int frame = 0; frame < 50; ++frame) {
        snapshot_milliseconds += 40;
        lv_tick_inc(40);
        lv_timer_handler();
        pages.Tick();
    }
    printf("Resting dice redraws in 2 seconds: %u\n", flushes - before);
    if (flushes != before) return 4;
    pages.Side(1);
    Snapshot("2048.ppm");
    printf("PASS factory page snapshots and no continuous idle redraw\n");
    return 0;
}
