from pathlib import Path

OUT = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_action_assets_15l.h')

# 15O: hard-edged RGB565-friendly palette. Every visible pixel is fully opaque;
# transparency is binary. This deliberately avoids soft alpha/anti-aliasing that
# looked mottled on the small TFT.
PAL = {
    'T': (0, 0, 0, 0),
    'K': (80, 74, 104, 255),
    'W': (255, 248, 236, 255),
    'C': (250, 226, 202, 255),
    'P': (245, 151, 181, 255),
    'B': (147, 205, 239, 255),
    'b': (102, 170, 222, 255),
    'G': (247, 198, 80, 255),
    'g': (255, 226, 145, 255),
    'O': (240, 166, 101, 255),
    'R': (173, 116, 87, 255),
    'M': (153, 214, 193, 255),
    'H': (255, 218, 198, 255),
    'h': (238, 179, 164, 255),
}


def blank(w, h):
    return [['T'] * w for _ in range(h)]


def put(im, x, y, c):
    if 0 <= y < len(im) and 0 <= x < len(im[0]):
        im[y][x] = c


def rect(im, x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(im, x, y, c)


def ellipse(im, x0, y0, x1, y1, c):
    cx = (x0 + x1) / 2
    cy = (y0 + y1) / 2
    rx = max(.5, (x1 - x0 + 1) / 2)
    ry = max(.5, (y1 - y0 + 1) / 2)
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1:
                put(im, x, y, c)


def poly(im, pts, c):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    for y in range(min(ys), max(ys) + 1):
        for x in range(min(xs), max(xs) + 1):
            inside = False
            j = len(pts) - 1
            for i in range(len(pts)):
                xi, yi = pts[i]
                xj, yj = pts[j]
                if ((yi > y) != (yj > y)) and x < (xj - xi) * (y - yi) / (yj - yi + 1e-9) + xi:
                    inside = not inside
                j = i
            if inside:
                put(im, x, y, c)


def line(im, x0, y0, x1, y1, c, w=1):
    dx = abs(x1 - x0)
    sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0)
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        for oy in range(-(w // 2), w // 2 + 1):
            for ox in range(-(w // 2), w // 2 + 1):
                put(im, x0 + ox, y0 + oy, c)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def plate(im):
    # IDENTICAL in every fish frame. This is the visual anchor that prevents
    # apparent movement when the food body changes between bite stages.
    ellipse(im, 7, 34, 56, 45, 'K')
    ellipse(im, 9, 35, 54, 43, 'B')
    ellipse(im, 13, 36, 50, 41, 'W')
    rect(im, 22, 43, 41, 44, 'K')


def fish(stage):
    # All foods use EXACTLY 64x48. No LVGL object-resize between bite frames.
    im = blank(64, 48)
    plate(im)
    x_end = [53, 43, 32][stage]
    body = blank(64, 48)
    ellipse(body, 13, 19, 49, 35, 'K')
    ellipse(body, 15, 20, 47, 33, 'g')
    ellipse(body, 17, 21, 45, 32, 'W')
    poly(body, [(47, 23), (57, 18), (54, 27), (58, 33), (47, 30)], 'K')
    poly(body, [(47, 24), (54, 20), (52, 27), (55, 31), (47, 29)], 'O')
    ellipse(body, 19, 24, 24, 29, 'R')
    put(body, 21, 24, 'W')
    for yy in (23, 27, 31):
        line(body, 28, yy, 43, yy, 'O')
    # Remove the eaten third inside the fixed canvas. Plate never moves.
    for y in range(12, 36):
        for x in range(x_end + 1, 64):
            body[y][x] = 'T'
    for cy in (21, 26, 31):
        ellipse(body, x_end - 2, cy - 2, x_end + 2, cy + 2, 'T')
    for y, row in enumerate(body):
        for x, c in enumerate(row):
            if c != 'T':
                im[y][x] = c
    return im


def meal(stage):
    im = blank(64, 48)
    # Bowl geometry is byte-for-byte identical in all three frames.
    ellipse(im, 9, 27, 54, 43, 'K')
    ellipse(im, 11, 28, 52, 40, 'B')
    rect(im, 14, 34, 49, 40, 'B')
    rect(im, 19, 40, 44, 43, 'b')
    ellipse(im, 13, 25, 50, 33, 'K')
    ellipse(im, 15, 26, 48, 31, 'W')
    if stage <= 2:
        ellipse(im, 17, 22, 28, 29, 'O')
    if stage <= 1:
        ellipse(im, 29, 20, 40, 29, 'R')
    if stage == 0:
        ellipse(im, 38, 18, 48, 27, 'M')
        rect(im, 41, 16, 44, 22, 'M')
    if stage == 0:
        ellipse(im, 19, 18, 39, 28, 'C')
    elif stage == 1:
        ellipse(im, 21, 21, 38, 29, 'C')
    else:
        ellipse(im, 23, 24, 35, 30, 'C')
    return im


def dessert(stage):
    im = blank(64, 48)
    # Cup/base is fixed to the same bottom-center anchor in every stage.
    poly(im, [(18, 22), (46, 22), (43, 42), (21, 42)], 'K')
    poly(im, [(20, 24), (44, 24), (41, 40), (23, 40)], 'B')
    rect(im, 24, 37, 40, 42, 'W')
    rect(im, 28, 42, 36, 45, 'K')
    if stage == 0:
        ellipse(im, 18, 13, 46, 28, 'W')
        ellipse(im, 22, 10, 42, 23, 'C')
        poly(im, [(32, 5), (35, 11), (42, 12), (37, 17), (39, 24),
                  (32, 20), (25, 24), (27, 17), (22, 12), (29, 11)], 'G')
        ellipse(im, 20, 20, 25, 25, 'P')
        ellipse(im, 40, 20, 45, 25, 'P')
    elif stage == 1:
        ellipse(im, 20, 16, 44, 29, 'W')
        ellipse(im, 24, 14, 40, 24, 'C')
        poly(im, [(32, 10), (34, 15), (39, 16), (35, 20), (36, 25),
                  (32, 22), (27, 25), (29, 20), (25, 16), (30, 15)], 'G')
        ellipse(im, 40, 22, 45, 27, 'P')
    else:
        ellipse(im, 22, 21, 42, 30, 'W')
        ellipse(im, 26, 20, 38, 27, 'C')
        ellipse(im, 27, 24, 32, 29, 'P')
    return im


def hand():
    im = blank(52, 28)
    poly(im, [(1, 16), (8, 12), (15, 15), (15, 27), (1, 27)], 'K')
    poly(im, [(2, 17), (8, 13), (13, 16), (13, 26), (2, 26)], 'B')
    rect(im, 8, 14, 14, 25, 'W')
    poly(im, [(12, 15), (18, 10), (20, 5), (23, 4), (25, 6), (24, 12),
              (27, 7), (30, 7), (31, 9), (29, 14), (33, 10), (36, 11),
              (36, 13), (32, 17), (42, 15), (49, 17), (49, 20), (42, 23),
              (31, 24), (22, 23), (15, 20)], 'K')
    poly(im, [(14, 15), (19, 11), (21, 6), (23, 5), (24, 7), (22, 14),
              (26, 15), (28, 8), (30, 8), (29, 15), (32, 16), (34, 11),
              (35, 12), (32, 18), (41, 16), (47, 18), (47, 19), (41, 21),
              (31, 22), (23, 21), (16, 19)], 'H')
    line(im, 19, 18, 32, 19, 'h')
    rect(im, 17, 14, 19, 16, 'W')
    put(im, 23, 6, 'W')
    return im


def heart():
    # Native 38x38 source: no runtime scaling/interpolation on the TFT.
    im = blank(38, 38)
    ellipse(im, 5, 5, 18, 20, 'K')
    ellipse(im, 19, 5, 32, 20, 'K')
    poly(im, [(5, 13), (32, 13), (19, 34)], 'K')
    ellipse(im, 7, 7, 17, 18, 'P')
    ellipse(im, 20, 7, 30, 18, 'P')
    poly(im, [(7, 13), (30, 13), (19, 31)], 'P')
    ellipse(im, 10, 9, 13, 12, 'W')
    put(im, 30, 8, 'g')
    put(im, 5, 25, 'B')
    return im


def sparkle():
    # Used only for the Play finish, not during eating.
    im = blank(20, 20)
    poly(im, [(10, 1), (12, 7), (18, 10), (12, 12),
              (10, 19), (8, 12), (2, 10), (8, 8)], 'G')
    poly(im, [(10, 5), (11, 9), (15, 10), (11, 11),
              (10, 15), (9, 11), (5, 10), (9, 9)], 'W')
    return im


def emit(lines, name, im):
    h = len(im)
    w = len(im[0])
    data = []
    opaque_colors = set()
    for row in im:
        for key in row:
            r, g, b, a = PAL[key]
            data.extend((b, g, r, a))
            if a:
                opaque_colors.add((r, g, b))
    arr = f'{name}_map'
    lines.append(f'// {name}: {w}x{h}, {len(opaque_colors)} opaque colors + binary transparency')
    lines.append(f'alignas(4) static const uint8_t {arr}[] = {{')
    for i in range(0, len(data), 20):
        lines.append('    ' + ', '.join(f'0x{x:02X}' for x in data[i:i + 20]) + ',')
    lines += [
        '};',
        f'static const lv_image_dsc_t {name} = {{',
        f'    {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_ARGB8888, 0, {w}, {h}, {w * 4}, 0}},',
        f'    sizeof({arr}), {arr}, nullptr, nullptr',
        '};',
        ''
    ]


foods = [('fish', fish), ('meal', meal), ('dessert', dessert)]

# Build-time invariants: these are the bugs 15O is specifically designed to fix.
for food_name, fn in foods:
    frames = [fn(i) for i in range(3)]
    assert all(len(frame) == 48 and len(frame[0]) == 64 for frame in frames), food_name
    for frame in frames:
        assert all(PAL[k][3] in (0, 255) for row in frame for k in row), food_name
        used = {PAL[k][:3] for row in frame for k in row if k != 'T'}
        assert len(used) <= 8, (food_name, len(used))

lines = [
    '#pragma once', '', '#include <lvgl.h>', '#include <cstddef>', '',
    '// Mochi 15O action assets: fixed-canvas, hard-edged, RGB565-friendly pixel art.',
    '// Every food frame is EXACTLY 64x48 and uses the same bottom-center anchor.',
    '// Pixels are either fully transparent or fully opaque; no semi-alpha/anti-aliasing.',
    ''
]
for name, fn in foods:
    for stage in range(3):
        emit(lines, f'mochi_action_{name}_{stage}', fn(stage))
emit(lines, 'mochi_action_hand', hand())
emit(lines, 'mochi_action_heart', heart())
emit(lines, 'mochi_action_sparkle', sparkle())
lines += [
    'static const lv_image_dsc_t* const kMochiActionFoodStages[3][3] = {',
    '    {&mochi_action_fish_0, &mochi_action_fish_1, &mochi_action_fish_2},',
    '    {&mochi_action_meal_0, &mochi_action_meal_1, &mochi_action_meal_2},',
    '    {&mochi_action_dessert_0, &mochi_action_dessert_1, &mochi_action_dessert_2},',
    '};', '',
    'static const lv_image_dsc_t* const kMochiActionHand = &mochi_action_hand;',
    'static const lv_image_dsc_t* const kMochiActionHeart = &mochi_action_heart;',
    'static const lv_image_dsc_t* const kMochiActionSparkle = &mochi_action_sparkle;', ''
]
OUT.write_text('\n'.join(lines), encoding='utf-8')
print(f'Generated 15O fixed-canvas action art {OUT} ({OUT.stat().st_size} bytes)')
