# finish_billboards.py
# render_billboards.py で撮った絵を切り抜き、1 枚の絵 (4 x 2 升・1 升 256 点) にまとめる。
# 升の中は、物の足元を升の下の辺の真ん中にそろえ、高さか幅のどちらかを升いっぱいにする。
# 升ごとの物の幅と高さ (升に対する割合) は Resources/Data/billboards.json に書く (ゲームが板の大きさを決めるのに使う)
# 使い方: python scripts/lowpoly/finish_billboards.py
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1].parent
RAW = ROOT / "../Generated/BillboardRaw"
OUT = ROOT / "Resources/Textures/Lowpoly/billboards.png"
DATA = ROOT / "Resources/Data/billboards.json"
NAMES = ["ki_maru", "sugi", "sakura", "matsu", "kaki_no_ki", "house_hiraya", "house_kawara", "my_house"]
CELL, COLS, ROWS, PAD = 256, 4, 2, 4

atlas = Image.new("RGBA", (CELL * COLS, CELL * ROWS), (0, 0, 0, 0))
cells = []
for i, name in enumerate(NAMES):
    im = Image.open(RAW / f"{name}.png").convert("RGBA")
    box = im.getchannel("A").point(lambda a: 255 if a > 24 else 0).getbbox()
    im = im.crop(box)
    w, h = im.size
    scale = min((CELL - 2 * PAD) / w, (CELL - PAD) / h)
    im = im.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.LANCZOS)
    cx, cy = (i % COLS) * CELL, (i // COLS) * CELL
    atlas.alpha_composite(im, (cx + (CELL - im.width) // 2, cy + CELL - im.height))
    cells.append({"id": name, "width": im.width / CELL, "height": im.height / CELL})
# (切り抜きの縁が背景の色に混ざらないよう、透明な所の色を近くの物の色で埋める)
px = atlas.load()
for _ in range(4):
    grow = atlas.copy()
    gp = grow.load()
    for y in range(atlas.height):
        for x in range(atlas.width):
            if px[x, y][3] > 0:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < atlas.width and 0 <= ny < atlas.height and px[nx, ny][3] > 0:
                    r, g, b, _ = px[nx, ny]
                    gp[x, y] = (r, g, b, 0)
                    break
    # (色だけ広げ、透明さはそのまま。次の回は広げた色も使う)
    for y in range(atlas.height):
        for x in range(atlas.width):
            if px[x, y][3] == 0 and gp[x, y][:3] != (0, 0, 0):
                px[x, y] = (gp[x, y][0], gp[x, y][1], gp[x, y][2], 1)
for y in range(atlas.height):
    for x in range(atlas.width):
        if px[x, y][3] == 1:
            px[x, y] = (px[x, y][0], px[x, y][1], px[x, y][2], 0)
atlas.save(OUT)
DATA.write_text(json.dumps({"_comment": "積み木の街の遠くの景色の板 (billboards.png の升。4 x 2。幅と高さは升に対する割合)",
                            "columns": COLS, "rows": ROWS, "cells": cells}, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
print("billboards", OUT, len(cells))
