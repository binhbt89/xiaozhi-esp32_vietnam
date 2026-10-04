#pragma once

#include "display/lcd_display.h"
#include <lvgl.h>
#include <cstring>

class MochiSpiLcdDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* mochi_chat_bubble_ = nullptr;
    lv_obj_t* mochi_chat_label_ = nullptr;

    void ApplyMochiLayout() {
        if (!Lock(1000)) {
            return;
        }

        lv_obj_t* screen = lv_screen_active();

        // Keep the existing GIF sizes untouched; move only the LVGL container.
        if (emoji_box_ != nullptr) {
            lv_obj_set_parent(emoji_box_, screen);
            lv_obj_set_style_flex_grow(emoji_box_, 0, 0);
            lv_obj_align(emoji_box_, LV_ALIGN_BOTTOM_MID, 0, -5);
            lv_obj_move_foreground(emoji_box_);
        }

        // Dedicated answer bubble. Do not reuse the base dynamic chat label.
        mochi_chat_bubble_ = lv_obj_create(screen);
        lv_obj_set_width(mochi_chat_bubble_, 236);
        lv_obj_set_height(mochi_chat_bubble_, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(mochi_chat_bubble_, 12, 0);
        lv_obj_set_style_bg_color(mochi_chat_bubble_, lv_color_hex(0xF8F7F2), 0);
        lv_obj_set_style_bg_opa(mochi_chat_bubble_, static_cast<lv_opa_t>(191), 0); // ~75%
        lv_obj_set_style_border_width(mochi_chat_bubble_, 1, 0);
        lv_obj_set_style_border_color(mochi_chat_bubble_, lv_color_hex(0xD8D6D0), 0);
        lv_obj_set_style_border_opa(mochi_chat_bubble_, static_cast<lv_opa_t>(120), 0);
        lv_obj_set_style_pad_left(mochi_chat_bubble_, 10, 0);
        lv_obj_set_style_pad_right(mochi_chat_bubble_, 10, 0);
        lv_obj_set_style_pad_top(mochi_chat_bubble_, 7, 0);
        lv_obj_set_style_pad_bottom(mochi_chat_bubble_, 7, 0);
        lv_obj_set_scrollbar_mode(mochi_chat_bubble_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, 0, 54);

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

public:
    MochiSpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io,
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
    }

    void SetChatMessage(const char* role, const char* content) override {
        if (mochi_chat_bubble_ == nullptr || mochi_chat_label_ == nullptr) {
            return;
        }
        if (!Lock(1000)) {
            return;
        }

        const bool is_user = (role != nullptr && std::strcmp(role, "user") == 0);
        if (is_user || content == nullptr || content[0] == '\0') {
            lv_label_set_text(mochi_chat_label_, "");
            lv_obj_add_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(mochi_chat_label_, content);
            lv_obj_remove_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, 0, 54);
            lv_obj_move_foreground(mochi_chat_bubble_);
        }

        Unlock();
    }
};
