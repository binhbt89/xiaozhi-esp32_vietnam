#pragma once

#include "display/lcd_display.h"
#include <lvgl.h>
#include <cstring>

// Mochi UI layer for the real 1.83-inch ST7789 board.
// Keep the base LVGL parent/flex layout intact: re-parenting emoji_box_ was the
// source of the previous missing/cropped pet regression. Only translate it.
class Mochi183LcdDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* mochi_chat_bubble_ = nullptr;
    lv_obj_t* mochi_chat_label_ = nullptr;

    void ApplyMochiLayout() {
        if (!Lock(1000)) {
            return;
        }

        lv_obj_t* screen = lv_screen_active();

        // The stock empty chat label still participates in the vertical flex
        // layout. Hide it because replies use our overlay bubble instead.
        if (chat_message_label_ != nullptr) {
            lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        }

        // Keep GIF dimensions, decoder and parent untouched. With the old chat
        // label hidden, the pet is centered by the stock layout; +50 px puts an
        // 80-96 px Mochi safely near the bottom without clipping.
        if (emoji_box_ != nullptr) {
            lv_obj_set_style_translate_y(emoji_box_, 50, 0);
            lv_obj_move_foreground(emoji_box_);
        }

        // Independent translucent assistant-response bubble near the top.
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
    }

    void SetChatMessage(const char* role, const char* content) override {
        if (mochi_chat_bubble_ == nullptr || mochi_chat_label_ == nullptr) {
            return;
        }
        if (!Lock(1000)) {
            return;
        }

        // Only spoken assistant replies use this bubble. User/system/status text
        // stays out of the scene, preventing startup/status messages from
        // covering Mochi or the room background.
        const bool is_assistant = (role != nullptr && std::strcmp(role, "assistant") == 0);
        if (!is_assistant || content == nullptr || content[0] == '\0') {
            lv_label_set_text(mochi_chat_label_, "");
            lv_obj_add_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(mochi_chat_label_, content);
            lv_obj_remove_flag(mochi_chat_bubble_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(mochi_chat_bubble_, LV_ALIGN_TOP_MID, 0, 48);
            lv_obj_move_foreground(mochi_chat_bubble_);
        }

        Unlock();
    }
};
