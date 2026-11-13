# finish_collection.py
# render_collection.py の画像を正方形に切り抜き、未発見の影絵と一緒に Resources/Textures/UI/Collection へ置く
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "../Generated/CollectionRaw"
OUT = ROOT / "Resources/Textures/UI/Collection"
OUT.mkdir(parents=True, exist_ok=True)
for path in sorted(RAW.glob("*.png")):
    image = Image.open(path).convert("RGBA")
    image = image.crop(image.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox())
    # 縦横の長い方に合わせた正方形の中央に置く (升目に同じ大きさで並べるため)
    side = max(image.size) + 8
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(image, ((side - image.size[0]) // 2, (side - image.size[1]) // 2))
    square = square.resize((192, 192), Image.LANCZOS)
    square.save(OUT / f"{path.stem}.png")
    shape = Image.new("RGBA", square.size, (255, 255, 255, 0))
    shape.putalpha(square.getchannel("A"))
    shape.save(OUT / f"{path.stem}_sil.png")
    print(path.stem)
