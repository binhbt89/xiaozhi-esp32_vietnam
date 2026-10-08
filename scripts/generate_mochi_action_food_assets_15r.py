from pathlib import Path
import base64, zlib

OUT = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_action_food_assets_15q.h')

# 15R: nine final standalone food bitmaps. No runtime erase/mask logic.
# The plate/bowl/cup is baked into every stage and the vessel pixels are
# visually locked across stages; only the edible content changes.
DATA = {
    'fish0': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
    'fish1': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
    'fish2': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
    'meal0': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
    'meal1': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
    'meal2': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
    'dessert0': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
    'dessert1': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
    'dessert2': 'eNrtWC1w20oQLnigsCAgoKAgwCDAoECgQCDAIECgQKBAoEAgQKBAIEAgwKBAUCBAoEAg4ECAgIGAgYCBgIGAgUCAQMCBggOd2fftnRz/pa8zSfNacDuzc5Ksn7tvv/12z69eWbNmzZq1a9euXbt27dq1a9euXbt27dq1a9euffv2ffv2Lfu1ayffmn3d7av6r9Vx7P7/+0+7z+v5v1/x2+v3fXv3f3n3n3n7y7tJ3H7r2t3P3Y7n7t3u3e7d2u3Y7u3c7d2u3Q7v3a7d2u3I7v3Y7d2u3E7u3f7d2u3A7v3b7d2u2+7v3c7d2u2+7v3d7d2u2+7v3e7d2u2+7v3f7d2u2+7v3g7d2u2+7v3h7d2u2+7v3i7d2u2+7v3j7d2u2+7v3k7d2u2+7v3l7d2u2+7v3m7d2u2+7v3n7d2u2+7v3o7d2u2+7v3p7d2u2+7v3q7d2u2+7v3r7d2u2+7v3s7d2u2+7v3t7d2u2+7v3u7d2u2+7v3v7d2u2+7v3w7d2u2+7v3x7d2u2+7v3y7d2u2+7v3z7d2u2+7v30==',
}

def emit(lines, name, rgba):
    w, h = 68, 54
    assert len(rgba) == w*h*4
    # Validate binary alpha and RGB565-snapped channels.
    for p in range(0, len(rgba), 4):
        r,g,b,a = rgba[p:p+4]
        assert a in (0,255)
        if a:
            assert (r & 0x07) == 0
            assert (g & 0x03) == 0
            assert (b & 0x07) == 0
    bgra = bytearray()
    for p in range(0, len(rgba), 4):
        r,g,b,a = rgba[p:p+4]
        bgra.extend((b,g,r,a))
    arr = f'mochi_action_food_15r_{name}_map'
    lines.append(f'alignas(4) static const uint8_t {arr}[] = {{')
    for i in range(0, len(bgra), 20):
        lines.append('    ' + ', '.join(f'0x{x:02X}' for x in bgra[i:i+20]) + ',')
    lines += [
        '};',
        f'static const lv_image_dsc_t mochi_action_food_15r_{name} = {{',
        '    {LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_ARGB8888, 0, 68, 54, 272, 0},',
        f'    sizeof({arr}), {arr}, nullptr, nullptr',
        '};', ''
    ]

frames = {}
for name, payload in DATA.items():
    rgba = zlib.decompress(base64.b64decode(payload))
    assert len(rgba) == 68*54*4
    frames[name] = rgba

# Build-time invariant: the locked vessel regions are byte-identical across
# all 3 stages. This specifically prevents 15Q's plate/bowl/cup color loss.
VESSEL_REGIONS = {
    'fish': lambda x,y: y >= 29 or (y >= 20 and (x <= 12 or x >= 55)),
    'meal': lambda x,y: y >= 30 or (y >= 24 and (x <= 10 or x >= 57)),
    'dessert': lambda x,y: y >= 31 or (y >= 26 and (x <= 17 or x >= 50)),
}
for cat, pred in VESSEL_REGIONS.items():
    ref = frames[f'{cat}0']
    for stage in (1,2):
        cur = frames[f'{cat}{stage}']
        for y in range(54):
            for x in range(68):
                if not pred(x,y):
                    continue
                p = (y*68+x)*4
                # Only compare pixels visible in the reference vessel.
                if ref[p+3] != 0:
                    assert ref[p:p+4] == cur[p:p+4], (cat, stage, x, y)

lines = [
    '#pragma once', '', '#include <lvgl.h>', '#include <cstddef>', '',
    '// Mochi 15R full-stage food redraw: 9 standalone 68x54 bitmaps.',
    '// Vessel pixels are locked; no erase/subtract animation is used.', ''
]
for cat in ('fish','meal','dessert'):
    for stage in range(3):
        emit(lines, f'{cat}{stage}', frames[f'{cat}{stage}'])

lines += [
    'static const lv_image_dsc_t* const kMochiActionFoodStages15Q[3][3] = {',
    '    {&mochi_action_food_15r_fish0, &mochi_action_food_15r_fish1, &mochi_action_food_15r_fish2},',
    '    {&mochi_action_food_15r_meal0, &mochi_action_food_15r_meal1, &mochi_action_food_15r_meal2},',
    '    {&mochi_action_food_15r_dessert0, &mochi_action_food_15r_dessert1, &mochi_action_food_15r_dessert2},',
    '};', ''
]
OUT.write_text('\n'.join(lines), encoding='utf-8')
print('Generated Mochi 15R: 9 standalone food stages with locked vessel pixels', OUT)