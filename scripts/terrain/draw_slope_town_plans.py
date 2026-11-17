# draw_slope_town_plans.py
# 坂の町の設計案 (slope_town_design.py の JSON) から、平面図・道と勾配の図・擁壁の図・断面図・大きさごとの行ける範囲の図を描く (PIL)。
# 使い方: python scripts/terrain/draw_slope_town_plans.py terrain.json 出力のフォルダ
import json, math, os, sys, collections
from PIL import Image, ImageDraw, ImageFont

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
D = json.load(open(src, encoding="utf-8"))
HALF = D["half"]; N = 2 * HALF + 1
H, K, B = D["height"], D["kind"], D["base"]
S = 4                      # 1m を何ピクセルで描くか
MAP = N * S
FONT = "C:/Windows/Fonts/YuGothB.ttc"
def font(size): return ImageFont.truetype(FONT, size)
F_TITLE, F_BIG, F_MID, F_SMALL = font(40), font(26), font(20), font(16)
PAPER = (247, 244, 236); INK = (40, 38, 44)

def px(x, z):
    """町の座標 (m) → 地図のピクセル (北が上)"""
    return (x + HALF) * S, (HALF - z) * S

def lerp(a, b, t): return tuple(int(a[k] + (b[k] - a[k]) * t) for k in range(3))
def ramp(stops, v):
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if v <= b: return lerp(ca, cb, max(0, min(1, (v - a) / (b - a))))
    return stops[-1][1]
HYPSO = [(-3, (120, 170, 120)), (0, (150, 196, 120)), (10, (186, 210, 132)), (20, (214, 206, 140)), (30, (206, 176, 120)), (40, (180, 150, 110)), (48, (160, 130, 100))]

def shade(i, j):
    """左上から光が当たったときの明るさ (0.6〜1.15)"""
    i0, i1, j0, j1 = max(0, i - 1), min(N - 1, i + 1), max(0, j - 1), min(N - 1, j + 1)
    dx = (H[j][i1] - H[j][i0]) / (i1 - i0); dz = (H[j1][i] - H[j0][i]) / (j1 - j0)
    nx, ny, nz = -dx, 1.0, -dz; L = math.sqrt(nx * nx + ny * ny + nz * nz)
    lx, ly, lz = -0.5, 0.75, 0.45; l = math.sqrt(lx * lx + ly * ly + lz * lz)
    return 0.55 + 0.6 * max(0, (nx * lx + ny * ly + nz * lz) / (L * l))

def kind_color(i, j):
    k = K[j][i]; h = H[j][i]
    if k == 4: return (96, 160, 214)
    if k == 5: return (118, 112, 106)
    if k == 2: return (96, 98, 108)
    if k == 3: return (176, 170, 158)
    if k == 1: return lerp((234, 222, 196), (214, 198, 168), min(1, max(0, h / 45)))
    if k == 6: return lerp(ramp(HYPSO, h), (120, 168, 90), 0.35)
    if k == 7: return (226, 216, 190)
    return ramp(HYPSO, h)

def base_map(contours=True, walls=False, tint=None, edges=False):
    img = Image.new("RGB", (N, N))
    pix = img.load()
    for j in range(N):
        for i in range(N):
            c = tint(i, j) if tint else kind_color(i, j)
            s = shade(i, j)
            pix[i, N - 1 - j] = tuple(min(255, int(v * s)) for v in c)
    img = img.resize((MAP, MAP), Image.NEAREST)
    d = ImageDraw.Draw(img)
    if contours:
        # 2m ごとの等高線 (地面と敷地の所だけ。10m ごとは太く)
        for j in range(N - 1):
            for i in range(N - 1):
                if K[j][i] in (1, 2, 3, 4, 5, 6, 7): continue
                a = H[j][i]
                for (ii, jj) in ((i + 1, j), (i, j + 1)):
                    b = H[jj][ii]
                    if K[jj][ii] in (2, 3, 4, 5): continue
                    lo, hi = min(a, b), max(a, b)
                    if hi - lo > 0.9: continue   # 段差 (擁壁) は等高線にしない
                    for level in range(int(math.floor(lo / 2) + 1) * 2, int(math.floor(hi)) + 1, 2):
                        if lo < level <= hi:
                            x0, y0 = i * S, (N - 1 - j) * S
                            if ii != i: d.line([(x0 + S, y0), (x0 + S, y0 + S)], fill=(90, 84, 70) if level % 10 else (60, 54, 44), width=1 if level % 10 else 2)
                            else: d.line([(x0, y0), (x0 + S, y0)], fill=(90, 84, 70) if level % 10 else (60, 54, 44), width=1 if level % 10 else 2)
    if walls:
        draw_walls(d)
    if edges:
        # 段のへり (擁壁・石垣) を細い線で描く
        for j in range(N - 1):
            for i in range(N - 1):
                for (ii, jj) in ((i + 1, j), (i, j + 1)):
                    if 3 in (K[j][i], K[jj][ii]) or abs(H[j][i] - H[jj][ii]) < 0.6: continue
                    x0, y0 = i * S, (N - 1 - j) * S
                    if ii != i: d.line([(x0 + S, y0), (x0 + S, y0 + S)], fill=(84, 70, 56), width=1)
                    else: d.line([(x0, y0), (x0 + S, y0)], fill=(84, 70, 56), width=1)
    return img

def draw_walls(d, thick=False):
    """擁壁 (隣との高さの差が 0.6m 以上の所) を、高さの差で色分けして描く"""
    for j in range(N - 1):
        for i in range(N - 1):
            for (ii, jj) in ((i + 1, j), (i, j + 1)):
                if 3 in (K[j][i], K[jj][ii]): continue
                diff = abs(H[j][i] - H[jj][ii])
                if diff < 0.6: continue
                col = (250, 220, 60) if diff < 1.5 else (240, 140, 40) if diff < 3 else (210, 40, 40)
                x0, y0 = i * S, (N - 1 - j) * S
                w = 3 if thick else 2
                if ii != i: d.line([(x0 + S, y0 - S // 2 + 2), (x0 + S, y0 + S + S // 2 - 2)], fill=col, width=w)
                else: d.line([(x0 - S // 2 + 2, y0), (x0 + S + S // 2 - 2, y0)], fill=col, width=w)

def draw_houses(d):
    for lot in D["lots"]:
        x, z, yaw = lot["x"], lot["z"], lot["yaw"]
        fx, fz = math.sin(yaw), math.cos(yaw); rx, rz = fz, -fx
        hw, hd = 4.2, 3.6
        pts = [px(x + rx * a + fx * b, z + rz * a + fz * b) for a, b in ((-hw, -hd), (hw, -hd), (hw, hd), (-hw, hd))]
        d.polygon(pts, fill=(196, 120, 90), outline=(90, 50, 40))
        d.line([px(x - rx * hw, z - rz * hw), px(x + rx * hw, z + rz * hw)], fill=(120, 70, 55), width=1)

def label(d, text, x, y, f=F_MID, fill=INK, bg=(255, 255, 255, 220)):
    w = d.textlength(text, font=f); h = f.size
    d.rounded_rectangle([x - w / 2 - 6, y - h / 2 - 5, x + w / 2 + 6, y + h / 2 + 5], radius=6, fill=bg[:3], outline=(80, 80, 80))
    d.text((x - w / 2, y - h / 2 - 2), text, font=f, fill=fill)

def frame(img, title, sub, legend=None, side=360):
    """余白・題名・方位・縮尺・凡例を付けた紙にする"""
    W, Hh = MAP + side + 60, MAP + 150
    page = Image.new("RGB", (W, Hh), PAPER); page.paste(img, (30, 120))
    d = ImageDraw.Draw(page)
    d.text((30, 24), title, font=F_TITLE, fill=INK); d.text((32, 76), sub, font=F_SMALL, fill=(90, 88, 96))
    d.rectangle([30, 120, 30 + MAP, 120 + MAP], outline=INK, width=2)
    # 方位と縮尺
    cx, cy = 30 + MAP - 50, 170
    d.polygon([(cx, cy - 34), (cx - 14, cy + 6), (cx, cy - 2), (cx + 14, cy + 6)], fill=INK); d.text((cx - 8, cy + 8), "北", font=F_MID, fill=INK)
    sx, sy = 50, 120 + MAP - 40
    d.rectangle([sx - 8, sy - 30, sx + 50 * S + 14, sy + 14], fill=(255, 255, 255))
    for k in range(5): d.rectangle([sx + k * 10 * S, sy, sx + (k + 1) * 10 * S, sy + 8], fill=INK if k % 2 == 0 else (255, 255, 255), outline=INK)
    d.text((sx, sy - 26), "0", font=F_SMALL, fill=INK); d.text((sx + 50 * S - 20, sy - 26), "50m", font=F_SMALL, fill=INK)
    if legend:
        lx, ly = 30 + MAP + 30, 130
        d.text((lx, ly), "凡例", font=F_BIG, fill=INK); ly += 44
        for item in legend:
            if item[0] == "text":
                for line in item[1].split("\n"):
                    d.text((lx, ly), line, font=F_SMALL, fill=INK); ly += 24
                ly += 6; continue
            kind, col, text = item
            if kind == "box": d.rectangle([lx, ly, lx + 34, ly + 22], fill=col, outline=(60, 60, 60))
            else: d.line([(lx, ly + 11), (lx + 34, ly + 11)], fill=col, width=5)
            d.text((lx + 46, ly), text, font=F_SMALL, fill=INK); ly += 34
    return page

def road_names(d):
    for r in D["roads"]:
        p = r["path"]; k = len(p) // 2; x, y = px(*p[k])
        label(d, r["name"], x, y, F_SMALL, fill=(255, 255, 255), bg=(60, 62, 72))

def place_names(d):
    for name, x, z in D["places"]:
        X, Y = px(x, z); label(d, name, X, Y, F_MID)

def spawn_mark(d):
    X, Y = px(*D["spawn"]); d.ellipse([X - 12, Y - 12, X + 12, Y + 12], fill=(60, 200, 90), outline=INK, width=3)
    label(d, "スタート", X - 60, Y - 4, F_SMALL)

# ---------- 1. 全体計画図 ----------
img = base_map(contours=False, edges=True); d = ImageDraw.Draw(img)
draw_houses(d); place_names(d); road_names(d); spawn_mark(d)
for s in D["stairs"]:
    (x0, z0), (x1, z1) = s["from"], s["to"]; X0, Y0 = px(x0, z0); X1, Y1 = px(x1, z1)
    d.line([(X0, Y0), (X1, Y1)], fill=(80, 70, 60), width=2)
frame(img, "坂の町 全体計画図 (案)", "南の川と線路から、北の神社の山と団地の台地へ約 45m 上がる斜面の町。道は斜面に沿って曲がりながら上り、道の外は等高線に沿った段 (2m、神社の山は 3m)",
      [("box", lerp((150, 196, 120), (120, 168, 90), .35), "段々畑・庭 (低い)"), ("box", lerp((206, 176, 120), (120, 168, 90), .35), "段々畑・庭 (高い)"), ("box", (228, 214, 186), "家の敷地 (ひな壇)"),
       ("box", (96, 98, 108), "坂道・道路"), ("box", (176, 170, 158), "石段 (近道)"), ("box", (226, 216, 190), "境内 (神社・お寺の広場)"), ("box", (96, 160, 214), "川"), ("box", (118, 112, 106), "線路"),
       ("box", (196, 120, 90), "家"), ("text", "道の数: 8 本 (団々坂・つづら折り\n・商店街・尾根道など)\n石段: 4 本\n家の敷地: %d 区画\n高さ: 川 -2m 〜 神社 45m" % len(D["lots"]))]).save(os.path.join(out, "01_全体計画図.png"))

# ---------- 2. 道と勾配 ----------
def plain(i, j):
    k = K[j][i]
    if k == 4: return (150, 190, 226)
    if k in (2, 3): return (226, 224, 218)
    return lerp((236, 238, 228), (212, 214, 200), min(1, max(0, H[j][i] / 45)))
img = base_map(contours=False, tint=plain); d = ImageDraw.Draw(img)
def grade_color(g):
    return (70, 170, 90) if g < 0.05 else (240, 200, 50) if g < 0.10 else (236, 120, 40) if g < 0.13 else (210, 50, 50)
for r in D["roads"]:
    p, h = r["path"], r["height"]
    for k in range(0, len(p) - 4, 2):
        g = abs(h[k + 4] - h[k]) / 4
        d.line([px(*p[k]), px(*p[k + 4])], fill=grade_color(g), width=int(r["width"] * S * 0.9))
    # 上る向きの矢印
    for k in range(20, len(p) - 10, 40):
        up = h[k + 6] > h[k]; a, b = (p[k], p[k + 6]) if up else (p[k + 6], p[k])
        if abs(h[k + 6] - h[k]) < 0.15: continue
        (X0, Y0), (X1, Y1) = px(*a), px(*b); ang = math.atan2(Y1 - Y0, X1 - X0)
        d.line([(X0, Y0), (X1, Y1)], fill=INK, width=3)
        d.polygon([(X1, Y1), (X1 - 14 * math.cos(ang - .5), Y1 - 14 * math.sin(ang - .5)), (X1 - 14 * math.cos(ang + .5), Y1 - 14 * math.sin(ang + .5))], fill=INK)
for s in D["stairs"]:
    (x0, z0), (x1, z1) = s["from"], s["to"]; L = s["length"]; ux, uz = (x1 - x0) / L, (z1 - z0) / L
    for k in range(0, int(L) + 1, 1):
        a = px(x0 + ux * k - uz * s["width"] / 2, z0 + uz * k + ux * s["width"] / 2); b = px(x0 + ux * k + uz * s["width"] / 2, z0 + uz * k - ux * s["width"] / 2)
        d.line([a, b], fill=(110, 90, 70), width=2)
road_names(d)
for s in D["stairs"]:
    X, Y = px(*s["to"]); label(d, "%s (%d段)" % (s["name"], s["steps"]), X, Y - 22, F_SMALL)
lines = []
for r in D["roads"]:
    h = r["height"]; lines.append("%s  %dm  %.0f→%.0fm" % (r["name"], len(r["path"]), h[0], h[-1]))
frame(img, "道のつながりと勾配", "坂道は勾配の上限を決めて地面に沿わせた (団々坂 13%・つづら折り 15%)。矢印は上る向き。石段は坂を遠回りせずに上れる近道",
      [("line", (70, 170, 90), "ほぼ平ら (5% 未満)"), ("line", (240, 200, 50), "ゆるい坂 (5〜10%)"), ("line", (236, 120, 40), "坂 (10〜13%)"), ("line", (210, 50, 50), "急な坂 (13% 以上)"),
       ("line", (110, 90, 70), "石段"), ("text", "道の長さと高さ:\n" + "\n".join(lines))], side=420).save(os.path.join(out, "02_道と勾配.png"))

# ---------- 3. 高さと擁壁 ----------
img = base_map(contours=True, walls=True); d = ImageDraw.Draw(img)
for (x, z) in [(-20, -96), (-30, -55), (-62, -22), (-18, 6), (-14, 40), (-52, 66), (-70, 96), (82, 92), (116, 26), (78, -34), (24, 44), (-110, 15), (38, -10), (-100, 70)]:
    i, j = int(math.floor(x + .5)) + HALF, int(math.floor(z + .5)) + HALF; X, Y = px(x, z)
    d.ellipse([X - 5, Y - 5, X + 5, Y + 5], fill=INK); label(d, "%.0fm" % H[j][i], X + 30, Y - 14, F_SMALL)
frame(img, "高さと擁壁", "段差は家の敷地のまわりと、道の切り通し・盛り土の所だけにできる。擁壁の高さが、跳び越えるのに要る体の大きさ (直径) になる",
      [("line", (250, 220, 60), "擁壁 0.6〜1.5m"), ("line", (240, 140, 40), "擁壁 1.5〜3m"), ("line", (210, 40, 40), "擁壁 3m 以上"),
       ("text", "黒い点の数字は地面の高さ\n\n小さいうちは坂道と石段で上り、\n大きくなると擁壁を跳び越えて\n敷地から敷地へ近道できる")]).save(os.path.join(out, "03_高さと擁壁.png"))

# ---------- 4・5. 断面図 ----------
def section(ends, title, sub, pts, file):
    """pts は断面の線 (始まりと終わり)。高さは 2 倍に強調して描く"""
    (x0, z0), (x1, z1) = pts; L = math.hypot(x1 - x0, z1 - z0); W = 1700; Hh = 760; sx = (W - 160) / L; sy = sx * 2
    page = Image.new("RGB", (W, Hh), PAPER); d = ImageDraw.Draw(page)
    d.text((40, 24), title, font=F_TITLE, fill=INK); d.text((42, 76), sub, font=F_SMALL, fill=(90, 88, 96))
    base_y = Hh - 120
    def Y(h): return base_y - (h + 4) * sy
    # 目盛り
    for h in range(0, 50, 10):
        d.line([(80, Y(h)), (W - 60, Y(h))], fill=(220, 214, 200)); d.text((40, Y(h) - 10), "%dm" % h, font=F_SMALL, fill=(120, 116, 110))
    prev = None
    samples = []
    for k in range(int(L) + 1):
        x, z = x0 + (x1 - x0) * k / L, z0 + (z1 - z0) * k / L; i, j = int(math.floor(x + .5)) + HALF, int(math.floor(z + .5)) + HALF
        samples.append((k, H[j][i], K[j][i], B[j][i]))
    # 元の地面 (点線)
    for (k, h, kk, b), (k2, h2, kk2, b2) in zip(samples, samples[1:]):
        if k % 2 == 0: d.line([(80 + k * sx, Y(b)), (80 + k2 * sx, Y(b2))], fill=(150, 140, 120), width=1)
    # 地面の断面 (塗り)
    poly = [(80, base_y + 40)] + [(80 + k * sx, Y(h)) for k, h, _, _ in samples] + [(80 + L * sx, base_y + 40)]
    d.polygon(poly, fill=(212, 196, 160))
    cols = {0: (120, 160, 90), 1: (200, 170, 120), 2: (80, 82, 92), 3: (160, 150, 140), 4: (80, 150, 210), 5: (110, 100, 96), 6: (110, 160, 90), 7: (220, 200, 160)}
    for (k, h, kk, _), (k2, h2, _, _) in zip(samples, samples[1:]):
        d.line([(80 + k * sx, Y(h)), (80 + k2 * sx, Y(h2))], fill=cols[kk], width=6)
        if abs(h2 - h) >= 0.6 and 3 not in (kk,):
            d.line([(80 + k2 * sx, Y(h)), (80 + k2 * sx, Y(h2))], fill=(200, 60, 40), width=4)
    # 家 (敷地の上に立つ)
    for lot in D["lots"]:
        # 断面の線からの距離
        vx, vz = x1 - x0, z1 - z0; t = ((lot["x"] - x0) * vx + (lot["z"] - z0) * vz) / (L * L)
        if not 0 <= t <= 1: continue
        dist = abs((lot["x"] - x0) * vz - (lot["z"] - z0) * vx) / L
        if dist > 6: continue
        cx = 80 + t * L * sx; w = 8 * sx; hh = 6.5 * sy
        d.rectangle([cx - w / 2, Y(lot["h"]) - hh, cx + w / 2, Y(lot["h"])], fill=(236, 226, 206), outline=(90, 70, 60), width=2)
        d.polygon([(cx - w / 2 - 6, Y(lot["h"]) - hh), (cx + w / 2 + 6, Y(lot["h"]) - hh), (cx, Y(lot["h"]) - hh - 3 * sy)], fill=(80, 88, 104))
    # 人の大きさ (1m の体)
    d.text((420, Hh - 70), "高さは 2 倍に強調。赤い縦線は擁壁、灰色の太線は道、緑は段々畑・庭、点線は元の地面", font=F_SMALL, fill=INK)
    for place, x, z in D["places"]:
        vx, vz = x1 - x0, z1 - z0; t = ((x - x0) * vx + (z - z0) * vz) / (L * L); dist = abs((x - x0) * vz - (z - z0) * vx) / L
        if 0 <= t <= 1 and dist < 22: label(d, place, 80 + t * L * sx, 150, F_SMALL)
    d.text((70, base_y + 50), ends[0], font=F_BIG, fill=INK); d.text((W - 90, base_y + 50), ends[1], font=F_BIG, fill=INK)
    page.save(os.path.join(out, file))

section(("南", "北"), "断面図 A-A (南北、団々坂ぞい x = -30m)", "川 → 線路 → 駅前通り → 商店街 → 団々坂の曲がり角 → 公園 → 尾根道 → 神社の山", ((-30, -128), (-30, 128)), "04_断面図A_南北.png")
section(("西", "東"), "断面図 B-B (東西、z = 10m)", "西の坂 → 住宅街のひな壇 → 団々坂 → 沢 → 住宅街の道 → つづら折りの折り返し", ((-128, 10), (128, 10)), "05_断面図B_東西.png")

# ---------- 6. 大きさごとに行ける範囲 ----------
SIZES = [(1, (70, 160, 230)), (2, (90, 200, 120)), (4, (240, 200, 60)), (8, (236, 120, 50))]
reach = [[0] * N for _ in range(N)]
si, sj = int(D["spawn"][0]) + HALF, int(D["spawn"][1]) + HALF
TARGETS = [("神社", -78, 100), ("団地の台地", 82, 92)]
trip = {}
for level, (size, _) in reversed(list(enumerate(SIZES))):
    seen = [[False] * N for _ in range(N)]; q = collections.deque([(si, sj)]); seen[sj][si] = True
    dist = {(si, sj): 0}
    climb = max(0.45, size * 0.9)    # 段差ジャンプは体の直径まで (余裕を見て 9 割)
    while q:
        i, j = q.popleft(); reach[j][i] = level + 1
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ii, jj = i + di, j + dj
            if not (0 <= ii < N and 0 <= jj < N) or seen[jj][ii]: continue
            if H[jj][ii] - H[j][i] > climb: continue
            seen[jj][ii] = True; dist[(ii, jj)] = dist[(i, j)] + 1; q.append((ii, jj))
    for name, x, z in TARGETS: trip[(name, size)] = dist.get((x + HALF, z + HALF))
def reach_color(i, j):
    lv = reach[j][i]
    if lv == 0: return (150, 150, 150)
    return lerp(SIZES[lv - 1][1], (255, 255, 255), 0.25 if K[j][i] != 2 else 0)
img = base_map(contours=False, tint=reach_color); d = ImageDraw.Draw(img)
draw_houses(d); place_names(d); spawn_mark(d)
counts = [sum(1 for j in range(N) for i in range(N) if reach[j][i] and reach[j][i] <= lv + 1) for lv in range(len(SIZES))]
frame(img, "大きさごとに行ける範囲", "スタートから、段差ジャンプ (体の直径まで) で上れる所を色分け。家と塀は飲み込むまで通れないので、実際はもう少し狭くなる",
      [("box", lerp(SIZES[0][1], (255, 255, 255), .25), "1m で行ける (%d%%)" % (100 * counts[0] // (N * N))), ("box", lerp(SIZES[1][1], (255, 255, 255), .25), "2m で行ける (%d%%)" % (100 * counts[1] // (N * N))),
       ("box", lerp(SIZES[2][1], (255, 255, 255), .25), "4m で行ける (%d%%)" % (100 * counts[2] // (N * N))), ("box", lerp(SIZES[3][1], (255, 255, 255), .25), "8m で行ける (%d%%)" % (100 * counts[3] // (N * N))),
       ("box", (150, 150, 150), "8m でも行けない"),
       ("text", "降りるのは自由なので、道で上まで\n行けば段を飛び降りてどこへでも\n行ける。大きさが効くのは上りの近道。\n\nスタートからの道のり:\n" + "\n".join(name + "\n  " + "  ".join("%dm体:%s" % (size, ("%dm" % trip[(name, size)]) if trip[(name, size)] else "×") for size, _ in SIZES) for name, _, _ in TARGETS))], side=480).save(os.path.join(out, "06_大きさごとに行ける範囲.png"))
print("done")
