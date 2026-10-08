from pathlib import Path

P=Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h')
s=P.read_text(encoding='utf-8')

def one(old,new,label):
    global s
    n=s.count(old)
    if n!=1:
        raise SystemExit(f'{label}: expected 1 match, got {n}')
    s=s.replace(old,new,1)

# 15N placement pass.
# Geometry reference: Mochi is the original 80x80 sprite with its object center
# at screen-center + (reaction_base_x_, 50). Food is kept on nearly the same
# baseline as Mochi and far enough sideways that the sprites do not overlap.
# With the 284x240 panel, current walk range (-50..50) and right-side HUD, the
# resulting food bounds remain inside the scene safe area for all three foods.
one('const int food_x = reaction_base_x_ + reaction_side_ * 45;',
    'const int food_x = reaction_base_x_ + reaction_side_ * 58;',
    'food side placement')
one('food_x, 42);',
    'food_x, 58);',
    'food baseline placement')

# The 56x30 hand should physically overlap the visible top of the 80x80 Mochi
# sprite. Sweep it over the forehead/crown instead of floating above the cat.
one('reaction_base_x_ - 24, 0);',
    'reaction_base_x_ - 18, 16);',
    'pet start hand placement')
one('const int hand_x = reaction_base_x_ - 24 + phase * 12;',
    'const int hand_x = reaction_base_x_ - 18 + phase * 8;',
    'pet hand sweep x')
one('const int hand_y = (phase == 2) ? 3 : 0;',
    'const int hand_y = (phase == 2) ? 18 : 15;',
    'pet hand sweep y')

# Heart is intentionally larger and closer to Mochi's head. LVGL scale 320 is
# 1.25x (256 == 1.0x), so the 30x30 source renders at about 38x38 without adding
# another bitmap to flash.
one('''    void ShowHappyHeartLvgl(int x = 0, int y = -4) {
        SetReactionImageLvgl(reaction_aux_, kMochiActionHeart, x, y);
    }''',
    '''    void ShowHappyHeartLvgl(int x = 0, int y = 10) {
        SetReactionImageLvgl(reaction_aux_, kMochiActionHeart, x, y);
        if (reaction_aux_ != nullptr) lv_image_set_scale(reaction_aux_, 320);
    }''',
    'heart size and default placement')
one('ShowHappyHeartLvgl(reaction_base_x_, -3);',
    'ShowHappyHeartLvgl(reaction_base_x_, 10);',
    'feed heart placement')
# There are three -5 callers: Pet, Play and Wake. Move all three together.
n=s.count('ShowHappyHeartLvgl(reaction_base_x_, -5);')
if n!=3:
    raise SystemExit(f'heart action callers: expected 3 matches, got {n}')
s=s.replace('ShowHappyHeartLvgl(reaction_base_x_, -5);',
            'ShowHappyHeartLvgl(reaction_base_x_, 10);')

# Heart pulse positions must use the same close-to-head baseline instead of
# jumping back to the old negative-Y coordinates after the first frame.
n=s.count('reaction_base_x_, -5 - pulse * 2);')
if n!=3:
    raise SystemExit(f'pet/play/wake heart pulse: expected 3 matches, got {n}')
s=s.replace('reaction_base_x_, -5 - pulse * 2);',
            'reaction_base_x_, 10 - pulse * 2);')
one('reaction_base_x_, -3 - pulse * 2);',
    'reaction_base_x_, 10 - pulse * 2);',
    'feed heart pulse')

# A sparkle on every bite looked like an unrelated impact effect and could also
# crowd the food edge. Eating now reads cleanly as: bite motion -> food shrinks.
one('        const int mouth_fx_x = reaction_base_x_ + reaction_side_ * 25;\n',
    '',
    'remove bite sparkle coordinate')
one('''            SetReactionImageLvgl(reaction_aux_, kMochiActionSparkle,
                                 mouth_fx_x, 20);
''',
    '',
    'remove bite sparkle')

P.write_text(s,encoding='utf-8')
print('Applied Mochi 15N: safe food placement, contact petting, larger close heart, clean bite animation')
