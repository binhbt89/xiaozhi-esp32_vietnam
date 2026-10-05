#pragma once

#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_image.h"
#include "application.h"
#include "assets.h"

#include <lvgl.h>
#include <esp_timer.h>
#include <esp_random.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <memory>

class Mochi183LcdDisplay : public SpiLcdDisplay {
private:
    enum class AmbientPeriod {
        Unknown = -1,
        Morning = 0,
        Noon,
        Afternoon,
        Night
    };

    lv_obj_t* mochi_chat_bubble_ = nullptr;
    lv_obj_t* mochi_chat_label_ = nullptr;
    lv_obj_t* sun_patch_ = nullptr;
    lv_obj_t* night_glow_ = nullptr;
    lv_obj_t* night_floor_glow_ = nullptr;

    esp_timer_handle_t ambient_timer_ = nullptr;

    std::shared_ptr<LvglCBinImage> bg_morning_;
    std::shared_ptr<LvglCBinImage> bg_noon_;
    std::shared_ptr<LvglCBinImage> bg_afternoon_;
    std::shared_ptr<LvglCBinImage> bg_night_;
    bool ambient_backgrounds_loaded_ = false;

    AmbientPeriod ambient_period_ = AmbientPeriod::Unknown;
    int ambient_boot_grace_ = 18;
    uint32_t ambient_phase_ = 0;

    int mochi_x_ = 0;
    int mochi_target_x_ = 0;
    int action_ticks_left_ = 0;

    static void AmbientTimerThunk(void* arg) {
        auto* self = static_cast<Mochi183LcdDisplay*>(arg);
        if (self != nullptr) {
            self->AmbientTick();
        }
    }

    void CreateAmbientObjects(lv_obj_t* screen) {
        sun_patch_ = lv_obj_create(screen);
        lv_obj_remove_style_all(sun_patch_);
        lv_obj_set_size(sun_patch_, 68, 38);
        lv_obj_set_style_radius(sun_patch_, 6, 0);
        lv_obj_set_style_bg_color(sun_patch_, lv_color_hex(0xFFF0A8), 0);
        lv_obj_set_style_bg_opa(sun_patch_, static_cast<lv_opa_t>(24), 0);
        lv_obj_clear_flag(sun_patch_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(sun_patch_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(sun_patch_);

        night_glow_ = lv_obj_create(screen);
        lv_obj_remove_style_all(night_glow_);
        lv_obj_set_size(night_glow_, 34, 34);
        lv_obj_set_style_radius(night_glow_, 17, 0);
        lv_obj_set_style_bg_color(night_glow_, lv_color_hex(0xFFB45E), 0);
        lv_obj_set_style_bg_opa(night_glow_, static_cast<lv_opa_t>(32), 0);
        lv_obj_clear_flag(night_glow_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(night_glow_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(night_glow_);

        night_floor_glow_ = lv_obj_create(screen);
        lv_obj_remove_style_all(night_floor_glow_);
        lv_obj_set_size(night_floor_glow_, 48, 18);
        lv_obj_set_style_radius(night_floor_glow_, 8, 0);
        lv_obj_set_style_bg_color(night_floor_glow_, lv_color_hex(0xFFCA78), 0);
        lv_obj_set_style_bg_opa(night_floor_glow_, static_cast<lv_opa_t>(18), 0);
        lv_obj_clear_flag(night_floor_glow_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(night_floor_glow_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(night_floor_glow_);
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

    bool LoadCbinBackground(const char* name, std::shared_ptr<LvglCBinImage>& output) {
        void* ptr = nullptr;
        size_t size = 0;
        auto& assets = Assets::GetInstance();
        if (!assets.checksum_valid()) {
            return false;
        }
        if (!assets.GetAssetData(name, ptr, size) || ptr == nullptr || size < 32) {
            return false;
        }
        output = std::make_shared<LvglCBinImage>(ptr);
        return output != nullptr && output->image_dsc() != nullptr;
    }

    void EnsureAmbientBackgroundsLoaded() {
        if (ambient_backgrounds_loaded_) {
            return;
        }

        bool ok = true;
        ok &= LoadCbinBackground("background_morning.raw", bg_morning_);
        ok &= LoadCbinBackground("background_noon.raw", bg_noon_);
        ok &= LoadCbinBackground("background_afternoon.raw", bg_afternoon_);
        ok &= LoadCbinBackground("background_night.raw", bg_night_);
        ambient_backgrounds_loaded_ = ok;
    }

    AmbientPeriod GetAmbientPeriod() {
        time_t now = time(nullptr);
        struct tm tm_now {};
        gmtime_r(&now, &tm_now);

        if (tm_now.tm_year < (2024 - 1900)) {
            return AmbientPeriod::Noon;
        }

        const int hour = tm_now.tm_hour;
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

    std::shared_ptr<LvglCBinImage> BackgroundFor(AmbientPeriod period) {
        switch (period) {
            case AmbientPeriod::Morning: return bg_morning_;
            case AmbientPeriod::Noon: return bg_noon_;
            case AmbientPeriod::Afternoon: return bg_afternoon_;
            case AmbientPeriod::Night: return bg_night_;
            default: return nullptr;
        }
    }

    void ApplyAmbientBackground(AmbientPeriod period) {
        EnsureAmbientBackgroundsLoaded();
        if (!ambient_backgrounds_loaded_) {
            return;
        }

        auto background = BackgroundFor(period);
        if (background == nullptr || background->image_dsc() == nullptr) {
            return;
        }

        if (!Lock(1000)) return;
        lv_obj_t* screen = lv_screen_active();
        lv_obj_set_style_bg_image_src(screen, background->image_dsc(), 0);
        lv_obj_set_style_bg_image_opa(screen, LV_OPA_COVER, 0);
        lv_obj_invalidate(screen);
        Unlock();
    }

    void UpdateAmbientLighting(AmbientPeriod period) {
        if (period != ambient_period_) {
            ambient_period_ = period;
            ApplyAmbientBackground(period);
        }

        if (!Lock(1000)) return;

        ++ambient_phase_;

        if (period == AmbientPeriod::Noon || period == AmbientPeriod::Afternoon) {
            if (sun_patch_ != nullptr) {
                lv_obj_remove_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);
                int phase = static_cast<int>(ambient_phase_ % 120);
                int walk = phase <= 60 ? phase : (120 - phase);
                int x = 92 + walk;
                int y = (period == AmbientPeriod::Noon) ? 137 : 146;
                int flicker = static_cast<int>(esp_random() % 6);

                lv_obj_set_pos(sun_patch_, x, y + (flicker & 1));
                lv_obj_set_style_bg_color(
                    sun_patch_,
                    period == AmbientPeriod::Noon ? lv_color_hex(0xFFF0A8)
                                                  : lv_color_hex(0xFFCB78),
                    0);
                lv_obj_set_style_bg_opa(
                    sun_patch_,
                    static_cast<lv_opa_t>((period == AmbientPeriod::Noon ? 20 : 16) + flicker),
                    0);
            }

            if (night_glow_ != nullptr) {
                lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
            }
            if (night_floor_glow_ != nullptr) {
                lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (period == AmbientPeriod::Night) {
            if (sun_patch_ != nullptr) {
                lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);
            }

            int jitter_x = static_cast<int>(esp_random() % 3) - 1;
            int jitter_y = static_cast<int>(esp_random() % 3) - 1;
            int glow = 26 + static_cast<int>(esp_random() % 18);

            if (night_glow_ != nullptr) {
                lv_obj_remove_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(night_glow_, 204 + jitter_x, 63 + jitter_y);
                lv_obj_set_style_bg_opa(night_glow_, static_cast<lv_opa_t>(glow), 0);
            }
            if (night_floor_glow_ != nullptr) {
                lv_obj_remove_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(night_floor_glow_, 185 - jitter_x, 108 + jitter_y);
                lv_obj_set_style_bg_opa(
                    night_floor_glow_,
                    static_cast<lv_opa_t>(14 + static_cast<int>(esp_random() % 12)),
                    0);
            }
        } else {
            if (sun_patch_ != nullptr) {
                lv_obj_add_flag(sun_patch_, LV_OBJ_FLAG_HIDDEN);
            }
            if (night_glow_ != nullptr) {
                lv_obj_add_flag(night_glow_, LV_OBJ_FLAG_HIDDEN);
            }
            if (night_floor_glow_ != nullptr) {
                lv_obj_add_flag(night_floor_glow_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        Unlock();
    }

    void SetAmbientEmotion(const char* emotion) {
        LcdDisplay::SetEmotion(emotion);
    }

    void ChooseNextIdleAction() {
        const uint32_t roll = esp_random() % 100;

        if (roll < 46) {
            const int positions[] = {-34, -18, 0, 18, 34};
            mochi_target_x_ = positions[esp_random() % 5];

            if (mochi_target_x_ == mochi_x_) {
                mochi_target_x_ = (mochi_x_ <= 0) ? 28 : -28;
            }

            if (mochi_target_x_ < mochi_x_) {
                SetAmbientEmotion("ambient_walk_left");
            } else {
                SetAmbientEmotion("ambient_walk_right");
            }

            int distance = std::abs(mochi_target_x_ - mochi_x_);
            action_ticks_left_ = std::max(5, distance / 4 + 2);
        } else if (roll < 62) {
            SetAmbientEmotion("ambient_sit");
            action_ticks_left_ = 5 + static_cast<int>(esp_random() % 5);
        } else if (roll < 74) {
            SetAmbientEmotion("ambient_groom");
            action_ticks_left_ = 5 + static_cast<int>(esp_random() % 4);
        } else if (roll < 84) {
            SetAmbientEmotion("ambient_lie");
            action_ticks_left_ = 7 + static_cast<int>(esp_random() % 5);
        } else if (roll < 92) {
            SetAmbientEmotion("ambient_sleep");
            action_ticks_left_ = 10 + static_cast<int>(esp_random() % 7);
        } else {
            SetAmbientEmotion("ambient_idle");
            mochi_target_x_ = mochi_x_;
            action_ticks_left_ = 5 + static_cast<int>(esp_random() % 5);
        }
    }

    void StepMochiPosition() {
        if (mochi_x_ < mochi_target_x_) {
            mochi_x_ = std::min(mochi_x_ + 4, mochi_target_x_);
        } else if (mochi_x_ > mochi_target_x_) {
            mochi_x_ = std::max(mochi_x_ - 4, mochi_target_x_);
        }

        if (!Lock(1000)) return;
        if (emoji_box_ != nullptr) {
            lv_obj_set_style_translate_x(emoji_box_, mochi_x_, 0);
        }
        Unlock();
    }

    void AmbientTick() {
        if (ambient_boot_grace_ > 0) {
            --ambient_boot_grace_;
            return;
        }

        const AmbientPeriod period = GetAmbientPeriod();
        UpdateAmbientLighting(period);

        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            action_ticks_left_ = 2;
            return;
        }

        StepMochiPosition();

        if (action_ticks_left_ > 0) {
            --action_ticks_left_;
            return;
        }

        ChooseNextIdleAction();
    }

    void StartAmbientTimer() {
        esp_timer_create_args_t args = {};
        args.callback = &Mochi183LcdDisplay::AmbientTimerThunk;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "mochi_ambient";
        args.skip_unhandled_events = true;

        if (esp_timer_create(&args, &ambient_timer_) == ESP_OK) {
            esp_timer_start_periodic(ambient_timer_, 1000000);
        }
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
        StartAmbientTimer();
    }

    ~Mochi183LcdDisplay() override {
        if (ambient_timer_ != nullptr) {
            esp_timer_stop(ambient_timer_);
            esp_timer_delete(ambient_timer_);
            ambient_timer_ = nullptr;
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
