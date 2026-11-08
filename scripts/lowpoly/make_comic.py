# make_comic.py
# 漫画の描き文字を 1 枚の画像 (Resources/Textures/UI/Comic/atlas.png) と位置の表
# (Resources/Data/comic_words.json) にまとめる。整ったフォントのまま使わず、
# 書体は HUD と同じ物 (Zen Kaku Gothic と Anton) にそろえ、字の配置と縁と影で描き文字にする。
# 使い方: python scripts/lowpoly/make_comic.py [--preview 画像のパス]
import json
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
HEAVY = ROOT / "Resources/Fonts/ZenKakuGothicNew/ZenKakuGothicNew-Black.ttf"
ATLAS = 2048
SHRINK = .5
INK = (22, 18, 20)
PAPER = (252, 248, 236)

# 言葉の組: 飲む音 (小・中・大)、ぶつかる音 (軽い・強い)
WORDS = {
    "gulpSmall": ["ぱくっ", "もぐ", "ちゅるん", "ぺろり"],
    "gulpMiddle": ["ごくん", "もぐもぐ", "ぱくん"],
    "gulpBig": ["ガブッ", "ゴックン", "バクッ"],
    "bumpSoft": ["ゴチン", "ゴッ", "ガンッ"],
    "bumpHard": ["ドカーン", "グシャッ", "ガガーン"],
    # ギミック: 絵の具に飛び込む・水鉄砲・だるま落とし・ボウリング・ドミノ・ばらまき・液体をつたう・紙飛行機・同じ色を飲む
    "gimmickSplash": ["ベチャッ", "ぱしゃっ"],
    "gimmickShot": ["ビュッ"],
    "gimmickHit": ["スコーン", "カコン"],
    "gimmickMiss": ["グラッ"],
    "gimmickWin": ["ストン！"],
    "gimmickStrike": ["ストライク！"],
    "gimmickPins": ["カコーン", "ガラガラ"],
    "gimmickDomino": ["パタパタ"],
    "gimmickBurst": ["ドサーッ", "ジャラジャラ"],
    "gimmickFlow": ["ヌルーッ", "とろ〜"],
    "gimmickFly": ["スーッ"],
    "paintMatch": ["おそろい"],
}


def wobble(rng, amount, freq):
    a, b, c, d = (rng.uniform(0, 6.28) for _ in range(4))
    return lambda x, y: (amount * (math.sin(x * freq + a) + .5 * math.sin(y * freq * 1.7 + b)),
                         amount * (math.sin(y * freq + c) + .5 * math.sin(x * freq * 1.3 + d)))


def warp(image, fn, cell=32):
    # 細かい格子ごとに少しずつずらして、線を手で引いたように曲げる
    w, h = image.size
    mesh = []
    for y in range(0, h, cell):
        for x in range(0, w, cell):
            box = (x, y, min(x + cell, w), min(y + cell, h))
            q = []
            for px, py in ((box[0], box[1]), (box[0], box[3]), (box[2], box[3]), (box[2], box[1])):
                dx, dy = fn(px, py)
                q += [px + dx, py + dy]
            mesh.append((box, q))
    return image.transform(image.size, Image.MESH, mesh, Image.BICUBIC)


def ink(mask, blur=1.4):
    # 角を少しだけ丸めた、なめらかな線にする
    soft = mask.filter(ImageFilter.GaussianBlur(blur))
    return soft.point(lambda v: 255 if v > 120 else 0).filter(ImageFilter.GaussianBlur(.7))


def grow(mask, width):
    # 字の形をどこも同じ太さで太らせる (縁取りの太さがむらにならない)
    wide = mask.filter(ImageFilter.GaussianBlur(width * .62))
    return wide.point(lambda v: 255 if v > 26 else 0).filter(ImageFilter.GaussianBlur(.8))


def glyph_mask(ch, font_path, size, rng, tilt, squash, wob):
    canvas = int(size * 1.9)
    m = Image.new("L", (canvas, canvas), 0)
    d = ImageDraw.Draw(m)
    font = ImageFont.truetype(str(font_path), size)
    d.text((canvas / 2, canvas / 2), ch, font=font, fill=255, anchor="mm")
    m = warp(m, wobble(rng, wob, .021))
    # 横や縦に潰し、傾ける
    sx, sy = squash
    nw, nh = int(canvas * sx), int(canvas * sy)
    m = m.resize((nw, nh), Image.BICUBIC)
    pad = Image.new("L", (canvas * 2, canvas * 2), 0)
    pad.paste(m, ((canvas * 2 - nw) // 2, (canvas * 2 - nh) // 2))
    shear = rng.uniform(-.16, .1)
    pad = pad.transform(pad.size, Image.AFFINE, (1, shear, -shear * canvas, 0, 1, 0), Image.BICUBIC)
    return pad.rotate(tilt, Image.BICUBIC)


STYLES = {
    # 飲む音: 軽い音は小さく跳ねる
    "gulpSmall": dict(size=104, bold=0, fill=(255, 255, 255), edge=9, shadow=0, tilt=11, step=.86, rise=.12, grow=0, wob=1),
    "gulpMiddle": dict(size=120, bold=0, fill=(255, 216, 70), edge=10, shadow=0, tilt=10, step=.84, rise=.1, grow=.05, wob=1),
    "gulpBig": dict(size=160, bold=0, fill=(250, 150, 44), edge=11, shadow=12, tilt=9, step=.78, rise=.08, grow=.12, wob=1),
    # ぶつかる音: 太い字が奥から手前へ大きくなる
    "bumpSoft": dict(size=185, bold=0, fill=(236, 66, 44), edge=12, shadow=16, tilt=10, step=.74, rise=.1, grow=.16, wob=1),
    "bumpHard": dict(size=215, bold=0, fill=(226, 34, 30), edge=13, shadow=22, tilt=9, step=.7, rise=.09, grow=.2, wob=1),
    # ギミック: 水の音は水色、当たる音は黄色、大成功は大きな橙
    "gimmickSplash": dict(size=140, bold=0, fill=(120, 200, 245), edge=10, shadow=10, tilt=10, step=.8, rise=.1, grow=.08, wob=1),
    "gimmickShot": dict(size=120, bold=0, fill=(120, 200, 245), edge=9, shadow=0, tilt=8, step=.8, rise=.06, grow=.1, wob=1),
    "gimmickHit": dict(size=150, bold=0, fill=(255, 216, 70), edge=11, shadow=12, tilt=9, step=.78, rise=.08, grow=.12, wob=1),
    "gimmickMiss": dict(size=150, bold=0, fill=(170, 160, 230), edge=11, shadow=10, tilt=14, step=.8, rise=.14, grow=0, wob=1),
    "gimmickWin": dict(size=190, bold=0, fill=(250, 150, 44), edge=12, shadow=16, tilt=8, step=.76, rise=.08, grow=.14, wob=1),
    "gimmickStrike": dict(size=200, bold=0, fill=(250, 150, 44), edge=13, shadow=18, tilt=7, step=.72, rise=.06, grow=.16, wob=1),
    "gimmickPins": dict(size=160, bold=0, fill=(255, 216, 70), edge=11, shadow=12, tilt=10, step=.78, rise=.1, grow=.1, wob=1),
    "gimmickDomino": dict(size=130, bold=0, fill=(255, 255, 255), edge=10, shadow=0, tilt=12, step=.82, rise=.12, grow=0, wob=1),
    "gimmickBurst": dict(size=170, bold=0, fill=(250, 150, 44), edge=12, shadow=14, tilt=9, step=.76, rise=.08, grow=.12, wob=1),
    "gimmickFlow": dict(size=130, bold=0, fill=(240, 180, 60), edge=10, shadow=0, tilt=6, step=.82, rise=.04, grow=0, wob=1),
    "gimmickFly": dict(size=130, bold=0, fill=(255, 255, 255), edge=10, shadow=0, tilt=4, step=.84, rise=.02, grow=0, wob=1),
    "paintMatch": dict(size=110, bold=0, fill=(255, 140, 180), edge=9, shadow=0, tilt=10, step=.84, rise=.1, grow=0, wob=1),
}


def render_word(text, style, rng):
    """1 字ずつの画像と、言葉の中の位置を返す"""
    s = STYLES[style]
    n = len(text)
    out = []
    x = 0.0
    sign = rng.choice((-1, 1))
    for i, ch in enumerate(text):
        size = int(s["size"] * (1 + s["grow"] * (i - (n - 1) * .35)))
        tilt = rng.uniform(-s["tilt"], s["tilt"]) + sign * 3
        squash = (rng.uniform(.94, 1.08), rng.uniform(.96, 1.1)) if s["wob"] else (1, 1)
        small = ch in "っッゃゅょャュョ"
        if small:
            size = int(size * .72)
        if ch == "ー":
            tilt -= 6
        m = glyph_mask(ch, HEAVY, size, rng, tilt, squash, (2 + size * .012) * s["wob"])
        m = ink(m)
        box = m.getbbox()
        m = m.crop((box[0] - 40, box[1] - 40, box[2] + 40, box[3] + 40))
        w, h = m.size
        layer = paint(m, s, rng)
        # 余白を切り、1 枚の画像へ入るよう縮める
        box = layer.getbbox()
        ox, oy = (box[0] + box[2]) / 2 - layer.size[0] / 2, (box[1] + box[3]) / 2 - layer.size[1] / 2
        layer = layer.crop(box)
        layer = layer.resize((int(layer.size[0] * SHRINK), int(layer.size[1] * SHRINK)), Image.LANCZOS)
        y = math.sin(i * 1.9 + rng.uniform(-.6, .6)) * s["size"] * s["rise"] + (s["size"] * .22 if small else 0)
        out.append((layer, (x + w * .5 + ox) * SHRINK, (y + oy) * SHRINK))
        x += (w - 80) * s["step"] + 26
    mid = (out[0][1] + out[-1][1]) * .5
    return [(g, gx - mid, gy) for g, gx, gy in out]


def paint(mask, s, rng):
    """字の形に、影・縁・色を付ける"""
    w, h = mask.size
    pad = 70
    W, H = w + pad * 2, h + pad * 2
    base = Image.new("L", (W, H), 0)
    base.paste(mask, (pad, pad))
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if s["shadow"]:
        # 右下へ伸びる影
        for k in range(s["shadow"], 0, -1):
            layer.paste(INK + (255,), (k, int(k * 1.15)), base)
    if s["edge"]:
        fat = grow(base, s["edge"])
        layer.paste(INK + (255,), (0, 0), fat)
    color = Image.new("RGBA", (W, H), s["fill"] + (255,))
    # 字の下の方に網点を重ねる
    if s["shadow"]:
        d = ImageDraw.Draw(color)
        r, g, b = s["fill"]
        dark = (int(r * .62), int(g * .5), int(b * .5), 255)
        for yy in range(int(H * .45), H, 15):
            for xx in range(-8 + (yy // 15 % 2) * 8, W, 16):
                rad = 2.2 + 5.2 * (yy / H - .45) / .55
                d.ellipse((xx - rad, yy - rad, xx + rad, yy + rad), fill=dark)
        # 上の縁に残した光
        hi = base.filter(ImageFilter.MinFilter(9))
        shifted = ImageChops.offset(hi, 0, 9)
        gloss = ImageChops.subtract(hi, shifted).filter(ImageFilter.GaussianBlur(1)).point(lambda v: 255 if v > 90 else 0)
        color.paste((255, 244, 214, 255), (0, 0), gloss)
    layer.paste(color, (0, 0), base)
    return layer


def lines_sprite(rng, big):
    """集中線: 長さも太さもばらばらで、片側に寄った線とインクの飛び"""
    S = 1520  # 大きく描いて縮め、線の先をなめらかにする
    image = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    c = S / 2
    n = 30 if big else 20
    for i in range(n):
        a = rng.uniform(0, 6.283)
        lean = 1 + .35 * math.cos(a - .9)
        r0 = c * rng.uniform(.36, .5)
        r1 = c * rng.uniform(.62, .98) * min(1.05, lean)
        w = rng.uniform(10, 30 if big else 22)
        side = (-math.sin(a), math.cos(a))
        rm = r0 + (r1 - r0) * .28
        mid = (c + math.cos(a) * rm, c + math.sin(a) * rm)
        d.polygon([(c + math.cos(a) * r0, c + math.sin(a) * r0), (mid[0] + side[0] * w, mid[1] + side[1] * w),
                   (c + math.cos(a) * r1, c + math.sin(a) * r1), (mid[0] - side[0] * w, mid[1] - side[1] * w)], fill=INK + (255,))
    for _ in range(18 if big else 10):
        a = rng.uniform(0, 6.283)
        r = c * rng.uniform(.5, .95)
        rad = rng.uniform(4, 15)
        x, y = c + math.cos(a) * r, c + math.sin(a) * r
        d.ellipse((x - rad, y - rad, x + rad, y + rad), fill=INK + (255,))
    return image.resize((S // 2, S // 2), Image.LANCZOS)


def arrow_sprite():
    """小さくなった印の青い下矢印: 黒い線と右下の硬い影は HUD の札と同じ"""
    W, H = 400, 480
    shape = Image.new("L", (W, H), 0)
    ImageDraw.Draw(shape).polygon([(130, 40), (250, 40), (250, 230), (340, 230), (190, 420), (40, 230), (130, 230)], fill=255)
    shape = shape.filter(ImageFilter.GaussianBlur(7)).point(lambda v: 255 if v > 127 else 0)
    line = shape.filter(ImageFilter.GaussianBlur(11)).point(lambda v: 255 if v > 8 else 0)
    image = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    image.paste(INK + (255,), (16, 20), line)
    image.paste(INK + (255,), (0, 0), line)
    image.paste((56, 132, 240, 255), (0, 0), shape)
    return image.resize((W // 2, H // 2), Image.LANCZOS)


def build():
    rng = random.Random(2026)
    sprites = []  # (鍵, 画像)
    table = {"sets": {}, "words": {}, "lines": {}}
    for style, words in WORDS.items():
        table["sets"][style] = words
        for word in words:
            parts = render_word(word, style, rng)
            table["words"][word] = []
            for k, (image, x, y) in enumerate(parts):
                sprites.append(((word, k), image))
                table["words"][word].append({"x": round(x, 1), "y": round(y, 1)})
    for key, big in (("lightLines", False), ("heavyLines", True)):
        art = lines_sprite(rng, big)
        sprites.append((("lines", key), art.resize((art.size[0] * 2 // 3, art.size[1] * 2 // 3), Image.LANCZOS)))
    sprites.append((("lines", "arrow"), arrow_sprite()))
    # 高さの順に棚へ詰める
    sprites.sort(key=lambda s: -s[1].size[1])
    atlas = Image.new("RGBA", (ATLAS, ATLAS), (0, 0, 0, 0))
    x = y = shelf = 0
    for key, image in sprites:
        w, h = image.size
        if x + w > ATLAS:
            x, y, shelf = 0, y + shelf + 2, 0
        assert y + h <= ATLAS, "atlas is full"
        atlas.paste(image, (x, y))
        rect = {"u": x, "v": y, "w": w, "h": h}
        if key[0] == "lines":
            table["lines"][key[1]] = rect
        else:
            table["words"][key[0]][key[1]].update(rect)
        x += w + 2
        shelf = max(shelf, h)
    return atlas, table


def preview(atlas, table, path):
    sheet = Image.new("RGBA", (1500, 900), (205, 178, 138, 255))
    d = ImageDraw.Draw(sheet)
    d.rectangle((0, 0, 1500, 450), fill=(232, 224, 205, 255))

    def stamp(word, cx, cy, s):
        s /= SHRINK
        for g in table["words"][word]:
            piece = atlas.crop((g["u"], g["v"], g["u"] + g["w"], g["v"] + g["h"])).resize((int(g["w"] * s), int(g["h"] * s)), Image.LANCZOS)
            sheet.alpha_composite(piece, (int(cx + g["x"] * s - piece.size[0] / 2), int(cy + g["y"] * s - piece.size[1] / 2)))

    def rect(r, cx, cy, s):
        s *= 1.5
        piece = atlas.crop((r["u"], r["v"], r["u"] + r["w"], r["v"] + r["h"])).resize((int(r["w"] * s), int(r["h"] * s)), Image.LANCZOS)
        sheet.alpha_composite(piece, (int(cx - piece.size[0] / 2), int(cy - piece.size[1] / 2)))

    rect(table["lines"]["heavyLines"], 300, 230, .8)
    stamp("ドカーン", 300, 230, .75)
    rect(table["lines"]["lightLines"], 760, 220, .6)
    stamp("ガンッ", 760, 220, .75)
    stamp("グシャッ", 1180, 180, .7)
    stamp("ガブッ", 1180, 340, .55)
    rect(table["lines"]["arrow"], 220, 620, .6)
    stamp("ぱくっ", 560, 560, .55)
    stamp("もぐもぐ", 560, 700, .55)
    stamp("ちゅるん", 900, 560, .55)
    stamp("ゴックン", 920, 720, .55)
    stamp("ごくん", 1260, 600, .6)
    sheet.convert("RGB").save(path)


atlas, table = build()
if "--preview" in sys.argv:
    preview(atlas, table, sys.argv[sys.argv.index("--preview") + 1])
    print("preview only")
else:
    out = ROOT / "Resources/Textures/UI/Comic"
    out.mkdir(parents=True, exist_ok=True)
    atlas.save(out / "atlas.png")
    table["size"] = ATLAS
    (ROOT / "Resources/Data/comic_words.json").write_text(json.dumps(table, ensure_ascii=False, indent=1), encoding="utf-8")
    print("wrote atlas")
