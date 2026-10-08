from pathlib import Path
import base64
import runpy
import zlib

# Reuse the compact 15Q RGBA payload without duplicating ~15 KB of encoded art.
# run_path writes an intermediate header; this file immediately replaces it with
# the validated BGRA byte order used by the existing LVGL ARGB8888 assets.
ns = runpy.run_path('scripts/generate_mochi_action_food_assets_15q.py')
DATA = ns['DATA']
OUT = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_action_food_assets_15q.h')


def emit(lines, name, blob):
    rgba = zlib.decompress(base64.b64decode(blob))
    assert len(rgba) == 68 * 54 * 4

    visible = []
    colors = set()
    for px in range(68 * 54):
        r, g, b, a = rgba[px * 4:px * 4 + 4]
        assert a in (0, 255), (name, 'semi-alpha', a)
        if a:
            x = px % 68
            y = px // 68
            visible.append((x, y))
            colors.add((r, g, b))
            # RGB values were snapped to the real RGB565 grid before embedding.
            assert r == (((r >> 3) << 3) | (r >> 5)), (name, 'R', r)
            assert g == (((g >> 2) << 2) | (g >> 6)), (name, 'G', g)
            assert b == (((b >> 3) << 3) | (b >> 5)), (name, 'B', b)

    assert visible, name
    xs = [p[0] for p in visible]
    ys = [p[1] for p in visible]
    # Every finished sprite has at least 2 transparent pixels around all edges.
    assert min(xs) >= 2 and max(xs) <= 65, (name, min(xs), max(xs))
    assert min(ys) >= 2 and max(ys) <= 51, (name, min(ys), max(ys))
    assert len(colors) <= 17, (name, len(colors))

    # LVGL's ARGB8888 byte layout used by the Mochi project is BGRA in memory.
    bgra = bytearray()
    for px in range(68 * 54):
        r, g, b, a = rgba[px * 4:px * 4 + 4]
        bgra.extend((b, g, r, a))

    arr = name + '_map'
    lines.append(f'// {name}: 68x54, {len(colors)} visible RGB565 colors, binary alpha')
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
    '// Mochi 15Q CLEAN FOOD: reconstructed from the approved full artwork sheet.',
    '// All nine frames are complete standalone 68x54 canvases.',
    '// Same bottom-center anchor, >=2 px transparent safety margin on every edge.',
    '// RGB565-snapped palette, no dithering, binary alpha, no runtime rescaling.',
    ''
]
for key in ('fish0', 'fish1', 'fish2', 'meal0', 'meal1', 'meal2',
            'dessert0', 'dessert1', 'dessert2'):
    emit(lines, 'mochi_action_15q_' + key, DATA[key])

lines += [
    'static const lv_image_dsc_t* const kMochiActionFoodStages15Q[3][3] = {',
    '    {&mochi_action_15q_fish0, &mochi_action_15q_fish1, &mochi_action_15q_fish2},',
    '    {&mochi_action_15q_meal0, &mochi_action_15q_meal1, &mochi_action_15q_meal2},',
    '    {&mochi_action_15q_dessert0, &mochi_action_15q_dessert1, &mochi_action_15q_dessert2},',
    '};', ''
]
OUT.write_text('\n'.join(lines), encoding='utf-8')
print('Generated and validated Mochi 15Q clean food sprites', OUT, OUT.stat().st_size)
