from pathlib import Path

P=Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h')
s=P.read_text(encoding='utf-8')

def one(old,new,label):
    global s
    n=s.count(old)
    if n!=1:
        raise SystemExit(f'{label}: expected 1 match, got {n}')
    s=s.replace(old,new,1)

# 15M sprites are slightly wider and more detailed than the 15L primitives.
# Keep the food near Mochi's muzzle while avoiding HUD overlap.
one('const int food_x = reaction_base_x_ + reaction_side_ * 45;',
    'const int food_x = reaction_base_x_ + reaction_side_ * 43;',
    'food placement')

# The new 56x30 hand has a wrist/cuff and needs a smoother three-stroke sweep.
one('reaction_base_x_ - 24, 0);',
    'reaction_base_x_ - 30, 2);',
    'pet start hand placement')
one('const int hand_x = reaction_base_x_ - 24 + phase * 12;',
    'const int hand_x = reaction_base_x_ - 30 + phase * 10;',
    'pet hand sweep x')
one('const int hand_y = (phase == 2) ? 3 : 0;',
    'const int hand_y = (phase == 2) ? 5 : 2;',
    'pet hand sweep y')

P.write_text(s,encoding='utf-8')
print('Applied Mochi 15M art placement polish')
