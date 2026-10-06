from pathlib import Path

BOARD = Path("main/boards/xingzhi-cube-1.83tft-wifi/xingzhi-cube-1.83tft-wifi.cc")
DISPLAY = Path("main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


# -----------------------------------------------------------------------------
# Talk long press: 2 seconds -> 1 second. Keep the existing PressDown/PressUp
# consumed-event design so releasing after long press cannot trigger voice.
# -----------------------------------------------------------------------------
board = BOARD.read_text(encoding="utf-8")
board = replace_once(
    board,
    "#define MOCHI_MENU_LONG_PRESS_MS 2000",
    "#define MOCHI_MENU_LONG_PRESS_MS 1000",
    "menu long-press constant",
)
board = board.replace(
    "Talk uses PressDown/PressUp plus the component's real 2-second long-press",
    "Talk uses PressDown/PressUp plus the component's real 1-second long-press",
)
BOARD.write_text(board, encoding="utf-8")


# -----------------------------------------------------------------------------
# HUD polish: preserve a single narrow vertical column. Each item remains
# icon-above/value-below. Icons are custom LVGL geometry (no icon font, no new
# asset partition), making their pixel footprint deterministic on the 284x240
# rounded screen.
# -----------------------------------------------------------------------------
display = DISPLAY.read_text(encoding="utf-8")
display = replace_once(display, "static constexpr int kHudWidthPx = 38;", "static constexpr int kHudWidthPx = 30;", "HUD width")
display = replace_once(display, "static constexpr int kHudRightMarginPx = 2;", "static constexpr int kHudRightMarginPx = 8;", "HUD right margin")
display = replace_once(display, "static constexpr int kHudTopGapPx = 2;", "static constexpr int kHudTopGapPx = 4;\n    static constexpr int kHudBottomSafePx = 16;", "HUD safe margins")

display = display.replace("// 15D HUD:", "// 15E HUD:")

display_start = display.find("    void CreateHudLvgl(lv_obj_t* screen) {")
display_end = display.find("    void RefreshHudLvgl(bool force = false) {", display_start)
if display_start < 0 or display_end < 0:
    raise SystemExit("HUD function anchors not found")

new_hud = r'''    lv_obj_t* MakeHudShapeLvgl(lv_obj_t* parent, int x, int y, int w, int h,
                                    lv_color_t color, int radius = 0) {
        lv_obj_t* shape = lv_obj_create(parent);
        lv_obj_remove_style_all(shape);
        lv_obj_set_size(shape, w, h);
        lv_obj_set_pos(shape, x, y);
        lv_obj_set_style_bg_color(shape, color, 0);
        lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(shape, radius, 0);
        lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(shape, LV_OBJ_FLAG_CLICKABLE);
        return shape;
    }

    void CreateHudIconLvgl(lv_obj_t* row, size_t index) {
        // 14x13 hand-drawn micro icons. These are LVGL geometry, not glyphs,
        // so icon size/alignment is independent of the text font metrics.
        lv_obj_t* root = lv_obj_create(row);
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, 14, 13);
        lv_obj_align(root, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);

        const lv_color_t dark = lv_color_hex(0x3B4652);
        switch (index) {
            case 0: { // Fullness: warm food bowl + two steam strokes
                const lv_color_t bowl = lv_color_hex(0xF5A65B);
                const lv_color_t steam = lv_color_hex(0xFFE0A3);
                MakeHudShapeLvgl(root, 3, 0, 2, 3, steam, 1);
                MakeHudShapeLvgl(root, 8, 1, 2, 3, steam, 1);
                MakeHudShapeLvgl(root, 1, 5, 12, 2, bowl, 1);
                MakeHudShapeLvgl(root, 3, 7, 8, 5, bowl, 4);
                break;
            }
            case 1: { // Energy: compact blocky lightning bolt
                const lv_color_t bolt = lv_color_hex(0xFFD84D);
                MakeHudShapeLvgl(root, 8, 0, 4, 3, bolt, 1);
                MakeHudShapeLvgl(root, 5, 3, 6, 3, bolt, 1);
                MakeHudShapeLvgl(root, 3, 6, 6, 3, bolt, 1);
                MakeHudShapeLvgl(root, 2, 9, 4, 3, bolt, 1);
                break;
            }
            case 2: { // Mood: small smiling face
                const lv_color_t face = lv_color_hex(0x7ED6DF);
                lv_obj_t* circle = MakeHudShapeLvgl(root, 1, 0, 12, 12, face, 6);
                lv_obj_set_style_border_width(circle, 1, 0);
                lv_obj_set_style_border_color(circle, lv_color_hex(0xDDF8FA), 0);
                MakeHudShapeLvgl(root, 4, 4, 2, 2, dark, 1);
                MakeHudShapeLvgl(root, 8, 4, 2, 2, dark, 1);
                MakeHudShapeLvgl(root, 4, 8, 6, 2, dark, 1);
                break;
            }
            case 3: { // Friendship: pixel-heart silhouette
                const lv_color_t heart = lv_color_hex(0xFF7895);
                MakeHudShapeLvgl(root, 2, 1, 5, 5, heart, 3);
                MakeHudShapeLvgl(root, 7, 1, 5, 5, heart, 3);
                MakeHudShapeLvgl(root, 2, 4, 10, 4, heart, 1);
                MakeHudShapeLvgl(root, 4, 8, 6, 3, heart, 1);
                MakeHudShapeLvgl(root, 6, 11, 2, 2, heart, 1);
                break;
            }
            case 4: { // Coin: gold coin with a darker center mark
                const lv_color_t gold = lv_color_hex(0xF7C948);
                lv_obj_t* coin = MakeHudShapeLvgl(root, 1, 0, 12, 12, gold, 6);
                lv_obj_set_style_border_width(coin, 1, 0);
                lv_obj_set_style_border_color(coin, lv_color_hex(0xFFE8A1), 0);
                MakeHudShapeLvgl(root, 6, 3, 2, 6, lv_color_hex(0xB87B1B), 1);
                break;
            }
            default:
                break;
        }
    }

    void CreateHudLvgl(lv_obj_t* screen) {
        const int status_h = StatusBarHeightLvgl();
        const int available_h = static_cast<int>(LV_VER_RES) - status_h -
                                kHudTopGapPx - kHudBottomSafePx;
        const int panel_h = std::min(180, std::max(160, available_h));

        hud_panel_ = lv_obj_create(screen);
        lv_obj_set_size(hud_panel_, kHudWidthPx, panel_h);
        lv_obj_set_pos(hud_panel_, LV_HOR_RES - kHudWidthPx - kHudRightMarginPx,
                       status_h + kHudTopGapPx);
        lv_obj_set_style_radius(hud_panel_, 8, 0);
        lv_obj_set_style_bg_color(hud_panel_, lv_color_hex(0x17202A), 0);
        lv_obj_set_style_bg_opa(hud_panel_, static_cast<lv_opa_t>(88), 0);
        lv_obj_set_style_border_width(hud_panel_, 0, 0);
        lv_obj_set_style_pad_all(hud_panel_, 1, 0);
        lv_obj_set_style_pad_row(hud_panel_, 0, 0);
        lv_obj_set_flex_flow(hud_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(hud_panel_, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(hud_panel_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(hud_panel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(hud_panel_, LV_OBJ_FLAG_CLICKABLE);

        for (size_t i = 0; i < kHudRowCount; ++i) {
            lv_obj_t* row = lv_obj_create(hud_panel_);
            lv_obj_set_size(row, 27, 30);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

            CreateHudIconLvgl(row, i);

            hud_value_labels_[i] = lv_label_create(row);
            lv_label_set_text(hud_value_labels_[i], i == 4 ? "000" : "00");
            lv_obj_set_width(hud_value_labels_[i], 27);
            lv_obj_set_style_text_align(hud_value_labels_[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_letter_space(hud_value_labels_[i], -1, 0);
            lv_obj_set_style_text_color(hud_value_labels_[i], lv_color_white(), 0);
            lv_obj_align(hud_value_labels_[i], LV_ALIGN_BOTTOM_MID, 0, 1);
        }

        lv_obj_move_foreground(hud_panel_);
    }

'''

display = display[:display_start] + new_hud + display[display_end:]
DISPLAY.write_text(display, encoding="utf-8")

print("Applied Mochi 15E HUD polish: 1s Talk hold, safe narrow HUD, custom LVGL icons")
