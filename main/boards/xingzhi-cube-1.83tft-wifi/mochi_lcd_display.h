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
    static constexpr time_t kValidEpochThreshold = 1704067200;
    static constexpr int kVietnamUtcOffsetSeconds = 7 * 60 * 60;
    static constexpr uint32_t kAmbientTimerMs = 200;
    static constexpr uint32_t kIdleSettleTicks = 25;          // 5 seconds
    static constexpr uint32_t kBackgroundReapplyTicks = 300; // 60 seconds
    static constexpr int kMoveStepPx = 5;

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
        // Hunger also suppresses energetic wandering, but less strongly than
        // true low Energy so hunger and fatigue remain visually distinguishable.
        } else if (pet.hunger >= 75) {
            walk_weight = 18;
            sit_weight = 38;
            lie_weight = 24;
            sleep_weight = 10;
            idle_weight = 10;
        // A happy, well-rested Mochi is more lively. No new asset is required;
        // we express this only through the stable Fix14 walk/idle set.
        } else if (pet.mood >= 80 && pet.energy >= 50 && pet.hunger < 70) {
            walk_weight = 60;
            sit_weight = 12;
            lie_weight = 8;
            sleep_weight = 4;
            idle_weight = 16;
        }

        // Friendship is a light long-term bias, not a dominant state. Higher
        // friendship slightly favors active/attentive behavior without making
        // low Energy or hunger disappear.
        if (pet.friendship >= 60 && pet.energy > 35 && pet.hunger < 75) {
            walk_weight += 4;
            idle_weight += 4;
            sit_weight = std::max(6, sit_weight - 4);
            lie_weight = std::max(5, lie_weight - 4);
        }

        const int total_weight = walk_weight + sit_weight + lie_weight + sleep_weight + idle_weight;
        int roll = static_cast<int>(esp_random() % static_cast<uint32_t>(total_weight));

        if (roll < walk_weight) {
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