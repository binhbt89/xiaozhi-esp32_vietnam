#pragma once

#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_image.h"
#include "application.h"
#include "assets.h"
#include "pet/pet_state_engine.h"

#include <lvgl.h>
#include <esp_random.h>
#include <esp_log.h>
#include <esp_sntp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

class Mochi183LcdDisplay : public SpiLcdDisplay {
private:
    enum class AmbientPeriod {
        Unknown = -1,
        Morning = 0,
        Noon,
        Afternoon,
        Night
    };

    enum class PetMenuItem : uint8_t {
        Pet = 0,
        Play,
        SleepWake,
        Close,
        Count
    };

    static constexpr const char* kAmbientTag = "MochiAmbient";
    static constexpr time_t kValidEpochThreshold = 1704067200;
    static constexpr int kVietnamUtcOffsetSeconds = 7 * 60 * 60;
    static constexpr uint32_t kAmbientTimerMs = 200;
    static constexpr uint32_t kIdleSettleTicks = 25;          // 5 seconds
    static constexpr uint32_t kBackgroundReapplyTicks = 300; // 60 seconds
    static constexpr int kMoveStepPx = 5;
    static constexpr int kHudWidthPx = 38;
    static constexpr int kHudRightMarginPx = 2;
    static constexpr int kHudTopGapPx = 2;
    static constexpr uint32_t kHudRefreshTicks = 5;           // 1 second
    static constexpr size_t kHudRowCount = 5;
    static constexpr size_t kMenuItemCount = static_cast<size_t>(PetMenuItem::Count);

    lv_obj_t* mochi_chat_bubble_ = nullptr;
    lv_obj_t* mochi_chat_label_ = nullptr;
    lv_obj_t* sun_patch_ = nullptr;
    lv_obj_t* night_glow_ = nullptr;
    lv_obj_t* night_floor_glow_ = nullptr;
    lv_timer_t* ambient_timer_ = nullptr;

    // 15D HUD: a narrow vertical strip on the right. It is created on the
    // screen (not container_) so background replacement can never overwrite it.
    lv_obj_t* hud_panel_ = nullptr;
    std::array<lv_obj_t*, kHudRowCount> hud_value_labels_{};
    std::array<int, kHudRowCount> hud_last_values_{{-1, -1, -1, -1, -1}};
    uint32_t hud_refresh_ticks_ = 0;

    // 15D menu: button callbacks only post atomic requests. All LVGL object
    // mutation remains inside AmbientTickLvgl(), preserving the Fix14 model.
    lv_obj_t* menu_overlay_ = nullptr;
    lv_obj_t* menu_card_ = nullptr;
    std::array<lv_obj_t*, kMenuItemCount> menu_rows_{};
    std::array<lv_obj_t*, kMenuItemCount> menu_row_labels_{};
    uint8_t menu_selected_ = 0;
    std::atomic_bool menu_open_{false};
    std::atomic_bool menu_toggle_requested_{false};
    std::atomic_int menu_move_requested_{0};
    std::atomic_bool menu_select_requested_{false};

    std::shared_ptr<LvglCBinImage> active_background_;
    AmbientPeriod ambient_period_ = AmbientPeriod::Unknown;
    uint32_t ambient_phase_ = 0;
    uint32_t idle_stable_ticks_ = 0;
    uint32_t background_reapply_ticks_ = 0;

    int mochi_x_ = 0;
    int mochi_target_x_ = 0;
    int action_ticks_left_ = 0;

    std::atomic_bool ambient_sntp_requested_{false};
    std::atomic_bool ambient_sntp_started_{false};
    int last_logged_hour_ = -1;

    static void AmbientLvglTimerThunk(lv_timer_t* timer) {
        auto* self = static_cast<Mochi183LcdDisplay*>(lv_timer_get_user_data(timer));
        if (self != nullptr) {
            self->AmbientTickLvgl();
        }
    }

    static const char* BackgroundNameFor(AmbientPeriod period) {
        switch (period) {
            case AmbientPeriod::Morning: return "background_morning.raw";
            case AmbientPeriod::Noon: return "background_noon.raw";
            case AmbientPeriod::Afternoon: return "background_afternoon.raw";
            case AmbientPeriod::Night: return "background_night.raw";
            default: return nullptr;
        }
    }

    int StatusBarHeightLvgl() const {
        if (status_bar_ == nullptr) {
            return 24;
        }
        lv_obj_update_layout(status_bar_);
        const int h = lv_obj_get_height(status_bar_);
        return (h >= 16 && h <= 48) ? h : 24;
    }

    void RequestSntpFallback() {
        bool expected = false;
        if (!ambient_sntp_requested_.compare_exchange_strong(expected, true)) {
            return;
        }

        Application::GetInstance().Schedule([this]() {
            if (ambient_sntp_started_.load()) {
                return;
            }
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            ambient_sntp_started_.store(true);
            ESP_LOGW(kAmbientTag, "System clock invalid; SNTP fallback started");
        });
    }

    AmbientPeriod GetAmbientPeriod() {
        time_t now = time(nullptr);
        if (now < kValidEpochThreshold) {
            RequestSntpFallback();
            return AmbientPeriod::Unknown;
        }

        time_t scene_time = now;
        const bool using_sntp = ambient_sntp_started_.load();
        if (using_sntp) {
            scene_time += kVietnamUtcOffsetSeconds;
        }

        struct tm tm_now {};
        gmtime_r(&scene_time, &tm_now);
        const int hour = tm_now.tm_hour;

        if (hour != last_logged_hour_) {
            last_logged_hour_ = hour;
            ESP_LOGI(kAmbientTag, "Ambient local hour=%02d source=%s",
                     hour, using_sntp ? "SNTP+UTC7" : "server_time");
        }

        if (hour >= 5 && hour < 11) return AmbientPeriod::Morning;
        if (hour >= 11 && hour < 15) return AmbientPeriod::Noon;
        if (hour >= 15 && hour < 18) return AmbientPeriod::Afternoon;
        return AmbientPeriod::Night;
    }

    bool LoadAndApplyBackgroundLvgl(AmbientPeriod period) {
        const char* name = BackgroundNameFor(period);
        if (name == nullptr || container_ == nullptr) {
            return false;
        }

        auto& assets = Assets::GetInstance();
        if (!assets.checksum_valid()) {
            return false;
        }

        void* ptr = nullptr;
        size_t size = 0;
        if (!assets.GetAssetData(name, ptr, size) || ptr == nullptr || size < 32) {
            ESP_LOGW(kAmbientTag, "Background asset not ready: %s", name);
            return false;
        }

        auto next = std::make_shared<LvglCBinImage>(ptr);
        if (next == nullptr || next->image_dsc() == nullptr) {
            ESP_LOGE(kAmbientTag, "Failed to create background descriptor: %s", name);
            return false;
        }

        auto previous = active_background_;
        active_background_ = next;
        lv_obj_set_style_bg_image_src(container_, active_background_->image_dsc(), 0);
        lv_obj_set_style_bg_image_opa(container_, LV_OPA_COVER, 0);
        lv_obj_invalidate(container_);
        ESP_LOGI(kAmbientTag, "Applied background: %s", name);
        return true;
    }

    void CreateAmbientObjects(lv_obj_t* screen) {
        sun_patch_ = lv_obj_create(screen);
        lv_obj_remove_style_all(sun_patch_);
        lv_obj_set_size(sun_patch_, 74, 42);
        lv_obj_set_style_radius(sun_patch_, 8, 0);
        lv_obj_set_style_bg_color(sun_patch_, lv_color_hex(0xFFF0A8), 0);
        lv_obj_set_style_bg_opa(sun_patch_, static_cast<lv_opa_t>(28), 0);
        lv_obj_clear_flag(sun_patch_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(sun_patch_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);

        night_glow_ = lv_obj_create(screen);
        lv_obj_remove_style_all(night_glow_);
        lv_obj_set_size(night_glow_, 54, 54);
        lv_obj_set_style_radius(night_glow_, 27, 0);
        lv_obj_set_style_bg_color(night_glow_, lv_color_hex(0xFFB45E), 0);
        lv_obj_set_style_bg_opa(night_glow_, static_cast<lv_opa_t>(48), 0);
        lv_obj_clear_flag(night_glow_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(night_glow_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);

        night_floor_glow_ = lv_obj_create(screen);
        lv_obj_remove_style_all(night_floor_glow_);
        lv_obj_set_size(night_floor_glow_, 82, 30);
        lv_obj_set_style_radius(night_floor_glow_, 15, 0);
        lv_obj_set_style_bg_color(night_floor_glow_, lv_color_hex(0xFFCA78), 0);
        lv_obj_set_style_bg_opa(night_floor_glow_, static_cast<lv_opa_t>(28), 0);
        lv_obj_clear_flag(night_floor_glow_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(night_floor_glow_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
    }

    void CreateHudLvgl(lv_obj_t* screen) {
        const int status_h = StatusBarHeightLvgl();
        const int panel_h = std::max(150, static_cast<int>(LV_VER_RES) - status_h - 4);

        hud_panel_ = lv_obj_create(screen);
        lv_obj_set_size(hud_panel_, kHudWidthPx, panel_h);
        lv_obj_set_pos(hud_panel_, LV_HOR_RES - kHudWidthPx - kHudRightMarginPx,
                       status_h + kHudTopGapPx);
        lv_obj_set_style_radius(hud_panel_, 9, 0);
        lv_obj_set_style_bg_color(hud_panel_, lv_color_hex(0x17202A), 0);
        lv_obj_set_style_bg_opa(hud_panel_, static_cast<lv_opa_t>(116), 0);
        lv_obj_set_style_border_width(hud_panel_, 0, 0);
        lv_obj_set_style_pad_all(hud_panel_, 2, 0);
        lv_obj_set_style_pad_row(hud_panel_, 0, 0);
        lv_obj_set_flex_flow(hud_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(hud_panel_, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(hud_panel_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(hud_panel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(hud_panel_, LV_OBJ_FLAG_CLICKABLE);

        // Tiny text badges are used instead of new bitmap/font assets. They are
        // intentionally language-light and fit a 38px strip: N=no/fullness,
        // +=Energy, :)=Mood, <3=Friendship, $=Coin.
        static const char* kHudIcons[kHudRowCount] = {"N", "+", ":)", "<3", "$"};
        for (size_t i = 0; i < kHudRowCount; ++i) {
            lv_obj_t* row = lv_obj_create(hud_panel_);
            lv_obj_set_size(row, 34, 34);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* icon = lv_label_create(row);
            lv_label_set_text(icon, kHudIcons[i]);
            lv_obj_set_style_text_color(icon, lv_color_hex(0xFFD98A), 0);
            lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, -1);

            hud_value_labels_[i] = lv_label_create(row);
            lv_label_set_text(hud_value_labels_[i], i == 4 ? "000" : "00");
            lv_obj_set_style_text_color(hud_value_labels_[i], lv_color_white(), 0);
            lv_obj_align(hud_value_labels_[i], LV_ALIGN_BOTTOM_MID, 0, 1);
        }

        lv_obj_move_foreground(hud_panel_);
    }

    void RefreshHudLvgl(bool force = false) {
        if (hud_panel_ == nullptr) return;
        const auto pet = PetStateEngine::GetInstance().GetSnapshot();
        const std::array<int, kHudRowCount> values = {
            static_cast<int>(pet.fullness),
            static_cast<int>(pet.energy),
            static_cast<int>(pet.mood),
            static_cast<int>(pet.friendship),
            static_cast<int>(pet.coins)
        };

        char buf[8];
        for (size_t i = 0; i < values.size(); ++i) {
            if (!force && hud_last_values_[i] == values[i]) continue;
            hud_last_values_[i] = values[i];
            if (i == 4) {
                std::snprintf(buf, sizeof(buf), "%03d", std::clamp(values[i], 0, 999));
            } else {
                std::snprintf(buf, sizeof(buf), "%02d", std::clamp(values[i], 0, 99));
            }
            if (hud_value_labels_[i] != nullptr) {
                lv_label_set_text(hud_value_labels_[i], buf);
            }
        }
        lv_obj_move_foreground(hud_panel_);
    }

    void CreateMenuLvgl(lv_obj_t* screen) {
        const int status_h = StatusBarHeightLvgl();
        menu_overlay_ = lv_obj_create(screen);
        lv_obj_set_pos(menu_overlay_, 0, status_h);
        lv_obj_set_size(menu_overlay_, LV_HOR_RES, LV_VER_RES - status_h);
        lv_obj_set_style_radius(menu_overlay_, 0, 0);
        lv_obj_set_style_bg_color(menu_overlay_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(menu_overlay_, static_cast<lv_opa_t>(92), 0);
        lv_obj_set_style_border_width(menu_overlay_, 0, 0);
        lv_obj_set_style_pad_all(menu_overlay_, 0, 0);
        lv_obj_set_scrollbar_mode(menu_overlay_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_CLICKABLE);

        menu_card_ = lv_obj_create(menu_overlay_);
        lv_obj_set_size(menu_card_, 174, 166);
        lv_obj_align(menu_card_, LV_ALIGN_CENTER, -8, 0);
        lv_obj_set_style_radius(menu_card_, 14, 0);
        lv_obj_set_style_bg_color(menu_card_, lv_color_hex(0x17202A), 0);
        lv_obj_set_style_bg_opa(menu_card_, static_cast<lv_opa_t>(220), 0);
        lv_obj_set_style_border_width(menu_card_, 1, 0);
        lv_obj_set_style_border_color(menu_card_, lv_color_hex(0x7F8C8D), 0);
        lv_obj_set_style_pad_all(menu_card_, 8, 0);
        lv_obj_set_style_pad_row(menu_card_, 4, 0);
        lv_obj_set_flex_flow(menu_card_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(menu_card_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(menu_card_, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t* title = lv_label_create(menu_card_);
        lv_label_set_text(title, "MOCHI");
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFD98A), 0);

        for (size_t i = 0; i < kMenuItemCount; ++i) {
            menu_rows_[i] = lv_obj_create(menu_card_);
            lv_obj_set_size(menu_rows_[i], 148, 27);
            lv_obj_set_style_radius(menu_rows_[i], 7, 0);
            lv_obj_set_style_border_width(menu_rows_[i], 0, 0);
            lv_obj_set_style_pad_all(menu_rows_[i], 4, 0);
            lv_obj_set_scrollbar_mode(menu_rows_[i], LV_SCROLLBAR_MODE_OFF);
            lv_obj_clear_flag(menu_rows_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(menu_rows_[i], LV_OBJ_FLAG_CLICKABLE);

            menu_row_labels_[i] = lv_label_create(menu_rows_[i]);
            lv_obj_set_style_text_color(menu_row_labels_[i], lv_color_white(), 0);
            lv_obj_align(menu_row_labels_[i], LV_ALIGN_LEFT_MID, 2, 0);
        }

        lv_obj_add_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        menu_open_.store(false);
    }

    void UpdateMenuRowsLvgl() {
        if (menu_overlay_ == nullptr) return;
        const auto pet = PetStateEngine::GetInstance().GetSnapshot();
        const char* texts[kMenuItemCount] = {
            "Pet",
            "Play (-10 E)",
            pet.sleeping ? "Wake" : "Sleep",
            "Close"
        };

        for (size_t i = 0; i < kMenuItemCount; ++i) {
            if (menu_row_labels_[i] != nullptr) {
                char line[32];
                std::snprintf(line, sizeof(line), "%s %s", i == menu_selected_ ? ">" : " ", texts[i]);
                lv_label_set_text(menu_row_labels_[i], line);
            }
            if (menu_rows_[i] != nullptr) {
                if (i == menu_selected_) {
                    lv_obj_set_style_bg_color(menu_rows_[i], lv_color_hex(0x3B536B), 0);
                    lv_obj_set_style_bg_opa(menu_rows_[i], static_cast<lv_opa_t>(190), 0);
                } else {
                    lv_obj_set_style_bg_opa(menu_rows_[i], LV_OPA_TRANSP, 0);
                }
            }
        }
    }

    void OpenMenuLvgl() {
        if (menu_overlay_ == nullptr) return;
        menu_selected_ = 0;
        UpdateMenuRowsLvgl();
        lv_obj_remove_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(menu_overlay_);
        menu_open_.store(true);
        action_ticks_left_ = 8;
    }

    void CloseMenuLvgl() {
        if (menu_overlay_ != nullptr) {
            lv_obj_add_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        }
        menu_open_.store(false);
        menu_move_requested_.store(0);
        menu_select_requested_.store(false);
        idle_stable_ticks_ = 0;
    }

    void SelectMenuItemLvgl() {
        const PetMenuItem item = static_cast<PetMenuItem>(menu_selected_);
        const auto pet = PetStateEngine::GetInstance().GetSnapshot();
        CloseMenuLvgl();

        // Persistent pet mutations are scheduled onto Application/main context.
        // The LVGL 200ms timer only decides which command was selected.
        Application::GetInstance().Schedule([item, was_sleeping = pet.sleeping]() {
            auto& engine = PetStateEngine::GetInstance();
            switch (item) {
                case PetMenuItem::Pet:
                    engine.Pet();
                    break;
                case PetMenuItem::Play:
                    engine.Play();
                    break;
                case PetMenuItem::SleepWake:
                    if (was_sleeping) engine.Wake();
                    else engine.Sleep();
                    break;
                case PetMenuItem::Close:
                case PetMenuItem::Count:
                    break;
            }
        });
    }

    void ProcessMenuRequestsLvgl() {
        const auto state = Application::GetInstance().GetDeviceState();

        // Voice always wins. A conversation closes the menu immediately rather
        // than allowing navigation to steal Talk/volume input.
        if (state != kDeviceStateIdle) {
            menu_toggle_requested_.store(false);
            menu_move_requested_.store(0);
            menu_select_requested_.store(false);
            if (menu_open_.load()) CloseMenuLvgl();
            return;
        }

        if (menu_toggle_requested_.exchange(false)) {
            if (menu_open_.load()) CloseMenuLvgl();
            else OpenMenuLvgl();
        }

        if (!menu_open_.load()) {
            menu_move_requested_.store(0);
            menu_select_requested_.store(false);
            return;
        }

        const int move = menu_move_requested_.exchange(0);
        if (move != 0) {
            int next = static_cast<int>(menu_selected_) + move;
            const int count = static_cast<int>(kMenuItemCount);
            next %= count;
            if (next < 0) next += count;
            menu_selected_ = static_cast<uint8_t>(next);
            UpdateMenuRowsLvgl();
        }

        if (menu_select_requested_.exchange(false)) {
            SelectMenuItemLvgl();
        }
    }

    void ApplyMochiLayout() {
        if (!Lock(1000)) return;

        lv_obj_t* screen = lv_screen_active();

        if (chat_message_label_ != nullptr) {
            lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        }

        if (emoji_box_ != nullptr) {
            lv_obj_set_style_translate_y(emoji_box_, 50, 0);
            lv_obj_set_style_translate_x(emoji_box_, 0, 0);
            lv_obj_move_foreground(emoji_box_);
        }

        CreateAmbientObjects(screen);

        mochi_chat_bubble_ = lv_obj_create(screen);
        // Leave a clean right margin for the always-visible 15D HUD.
        lv_obj_set_width(mochi_chat_bubble_, 220);
        lv_obj_set_height(mochi_chat_bubble_, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(mochi_chat_bubble_, 12, 0);
        lv_obj_set_style_bg_color(mochi_chat_bubble_, lv_color_hex(0xF8F7F2), 0);
        lv_obj_set_style_bg_opa(mochi_chat_bubble_, static_cast<lv_opa_t>(191), 0);
        lv_obj_set_style_border_width(mochi_chat_bubble_, 1, 0);
        lv_obj_set_style_border_color(mochi_chat_bubble_, lv_color_hex(0xD8D6D0), 0);
        lv_obj_set_style_border_opa(mochi_chat_bubble_, static_cast<lv_opa_t>(120), 0);
        lv_obj_set_style_pad_left(mochi_chat_bubble_, 10, 0);
        lv_obj_set_style_pad_right(mochi_chat_bubble_, 10, 0);
        lv_obj_set_style_pad_top(mochi_chat_bubble_, 7, 0);
        lv_obj_set_style_pad_bottom(mochi_chat_bubble_, 7, 0);
        lv_obj_set_scrollbar_mode(mochi_chat_bubble_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, -10, 48);

        mochi_chat_label_ = lv_label_create(mochi_chat_bubble_);
        lv_obj_set_width(mochi_chat_label_, 198);
        lv_label_set_long_mode(mochi_chat_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(mochi_chat_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(mochi_chat_label_, lv_color_hex(0x26364D), 0);
        lv_label_set_text(mochi_chat_label_, "");
        lv_obj_center(mochi_chat_label_);

        lv_obj_add_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(mochi_chat_bubble_);

        CreateHudLvgl(screen);
        CreateMenuLvgl(screen);
        Unlock();
    }

    void StartAmbientLvglTimer() {
        if (!Lock(1000)) return;
        if (ambient_timer_ == nullptr) {
            ambient_timer_ = lv_timer_create(&Mochi183LcdDisplay::AmbientLvglTimerThunk,
                                             kAmbientTimerMs, this);
        }
        Unlock();
    }

    void QueueAmbientEmotion(const char* emotion) {
        if (emotion == nullptr) return;
        std::string next_emotion(emotion);
        Application::GetInstance().Schedule([this, next_emotion]() {
            if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
                LcdDisplay::SetEmotion(next_emotion.c_str());
            }
        });
    }

    void ChooseNextIdleActionLvgl() {
        // Snapshot is read only when choosing a new action (every few seconds),
        // never every 200ms tick. PetStateEngine itself performs no LVGL/network
        // work, so this preserves the Fix14 LVGL safety model.
        const auto pet = PetStateEngine::GetInstance().GetSnapshot();

        // Logical sleep has absolute priority over decorative random behavior.
        // Voice UI still overrides this because AmbientTickLvgl pauses whenever
        // Application leaves Idle; after voice settles, sleep visuals resume.
        if (pet.sleeping) {
            mochi_target_x_ = mochi_x_;
            QueueAmbientEmotion("ambient_sleep");
            action_ticks_left_ = 40 + static_cast<int>(esp_random() % 21); // 8-12s
            return;
        }

        int walk_weight = 52;
        int sit_weight = 18;
        int lie_weight = 12;
        int sleep_weight = 8;
        int idle_weight = 10;

        // Tired Mochi becomes visibly calmer before the auto-sleep threshold.
        if (pet.energy <= 35) {
            walk_weight = 10;
            sit_weight = 25;
            lie_weight = 40;
            sleep_weight = 20;
            idle_weight = 5;
        // Low Fullness suppresses energetic wandering, but less strongly than
        // true low Energy so hunger and fatigue remain visually distinguishable.
        } else if (pet.fullness <= 24) {
            walk_weight = 18;
            sit_weight = 38;
            lie_weight = 24;
            sleep_weight = 10;
            idle_weight = 10;
        // A happy, well-rested and well-fed Mochi is more lively.
        } else if (pet.mood >= 80 && pet.energy >= 50 && pet.fullness > 29) {
            walk_weight = 60;
            sit_weight = 12;
            lie_weight = 8;
            sleep_weight = 4;
            idle_weight = 16;
        }

        // Friendship is a light long-term bias, not a dominant state.
        if (pet.friendship >= 60 && pet.energy > 35 && pet.fullness > 24) {
            walk_weight += 4;
            idle_weight += 4;
            sit_weight = std::max(6, sit_weight - 4);
            lie_weight = std::max(5, lie_weight - 4);
        }

        const int total_weight = walk_weight + sit_weight + lie_weight + sleep_weight + idle_weight;
        int roll = static_cast<int>(esp_random() % static_cast<uint32_t>(total_weight));

        if (roll < walk_weight) {
            // The 80px Mochi sprite at +50 ends around x=232 on a 284px screen;
            // HUD begins around x=244, leaving a physical gap between them.
            const int positions[] = {-50, -25, 0, 25, 50};
            mochi_target_x_ = positions[esp_random() % 5];
            if (mochi_target_x_ == mochi_x_) {
                mochi_target_x_ = (mochi_x_ <= 0) ? 50 : -50;
            }

            if (mochi_target_x_ < mochi_x_) {
                QueueAmbientEmotion("ambient_walk_left");
            } else {
                QueueAmbientEmotion("ambient_walk_right");
            }

            int distance = std::abs(mochi_target_x_ - mochi_x_);
            action_ticks_left_ = std::max(2, (distance + kMoveStepPx - 1) / kMoveStepPx);
            return;
        }

        roll -= walk_weight;
        if (roll < sit_weight) {
            mochi_target_x_ = mochi_x_;
            QueueAmbientEmotion("ambient_sit");
            action_ticks_left_ = 20 + static_cast<int>(esp_random() % 16); // 4.0-7.0s
            return;
        }

        roll -= sit_weight;
        if (roll < lie_weight) {
            mochi_target_x_ = mochi_x_;
            QueueAmbientEmotion("ambient_lie");
            action_ticks_left_ = 25 + static_cast<int>(esp_random() % 16); // 5.0-8.0s
            return;
        }

        roll -= lie_weight;
        if (roll < sleep_weight) {
            mochi_target_x_ = mochi_x_;
            QueueAmbientEmotion("ambient_sleep");
            action_ticks_left_ = 40 + static_cast<int>(esp_random() % 21); // 8.0-12.0s
            return;
        }

        mochi_target_x_ = mochi_x_;
        QueueAmbientEmotion("ambient_idle");
        action_ticks_left_ = 10 + static_cast<int>(esp_random() % 16); // 2.0-5.0s
    }

    void StepMochiPositionLvgl() {
        if (mochi_x_ < mochi_target_x_) {
            mochi_x_ = std::min(mochi_x_ + kMoveStepPx, mochi_target_x_);
        } else if (mochi_x_ > mochi_target_x_) {
            mochi_x_ = std::max(mochi_x_ - kMoveStepPx, mochi_target_x_);
        }

        if (emoji_box_ != nullptr) {
            lv_obj_set_style_translate_x(emoji_box_, mochi_x_, 0);
        }
    }

    void UpdateAmbientLightingLvgl(AmbientPeriod period) {
        ++ambient_phase_;

        if (period == AmbientPeriod::Noon || period == AmbientPeriod::Afternoon) {
            if (sun_patch_ != nullptr) {
                lv_obj_remove_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);

                const int travel_phase = static_cast<int>(ambient_phase_ % 400);
                const int travel = travel_phase <= 200 ? travel_phase : (400 - travel_phase);
                const int x = 55 + travel / 2; // 55..155, slow ~80s sweep
                const int y = (period == AmbientPeriod::Noon) ? 136 : 146;

                const int shimmer_phase = static_cast<int>(ambient_phase_ % 40);
                const int shimmer = shimmer_phase <= 20 ? shimmer_phase : (40 - shimmer_phase);
                const int opacity = (period == AmbientPeriod::Noon ? 25 : 21) + shimmer / 2;

                lv_obj_set_pos(sun_patch_, x, y);
                lv_obj_set_style_bg_color(
                    sun_patch_,
                    period == AmbientPeriod::Noon ? lv_color_hex(0xFFF0A8)
                                                  : lv_color_hex(0xFFCB78),
                    0);
                lv_obj_set_style_bg_opa(sun_patch_, static_cast<lv_opa_t>(opacity), 0);
            }
            if (night_glow_ != nullptr) lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
            if (night_floor_glow_ != nullptr) lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
        } else if (period == AmbientPeriod::Night) {
            if (sun_patch_ != nullptr) lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);

            // Slow candle/lamp breathing: visible but not a harsh strobe.
            const int pulse_phase = static_cast<int>(ambient_phase_ % 50);
            const int pulse = pulse_phase <= 25 ? pulse_phase : (50 - pulse_phase); // 0..25
            const int slow_step = static_cast<int>((ambient_phase_ / 4) % 3) - 1;
            const int tiny_flicker = static_cast<int>(esp_random() % 5) - 2;

            if (night_glow_ != nullptr) {
                lv_obj_remove_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(night_glow_, 196 + slow_step, 54 - slow_step);
                lv_obj_set_style_bg_opa(
                    night_glow_,
                    static_cast<lv_opa_t>(std::clamp(42 + pulse + tiny_flicker, 38, 70)),
                    0);
            }
            if (night_floor_glow_ != nullptr) {
                lv_obj_remove_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(night_floor_glow_, 166 - slow_step, 110 + slow_step);
                lv_obj_set_style_bg_opa(
                    night_floor_glow_,
                    static_cast<lv_opa_t>(std::clamp(24 + pulse / 2 + tiny_flicker, 20, 40)),
                    0);
            }
        } else {
            if (sun_patch_ != nullptr) lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);
            if (night_glow_ != nullptr) lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
            if (night_floor_glow_ != nullptr) lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void AmbientTickLvgl() {
        ProcessMenuRequestsLvgl();

        if (++hud_refresh_ticks_ >= kHudRefreshTicks) {
            hud_refresh_ticks_ = 0;
            RefreshHudLvgl();
        }

        // Menu is a foreground interaction mode; pause decorative movement while
        // it is open but keep HUD/menu refresh alive.
        if (menu_open_.load()) {
            return;
        }

        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            idle_stable_ticks_ = 0;
            action_ticks_left_ = 8;
            return;
        }

        if (!Assets::GetInstance().checksum_valid()) {
            idle_stable_ticks_ = 0;
            return;
        }

        if (idle_stable_ticks_ < kIdleSettleTicks) {
            ++idle_stable_ticks_;
            return;
        }

        const AmbientPeriod period = GetAmbientPeriod();
        if (period == AmbientPeriod::Unknown) {
            return;
        }

        const bool need_background = (period != ambient_period_) || (active_background_ == nullptr);
        if (need_background) {
            if (!LoadAndApplyBackgroundLvgl(period)) {
                return;
            }
            ambient_period_ = period;
            background_reapply_ticks_ = 0;
        } else {
            ++background_reapply_ticks_;
            if (background_reapply_ticks_ >= kBackgroundReapplyTicks &&
                active_background_ != nullptr && container_ != nullptr) {
                background_reapply_ticks_ = 0;
                lv_obj_set_style_bg_image_src(container_, active_background_->image_dsc(), 0);
                lv_obj_set_style_bg_image_opa(container_, LV_OPA_COVER, 0);
            }
        }

        UpdateAmbientLightingLvgl(period);
        StepMochiPositionLvgl();

        if (action_ticks_left_ > 0) {
            --action_ticks_left_;
            return;
        }
        ChooseNextIdleActionLvgl();
    }

public:
    Mochi183LcdDisplay(esp_lcd_panel_io_handle_t panel_io,
                       esp_lcd_panel_handle_t panel,
                       int width,
                       int height,
                       int offset_x,
                       int offset_y,
                       bool mirror_x,
                       bool mirror_y,
                       bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                        mirror_x, mirror_y, swap_xy) {
        ApplyMochiLayout();
        StartAmbientLvglTimer();
    }

    ~Mochi183LcdDisplay() override {
        if (ambient_timer_ != nullptr && Lock(1000)) {
            lv_timer_delete(ambient_timer_);
            ambient_timer_ = nullptr;
            Unlock();
        }
    }

    // Thread-safe button-facing API. No caller outside LVGL touches LVGL objects.
    void RequestMenuToggle() {
        menu_toggle_requested_.store(true);
    }

    void RequestMenuMove(int delta) {
        if (delta == 0) return;
        menu_move_requested_.fetch_add(delta);
    }

    void RequestMenuSelect() {
        menu_select_requested_.store(true);
    }

    bool IsMenuOpen() const {
        return menu_open_.load();
    }

    void SetChatMessage(const char* role, const char* content) override {
        if (mochi_chat_bubble_ == nullptr || mochi_chat_label_ == nullptr) return;
        if (!Lock(1000)) return;

        const bool is_user = role != nullptr && std::strcmp(role, "user") == 0;
        const bool is_assistant = role != nullptr && std::strcmp(role, "assistant") == 0;

        if ((!is_user && !is_assistant) || content == nullptr || content[0] == '\0') {
            lv_label_set_text(mochi_chat_label_, "");
            lv_obj_add_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (is_user) {
                lv_obj_set_style_bg_color(mochi_chat_bubble_, lv_color_hex(0xEAF3FF), 0);
                lv_obj_set_style_border_color(mochi_chat_bubble_, lv_color_hex(0xC9D9EC), 0);
            } else {
                lv_obj_set_style_bg_color(mochi_chat_bubble_, lv_color_hex(0xF8F7F2), 0);
                lv_obj_set_style_border_color(mochi_chat_bubble_, lv_color_hex(0xD8D6D0), 0);
            }
            lv_label_set_text(mochi_chat_label_, content);
            lv_obj_remove_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, -10, 48);
            lv_obj_move_foreground(mochi_chat_bubble_);
            if (hud_panel_ != nullptr) {
                lv_obj_move_foreground(hud_panel_);
            }
        }

        Unlock();
    }
};
