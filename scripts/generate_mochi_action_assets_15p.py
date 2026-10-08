from pathlib import Path
import base64
import math
import runpy
import zlib

OUT = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_action_assets_15l.h')

# Load the exact 15M approved artwork source. Running the script also emits the
# original header, which we intentionally replace below after quantization and
# fixed-canvas alignment.
SRC = runpy.run_path('scripts/generate_mochi_action_assets_15m.py')
A = SRC['A']

ALPHA_THRESHOLD = 96
FOOD_W = 68
FOOD_H = 54
FOOD_BASE_Y = 50
FOOD_CENTER_X = FOOD_W // 2


def decode(name):
    w, h, ps, payload = A[name]
    pal = [tuple(map(int, q.split(','))) for q in ps.split(';')]
    idx = zlib.decompress(base64.b64decode(payload))
    assert len(idx) == w * h, (name, len(idx), w * h)
    px = []
    for i in idx:
        px.append(pal[i])
    return w, h, px


def snap565(rgb):
    r, g, b = rgb
    r5 = int(round(r * 31 / 255))
    g6 = int(round(g * 63 / 255))
    b5 = int(round(b * 31 / 255))
    return (
        int(round(r5 * 255 / 31)),
        int(round(g6 * 255 / 63)),
        int(round(b5 * 255 / 31)),
    )


def dist(a, b):
    # RGB565-oriented weighted squared distance. Green gets a little more weight
    # because the panel has 6 green bits and human vision is most sensitive there.
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return 2 * dr * dr + 4 * dg * dg + 2 * db * db


def build_palette(images, max_colors):
    counts = {}
    for _w, _h, px in images:
        for r, g, b, a in px:
            if a < ALPHA_THRESHOLD:
                continue
            c = snap565((r, g, b))
            counts[c] = counts.get(c, 0) + 1
    if not counts:
        return [(255, 255, 255)]
    colors = list(counts)
    if len(colors) <= max_colors:
        return sorted(colors, key=lambda c: (-counts[c], c))

    # Deterministic weighted farthest-point initialization. This keeps frequent
    # body colors while also preserving dark outlines and bright highlights.
    first = max(colors, key=lambda c: counts[c])
    centers = [first]
    while len(centers) < max_colors:
        def score(c):
            d = min(dist(c, k) for k in centers)
            return d * math.sqrt(counts[c])
        nxt = max(colors, key=score)
        if nxt in centers:
            break
        centers.append(nxt)

    # Weighted k-means, snapped back to RGB565 after every update.
    for _ in range(10):
        groups = [[] for _ in centers]
        for c in colors:
            j = min(range(len(centers)), key=lambda i: dist(c, centers[i]))
            groups[j].append(c)
        new_centers = []
        for old, group in zip(centers, groups):
            if not group:
                new_centers.append(old)
                continue
            total = sum(counts[c] for c in group)
            r = sum(c[0] * counts[c] for c in group) / total
            g = sum(c[1] * counts[c] for c in group) / total
            b = sum(c[2] * counts[c] for c in group) / total
            new_centers.append(snap565((r, g, b)))
        # Dedupe without changing deterministic order.
        dedup = []
        for c in new_centers:
            if c not in dedup:
                dedup.append(c)
        centers = dedup
        while len(centers) < max_colors:
            remaining = [c for c in colors if c not in centers]
            if not remaining:
                break
            nxt = max(remaining,
                      key=lambda c: min(dist(c, k) for k in centers) * math.sqrt(counts[c]))
            centers.append(nxt)
    return centers


def quantize(image, palette):
    w, h, px = image
    out = []
    for r, g, b, a in px:
        if a < ALPHA_THRESHOLD:
            out.append((0, 0, 0, 0))
            continue
        c = snap565((r, g, b))
        q = min(palette, key=lambda k: dist(c, k))
        out.append((q[0], q[1], q[2], 255))
    return w, h, out


def alpha_bbox_lower(image):
    w, h, px = image
    y0 = int(h * 0.58)
    pts = []
    for y in range(y0, h):
        for x in range(w):
            if px[y * w + x][3]:
                pts.append((x, y))
    if not pts:
        for y in range(h):
            for x in range(w):
                if px[y * w + x][3]:
                    pts.append((x, y))
    if not pts:
        return w // 2, h - 1
    min_x = min(x for x, _ in pts)
    max_x = max(x for x, _ in pts)
    max_y = max(y for _, y in pts)
    return (min_x + max_x) // 2, max_y


def fixed_food_canvas(image):
    w, h, px = image
    anchor_x, anchor_y = alpha_bbox_lower(image)
    dx = FOOD_CENTER_X - anchor_x
    dy = FOOD_BASE_Y - anchor_y
    out = [(0, 0, 0, 0)] * (FOOD_W * FOOD_H)
    opaque = 0
    for y in range(h):
        for x in range(w):
            p = px[y * w + x]
            if p[3] == 0:
                continue
            nx = x + dx
            ny = y + dy
            if not (0 <= nx < FOOD_W and 0 <= ny < FOOD_H):
                raise SystemExit(f'15P clipping detected: src={w}x{h} pixel=({x},{y}) -> ({nx},{ny})')
            out[ny * FOOD_W + nx] = p
            opaque += 1
    assert opaque > 0

    # Require real transparent safety padding around every frame. This catches
    # accidental art touching the canvas edge before firmware is ever built.
    pts = [(i % FOOD_W, i // FOOD_W) for i, p in enumerate(out) if p[3]]
    min_x = min(x for x, _ in pts); max_x = max(x for x, _ in pts)
    min_y = min(y for _, y in pts); max_y = max(y for _, y in pts)
    if min_x < 2 or max_x > FOOD_W - 3 or min_y < 2 or max_y > FOOD_H - 3:
        raise SystemExit(f'15P safety padding failed: bbox=({min_x},{min_y})..({max_x},{max_y})')
    return FOOD_W, FOOD_H, out


def resize_nearest(image, nw, nh):
    w, h, px = image
    out = []
    for y in range(nh):
        sy = min(h - 1, (y * h) // nh)
        for x in range(nw):
            sx = min(w - 1, (x * w) // nw)
            out.append(px[sy * w + sx])
    return nw, nh, out


def emit(lines, name, image):
    w, h, px = image
    raw = bytearray()
    opaque_colors = set()
    alpha_values = set()
    for r, g, b, a in px:
        raw.extend((b, g, r, a))
        alpha_values.add(a)
        if a:
            opaque_colors.add((r, g, b))
    assert alpha_values.issubset({0, 255}), (name, alpha_values)
    arr = name + '_map'
    lines.append(f'// {name}: {w}x{h}, {len(opaque_colors)} RGB565-snapped opaque colors, binary alpha')
    lines.append(f'alignas(4) static const uint8_t {arr}[] = {{')
    for i in range(0, len(raw), 20):
        lines.append('    ' + ', '.join(f'0x{x:02X}' for x in raw[i:i + 20]) + ',')
    lines += [
        '};',
        f'static const lv_image_dsc_t {name} = {{',
        f'    {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_ARGB8888, 0, {w}, {h}, {w * 4}, 0}},',
        f'    sizeof({arr}), {arr}, nullptr, nullptr',
        '};',
        ''
    ]


food_groups = {}
for food in ('fish', 'meal', 'dessert'):
    originals = [decode(f'mochi_action_{food}_{i}') for i in range(3)]
    pal = build_palette(originals, 18)
    food_groups[food] = [fixed_food_canvas(quantize(img, pal)) for img in originals]

hand_src = decode('mochi_action_hand')
hand = quantize(hand_src, build_palette([hand_src], 16))
heart_src = decode('mochi_action_heart')
heart_q = quantize(heart_src, build_palette([heart_src], 14))
heart = resize_nearest(heart_q, 38, 38)
sparkle_src = decode('mochi_action_sparkle')
sparkle = quantize(sparkle_src, build_palette([sparkle_src], 10))

lines = [
    '#pragma once', '', '#include <lvgl.h>', '#include <cstddef>', '',
    '// Mochi 15P: approved 15M artwork restored, then RGB565-quantized without redesign.',
    '// All food stages use the SAME 68x54 canvas and stable lower-base anchor.',
    '// Colors are snapped to RGB565 and reduced per asset group; alpha is binary.',
    ''
]
for food in ('fish', 'meal', 'dessert'):
    for stage, image in enumerate(food_groups[food]):
        emit(lines, f'mochi_action_{food}_{stage}', image)
emit(lines, 'mochi_action_hand', hand)
emit(lines, 'mochi_action_heart', heart)
emit(lines, 'mochi_action_sparkle', sparkle)
lines += [
    'static const lv_image_dsc_t* const kMochiActionFoodStages[3][3] = {',
    '    {&mochi_action_fish_0, &mochi_action_fish_1, &mochi_action_fish_2},',
    '    {&mochi_action_meal_0, &mochi_action_meal_1, &mochi_action_meal_2},',
    '    {&mochi_action_dessert_0, &mochi_action_dessert_1, &mochi_action_dessert_2},',
    '};', '',
    'static const lv_image_dsc_t* const kMochiActionHand = &mochi_action_hand;',
    'static const lv_image_dsc_t* const kMochiActionHeart = &mochi_action_heart;',
    'static const lv_image_dsc_t* const kMochiActionSparkle = &mochi_action_sparkle;',
    ''
]
OUT.write_text('\n'.join(lines), encoding='utf-8')
print('Generated Mochi 15P approved-art RGB565 quantized assets', OUT, OUT.stat().st_size)
