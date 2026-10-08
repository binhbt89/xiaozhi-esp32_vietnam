from pathlib import Path
import base64
import runpy
import zlib

OUT = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_action_food_assets_15q.h')

# Start from the 15Q clean, uncropped artwork that already passed hardware review.
# 15R does NOT erase food at runtime. It bakes nine complete standalone frames
# and locks the plate/bowl/cup pixels to stage 0 so the vessel can never lose
# color or change shape between bites.
ns = runpy.run_path('scripts/generate_mochi_action_food_assets_15q.py')
DATA = ns['DATA']
W, H = 68, 54


def decode(key):
    rgba = bytearray(zlib.decompress(base64.b64decode(DATA[key])))
    assert len(rgba) == W * H * 4
    return rgba


def px(frame, x, y):
    p = (y * W + x) * 4
    return frame[p:p + 4]


def set_px(frame, x, y, value):
    p = (y * W + x) * 4
    frame[p:p + 4] = value


# These regions cover only the physical serving vessel / foreground rim. They
# intentionally avoid the edible center as much as possible. Stage 0 is the
# canonical vessel artwork. Every later stage copies these pixels byte-for-byte.
def is_vessel(cat, x, y):
    if cat == 'fish':
        return y >= 29 or (y >= 20 and (x <= 12 or x >= 55))
    if cat == 'meal':
        return y >= 30 or (y >= 24 and (x <= 10 or x >= 57))
    if cat == 'dessert':
        return y >= 31 or (y >= 26 and (x <= 17 or x >= 50))
    raise ValueError(cat)


frames = {}
for cat in ('fish', 'meal', 'dessert'):
    ref = decode(cat + '0')
    frames[cat + '0'] = ref
    for stage in (1, 2):
        cur = decode(cat + str(stage))
        for y in range(H):
            for x in range(W):
                if is_vessel(cat, x, y):
                    # Copy both visible and transparent pixels. This removes any
                    # stage-specific vessel fragment instead of merely painting
                    # over it, so the region is truly pixel-identical.
                    set_px(cur, x, y, px(ref, x, y))
        frames[cat + str(stage)] = cur


# Build-time regression check: the complete locked vessel region must be exactly
# identical across all three frames. This is the specific 15Q bug 15R fixes.
for cat in ('fish', 'meal', 'dessert'):
    ref = frames[cat + '0']
    for stage in (1, 2):
        cur = frames[cat + str(stage)]
        for y in range(H):
            for x in range(W):
                if is_vessel(cat, x, y):
                    assert px(cur, x, y) == px(ref, x, y), (cat, stage, x, y)


def emit(lines, name, rgba):
    visible = []
    colors = set()
    bgra = bytearray()
    for i in range(W * H):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        assert a in (0, 255), (name, 'semi-alpha', a)
        if a:
            x = i % W
            y = i // W
            visible.append((x, y))
            colors.add((r, g, b))
            # 15Q artwork is already snapped to the RGB565 grid.
            assert r == (((r >> 3) << 3) | (r >> 5)), (name, 'R', r)
            assert g == (((g >> 2) << 2) | (g >> 6)), (name, 'G', g)
            assert b == (((b >> 3) << 3) | (b >> 5)), (name, 'B', b)
        # LVGL ARGB8888 memory layout used by this project is BGRA.
        bgra.extend((b, g, r, a))

    assert visible, name
    xs = [p[0] for p in visible]
    ys = [p[1] for p in visible]
    assert min(xs) >= 2 and max(xs) <= 65, (name, min(xs), max(xs))
    assert min(ys) >= 2 and max(ys) <= 51, (name, min(ys), max(ys))

    arr = name + '_map'
    lines.append(f'// {name}: 68x54, {len(colors)} RGB565 colors, binary alpha')
    lines.append(f'alignas(4) static const uint8_t {arr}[] = {{')
    for i in range(0, len(bgra), 20):
        lines.append('    ' + ', '.join(f'0x{x:02X}' for x in bgra[i:i + 20]) + ',')
    lines += [
        '};',
        f'static const lv_image_dsc_t {name} = {{',
        '    {LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_ARGB8888, 0, 68, 54, 272, 0},',
        f'    sizeof({arr}), {arr}, nullptr, nullptr',
        '};',
        ''
    ]


lines = [
    '#pragma once', '', '#include <lvgl.h>', '#include <cstddef>', '',
    '// Mochi 15R FULL FOOD STAGES: 9 complete standalone 68x54 bitmaps.',
    '// Plate/bowl/cup pixels are locked across stages; only food content changes.',
    '// No runtime erase/mask/subtraction is used.',
    ''
]
for cat in ('fish', 'meal', 'dessert'):
    for stage in range(3):
        emit(lines, f'mochi_action_15r_{cat}{stage}', frames[cat + str(stage)])

lines += [
    'static const lv_image_dsc_t* const kMochiActionFoodStages15Q[3][3] = {',
    '    {&mochi_action_15r_fish0, &mochi_action_15r_fish1, &mochi_action_15r_fish2},',
    '    {&mochi_action_15r_meal0, &mochi_action_15r_meal1, &mochi_action_15r_meal2},',
    '    {&mochi_action_15r_dessert0, &mochi_action_15r_dessert1, &mochi_action_15r_dessert2},',
    '};', ''
]
OUT.write_text('\n'.join(lines), encoding='utf-8')
print('Generated Mochi 15R full-stage food sprites with locked vessels', OUT, OUT.stat().st_size)
