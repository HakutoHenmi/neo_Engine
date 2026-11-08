# make_palette.py
# ローポリの小物が共有する色の画像 (Resources/Textures/Lowpoly/palette.png) を作る。
# 色と将棋の駒の文字は palette.json に書く。使い方: python scripts/lowpoly/make_palette.py
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
spec = json.loads((Path(__file__).parent / "palette.json").read_text(encoding="utf-8"))
size, cell, columns = spec["size"], spec["cell"], spec["columns"]
image = Image.new("RGB", (size, size), (255, 0, 255))
draw = ImageDraw.Draw(image)
for i, rgb in enumerate(spec["colors"].values()):
    x, y = i % columns * cell, i // columns * cell
    draw.rectangle((x, y, x + cell - 1, y + cell - 1), fill=tuple(rgb))
# 駒の文字: 駒の色の上に黒い文字を書く
font = ImageFont.truetype(str(ROOT / "Resources/Fonts/ZenKakuGothicNew/ZenKakuGothicNew-Black.ttf"), int(spec["glyphCell"] * .62))
face = tuple(spec["colors"]["koma"])
for i, glyph in enumerate(spec["glyphs"].values()):
    g = spec["glyphCell"]
    x, y = i % (size // g) * g, spec["glyphRow"] + i // (size // g) * g
    draw.rectangle((x, y, x + g - 1, y + g - 1), fill=face)
    draw.text((x + g / 2, y + g * .56), glyph, font=font, fill=tuple(spec["colors"]["black"]), anchor="mm")
out = ROOT / "Resources/Textures/Lowpoly/palette.png"
out.parent.mkdir(parents=True, exist_ok=True)
image.save(out)
print("wrote", out)
