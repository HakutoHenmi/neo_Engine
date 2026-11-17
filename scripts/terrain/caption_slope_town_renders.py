# caption_slope_town_renders.py
# render_slope_town.py が描いた立体の絵に、題名と説明の帯を付けて設計図のフォルダへ保存する。
# 使い方: python scripts/terrain/caption_slope_town_renders.py 絵のフォルダ 出力のフォルダ
import os, sys
from PIL import Image, ImageDraw, ImageFont

src, out = sys.argv[1], sys.argv[2]
FONT = "C:/Windows/Fonts/YuGothB.ttc"
title_font, text_font = ImageFont.truetype(FONT, 40), ImageFont.truetype(FONT, 20)
SHOTS = [
    ("r07", "07_鳥瞰_南東から", "鳥瞰 (南東から)", "川と線路の谷から、北西の神社の山へ。坂道は斜面に沿って曲がりながら上り、道の外は等高線に沿った段 (段々畑・庭) になる"),
    ("r08", "08_鳥瞰_神社の山から", "鳥瞰 (神社の山から町を見下ろす)", "神社の境内 (一番高い 46m) から、つづら折りと団々坂、その先の線路まで。神社の山は 3m の石垣の段と林"),
    ("r09", "09_目線_団々坂を上る", "目線の高さ (団々坂の下から上を見る)", "1m の体の目線。道はゆるく曲がって上へ消えていき、両側の家は石垣の上の敷地に建つ。道の先が見えないので、上りたくなる"),
    ("r10", "10_つづら折り", "つづら折りの坂 (東の斜面)", "団地の台地へ上る折り返しの坂。折り返しの間の段を跳び越えられる大きさになると、まっすぐ上へ近道できる"),
    ("r11", "11_団々坂の曲がり角", "団々坂の曲がり角 (南西から)", "曲がり角ごとに道の高さが変わり、敷地の石垣が重なって見える。小さいうちは道をたどり、大きくなると石垣を越えていく"),
]
for name, file, title, text in SHOTS:
    img = Image.open(os.path.join(src, name + ".png")).convert("RGB")
    W, H = img.size; band = 120
    page = Image.new("RGB", (W, H + band), (247, 244, 236)); page.paste(img, (0, band))
    d = ImageDraw.Draw(page)
    d.text((30, 18), title, font=title_font, fill=(40, 38, 44)); d.text((32, 74), text, font=text_font, fill=(80, 78, 86))
    page.save(os.path.join(out, file + ".png"))
print("done")
