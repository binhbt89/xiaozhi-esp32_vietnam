from pathlib import Path

BOARD = Path("main/boards/xingzhi-cube-1.83tft-wifi/xingzhi-cube-1.83tft-wifi.cc")
src = BOARD.read_text(encoding="utf-8")

include_anchor = "#include <esp_sleep.h>\n"
include_block = "#include <esp_sleep.h>\n#include <esp_timer.h>\n#include <ctime>\n#include <cstring>\n"
if include_anchor not in src:
    raise SystemExit("Mochi POC1: include anchor not found")
src = src.replace(include_anchor, include_block, 1)

class_anchor = '#define TAG "XINGZHI_CUBE_1_54TFT_WIFI"\n'
mochi_class = r'''

// Mochi POC1 presentation layer. The Xiaozhi networking/audio/MCP/music
// pipeline remains unchanged; this class only changes the LCD presentation.
class MochiPetDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* world_tint_ = nullptr;
    esp_timer_handle_t world_timer_ = nullptr;

    static void WorldTimerCallback(void* arg) {
        static_cast<MochiPetDisplay*>(arg)->ApplyWorldTime();
    }

    void ApplyStateEmotion() {
        const auto state = Application::GetInstance().GetDeviceState();
        const char* mapped = nullptr;
        switch (state) {
            case kDeviceStateConnecting:
                mapped = "thinking";
                break;
            case kDeviceStateListening:
                mapped = "surprised"; // temporary known-good listening animation
                break;
            case kDeviceStateSpeaking:
                mapped = "happy";     // temporary known-good talking animation
                break;
            case kDeviceStateIdle:
                mapped = "neutral";
                break;
            default:
                break;
        }
        if (mapped != nullptr) {
            LcdDisplay::SetEmotion(mapped);
        }
    }

    void StyleMochiUi() {
        if (!Lock(500)) {
            return;
        }

        // Keep Mochi slightly low so the room/window remains visible.
        if (emoji_box_ != nullptr) {
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_FLOATING);
            lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 10);
        }

        // Compact assistant subtitle card at the bottom instead of text over
        // Mochi. User ASR text is suppressed in SetChatMessage().
        if (chat_message_label_ != nullptr) {
            lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_FLOATING);
            lv_obj_set_size(chat_message_label_, 264, 44);
            lv_obj_align(chat_message_label_, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_bg_color(chat_message_label_, lv_color_hex(0xFFF4E8), 0);
            lv_obj_set_style_bg_opa(chat_message_label_, 210, 0);
            lv_obj_set_style_radius(chat_message_label_, 8, 0);
            lv_obj_set_style_border_width(chat_message_label_, 0, 0);
            lv_obj_set_style_pad_left(chat_message_label_, 6, 0);
            lv_obj_set_style_pad_right(chat_message_label_, 6, 0);
            lv_obj_set_style_pad_top(chat_message_label_, 3, 0);
            lv_obj_set_style_pad_bottom(chat_message_label_, 3, 0);
            lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        }

        // Time-of-day tint: this sits above the room background but below the
        // normal UI/pet children. It proves realtime world changes before we
        // add separate sky/window assets.
        if (container_ != nullptr) {
            world_tint_ = lv_obj_create(container_);
            lv_obj_remove_style_all(world_tint_);
            lv_obj_set_size(world_tint_, LV_PCT(100), LV_PCT(100));
            lv_obj_align(world_tint_, LV_ALIGN_CENTER, 0, 0);
            lv_obj_add_flag(world_tint_, LV_OBJ_FLAG_FLOATING);
            lv_obj_clear_flag(world_tint_, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_border_width(world_tint_, 0, 0);
            lv_obj_move_to_index(world_tint_, 0);
        }

        Unlock();
    }

    void ApplyWorldTime() {
        time_t now = time(nullptr);
        struct tm local_tm {};
        localtime_r(&now, &local_tm);

        uint32_t color = 0xFFFFFF;
        lv_opa_t opa = LV_OPA_TRANSP;

        // Stay in normal daytime mode until SNTP has supplied a sane clock.
        if (local_tm.tm_year + 1900 >= 2024) {
            const int hour = local_tm.tm_hour;
            if (hour >= 5 && hour < 8) {
                color = 0xFFE8B8; // morning warmth
                opa = 24;
            } else if (hour >= 8 && hour < 17) {
                color = 0xFFFFFF; // day
                opa = LV_OPA_TRANSP;
            } else if (hour >= 17 && hour < 19) {
                color = 0xF5A36C; // sunset
                opa = 38;
            } else {
                color = 0x20345D; // night
                opa = 72;
            }
        }

        if (!Lock(100)) {
            return;
        }
        if (world_tint_ != nullptr) {
            lv_obj_set_style_bg_color(world_tint_, lv_color_hex(color), 0);
            lv_obj_set_style_bg_opa(world_tint_, opa, 0);
        }
        Unlock();
    }

public:
    MochiPetDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                    int width, int height, int offset_x, int offset_y,
                    bool mirror_x, bool mirror_y, bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                        mirror_x, mirror_y, swap_xy) {
        StyleMochiUi();
        ApplyWorldTime();

        esp_timer_create_args_t args = {};
        args.callback = WorldTimerCallback;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "mochi_world";
        args.skip_unhandled_events = true;
        if (esp_timer_create(&args, &world_timer_) == ESP_OK) {
            esp_timer_start_periodic(world_timer_, 60LL * 1000LL * 1000LL);
        }
    }

    ~MochiPetDisplay() override {
        if (world_timer_ != nullptr) {
            esp_timer_stop(world_timer_);
            esp_timer_delete(world_timer_);
        }
    }

    void SetStatus(const char* status) override {
        LcdDisplay::SetStatus(status);
        ApplyStateEmotion();
    }

    void SetEmotion(const char* emotion) override {
        const auto state = Application::GetInstance().GetDeviceState();
        if (state == kDeviceStateConnecting ||
            state == kDeviceStateListening ||
            state == kDeviceStateSpeaking) {
            ApplyStateEmotion();
            return;
        }
        LcdDisplay::SetEmotion(emotion);
    }

    void SetChatMessage(const char* role, const char* content) override {
        // Do not paint recognized user speech across Mochi/the room.
        if (role != nullptr && std::strcmp(role, "user") == 0) {
            return;
        }

        LcdDisplay::SetChatMessage(role, content);

        if (!Lock(100)) {
            return;
        }
        if (chat_message_label_ != nullptr) {
            if (content == nullptr || content[0] == '\0') {
                lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        Unlock();
    }
};
'''
if class_anchor not in src:
    raise SystemExit("Mochi POC1: class anchor not found")
src = src.replace(class_anchor, class_anchor + mochi_class, 1)

pointer_old = "    SpiLcdDisplay* display_;"
pointer_new = "    MochiPetDisplay* display_;"
if pointer_old not in src:
    raise SystemExit("Mochi POC1: display pointer anchor not found")
src = src.replace(pointer_old, pointer_new, 1)

ctor_old = """        display_ = new SpiLcdDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, \n            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);"""
ctor_new = """        display_ = new MochiPetDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,\n            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);"""
if ctor_old not in src:
    raise SystemExit("Mochi POC1: display constructor anchor not found")
src = src.replace(ctor_old, ctor_new, 1)

BOARD.write_text(src, encoding="utf-8")
print("Mochi POC1 source overlay applied successfully")
