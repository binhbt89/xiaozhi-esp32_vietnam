from pathlib import Path

P = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_lcd_display.h')
s = P.read_text(encoding='utf-8')


def one(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f'{label}: expected one match, got {n}')
    s = s.replace(old, new, 1)


# Keep every 15P layout/interaction fix. Only substitute the food source table
# with 15Q's freshly reconstructed, uncropped fixed-canvas artwork.
one('#include "mochi_action_assets_15l.h"',
    '#include "mochi_action_assets_15l.h"\n#include "mochi_action_food_assets_15q.h"',
    '15Q food header include')
one('kMochiActionFoodStages[reaction_food_index_][stage]',
    'kMochiActionFoodStages15Q[reaction_food_index_][stage]',
    '15Q food stage table')

P.write_text(s, encoding='utf-8')
print('Applied Mochi 15Q: clean uncropped food sprites on locked 15P geometry')
