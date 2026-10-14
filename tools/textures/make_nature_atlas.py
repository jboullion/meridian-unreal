"""
The pictures of ambient life's small creatures (docs/adr/0013-ambient-life.md): butterflies, fish, birds and bats.
The game has no art for them, so Meridian Shards draws them in code (packages/render/src/nature.ts and flyers.ts,
ours); these are the same shapes, drawn the same way (4 x 4 samples a pixel), into one atlas:

    python tools/textures/make_nature_atlas.py

-> build/textures_placeholder/T_NatureAtlas.png, 4 columns x 8 rows of 64 px frames (256 x 512), grey by each
   shape's tone with its coverage in alpha (tinted per creature in M_Nature):
   row 0 a butterfly's wing beats (open 1, 0.55, 0.12, 0.55), row 1 a fish leaping (nose up 0.6, level,
   down 0.6, level), row 2 a bird from below (tips raised 12, 2, -9, 2 px), row 3 a bat (14, 2, -12, 2),
   row 4 a soft round dot (splash drops); the rest empty.
"""
import math
import os

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "build", "textures_placeholder", "T_NatureAtlas.png")
FRAME, COLS, ROWS = 64, 4, 8


def coverage(shape, px, py):
    n, tone = 0, 0.0
    for sy in range(4):
        for sx in range(4):
            t = shape(px + (sx + 0.5) / 4, py + (sy + 0.5) / 4)
            if t:
                n += 1
                tone += t
    return n / 16.0, (tone / n if n else 0.0)


def butterfly(open_):
    """A butterfly seen from the front, its wings open wide (1) to edge-on (0) (nature.ts butterfly)"""
    c, y0 = FRAME / 2, 32

    def shape(x, y):
        dx, dy = abs(x - c), y - y0
        if dx < 1.3 and -9 < dy < 9:
            return 0.25  # body
        if -14 < dy < -9 and abs(dx - (14 + dy) * 0.5) < 0.6:
            return 0.25  # antennae
        w = max(0.08, open_)
        ux = dx / w
        fore = ((ux - 11) / 11) ** 2 + ((dy + 5) / 8.5) ** 2
        hind = ((ux - 8) / 8) ** 2 + ((dy - 6) / 6.5) ** 2
        if dx < 1.3 or (fore > 1 and hind > 1):
            return 0
        rim = min(fore, hind) > 0.62
        spot = math.hypot(ux - 14, dy + 6) < 2.6
        return 0.45 if rim or spot else 1
    return shape


def fish(pitch):
    """A fish from the side, facing right, nose up by pitch radians (nature.ts fish)"""
    c = FRAME / 2
    ca, sa = math.cos(pitch), math.sin(pitch)

    def shape(x, y):
        lx = (x - c) * ca - (y - c) * sa
        ly = (x - c) * sa + (y - c) * ca
        if (lx / 14) ** 2 + (ly / 5) ** 2 <= 1:
            if math.hypot(lx - 9, ly + 1) < 1.3:
                return 0.15  # eye
            return 0.85 if ly > 1.5 else 0.6  # a lighter belly
        if -20 < lx < -11 and abs(ly) < (-11 - lx) * 0.75:
            return 0.55  # the tail
        if -8 < ly < -4 and -4 < lx < 4 and -ly - 4 < (4 - abs(lx)) * 0.9:
            return 0.5  # a fin on the back
        return 0
    return shape


def near_segment(ax, ay, bx, by, x, y, r):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy or 1)))
    return math.hypot(ax + t * dx - x, ay + t * dy - y) <= r


def in_polygon(pts, x, y):
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def bird(lift):
    """A bird from below: a small body and two long wings bent at the wrist (flyers.ts birdShape)"""
    c, y0 = FRAME / 2, 34

    def inside(x, y):
        dx = abs(x - c)
        if ((x - c) / 3.2) ** 2 + ((y - y0) / 7) ** 2 <= 1:
            return True
        if math.hypot(x - c, y - (y0 - 7)) <= 2.4:
            return True
        if near_segment(c - 2.5, y0 + 6, c, y0 + 10, x, y, 1.6) or near_segment(c + 2.5, y0 + 6, c, y0 + 10, x, y, 1.6):
            return True
        wx, wy = 13, y0 - 1 - lift * 0.55
        tx, ty = 29, y0 - lift
        if near_segment(3, y0 - 1, wx, wy, dx, y, 2.6):
            return True
        for k in range(7):
            t = k / 6
            if math.hypot(dx - (wx + (tx - wx) * t), y - (wy + (ty - wy) * t)) <= 2.4 - 1.6 * t:
                return True
        return False
    return lambda x, y: 1 if inside(x, y) else 0


def bat(lift):
    """A bat: a round body with ears, webbed wings with a scalloped trailing edge (flyers.ts batShape)"""
    c, y0 = FRAME / 2, 32
    body, wing_tone = 0.35, 1

    def shape(x, y):
        dx = abs(x - c)
        if ((x - c) / 4) ** 2 + ((y - y0) / 6) ** 2 <= 1:
            return body
        if in_polygon([(c - 4, y0 - 4), (c - 3, y0 - 11), (c - 1, y0 - 5)], x, y) or in_polygon([(c + 4, y0 - 4), (c + 3, y0 - 11), (c + 1, y0 - 5)], x, y):
            return body
        wrist = (15, y0 - 7 - lift * 0.7)
        tip_y = y0 - 4 - lift
        wing = [(3, y0 - 4), wrist, (30, tip_y), (28, tip_y + 8), (21, y0 + 8 - lift * 0.4), (12, y0 + 11 - lift * 0.2), (3, y0 + 8)]
        if not in_polygon(wing, dx, y):
            return 0
        for sx, sy in ((25, tip_y + 11), (17, y0 + 12 - lift * 0.3), (8, y0 + 13 - lift * 0.1)):
            if math.hypot(dx - sx, y - sy) < 4.6:
                return 0
        if (near_segment(3, y0 - 4, wrist[0], wrist[1], dx, y, 1.3) or near_segment(wrist[0], wrist[1], 30, tip_y, dx, y, 1)
                or near_segment(wrist[0], wrist[1], 21, y0 + 8 - lift * 0.4, dx, y, 0.8)):
            return body
        return wing_tone
    return shape


def dot(x, y):
    d = math.hypot(x - FRAME / 2, y - FRAME / 2) / (FRAME / 2)
    return max(0.01, (1 - d) ** 1.5) if d < 1 else 0


def main():
    img = Image.new("RGBA", (FRAME * COLS, FRAME * ROWS), (0, 0, 0, 0))
    px = img.load()
    rows = [
        [butterfly(o) for o in (1, 0.55, 0.12, 0.55)],
        [fish(p) for p in (0.6, 0, -0.6, 0)],
        [bird(l) for l in (12, 2, -9, 2)],
        [bat(l) for l in (14, 2, -12, 2)],
    ]
    for row, frames in enumerate(rows):
        for col, shape in enumerate(frames):
            for y in range(FRAME):
                for x in range(FRAME):
                    a, tone = coverage(shape, x, y)
                    if a:
                        g = round(tone * 255)
                        px[col * FRAME + x, row * FRAME + y] = (g, g, g, round(a * 255))
    # the dot: full colour, its glow in alpha
    for y in range(FRAME):
        for x in range(FRAME):
            a, tone = coverage(dot, x, y)
            if a:
                px[x, 4 * FRAME + y] = (255, 255, 255, round(tone * 255))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print("nature atlas -> %s" % os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
