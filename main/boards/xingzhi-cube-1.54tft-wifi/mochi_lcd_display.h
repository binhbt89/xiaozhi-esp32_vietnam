#pragma once

#include "display/lcd_display.h"
#include <lvgl.h>
#include <cstring>

// Mochi-specific layout overlay for the Xingzhi 1.54 TFT.
// Keeps the Xiaozhi audio/network/app stack unchanged, but moves the pet
// to the lower part of the screen and renders chat text in a translucent bubble.
class MochiSpiLcdDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* mochi_chat_bubble_ = nullptr;

    void ApplyMochiLayout() {
        if (!Lock(1000)) {
            return;
        }

        lv_obj_t* screen = lv_screen_active();

        // Move the pet/emotion layer to the bottom of the 284x240 screen.
        if (emoji_box_ != nullptr) {
            lv_obj_set_parent(emoji_box_, screen);
            lv_obj_set_style_flex_grow(emoji_box_, 0, 0);
            lv_obj_align(emoji_box_, LV_ALIGN_BOTTOM_MID, 0, -4);
        }

        // Replace the plain chat text area with a soft white 75%-opacity bubble.
        if (chat_message_label_ != nullptr) {
            mochi_chat_bubble_ = lv_obj_create(screen);
            lv_obj_set_width(mochi_chat_bubble_, 242);
            lv_obj_set_height(mochi_chat_bubble_, LV_SIZE_CONTENT);
            lv_obj_set_style_radius(mochi_chat_bubble_, 10, 0);
            lv_obj_set_style_bg_color(mochi_chat_bubble_, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(mochi_chat_bubble_, static_cast<lv_opa_t>(191), 0); // 75%
            lv_obj_set_style_border_width(mochi_chat_bubble_, 0, 0);
            lv_obj_set_style_pad_left(mochi_chat_bubble_, 10, 0);
            lv_obj_set_style_pad_right(mochi_chat_bubble_, 10, 0);
            lv_obj_set_style_pad_top(mochi_chat_bubble_, 7, 0);
            lv_obj_set_style_pad_bottom(mochi_chat_bubble_, 7, 0);
            lv_obj_set_scrollbar_mode(mochi_chat_bubble_, LV_SCROLLBAR_MODE_OFF);
            lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, 0, 50);

            lv_obj_set_parent(chat_message_label_, mochi_chat_bubble_);
            lv_obj_set_width(chat_message_label_, 222);
            lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(chat_message_label_, lv_color_hex(0x26364D), 0);
            lv_obj_center(chat_message_label_);

            lv_obj_add_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
        }

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
        (void)role;
        if (chat_message_label_ == nullptr || mochi_chat_bubble_ == nullptr) {
            return;
        }
        if (!Lock(1000)) {
            return;
        }

        if (content == nullptr || content[0] == '\0') {
            lv_label_set_text(chat_message_label_, "");
            lv_obj_add_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(chat_message_label_, content);
            lv_obj_remove_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, 0, 50);
        }

        Unlock();
    }
};
