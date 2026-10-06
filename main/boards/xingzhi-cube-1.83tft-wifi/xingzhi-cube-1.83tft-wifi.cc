#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "mochi_lcd_display.h"
#include "pet/pet_state_engine.h"
#ifdef CONFIG_SD_CARD_MMC_INTERFACE
#include "sdmmc.h"
#elif defined(CONFIG_SD_CARD_SPI_INTERFACE)
#include "sdspi.h"
#endif
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "power_manager.h"

#include <atomic>
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_timer.h>
#include <wifi_station.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#define TAG "XINGZHI_CUBE_1_83TFT_WIFI_MOCHI"
#define MOCHI_BACKLIGHT_PERCENT 25
#define MOCHI_MENU_LONG_PRESS_MS 2000

class XINGZHI_CUBE_1_54TFT_WIFI : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Mochi183LcdDisplay* display_ = nullptr;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    // Talk uses PressDown/PressUp plus the component's real 2-second long-press
    // event. This deliberately avoids registering OnClick, so releasing after a
    // menu long-press can never accidentally start a voice transaction.
    std::atomic<int64_t> talk_press_started_us_{0};
    std::atomic_bool talk_long_press_consumed_{false};

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_38);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_21);
        rtc_gpio_set_direction(GPIO_NUM_21, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_21, 1);

        power_save_timer_ = new PowerSaveTimer(-1, SECONDS_TO_SLEEP_MODE, SECONDS_TO_SHUTDOWN);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->SetBrightness(MOCHI_BACKLIGHT_PERCENT, false);
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            PetStateEngine::GetInstance().SaveNow();
            rtc_gpio_set_level(GPIO_NUM_21, 0);
            rtc_gpio_hold_en(GPIO_NUM_21);
            esp_lcd_panel_disp_on_off(panel_, false);
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void HandleTalkShortPress() {
        power_save_timer_->WakeUp();
        auto& app = Application::GetInstance();
        const auto state = app.GetDeviceState();

        // Never reset credentials from the normal talk/menu button.
        if (state == kDeviceStateStarting) {
            ESP_LOGW(TAG, "Talk button ignored while startup/network init is still running");
            return;
        }

        // Voice has priority over menu. If a conversation is already speaking,
        // preserve the hardware-PASS barge-in behavior exactly.
        if (state == kDeviceStateSpeaking) {
            WifiStation::GetInstance().SetPowerSaveMode(false);
            app.Schedule([&app]() {
                app.AbortSpeaking(kAbortReasonNone);
                app.SetDeviceState(kDeviceStateListening);
            });
            return;
        }

        // While Idle and menu is open, Talk short means Select/OK rather than
        // starting voice. The menu itself auto-closes if voice becomes active.
        if (state == kDeviceStateIdle && display_ != nullptr && display_->IsMenuOpen()) {
            display_->RequestMenuSelect();
            return;
        }

        WifiStation::GetInstance().SetPowerSaveMode(false);
        app.ToggleChatState();
    }

    void HandleTalkLongPress() {
        power_save_timer_->WakeUp();
        auto& app = Application::GetInstance();
        const auto state = app.GetDeviceState();

        if (state == kDeviceStateStarting) {
            ESP_LOGW(TAG, "Talk long-press ignored while startup/network init is running");
            return;
        }

        // Menu is only an Idle interaction. During listening/speaking, Talk
        // remains a voice control so a menu gesture can never steal barge-in.
        if (state == kDeviceStateIdle && display_ != nullptr) {
            display_->RequestMenuToggle();
            return;
        }

        HandleTalkShortPress();
    }

    bool HandleMenuMoveIfOpen(int delta) {
        if (display_ == nullptr || !display_->IsMenuOpen()) {
            return false;
        }
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            return false;
        }
        power_save_timer_->WakeUp();
        display_->RequestMenuMove(delta);
        return true;
    }

    void InitializeButtons() {
        boot_button_.OnPressDown([this]() {
            power_save_timer_->WakeUp();
            talk_press_started_us_.store(esp_timer_get_time());
            talk_long_press_consumed_.store(false);
        });

        boot_button_.OnLongPress([this]() {
            talk_long_press_consumed_.store(true);
            HandleTalkLongPress();
        });

        boot_button_.OnPressUp([this]() {
            const int64_t started = talk_press_started_us_.exchange(0);
            const bool consumed = talk_long_press_consumed_.exchange(false);
            if (consumed) {
                return;
            }

            // Defensive fallback: if a very long release races the component's
            // long-press callback, treat it as a menu long-press, never a click.
            const int64_t held_us = started > 0 ? (esp_timer_get_time() - started) : 0;
            if (held_us >= static_cast<int64_t>(MOCHI_MENU_LONG_PRESS_MS) * 1000) {
                HandleTalkLongPress();
                return;
            }
            HandleTalkShortPress();
        });

        volume_up_button_.OnClick([this]() {
            if (HandleMenuMoveIfOpen(-1)) return;
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) volume = 100;
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            if (HandleMenuMoveIfOpen(-1)) return;
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            if (HandleMenuMoveIfOpen(1)) return;
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) volume = 0;
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            if (HandleMenuMoveIfOpen(1)) return;
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeSt7789Display() {
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new Mochi183LcdDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
    XINGZHI_CUBE_1_54TFT_WIFI() :
        boot_button_(BOOT_BUTTON_GPIO, false, MOCHI_MENU_LONG_PRESS_MS, 50),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeSpi();
        InitializeButtons();
        InitializeSt7789Display();
        PetStateEngine::GetInstance().Initialize();
        GetBacklight()->SetBrightness(MOCHI_BACKLIGHT_PERCENT, false);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveMode(enabled);
    }

#ifdef CONFIG_SD_CARD_MMC_INTERFACE
    virtual SdCard* GetSdCard() override {
#ifdef CARD_SDMMC_BUS_WIDTH_4BIT
        static SdMMC sdmmc(CARD_SDMMC_CLK_GPIO,
                           CARD_SDMMC_CMD_GPIO,
                           CARD_SDMMC_D0_GPIO,
                           CARD_SDMMC_D1_GPIO,
                           CARD_SDMMC_D2_GPIO,
                           CARD_SDMMC_D3_GPIO);
#else
#ifdef CARD_SDMMC_D3_GPIO
        if (CARD_SDMMC_D3_GPIO != GPIO_NUM_NC) {
            gpio_set_direction(CARD_SDMMC_D3_GPIO, GPIO_MODE_INPUT);
            gpio_pullup_en(CARD_SDMMC_D3_GPIO);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
#endif
        static SdMMC sdmmc(CARD_SDMMC_CLK_GPIO,
                           CARD_SDMMC_CMD_GPIO,
                           CARD_SDMMC_D0_GPIO);
#endif
        return &sdmmc;
    }
#endif
#ifdef CONFIG_SD_CARD_SPI_INTERFACE
    virtual SdCard* GetSdCard() override {
        static SdSPI sdspi(CARD_SPI_MISO_GPIO,
                           CARD_SPI_MOSI_GPIO,
                           CARD_SPI_SCLK_GPIO,
                           CARD_SPI_CS_GPIO);
        return &sdspi;
    }
#endif
};

DECLARE_BOARD(XINGZHI_CUBE_1_54TFT_WIFI);
