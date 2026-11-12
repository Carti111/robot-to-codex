#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "touch_element/touch_button.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <driver/uart.h>
#include <cstring>

#include "esp32_camera.h"
#include "spark_features.h"
#include "spark_display.h"
#include "spark_battery_state.h"
#include <adc_battery_estimation.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali_scheme.h>
#include <driver/usb_serial_jtag.h>
#include <esp_timer.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "esp_sparkbot"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class SparkBotEs8311AudioCodec : public Es8311AudioCodec {
private:    

public:
    SparkBotEs8311AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
                        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                        gpio_num_t pa_pin, uint8_t es8311_addr, bool use_mclk = true)
        : Es8311AudioCodec(i2c_master_handle, i2c_port, input_sample_rate, output_sample_rate,
                             mclk,  bclk,  ws,  dout,  din,pa_pin,  es8311_addr,  use_mclk = true) {}

    void EnableOutput(bool enable) override {
        if (enable == output_enabled_) {
            return;
        }
        if (enable) {
            Es8311AudioCodec::EnableOutput(enable);
        } else {
           // Nothing todo because the display io and PA io conflict
        }
    }
};

class EspSparkBot : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    struct TouchContext { EspSparkBot* board; int index; };
    touch_button_handle_t touches_[3]{};
    TouchContext touch_contexts_[3]{};
    uint64_t pressed_at_[3]{};
    bool touch_cancelled_[3]{}, touch_long_[3]{}, touch_dispatched_[3]{};
    unsigned touch_mask_ = 0;
    esp_timer_handle_t touch_heal_timer_ = nullptr;

    // 松开事件丢失时，避免旧的按下位长期占用 touch_mask_ 导致后续触摸全部被取消
    void ClearStaleTouches(uint64_t now) {
        for (int i = 0; i < 3; ++i) {
            if ((touch_mask_ & (1U << i)) && now - pressed_at_[i] > 5000) {
                touch_mask_ &= ~(1U << i);
                touch_cancelled_[i] = false;
                touch_long_[i] = false;
                touch_dispatched_[i] = false;
            }
        }
    }
    SparkFeatures* features_ = nullptr;
    Display* display_;
    Esp32Camera* camera_;
    light_mode_t light_mode_ = LIGHT_MODE_ALWAYS_ON;
    std::atomic<uint32_t> motion_generation_{0};
    adc_oneshot_unit_handle_t battery_adc_ = nullptr;
    adc_cali_handle_t battery_calibration_ = nullptr;
    adc_battery_estimation_handle_t battery_estimator_ = nullptr;
    SparkBatteryState battery_state_;
    std::mutex battery_mutex_;
    uint64_t last_battery_sample_ = 0;
    uint64_t last_battery_diagnostic_ = 0;
    int last_battery_voltage_ = 0;
    bool battery_usb_connected_ = false;
    // The upstream estimator decides "charging" from a voltage-trend window and keeps the
    // previous state whenever the window is flat, so it can latch a false charging state.
    // Qualify it here: only a sustained voltage rise counts as charging.
    static constexpr int kChargeWindowSamples = 90;
    int charge_history_[kChargeWindowSamples]{};
    int charge_history_index_ = 0;
    int charge_history_count_ = 0;
    int charge_rise_mv_ = 0;
    bool charge_qualified_ = false;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeBattery() {
        // Official factory app_power uses ADC2 channel 3 (GPIO14) and a 100k/100k divider.
        adc_oneshot_unit_init_cfg_t unit{};
        unit.unit_id = ADC_UNIT_2;
        esp_err_t error = adc_oneshot_new_unit(&unit, &battery_adc_);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "Battery ADC unavailable: %s", esp_err_to_name(error));
            return;
        }
        adc_oneshot_chan_cfg_t channel{};
        channel.bitwidth = ADC_BITWIDTH_DEFAULT;
        channel.atten = ADC_ATTEN_DB_12;
        error = adc_oneshot_config_channel(battery_adc_, ADC_CHANNEL_3, &channel);
        adc_cali_curve_fitting_config_t calibration{};
        calibration.unit_id = ADC_UNIT_2;
        calibration.chan = ADC_CHANNEL_3;
        calibration.atten = ADC_ATTEN_DB_12;
        calibration.bitwidth = ADC_BITWIDTH_DEFAULT;
        if (error == ESP_OK) error = adc_cali_create_scheme_curve_fitting(&calibration, &battery_calibration_);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "Battery calibration unavailable: %s", esp_err_to_name(error));
            adc_oneshot_del_unit(battery_adc_);
            battery_adc_ = nullptr;
            return;
        }
        // Match the factory app_power primary path, including its default OCV curve and filters.
        adc_battery_estimation_t estimation{};
        estimation.external.adc_handle = battery_adc_;
        estimation.external.adc_cali_handle = battery_calibration_;
        estimation.adc_channel = ADC_CHANNEL_3;
        estimation.upper_resistor = 100000;
        estimation.lower_resistor = 100000;
        battery_estimator_ = adc_battery_estimation_create(&estimation);
        if (!battery_estimator_) {
            ESP_LOGW(TAG, "Factory battery estimator unavailable");
            return;
        }
        ESP_LOGI(TAG, "Factory battery estimator ready; waiting for stable startup samples");
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_GPIO;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_GPIO;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    // Touch state is owned by the application loop, alongside the page and voice state.
    void HandleTouch(int index, touch_button_event_t event, uint64_t now) {
        ClearStaleTouches(now);
        if (event == TOUCH_BUTTON_EVT_ON_PRESS) {
            pressed_at_[index] = now;
            touch_cancelled_[index] = false;
            touch_long_[index] = false;
            touch_dispatched_[index] = false;
            touch_mask_ |= 1U << index;
            if (touch_mask_ & (touch_mask_ - 1)) {
                for (auto& cancelled : touch_cancelled_) cancelled = true;
                return;
            }
            if (!features_) return;
            if (index == 0) {
                touch_dispatched_[index] = features_->TopOnPress();
            } else {
                features_->Side(index == 1 ? -1 : 1);
                touch_dispatched_[index] = true;
            }
        } else if (event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {
            touch_long_[index] = true;
            if (index == 0 && !touch_cancelled_[index] && features_) features_->LongTop();
        } else if (event == TOUCH_BUTTON_EVT_ON_RELEASE) {
            touch_mask_ &= ~(1U << index);
            uint64_t duration = now - pressed_at_[index];
            if (!features_ || touch_cancelled_[index] || touch_long_[index] || touch_dispatched_[index] ||
                touch_mask_ || duration < 45 || duration > 800) return;
            if (index == 0) features_->Top();
        }
    }
    void InitializePairingTouch() {
        touch_elem_global_config_t element = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
        touch_button_global_config_t buttons = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
        esp_err_t error = touch_element_install(&element);
        if (error == ESP_OK) error = touch_button_install(&buttons);
        const touch_pad_t channels[] = {TOUCH_PAD_NUM1, TOUCH_PAD_NUM2, TOUCH_PAD_NUM3};
        const float sensitivities[] = {0.025F, 0.035F, 0.035F};
        for (int i = 0; i < 3 && error == ESP_OK; ++i) {
            touch_contexts_[i] = {this, i};
            touch_button_config_t config{ .channel_num = channels[i], .channel_sens = sensitivities[i] };
            error = touch_button_create(&config, &touches_[i]);
            if (error == ESP_OK) error = touch_button_subscribe_event(touches_[i], TOUCH_ELEM_EVENT_ON_PRESS | TOUCH_ELEM_EVENT_ON_RELEASE | TOUCH_ELEM_EVENT_ON_LONGPRESS, &touch_contexts_[i]);
            if (error == ESP_OK) error = touch_button_set_dispatch_method(touches_[i], TOUCH_ELEM_DISP_CALLBACK);
            if (error == ESP_OK) error = touch_button_set_callback(touches_[i],
                [](touch_button_handle_t, touch_button_message_t* message, void* arg) {
                    auto context = static_cast<TouchContext*>(arg);
                    auto board = context->board;
                    int index = context->index;
                    auto event = message->event;
                    uint64_t timestamp = esp_timer_get_time() / 1000;
                    Application::GetInstance().Schedule([board, index, event, timestamp]() {
                        board->HandleTouch(index, event, timestamp);
                    });
                });
            if (error == ESP_OK) error = touch_button_set_longpress(touches_[i], 3000);
        }
        if (error == ESP_OK) error = touch_element_start();
        if (error != ESP_OK) { ESP_LOGW(TAG, "Touch controls unavailable: %s", esp_err_to_name(error)); return; }
        esp_timer_create_args_t heal_args{};
        heal_args.name = "touch_heal";
        heal_args.arg = this;
        heal_args.callback = [](void* context) {
            auto board = static_cast<EspSparkBot*>(context);
            Application::GetInstance().Schedule([board]() {
                board->ClearStaleTouches(esp_timer_get_time() / 1000);
            });
        };
        if (esp_timer_create(&heal_args, &touch_heal_timer_) == ESP_OK) {
            esp_timer_start_periodic(touch_heal_timer_, 1000000);
        }
        ESP_LOGI(TAG, "Feature touch ready: top=1 left=2 right=3");
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_GPIO;
        io_config.dc_gpio_num = DISPLAY_DC_GPIO;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_disp_on_off(panel, true);
        auto display = new SparkDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
                                        .emoji_font = font_emoji_64_init(),
                                    });
        display->InitializeHomepage();
        display_ = display;
    }

    void InitializeCamera() {
        camera_config_t camera_config = {};

        camera_config.pin_pwdn = SPARKBOT_CAMERA_PWDN;
        camera_config.pin_reset = SPARKBOT_CAMERA_RESET;
        camera_config.pin_xclk = SPARKBOT_CAMERA_XCLK;
        camera_config.pin_pclk = SPARKBOT_CAMERA_PCLK;
        camera_config.pin_sccb_sda = SPARKBOT_CAMERA_SIOD;
        camera_config.pin_sccb_scl = SPARKBOT_CAMERA_SIOC;

        camera_config.pin_d0 = SPARKBOT_CAMERA_D0;
        camera_config.pin_d1 = SPARKBOT_CAMERA_D1;
        camera_config.pin_d2 = SPARKBOT_CAMERA_D2;
        camera_config.pin_d3 = SPARKBOT_CAMERA_D3;
        camera_config.pin_d4 = SPARKBOT_CAMERA_D4;
        camera_config.pin_d5 = SPARKBOT_CAMERA_D5;
        camera_config.pin_d6 = SPARKBOT_CAMERA_D6;
        camera_config.pin_d7 = SPARKBOT_CAMERA_D7;

        camera_config.pin_vsync = SPARKBOT_CAMERA_VSYNC;
        camera_config.pin_href = SPARKBOT_CAMERA_HSYNC;
        camera_config.pin_pclk = SPARKBOT_CAMERA_PCLK;
        camera_config.xclk_freq_hz = SPARKBOT_CAMERA_XCLK_FREQ;
        camera_config.ledc_timer = SPARKBOT_LEDC_TIMER;
        camera_config.ledc_channel = SPARKBOT_LEDC_CHANNEL;
        camera_config.fb_location = CAMERA_FB_IN_PSRAM;
        
        camera_config.sccb_i2c_port = I2C_NUM_0;
        
        camera_config.pixel_format = PIXFORMAT_RGB565;
        camera_config.frame_size = FRAMESIZE_240X240;
        camera_config.jpeg_quality = 12;
        camera_config.fb_count = 1;
        camera_config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        
        camera_ = new Esp32Camera(camera_config);

        Settings settings("sparkbot", false);
        // 考虑到部分复刻使用了不可动摄像头的设计，默认启用翻转
        bool camera_flipped = static_cast<bool>(settings.GetInt("camera-flipped", 1));
        camera_->SetHMirror(camera_flipped);
        camera_->SetVFlip(camera_flipped);
    }

    /*
        ESP-SparkBot 的底座
        https://gitee.com/esp-friends/esp_sparkbot/tree/master/example/tank/c2_tracked_chassis
    */
    void InitializeEchoUart() {
        uart_config_t uart_config = {
            .baud_rate = ECHO_UART_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        int intr_alloc_flags = 0;

        ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
        ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, UART_ECHO_TXD, UART_ECHO_RXD, UART_ECHO_RTS, UART_ECHO_CTS));

        SendUartMessage("w2");
    }

    void SendUartMessage(const char * command_str) {
        uint8_t len = strlen(command_str);
        uart_write_bytes(ECHO_UART_PORT_NUM, command_str, len);
        ESP_LOGI(TAG, "Sent command: %s", command_str);
    }

    struct ChassisMotionArgs {
        EspSparkBot* board;
        float x;
        float y;
        int duration_ms;
        uint32_t generation;
    };

    // The C2 chassis stops itself when no command arrives for about half a second,
    // so keep resending the target speed for the requested duration, then stop.
    static void ChassisMotionTask(void* arg) {
        auto* motion = static_cast<ChassisMotionArgs*>(arg);
        EspSparkBot* board = motion->board;
        const float x = motion->x;
        const float y = motion->y;
        const int duration_ms = motion->duration_ms;
        const uint32_t generation = motion->generation;
        delete motion;

        const int64_t started_us = esp_timer_get_time();
        while (board->motion_generation_.load() == generation) {
            if ((esp_timer_get_time() - started_us) / 1000 >= duration_ms) {
                break;
            }
            char command[24];
            snprintf(command, sizeof(command), "x%.2f y%.2f", x, y);
            board->SendUartMessage(command);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        // A newer command owns the chassis; only the latest motion may stop it.
        if (board->motion_generation_.load() == generation) {
            board->SendUartMessage("x0.0 y0.0");
        }
        vTaskDelete(nullptr);
    }

    void StartChassisMotion(float x, float y, int speed, int duration_ms) {
        const float scale = std::clamp(speed, 10, 100) / 100.0f;
        const uint32_t generation = ++motion_generation_;
        auto* args = new ChassisMotionArgs{
            this, x * scale, y * scale, std::clamp(duration_ms, 1000, 60000), generation};
        if (xTaskCreate(ChassisMotionTask, "chassis_motion", 3072, args, 5, nullptr) != pdPASS) {
            delete args;
            ESP_LOGE(TAG, "Failed to start chassis motion task");
            SendUartMessage("x0.0 y0.0");
        }
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        // 定义设备的属性
        mcp_server.AddTool("self.chassis.get_light_mode", "获取灯光效果编号", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            if (light_mode_ < 2) {
                return 1;
            } else {
                return light_mode_ - 2;
            }
        });

        mcp_server.AddTool("self.chassis.go_forward", "前进。speed 为速度百分比，duration_ms 为持续毫秒数（1000–60000，即 1–60 秒），两者共同决定行进幅度", PropertyList({
            Property("speed", kPropertyTypeInteger, 100, 10, 100),
            Property("duration_ms", kPropertyTypeInteger, 1000, 1000, 60000)
        }), [this](const PropertyList& properties) -> ReturnValue {
            StartChassisMotion(0.0f, 1.0f, properties["speed"].value<int>(), properties["duration_ms"].value<int>());
            return true;
        });

        mcp_server.AddTool("self.chassis.go_back", "后退。speed 为速度百分比，duration_ms 为持续毫秒数（1000–60000，即 1–60 秒），两者共同决定行进幅度", PropertyList({
            Property("speed", kPropertyTypeInteger, 100, 10, 100),
            Property("duration_ms", kPropertyTypeInteger, 1000, 1000, 60000)
        }), [this](const PropertyList& properties) -> ReturnValue {
            StartChassisMotion(0.0f, -1.0f, properties["speed"].value<int>(), properties["duration_ms"].value<int>());
            return true;
        });

        mcp_server.AddTool("self.chassis.turn_left", "向左转。speed 为速度百分比，duration_ms 为持续毫秒数（1000–60000，即 1–60 秒），两者共同决定转动幅度", PropertyList({
            Property("speed", kPropertyTypeInteger, 100, 10, 100),
            Property("duration_ms", kPropertyTypeInteger, 1000, 1000, 60000)
        }), [this](const PropertyList& properties) -> ReturnValue {
            StartChassisMotion(-1.0f, 0.0f, properties["speed"].value<int>(), properties["duration_ms"].value<int>());
            return true;
        });

        mcp_server.AddTool("self.chassis.turn_right", "向右转。speed 为速度百分比，duration_ms 为持续毫秒数（1000–60000，即 1–60 秒），两者共同决定转动幅度", PropertyList({
            Property("speed", kPropertyTypeInteger, 100, 10, 100),
            Property("duration_ms", kPropertyTypeInteger, 1000, 1000, 60000)
        }), [this](const PropertyList& properties) -> ReturnValue {
            StartChassisMotion(1.0f, 0.0f, properties["speed"].value<int>(), properties["duration_ms"].value<int>());
            return true;
        });
        
        mcp_server.AddTool("self.chassis.dance", "跳舞", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            SendUartMessage("d1");
            light_mode_ = LIGHT_MODE_MAX;
            return true;
        });

        mcp_server.AddTool("self.chassis.switch_light_mode", "打开灯光效果", PropertyList({
            Property("light_mode", kPropertyTypeInteger, 1, 6)
        }), [this](const PropertyList& properties) -> ReturnValue {
            char command_str[5] = {'w', 0, 0};
            char mode = static_cast<light_mode_t>(properties["light_mode"].value<int>());

            ESP_LOGI(TAG, "Switch Light Mode: %c", (mode + '0'));

            if (mode >= 3 && mode <= 8) {
                command_str[1] = mode + '0';
                SendUartMessage(command_str);
                return true;
            }
            throw std::runtime_error("Invalid light mode");
        });

        mcp_server.AddTool("self.camera.set_camera_flipped", "翻转摄像头图像方向", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            Settings settings("sparkbot", true);
            // 考虑到部分复刻使用了不可动摄像头的设计，默认启用翻转
            bool flipped = !static_cast<bool>(settings.GetInt("camera-flipped", 1));
            
            camera_->SetHMirror(flipped);
            camera_->SetVFlip(flipped);
            
            settings.SetInt("camera-flipped", flipped ? 1 : 0);
            
            return true;
        });
    }

public:
    EspSparkBot() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeBattery();
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeCamera();
        features_ = new SparkFeatures(display_, camera_, i2c_bus_);
        InitializePairingTouch();
        InitializeEchoUart();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
         static SparkBotEs8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!battery_adc_ || !battery_calibration_ || !battery_estimator_) return false;
        std::lock_guard<std::mutex> lock(battery_mutex_);
        uint64_t now = esp_timer_get_time() / 1000;
        // Initialization transients must not seed an empty-battery indication.
        if (now < 5000) return false;
        bool usb_connected = usb_serial_jtag_is_connected();
        if (usb_connected && !battery_usb_connected_ && last_battery_voltage_) {
            ESP_LOGI(TAG, "Battery before USB reconnect: voltage=%d mV", last_battery_voltage_);
        }
        battery_usb_connected_ = usb_connected;
        if (now - last_battery_sample_ >= 1000) {
            last_battery_sample_ = now;
            int voltage_sum = 0;
            esp_err_t error = ESP_OK;
            for (int i = 0; i < 8 && error == ESP_OK; ++i) {
                int raw = 0, millivolts = 0;
                error = adc_oneshot_read(battery_adc_, ADC_CHANNEL_3, &raw);
                if (error == ESP_OK) error = adc_cali_raw_to_voltage(battery_calibration_, raw, &millivolts);
                voltage_sum += millivolts;
            }
            if (error == ESP_OK) {
                last_battery_voltage_ = voltage_sum * 2 / 8;
                charge_history_[charge_history_index_] = last_battery_voltage_;
                charge_history_index_ = (charge_history_index_ + 1) % kChargeWindowSamples;
                if (charge_history_count_ < kChargeWindowSamples) ++charge_history_count_;
                if (charge_history_count_ >= kChargeWindowSamples) {
                    charge_rise_mv_ = last_battery_voltage_ - charge_history_[charge_history_index_];
                    if (charge_rise_mv_ >= 12) {
                        charge_qualified_ = true;
                    } else if (charge_rise_mv_ <= 4) {
                        charge_qualified_ = false;
                    }
                }
                if (battery_state_.AcceptVoltage(last_battery_voltage_)) {
                    float capacity = 0;
                    error = adc_battery_estimation_get_capacity(battery_estimator_, &capacity);
                    if (error == ESP_OK && std::isfinite(capacity)) {
                        battery_state_.StoreEstimate(std::clamp(static_cast<int>(capacity), 0, 100),
                                                     charge_qualified_ || usb_connected);
                    } else {
                        battery_state_.Invalidate();
                    }
                }
            } else {
                battery_state_.Invalidate();
            }
            if (now < 15000 || now - last_battery_diagnostic_ >= 30000) {
                last_battery_diagnostic_ = now;
                int measured_level = -1;
                battery_state_.GetLevel(measured_level);
                ESP_LOGI(TAG, "Battery sample: voltage=%d mV level=%d%% rise=%d mV charging=%d usb=%d status=%s",
                         last_battery_voltage_, measured_level, charge_rise_mv_,
                         (charge_qualified_ || usb_connected) ? 1 : 0, usb_connected ? 1 : 0,
                         esp_err_to_name(error));
            }
        }
        charging = battery_state_.IsCharging();
        discharging = !charging;
        return battery_state_.GetLevel(level);
    }

    bool AllowVoiceChat() const override { return !features_ || features_->AllowVoiceChat(); }
    bool ShowVoicePairing(const std::string& event, const std::string& code) override { return features_ && features_->PairingNotice(event, code); }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(EspSparkBot);
