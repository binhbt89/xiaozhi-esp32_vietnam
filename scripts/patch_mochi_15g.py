from pathlib import Path

DISPLAY = Path("main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


display = DISPLAY.read_text(encoding="utf-8")

# 15F is applied first. 15G only changes HUD presentation: restore the HUD close
# to the right edge and swap the LVGL geometry pictograms for true bitmap assets.
display = replace_once(
    display,
    '#include "pet/pet_state_engine.h"',
    '#include "pet/pet_state_engine.h"\n#include "mochi_hud_icon_assets.h"',
    "HUD bitmap include",
)
display = replace_once(
    display,
    "static constexpr int kHudRightMarginPx = 9;",
    "static constexpr int kHudRightMarginPx = 2;",
    "HUD right margin",
)
display = display.replace("// 15F HUD:", "// 15G HUD:")

start = display.find("    lv_obj_t* MakeHudShapeLvgl(")
end = display.find("    void RefreshHudLvgl(bool force = false) {", start)
if start < 0 or end < 0:
    raise SystemExit("15F HUD helper anchors not found")

new_hud = r'''    void CreateHudIconLvgl(lv_obj_t* row, size_t index) {
        if (index >= kMochiHudIconAssetCount) return;

        // 15G uses immutable 16x16 ARGB bitmap assets. The art is deliberately
        // pixel-based and palette-matched to Mochi/backgrounds, not a font glyph
        // and not a collection of runtime LVGL geometry primitives.
        lv_obj_t* icon = lv_image_create(row);
        lv_image_set_src(icon, kMochiHudIconAssets[index]);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
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
        lv_obj_set_style_bg_opa(hud_panel_, static_cast<lv_opa_t>(64), 0);
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
            lv_obj_set_size(row, 27, 31);
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
            // 16px icon + compact 14px value, with the value anchored low so
            // the two elements read as separate stacked units on real hardware.
            lv_obj_align(hud_value_labels_[i], LV_ALIGN_BOTTOM_MID, 0, 1);
        }

        lv_obj_move_foreground(hud_panel_);
    }

'''

display = display[:start] + new_hud + display[end:]
DISPLAY.write_text(display, encoding="utf-8")
print("Applied Mochi 15G: close-right HUD with pastel bitmap image assets")
