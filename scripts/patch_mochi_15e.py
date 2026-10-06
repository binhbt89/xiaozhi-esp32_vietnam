from pathlib import Path

BOARD = Path("main/boards/xingzhi-cube-1.83tft-wifi/xingzhi-cube-1.83tft-wifi.cc")
DISPLAY = Path("main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h")
PET = Path("main/pet/pet_state_engine.h")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


# -----------------------------------------------------------------------------
# Talk long press stays at the hardware-tested 1 second from 15E.
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
# 15F Play semantics: an explicit Play/game action is allowed to wake Mochi.
# The 15E/15D guard silently rejected Play while the persistent sleep flag was
# still set, which matched the hardware symptom: Feed/Wake cleared sleep first,
# then Play suddenly worked. Energy remains the only admission requirement.
# -----------------------------------------------------------------------------
pet = PET.read_text(encoding="utf-8")
pet = replace_once(
    pet,
    """        if (state_.sleeping || energy_cost == 0 || state_.energy < energy_cost) {
            return false;
        }

        state_.energy = SubClamp(state_.energy, energy_cost);""",
    """        if (energy_cost == 0 || state_.energy < energy_cost) {
            return false;
        }

        // Explicit Play wakes Mochi. This prevents a stale/persistent logical
        // sleep flag from making the menu action silently do nothing.
        state_.sleeping = false;
        state_.manual_sleep = false;
        state_.energy = SubClamp(state_.energy, energy_cost);""",
    "Play sleeping guard",
)
pet = replace_once(
    pet,
    "return !state_.sleeping && energy_cost > 0 && state_.energy >= energy_cost;",
    "return energy_cost > 0 && state_.energy >= energy_cost;",
    "CanPlay sleeping guard",
)
pet = replace_once(
    pet,
    """        if (state_.sleeping || state_.energy < energy_cost) {
            return;
        }
        if (coin_reward > 0) {""",
    """        if (state_.energy < energy_cost) {
            return;
        }
        state_.sleeping = false;
        state_.manual_sleep = false;
        if (coin_reward > 0) {""",
    "CompletePlay sleeping guard",
)
PET.write_text(pet, encoding="utf-8")


# -----------------------------------------------------------------------------
# HUD 15F: keep ONE narrow right-side column with icon above value. Numbers use
# the small built-in Montserrat 14 font and sit lower, leaving a real gap below
# the icon. Icons are redrawn as clean outlined pictograms using LVGL geometry,
# not Font Awesome and not text glyphs.
# -----------------------------------------------------------------------------
display = DISPLAY.read_text(encoding="utf-8")
display = replace_once(display, "static constexpr int kHudWidthPx = 38;", "static constexpr int kHudWidthPx = 30;", "HUD width")
display = replace_once(display, "static constexpr int kHudRightMarginPx = 2;", "static constexpr int kHudRightMarginPx = 9;", "HUD right margin")
display = replace_once(display, "static constexpr int kHudTopGapPx = 2;", "static constexpr int kHudTopGapPx = 5;\n    static constexpr int kHudBottomSafePx = 22;", "HUD safe margins")
display = display.replace("// 15D HUD:", "// 15F HUD:")

display_start = display.find("    void CreateHudLvgl(lv_obj_t* screen) {")
display_end = display.find("    void RefreshHudLvgl(bool force = false) {", display_start)
if display_start < 0 or display_end < 0:
    raise SystemExit("HUD function anchors not found")

new_hud = r'''    lv_obj_t* MakeHudShapeLvgl(lv_obj_t* parent, int x, int y, int w, int h,
                                    lv_color_t color, int radius = 0,
                                    bool filled = true) {
        lv_obj_t* shape = lv_obj_create(parent);
        lv_obj_remove_style_all(shape);
        lv_obj_set_size(shape, w, h);
        lv_obj_set_pos(shape, x, y);
        lv_obj_set_style_radius(shape, radius, 0);
        if (filled) {
            lv_obj_set_style_bg_color(shape, color, 0);
            lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(shape, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(shape, 1, 0);
            lv_obj_set_style_border_color(shape, color, 0);
            lv_obj_set_style_border_opa(shape, LV_OPA_COVER, 0);
        }
        lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(shape, LV_OBJ_FLAG_CLICKABLE);
        return shape;
    }

    lv_obj_t* MakeHudLineLvgl(lv_obj_t* parent, const lv_point_precise_t* points,
                              uint32_t count, lv_color_t color, int width = 2) {
        lv_obj_t* line = lv_line_create(parent);
        lv_line_set_points(line, points, count);
        lv_obj_set_style_line_color(line, color, 0);
        lv_obj_set_style_line_width(line, width, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
        return line;
    }

    void CreateHudIconLvgl(lv_obj_t* row, size_t index) {
        // 16x12 hand-drawn pictograms. Clean outlines survive this tiny display
        // better than the filled micro-blocks used in 15E.
        lv_obj_t* root = lv_obj_create(row);
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, 16, 12);
        lv_obj_align(root, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);

        switch (index) {
            case 0: { // Fullness: bowl with food/steam
                const lv_color_t c = lv_color_hex(0xFFB35C);
                MakeHudShapeLvgl(root, 2, 5, 12, 6, c, 4, false);
                MakeHudShapeLvgl(root, 5, 4, 2, 2, lv_color_hex(0xFFD18A), 1, true);
                MakeHudShapeLvgl(root, 9, 4, 2, 2, lv_color_hex(0xFFD18A), 1, true);
                MakeHudShapeLvgl(root, 5, 0, 1, 3, lv_color_hex(0xFFF0BF), 1, true);
                MakeHudShapeLvgl(root, 10, 1, 1, 2, lv_color_hex(0xFFF0BF), 1, true);
                break;
            }
            case 1: { // Energy: recognizable zig-zag lightning
                static const lv_point_precise_t pts[] = {
                    {10, 0}, {5, 5}, {8, 5}, {4, 11}, {12, 4}, {9, 4}
                };
                MakeHudLineLvgl(root, pts, 6, lv_color_hex(0xFFD94A), 2);
                break;
            }
            case 2: { // Mood: outlined smiley
                const lv_color_t c = lv_color_hex(0x72D8E8);
                MakeHudShapeLvgl(root, 2, 0, 12, 12, c, 6, false);
                MakeHudShapeLvgl(root, 5, 4, 2, 2, c, 1, true);
                MakeHudShapeLvgl(root, 10, 4, 2, 2, c, 1, true);
                static const lv_point_precise_t smile[] = {{5, 8}, {8, 10}, {11, 8}};
                MakeHudLineLvgl(root, smile, 3, c, 1);
                break;
            }
            case 3: { // Friendship: cleaner symmetric heart
                const lv_color_t c = lv_color_hex(0xFF708C);
                MakeHudShapeLvgl(root, 2, 1, 6, 6, c, 3, true);
                MakeHudShapeLvgl(root, 8, 1, 6, 6, c, 3, true);
                MakeHudShapeLvgl(root, 3, 4, 10, 4, c, 1, true);
                MakeHudShapeLvgl(root, 5, 8, 6, 2, c, 1, true);
                MakeHudShapeLvgl(root, 7, 10, 2, 2, c, 1, true);
                break;
            }
            case 4: { // Coin: ring + small center shine
                const lv_color_t c = lv_color_hex(0xF6C94D);
                MakeHudShapeLvgl(root, 2, 0, 12, 12, c, 6, false);
                MakeHudShapeLvgl(root, 5, 3, 6, 6, lv_color_hex(0xD89B24), 3, false);
                MakeHudShapeLvgl(root, 7, 2, 2, 3, lv_color_hex(0xFFF0A8), 1, true);
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
        const int panel_h = std::min(168, std::max(156, available_h));

        hud_panel_ = lv_obj_create(screen);
        lv_obj_set_size(hud_panel_, kHudWidthPx, panel_h);
        lv_obj_set_pos(hud_panel_, LV_HOR_RES - kHudWidthPx - kHudRightMarginPx,
                       status_h + kHudTopGapPx);
        lv_obj_set_style_radius(hud_panel_, 8, 0);
        lv_obj_set_style_bg_color(hud_panel_, lv_color_hex(0x17202A), 0);
        lv_obj_set_style_bg_opa(hud_panel_, static_cast<lv_opa_t>(72), 0);
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
            lv_obj_set_size(row, 27, 29);
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
            lv_obj_set_style_text_font(hud_value_labels_[i], &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(hud_value_labels_[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_letter_space(hud_value_labels_[i], -1, 0);
            lv_obj_set_style_text_color(hud_value_labels_[i], lv_color_white(), 0);
            // Bottom alignment creates a visible 2-3px gap below the 12px icon.
            lv_obj_align(hud_value_labels_[i], LV_ALIGN_BOTTOM_MID, 0, 1);
        }

        lv_obj_move_foreground(hud_panel_);
    }

'''

display = display[:display_start] + new_hud + display[display_end:]
DISPLAY.write_text(display, encoding="utf-8")

print("Applied Mochi 15F: cleaner compact HUD, smaller/lower values, 1s Talk hold, Play wake fix")
