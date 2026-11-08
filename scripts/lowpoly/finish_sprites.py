# finish_sprites.py
# render_sprites.py の画像を切り抜き、影用の形と一緒に Resources/Textures/UI/Scale へ置く。
# 縦横比を表示するので、Game/UI/ScaleUI.h の Sprite の値に使う
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "../Generated/SpriteRaw"
OUT = ROOT / "Resources/Textures/UI/Scale"
TINT = {"tsumiki": (235, 72, 62)}
for path in sorted(RAW.glob("*.png")):
    image = Image.open(path).convert("RGBA")
    if path.stem in TINT:
        r, g, b, a = image.split()
        t = TINT[path.stem]
        image = Image.merge("RGBA", (r.point(lambda v: v * t[0] // 255), g.point(lambda v: v * t[1] // 255), b.point(lambda v: v * t[2] // 255), a))
    image = image.crop(image.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox())
    image.save(OUT / f"{path.stem}.png")
    shape = Image.new("RGBA", image.size, (255, 255, 255, 0))
    shape.putalpha(image.getchannel("A"))
    shape.save(OUT / f"{path.stem}_sil.png")
    print(path.stem, round(image.size[0] / image.size[1], 3), image.size)
