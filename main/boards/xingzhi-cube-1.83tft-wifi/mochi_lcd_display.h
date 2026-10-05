#pragma once

#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_image.h"
#include "application.h"
#include "assets.h"

#include <lvgl.h>
#include <esp_random.h>
#include <esp_log.h>
#include <esp_sntp.h>

#include <algorithm>
#include <atomic>
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

    static constexpr const char* kAmbientTag = "MochiAmbient";
    static constexpr time_t kValidEpochThreshold = 1704067200; // 2024-01-01 UTC
    static constexpr int kVietnamUtcOffsetSeconds = 7 * 60 * 60;

    lv_obj_t* mochi_chat_bubble_ = nullptr;
    lv_obj_t* mochi_chat_label_ = nullptr;
    lv_obj_t* sun_patch_ = nullptr;
    lv_obj_t* night_glow_ = nullptr;
    lv_obj_t* night_floor_glow_ = nullptr;
    lv_timer_t* ambient_timer_ = nullptr;

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

    void RequestSntpFallback() {
        bool expected = false;
        if (!ambient_sntp_requested_.compare_exchange_strong(expected, true)) {
            return;
        }

        // Never do network setup from the LVGL timer callback. Queue it onto
        // the normal application event loop and let the LVGL timer simply wait
        // until time() becomes valid.
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

        // The existing OTA path writes server_time after applying the returned
        // timezone_offset, so gmtime_r(now) is already local time there.
        // Our SNTP fallback stores UTC, therefore add UTC+7 only when fallback
        // was actually started by this class.
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

        if (hour >= 5 && hour < 11) {
            return AmbientPeriod::Morning;
        }
        if (hour >= 11 && hour < 15) {
            return AmbientPeriod::Noon;
        }
        if (hour >= 15 && hour < 18) {
            return AmbientPeriod::Afternoon;
        }
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

        // Create only the one image descriptor we actually need. Do this from
        // LVGL's own timer context, then switch the SAME object that the stock
        // theme uses (container_). The old implementation changed the screen
        // background while the opaque container still displayed background_day,
        // so the user could never see the selected scene.
        auto next = std::make_shared<LvglCBinImage>(ptr);
        if (next == nullptr || next->image_dsc() == nullptr) {
            ESP_LOGE(kAmbientTag, "Failed to create background descriptor: %s", name);
            return false;
        }

        auto previous = active_background_; // keep old descriptor alive until switch completes
        active_background_ = next;
        lv_obj_set_style_bg_image_src(container_, active_background_->image_dsc(), 0);
        lv_obj_set_style_bg_image_opa(container_, LV_OPA_COVER, 0);
        lv_obj_invalidate(container_);

        ESP_LOGI(kAmbientTag, "Applied background: %s", name);
        return true;
    }

    void CreateAmbientObjects(lv_obj_t* screen) {
        // These are deliberately tiny translucent overlays. They are children
        // of the screen, created once, and only their position/opacity changes.
        sun_patch_ = lv_obj_create(screen);
        lv_obj_remove_style_all(sun_patch_);
        lv_obj_set_size(sun_patch_, 68, 38);
        lv_obj_set_style_radius(sun_patch_, 6, 0);
        lv_obj_set_style_bg_color(sun_patch_, lv_color_hex(0xFFF0A8), 0);
        lv_obj_set_style_bg_opa(sun_patch_, static_cast<lv_opa_t>(22), 0);
        lv_obj_clear_flag(sun_patch_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(sun_patch_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);

        night_glow_ = lv_obj_create(screen);
        lv_obj_remove_style_all(night_glow_);
        lv_obj_set_size(night_glow_, 34, 34);
        lv_obj_set_style_radius(night_glow_, 17, 0);
        lv_obj_set_style_bg_color(night_glow_, lv_color_hex(0xFFB45E), 0);
        lv_obj_set_style_bg_opa(night_glow_, static_cast<lv_opa_t>(30), 0);
        lv_obj_clear_flag(night_glow_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(night_glow_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);

        night_floor_glow_ = lv_obj_create(screen);
        lv_obj_remove_style_all(night_floor_glow_);
        lv_obj_set_size(night_floor_glow_, 48, 18);
        lv_obj_set_style_radius(night_floor_glow_, 8, 0);
        lv_obj_set_style_bg_color(night_floor_glow_, lv_color_hex(0xFFCA78), 0);
        lv_obj_set_style_bg_opa(night_floor_glow_, static_cast<lv_opa_t>(16), 0);
        lv_obj_clear_flag(night_floor_glow_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(night_floor_glow_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_set_width(mochi_chat_bubble_, 236);
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
        lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, 0, 48);

        mochi_chat_label_ = lv_label_create(mochi_chat_bubble_);
        lv_obj_set_width(mochi_chat_label_, 214);
        lv_label_set_long_mode(mochi_chat_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(mochi_chat_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(mochi_chat_label_, lv_color_hex(0x26364D), 0);
        lv_label_set_text(mochi_chat_label_, "");
        lv_obj_center(mochi_chat_label_);

        lv_obj_add_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(mochi_chat_bubble_);
        Unlock();
    }

    void StartAmbientLvglTimer() {
        if (!Lock(1000)) return;
        if (ambient_timer_ == nullptr) {
            ambient_timer_ = lv_timer_create(&Mochi183LcdDisplay::AmbientLvglTimerThunk, 1000, this);
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
        const uint32_t roll = esp_random() % 100;

        if (roll < 46) {
            const int positions[] = {-34, -18, 0, 18, 34};
            mochi_target_x_ = positions[esp_random() % 5];
            if (mochi_target_x_ == mochi_x_) {
                mochi_target_x_ = (mochi_x_ <= 0) ? 28 : -28;
            }

            if (mochi_target_x_ < mochi_x_) {
                QueueAmbientEmotion("ambient_walk_left");
            } else {
                QueueAmbientEmotion("ambient_walk_right");
            }

            int distance = std::abs(mochi_target_x_ - mochi_x_);
            action_ticks_left_ = std::max(5, distance / 4 + 2);
        } else if (roll < 62) {
            QueueAmbientEmotion("ambient_sit");
            action_ticks_left_ = 5 + static_cast<int>(esp_random() % 5);
        } else if (roll < 74) {
            QueueAmbientEmotion("ambient_groom");
            action_ticks_left_ = 5 + static_cast<int>(esp_random() % 4);
        } else if (roll < 84) {
            QueueAmbientEmotion("ambient_lie");
            action_ticks_left_ = 7 + static_cast<int>(esp_random() % 5);
        } else if (roll < 92) {
            QueueAmbientEmotion("ambient_sleep");
            action_ticks_left_ = 10 + static_cast<int>(esp_random() % 7);
        } else {
            QueueAmbientEmotion("ambient_idle");
            mochi_target_x_ = mochi_x_;
            action_ticks_left_ = 5 + static_cast<int>(esp_random() % 5);
        }
    }

    void StepMochiPositionLvgl() {
        if (mochi_x_ < mochi_target_x_) {
            mochi_x_ = std::min(mochi_x_ + 4, mochi_target_x_);
        } else if (mochi_x_ > mochi_target_x_) {
            mochi_x_ = std::max(mochi_x_ - 4, mochi_target_x_);
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
                int phase = static_cast<int>(ambient_phase_ % 120);
                int walk = phase <= 60 ? phase : (120 - phase);
                int x = 92 + walk;
                int y = (period == AmbientPeriod::Noon) ? 137 : 146;
                int flicker = static_cast<int>(esp_random() % 5);
                lv_obj_set_pos(sun_patch_, x, y + (flicker & 1));
                lv_obj_set_style_bg_color(
                    sun_patch_,
                    period == AmbientPeriod::Noon ? lv_color_hex(0xFFF0A8)
                                                  : lv_color_hex(0xFFCB78),
                    0);
                lv_obj_set_style_bg_opa(
                    sun_patch_,
                    static_cast<lv_opa_t>((period == AmbientPeriod::Noon ? 18 : 14) + flicker),
                    0);
            }
            if (night_glow_ != nullptr) lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
            if (night_floor_glow_ != nullptr) lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
        } else if (period == AmbientPeriod::Night) {
            if (sun_patch_ != nullptr) lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);

            int jitter_x = static_cast<int>(esp_random() % 3) - 1;
            int jitter_y = static_cast<int>(esp_random() % 3) - 1;
            if (night_glow_ != nullptr) {
                lv_obj_remove_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(night_glow_, 204 + jitter_x, 63 + jitter_y);
                lv_obj_set_style_bg_opa(
                    night_glow_, static_cast<lv_opa_t>(24 + static_cast<int>(esp_random() % 14)), 0);
            }
            if (night_floor_glow_ != nullptr) {
                lv_obj_remove_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(night_floor_glow_, 185 - jitter_x, 108 + jitter_y);
                lv_obj_set_style_bg_opa(
                    night_floor_glow_, static_cast<lv_opa_t>(12 + static_cast<int>(esp_random() % 10)), 0);
            }
        } else {
            if (sun_patch_ != nullptr) lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);
            if (night_glow_ != nullptr) lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
            if (night_floor_glow_ != nullptr) lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void AmbientTickLvgl() {
        // All decorative LVGL writes in Fix13 happen here, inside LVGL's own
        // timer handler. No FreeRTOS/esp_timer task touches LVGL objects.
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            idle_stable_ticks_ = 0;
            action_ticks_left_ = 2;
            return;
        }

        if (!Assets::GetInstance().checksum_valid()) {
            idle_stable_ticks_ = 0;
            return;
        }

        // Let the normal boot/network/assets pipeline settle for five full idle
        // seconds before loading or switching any background descriptor.
        if (idle_stable_ticks_ < 5) {
            ++idle_stable_ticks_;
            return;
        }

        const AmbientPeriod period = GetAmbientPeriod();
        if (period == AmbientPeriod::Unknown) {
            return;
        }

        bool need_background = (period != ambient_period_) || (active_background_ == nullptr);
        if (need_background) {
            if (!LoadAndApplyBackgroundLvgl(period)) {
                return;
            }
            ambient_period_ = period;
            background_reapply_ticks_ = 0;
        } else {
            ++background_reapply_ticks_;
            // Re-assert the selected image once a minute in case a theme refresh
            // replaced container_'s background. No descriptor recreation needed.
            if (background_reapply_ticks_ >= 60 && active_background_ != nullptr && container_ != nullptr) {
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
            lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, 0, 48);
            lv_obj_move_foreground(mochi_chat_bubble_);
        }

        Unlock();
    }
};
