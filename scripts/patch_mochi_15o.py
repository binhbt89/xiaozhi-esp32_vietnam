from pathlib import Path

P = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h')
s = P.read_text(encoding='utf-8')


def one(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f'{label}: expected 1 match, got {n}')
    s = s.replace(old, new, 1)


# 15O exact geometry model
# ------------------------
# Panel: 284x240 -> screen center (142,120)
# Mochi object: 80x80, center at (142 + reaction_base_x_, 170)
# HUD safe edge: x ~= 250
# Food canvas: fixed 64x48, center y=184 -> bounds y=160..208
# Food x offset 68 -> canvas overlaps the 80px Mochi bounding box by 4px,
# but the actual food art has internal transparent padding. This leaves a
# visible 3-13px mouth gap before the 7px bite nudge and prevents edge clipping.
# Right-side food is safe while reaction_base_x_ <= 8; beyond that we place it
# on the left. For the full walk range (-50..50), food canvas bounds stay in
# x=28..250 and never enter the 30px HUD rail.
one('reaction_side_ = (mochi_x_ >= 20) ? -1 : 1;',
    'reaction_side_ = (mochi_x_ > 8) ? -1 : 1;',
    'food safe-side threshold')
one('const int food_x = reaction_base_x_ + reaction_side_ * 58;',
    'const int food_x = reaction_base_x_ + reaction_side_ * 68;',
    'fixed food x anchor')
one('food_x, 58);',
    'food_x, 64);',
    'fixed food ground baseline')

# Every 15O food frame is exactly 64x48. Reset image scale every time the source
# changes so a previous heart/sparkle transform can never leak into another
# reaction image and make it appear shifted, enlarged or clipped.
one('''        if (obj == nullptr || src == nullptr) return;
        lv_image_set_src(obj, src);
        lv_obj_align(obj, LV_ALIGN_CENTER, x, y);''',
    '''        if (obj == nullptr || src == nullptr) return;
        lv_image_set_src(obj, src);
        lv_image_set_scale(obj, LV_SCALE_NONE);
        lv_obj_align(obj, LV_ALIGN_CENTER, x, y);''',
    'reaction image transform reset')

# 15O hand is 52x28 with a crisp binary-alpha silhouette. Re-center the sweep so
# the fingers cross the forehead/crown instead of floating over it.
one('reaction_base_x_ - 18, 16);',
    'reaction_base_x_ - 14, 14);',
    'pet hand start')
one('const int hand_x = reaction_base_x_ - 18 + phase * 8;',
    'const int hand_x = reaction_base_x_ - 14 + phase * 7;',
    'pet hand sweep x')
one('const int hand_y = (phase == 2) ? 18 : 15;',
    'const int hand_y = (phase == 2) ? 17 : 14;',
    'pet hand sweep y')

# Heart is now a native 38x38 bitmap. Remove runtime 1.25x scaling (which caused
# interpolation and could persist on the reused LVGL image object) and keep it
# close to the head at y=6.
one('''    void ShowHappyHeartLvgl(int x = 0, int y = 10) {
        SetReactionImageLvgl(reaction_aux_, kMochiActionHeart, x, y);
        if (reaction_aux_ != nullptr) lv_image_set_scale(reaction_aux_, 320);
    }''',
    '''    void ShowHappyHeartLvgl(int x = 0, int y = 6) {
        SetReactionImageLvgl(reaction_aux_, kMochiActionHeart, x, y);
    }''',
    'native heart size')

# All four explicit heart calls use the same native anchor.
n = s.count('ShowHappyHeartLvgl(reaction_base_x_, 10);')
if n != 4:
    raise SystemExit(f'heart callers: expected 4 matches, got {n}')
s = s.replace('ShowHappyHeartLvgl(reaction_base_x_, 10);',
              'ShowHappyHeartLvgl(reaction_base_x_, 6);')

# Feed/Pet/Play/Wake pulse animation must stay centered on the same baseline.
n = s.count('reaction_base_x_, 10 - pulse * 2);')
if n != 4:
    raise SystemExit(f'heart pulse anchors: expected 4 matches, got {n}')
s = s.replace('reaction_base_x_, 10 - pulse * 2);',
              'reaction_base_x_, 6 - pulse * 2);')

P.write_text(s, encoding='utf-8')
print('Applied Mochi 15O: fixed food anchor, stable frame transforms, RGB565-native heart/hand placement')
