# slope_town_design.py
# 坂の町 (ステージ 2) の地形の設計案。高さの格子 (1m ごと) と、道・石段・家の敷地・川・線路を作る。
# 平らな段を階段でつなぐのではなく、斜面の上を曲がりくねった坂道が上っていく町にする:
#   - 地面はなめらかな斜面 (南の川から北の丘へ約 40m 上がる。北西に神社の山、北東に団地の台地、真ん中に沢)
#   - 坂道は地面に沿って上り、勾配の上限を超える所は切り通し (山側に擁壁) と盛り土 (谷側に擁壁) になる
#   - 家の敷地は道ぞいの小さな平らな段 (ひな壇)。敷地ごとに高さが違い、道との間に石垣ができる
#   - 石段は坂道の近道で、まっすぐ急に上る
# 使い方: python scripts/terrain/slope_town_design.py 出力の JSON (省略時は Docs/SlopeTownDesign/terrain.json) [--game ゲーム用の JSON]
#   --game を付けると、ゲームが読む地形 (Resources/Data/slope_town_terrain.json) も書き出す
# 図は draw_slope_town_plans.py、立体の絵は render_slope_town.py (Blender) が、この JSON から作る
import json, math, os, sys

HALF = 128          # 町の半分の大きさ (m)。格子は -HALF..HALF の 1m ごと
N = HALF * 2 + 1

def smoothstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)

def gauss(x, z, cx, cz, r):
    return math.exp(-((x - cx) ** 2 + (z - cz) ** 2) / (r * r))

def river_z(x):
    return -117 + 4 * math.sin(x / 26) + 2 * math.sin(x / 9 + 1)

def base_height(x, z):
    """道や敷地を作る前の自然の地面の高さ (m)"""
    # 南から北へ上がる斜面。下はゆるく、真ん中が急で、上はまたゆるい
    h = 34 * smoothstep(-108, 112, z) ** 1.15
    # 北西の神社の山・北東の台地 (団地)・東の線路のトンネルの山
    h += 13 * gauss(x, z, -78, 100, 38)
    plateau = gauss(x, z, 82, 92, 46)
    h = h * (1 - plateau) + 31 * plateau
    h += 16 * gauss(x, z, 150, -96, 40)
    # 真ん中の沢: 北から南東へ下る小さな谷
    t = max(0.0, min(1.0, (60 - z) / 150))
    cx = 15 + 25 * t + 6 * math.sin(z / 18)
    if z < 70:
        h -= 4.5 * math.exp(-((x - cx) / 9) ** 2) * smoothstep(-100, -40, z) * (1 - smoothstep(55, 75, z))
    # 西のゆるい尾根
    h += 4 * gauss(x, z, -125, -20, 40)
    # わずかな起伏
    h += 0.7 * math.sin(x / 13 + z / 29) + 0.5 * math.sin(z / 11 - x / 23)
    # 川の谷
    d = abs(z - river_z(x))
    h = min(h, -1.8 + 0.25 * max(0.0, d - 6) ** 1.3) if d < 30 else h
    return h

# ---- 道: 通る点・幅・勾配の上限・種類 ----
# kind: avenue (駅前通り) / main (団々坂) / shop (商店街) / zigzag (つづら折り) / street (住宅街の道) / ridge (尾根道)
ROADS = [
    {"id": "ekimae", "name": "駅前通り", "kind": "avenue", "width": 8, "grade": 0.04,
     "points": [(-140, -97), (-60, -95), (0, -96), (60, -93), (140, -90)]},
    {"id": "dandanzaka", "name": "団々坂", "kind": "main", "width": 7, "grade": 0.13,
     "points": [(-8, -95), (-14, -75), (-30, -55), (-55, -38), (-62, -22), (-48, -5), (-18, 6), (-4, 22), (-14, 40), (-38, 54), (-52, 66)]},
    {"id": "shotengai", "name": "商店街", "kind": "shop", "width": 7, "grade": 0.06,
     "points": [(-140, -66), (-90, -62), (-38, -55), (0, -60), (40, -58), (70, -52), (140, -44)]},
    {"id": "tsuzura", "name": "つづら折り", "kind": "zigzag", "width": 6, "grade": 0.15,
     "points": [(70, -52), (78, -34), (114, -22), (116, -14), (80, -2), (78, 6), (114, 18), (116, 26), (82, 38), (80, 46), (100, 62)]},
    {"id": "naka", "name": "住宅街の道", "kind": "street", "width": 6, "grade": 0.10,
     "points": [(-140, -12), (-100, -10), (-62, -22)]},
    {"id": "naka2", "name": "住宅街の道 (東)", "kind": "street", "width": 6, "grade": 0.10,
     "points": [(-18, 6), (20, 12), (50, 8), (80, -2)]},
    {"id": "one", "name": "尾根道", "kind": "ridge", "width": 6, "grade": 0.12,
     "points": [(-52, 66), (-20, 82), (20, 90), (60, 84), (100, 62)]},
    {"id": "nishi", "name": "西の坂", "kind": "street", "width": 5, "grade": 0.14,
     "points": [(-100, -10), (-110, 15), (-96, 36), (-108, 56), (-100, 76)]},
]
# 石段: 始まり・終わり・幅 (坂道の近道。まっすぐ急に上る)
STAIRS = [
    {"id": "jinja", "name": "神社の石段", "from": (-52, 66), "to": (-71, 90), "width": 5},
    {"id": "tera", "name": "お寺の石段", "from": (20, 12), "to": (22, 34), "width": 4},
    {"id": "roji", "name": "路地の石段", "from": (-38, -55), "to": (-40, -30), "width": 3},
    {"id": "roji2", "name": "沢の石段", "from": (40, -58), "to": (44, -30), "width": 3},
]
# 境内: 平らな広場 (名前・中心・半径)。高さは中心の地面。石段がここへ上がる
PLAZAS = [("神社の境内", -78, 100, 12), ("お寺の境内", 24, 44, 10), ("公園", 62, 20, 9)]
# 道に面していて、道と同じ高さにする広場 (道から入れるように)。ほかの境内は石段で上がる
ROAD_LEVEL_PLAZAS = {"公園"}
# 場所の名前 (図の文字)
PLACES = [
    ("川", -40, -120), ("線路", 90, -104), ("駅", -50, -103), ("踏切", 4, -104), ("トンネル", 120, -104),
    ("商店街", -80, -70), ("銭湯", 10, -70), ("団々坂", -40, -44), ("住宅街", -95, -30), ("沢", 38, -10),
    ("お寺", 24, 44), ("墓地", 48, 40), ("神社の山", -78, 104), ("団地の台地", 82, 94), ("給水塔", 112, 108), ("公園", 62, 20),
]
# 開始の位置
SPAWN = (-20, -96)
# 段々畑・庭の段にする高さの下限 (これより低い所はなだらかな地面のまま)
TERRACE_FROM = 2.0
# 道・石段・境内のまわりで、家の敷地を作らない幅 (m)
ROAD_KEEP, STAIRS_KEEP, PLAZA_KEEP = 1.2, 2.5, 1.0
# 石段の両側の平らな肩の幅 (m)。ゲームの石段の箱もこの幅まで広げる (SlopeTownRules.h の SlopeStairsOverlap と同じ値)
STAIRS_SHOULDER = 1.5
# 道の横の擁壁の高さの上限 (m)。これより高い切り通し・盛り土は、道から離れるほど 1m あたり SIDE_GRADE m ずつ変わるなだらかな斜面にする
CUT_MAX, SIDE_GRADE, SIDE_REACH = 3.0, 0.6, 14
# 道の両側の平らな路肩の幅 (m)。地面との差が VERGE_MAX m より小さい所だけ、道と同じ高さにする
VERGE_WIDTH, VERGE_MAX = 1.5, 1.0
# 家の敷地と道の高さの差の上限 (m)。敷地は道より LOT_BELOW m までしか下げない (それより低い地面は盛る。1m の体が落ちて出られない庭を作らない)
LOT_MAX_STEP, LOT_BELOW = 2.5, 1.0
# 目印の建物 (slope_town.json の landmark) の足元に作る平らな台の、建物のまわりの余白 (m)
LANDMARK_MARGIN = 1.5
# 道が交わる所で、2 本の道の高さをまぜる幅 (m)。一番近い道の高さだけにすると、交差点に段ができる
JUNCTION_BLEND = 3.0
# 埋める小さなくぼみの大きさの上限 (マスの数)。まわりを全部高い地面に囲まれた、これ以下の大きさのくぼみは、まわりの一番低い高さまで埋める
HOLE_MAX = 80
# 埋める細い溝の幅の上限 (マス)。両側をこれ以内の距離で高い地面に挟まれた溝 (敷地と敷地の間・石段の横) は、低い方の側の高さまで埋める
TRENCH_MAX = 3
# 溝の底とみなす高さのそろい方 (m。道の横のなだらかな斜面で少し傾いた溝の底も同じ溝とする)
TRENCH_LEVEL = 0.3
# 線路の幅の半分 (m)。踏切が線路の土手の上に立つ幅にする
RAIL_HALF = 3
# 線路の北側で、線路の高さまで盛る土手の北の端 (z)
EMBANKMENT_TO = -96

def resample(points, step=1.0):
    out = [points[0]]
    for (x0, z0), (x1, z1) in zip(points, points[1:]):
        L = math.hypot(x1 - x0, z1 - z0); n = max(1, int(L / step))
        for k in range(1, n + 1):
            out.append((x0 + (x1 - x0) * k / n, z0 + (z1 - z0) * k / n))
    return out

def chaikin(points, rounds=3):
    """角を丸める (曲がりくねった道にする)"""
    for _ in range(rounds):
        q = [points[0]]
        for (x0, z0), (x1, z1) in zip(points, points[1:]):
            q.append((0.75 * x0 + 0.25 * x1, 0.75 * z0 + 0.25 * z1)); q.append((0.25 * x0 + 0.75 * x1, 0.25 * z0 + 0.75 * z1))
        q.append(points[-1]); points = q
    return points

def profile(path, grade, pins):
    """道の高さ: 地面に沿わせてならし、勾配の上限に収める。pins は {番号: 高さ} で固定する点"""
    h = [base_height(x, z) for x, z in path]
    w = 12
    sm = [sum(h[max(0, i - w):i + w + 1]) / len(h[max(0, i - w):i + w + 1]) for i in range(len(h))]
    for i, v in pins.items(): sm[i] = v
    # 固定点の間は、ならした高さを固定点に合わせて直線で補正する
    keys = sorted(pins)
    if keys:
        for a, b in zip([0] + keys, keys + [len(sm) - 1]):
            da = (pins[a] - sm[a]) if a in pins else 0; db = (pins[b] - sm[b]) if b in pins else 0
            for i in range(a, b + 1):
                t = (i - a) / max(1, b - a); sm[i] += da * (1 - t) + db * t
    # 点と点の間の長さ (曲がり角の近くは 1m より短い所がある。勾配はこの長さで決める)
    gap = [0.0] + [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(path, path[1:])]
    for _ in range(4):
        for i in range(1, len(sm)):
            if i in pins: continue
            g = grade * gap[i]
            sm[i] = min(max(sm[i], sm[i - 1] - g), sm[i - 1] + g)
        for i in range(len(sm) - 2, -1, -1):
            if i in pins: continue
            g = grade * gap[i + 1]
            sm[i] = min(max(sm[i], sm[i + 1] - g), sm[i + 1] + g)
    return sm

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
def model_bounds(id):
    """モデル (Resources/Models/Lowpoly/id.obj) の範囲 (cm)"""
    lo = [1e9] * 3; hi = [-1e9] * 3
    for line in open(os.path.join(ROOT, "Resources/Models/Lowpoly", id + ".obj")):
        if line.startswith("v "):
            v = list(map(float, line.split()[1:4]))
            for k in range(3): lo[k] = min(lo[k], v[k]); hi[k] = max(hi[k], v[k])
    return lo, hi
def landmark_footprints():
    """slope_town.json の目印の建物 (landmark) の上から見た範囲 (m)。ゲームの置き方 (向き・fit・mult) と同じに計算する"""
    data = json.load(open(os.path.join(ROOT, "Resources/Data/slope_town.json"), encoding="utf-8")); out = []
    for p in data["props"]:
        if not p.get("landmark"): continue
        lo, hi = model_bounds(p["id"]); sx, sz = (hi[0] - lo[0]) / 100, (hi[2] - lo[2]) / 100
        scale = p["fit"] / max(sx, sz) if p.get("fit") else p.get("mult", 1)
        yaw = p.get("yaw", 0) * math.pi / 180; cs, sn = math.cos(yaw), math.sin(yaw); x, z = p["at"]
        mx, mz = (lo[0] + hi[0]) / 200 * scale, (lo[2] + hi[2]) / 200 * scale; cx, cz = x + mx * cs + mz * sn, z - mx * sn + mz * cs
        hx, hz = .5 * scale * (abs(cs) * sx + abs(sn) * sz), .5 * scale * (abs(sn) * sx + abs(cs) * sz)
        out.append({"id": p["id"], "rect": (cx - hx, cz - hz, cx + hx, cz + hz)})
    return out

def build():
    H = [[base_height(i - HALF, j - HALF) for i in range(N)] for j in range(N)]   # H[j][i]、i が x、j が z
    base = [row[:] for row in H]
    K = [[0] * N for _ in range(N)]     # 0 地面 / 1 敷地 / 2 道 / 3 石段 / 4 川 / 5 線路 / 6 段々畑・庭 / 7 境内
    R = [[-1] * N for _ in range(N)]    # 道の番号
    # (round は 0.5 を偶数へ丸めて、1 つおきに抜けるので使わない)
    def cell(x, z): return int(math.floor(x + 0.5)) + HALF, int(math.floor(z + 0.5)) + HALF
    def inside(i, j): return 0 <= i < N and 0 <= j < N
    # ---- 道の線と高さ ----
    roads = []
    for r in ROADS:
        path = resample(chaikin(list(r["points"])), 1.0)
        pins = {}
        # 先に作った道の上から始まる・終わる道は、その高さに合わせる
        for end, idx in ((path[0], 0), (path[-1], len(path) - 1)):
            for o in roads:
                best = min(range(len(o["path"])), key=lambda k: (o["path"][k][0] - end[0]) ** 2 + (o["path"][k][1] - end[1]) ** 2)
                if math.hypot(o["path"][best][0] - end[0], o["path"][best][1] - end[1]) < 4: pins[idx] = o["height"][best]; break
        # 駅前通りは線路と同じ高さで平ら
        height = [1.2] * len(path) if r["id"] == "ekimae" else profile(path, r["grade"], pins)
        roads.append({**r, "path": path, "height": height})
    # ---- 川と線路 ----
    for x in range(-HALF, HALF + 1):
        zc = river_z(x)
        for dz in range(-7, 8):
            i, j = cell(x, zc + dz)
            if inside(i, j) and abs(dz) <= 5: H[j][i] = -2.2; K[j][i] = 4
    rail_z = -104
    for x in range(-HALF, HALF + 1):
        for dz in range(-RAIL_HALF, RAIL_HALF + 1):
            i, j = cell(x, rail_z + dz)
            if inside(i, j): H[j][i] = 1.6; K[j][i] = 5
    # ---- 道ごとに、各マスから一番近い道の点 (距離と、その点の高さ) ----
    # 道の高さは、マスに一番近い点の高さにする (以前は最後に調べた点の高さで、道を横切る向きにのこぎり状の段ができていた)
    near = [[(1e9, 0.0, -1)] * N for _ in range(N)]   # (距離, 高さ, 道の番号)
    near2 = [[(1e9, 0.0, -1)] * N for _ in range(N)]  # 2 番目に近い、別の道
    for n, rd in enumerate(roads):
        reach = rd["width"] / 2 + max(ROAD_KEEP, VERGE_WIDTH, SIDE_REACH) + 1
        for k, (px, pz) in enumerate(rd["path"]):
            ci, cj = cell(px, pz); r = int(reach) + 1
            for j in range(cj - r, cj + r + 1):
                for i in range(ci - r, ci + r + 1):
                    if not inside(i, j): continue
                    d = math.hypot(i - HALF - px, j - HALF - pz) - rd["width"] / 2   # 道の端からの距離 (道の中は負)
                    if d < near[j][i][0]:
                        if near[j][i][2] != n: near2[j][i] = near[j][i]
                        near[j][i] = (d, rd["height"][k], n)
                    elif near[j][i][2] != n and d < near2[j][i][0]: near2[j][i] = (d, rd["height"][k], n)
    # ---- 道・石段・境内の場所を先に決めておく (あとで作る道や石段が家の敷地を横切らないように) ----
    keep = [[False] * N for _ in range(N)]
    for j in range(N):
        for i in range(N):
            if near[j][i][0] <= ROAD_KEEP: keep[j][i] = True
    for s in STAIRS:
        (x0, z0), (x1, z1) = s["from"], s["to"]; L = math.hypot(x1 - x0, z1 - z0); ux, uz = (x1 - x0) / L, (z1 - z0) / L
        for j in range(N):
            for i in range(N):
                dx, dz = i - HALF - x0, j - HALF - z0; a = dx * ux + dz * uz; b = -dx * uz + dz * ux
                if -STAIRS_KEEP <= a <= L + STAIRS_KEEP and abs(b) <= s["width"] / 2 + STAIRS_KEEP: keep[j][i] = True
    for name, px0, pz0, r in PLAZAS:
        for j in range(N):
            for i in range(N):
                if math.hypot(i - HALF - px0, j - HALF - pz0) <= r + PLAZA_KEEP: keep[j][i] = True
    # ---- 目印の建物 (神社・お寺・銭湯・団地など) の平らな台: 建物の大きさ (モデル) に余白を足した範囲を、中の地面の平均の高さで平らにする ----
    # (以前は斜面の上にそのまま置いて、建物の下にすき間ができたり、半分埋まったりしていた)
    for lm in landmark_footprints():
        x0, z0, x1, z1 = lm["rect"]; i0, j0 = cell(x0 - LANDMARK_MARGIN, z0 - LANDMARK_MARGIN); i1, j1 = cell(x1 + LANDMARK_MARGIN, z1 + LANDMARK_MARGIN)
        cells = [(i, j) for j in range(j0, j1 + 1) for i in range(i0, i1 + 1) if inside(i, j) and near[j][i][0] > 0 and K[j][i] in (0,)]
        if not cells: continue
        hh = round(sum(base[j][i] for i, j in cells) / len(cells) * 2) / 2
        # 境内に重なる建物 (神社・お寺) は、境内と同じ高さにする (別々の高さにすると、建物の下に段ができる)
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
        for name, px0, pz0, r in PLAZAS:
            if math.hypot(cx - px0, cz - pz0) <= r + max(x1 - x0, z1 - z0) / 2: pi, pj = cell(px0, pz0); hh = round(base[pj][pi] * 2) / 2
        for i, j in cells: H[j][i] = hh; K[j][i] = 1; keep[j][i] = True
        for j in range(j0 - 1, j1 + 2):
            for i in range(i0 - 1, i1 + 2):
                if inside(i, j): keep[j][i] = True
    # ---- 家の敷地 (ひな壇): 道ぞいに、道の両側へ奥行き 11m・間口 12m の平らな段 ----
    lots = []
    def lot_cells(cx, cz, ux, uz, along, depth):
        """道に沿う向き (ux, uz)・外への向きに広がる長方形の中の格子 (回した長方形を 1 マスずつ調べて、すき間を作らない)"""
        vx, vz = -uz, ux
        corners = [(cx + ux * a + vx * b, cz + uz * a + vz * b) for a in (-along / 2, along / 2) for b in (0, depth)]
        i0, j0 = cell(min(c[0] for c in corners), min(c[1] for c in corners)); i1, j1 = cell(max(c[0] for c in corners), max(c[1] for c in corners))
        out = []
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                dx, dz = i - HALF - cx, j - HALF - cz; a = dx * ux + dz * uz; b = dx * vx + dz * vz
                if abs(a) <= along / 2 and 0 <= b <= depth: out.append((i - HALF, j - HALF))
        return out
    for rd in roads:
        if rd["kind"] in ("avenue",): spacing = 14
        else: spacing = 13
        path = rd["path"]; w = rd["width"] / 2
        for k in range(6, len(path) - 6, spacing):
            (x0, z0), (x1, z1) = path[k - 3], path[k + 3]
            L = math.hypot(x1 - x0, z1 - z0) or 1; ux, uz = (x1 - x0) / L, (z1 - z0) / L
            for side in (1, -1):
                vx, vz = -uz * side, ux * side
                cx, cz = path[k][0] + vx * (w + 1.5), path[k][1] + vz * (w + 1.5)
                pts = lot_cells(cx, cz, ux * side, uz * side, 11, 10)
                cells = []
                ok = True
                for px, pz in pts:
                    i, j = cell(px, pz)
                    if not inside(i, j) or K[j][i] != 0 or keep[j][i]: ok = False; break
                    cells.append((i, j))
                if not ok: continue
                # 道から近すぎる別の道があれば作らない
                mean = sum(base[j][i] for i, j in cells) / len(cells)
                road_h = rd["height"][k]
                # 敷地の高さは地面の平均を 0.5m 刻みにする (道より LOT_MAX_STEP m 以上離れる所は作らない: 崖になる)
                h = max(round(mean * 2) / 2, math.ceil((road_h - LOT_BELOW) * 2) / 2)
                if h - road_h > LOT_MAX_STEP: continue
                for i, j in cells: H[j][i] = h; K[j][i] = 1
                lots.append({"x": cx + vx * 5, "z": cz + vz * 5, "yaw": math.atan2(-vx, -vz), "h": h, "road": rd["id"], "w": 11, "d": 10})
    # ---- 残りの斜面は段々畑・庭・空き地の段にする (等高線に沿った段。段差が大きさの関門になる) ----
    # 段の高さは 2m (神社の山は石垣の 3m)。小さいうちは道と石段しか通れず、大きくなると段を跳び越えて近道できる
    for j in range(N):
        for i in range(N):
            if K[j][i] != 0: continue
            x, z = i - HALF, j - HALF
            # 谷の低い所 (TERRACE_FROM m より下) は段にしない: 段の刻みで 0m より下へ落ちて、道の横に出られない穴ができていた。
            # 川の岸もなだらかな斜面のまま残して、川へ落ちても上がれるようにする
            if H[j][i] < TERRACE_FROM: continue
            step = 3.0 if gauss(x, z, -78, 100, 38) > 0.35 else 2.0
            H[j][i] = math.floor(H[j][i] / step) * step
            K[j][i] = 6
    # ---- 線路と駅前通りの間の土手: 間の低い地面を線路の高さまで盛る (道の横に、上がれない溝ができないように) ----
    for j in range(N):
        z = j - HALF
        if not (rail_z + RAIL_HALF <= z <= EMBANKMENT_TO): continue
        for i in range(N):
            if K[j][i] in (0, 6) and H[j][i] < 1.6: H[j][i] = 1.6; K[j][i] = 0
    # ---- 境内 (平らな広場。段々畑より後、道より前) ----
    for name, px0, pz0, r in PLAZAS:
        ci, cj = cell(px0, pz0); hh = round(base[cj][ci] * 2) / 2
        if name in ROAD_LEVEL_PLAZAS: hh = near[cj][ci][1]
        for j in range(cj - r, cj + r + 1):
            for i in range(ci - r, ci + r + 1):
                if inside(i, j) and math.hypot(i - ci, j - cj) <= r: H[j][i] = hh; K[j][i] = 7
    # ---- 道の横の切り通し・盛り土をなだらかにする: 擁壁は CUT_MAX m まで。それより高い所は、道から離れるほど上がる (下がる) 斜面にする ----
    # (以前は道が斜面に深く切り込んで、谷のような道になっていた)
    for j in range(N):
        for i in range(N):
            d, rh, n = near[j][i]
            if n < 0 or d <= 0 or d > SIDE_REACH or K[j][i] not in (0, 6): continue
            rise = max(0.0, d - 1) * SIDE_GRADE; top, bottom = rh + CUT_MAX + rise, rh - CUT_MAX - rise
            if H[j][i] > top: H[j][i] = top; K[j][i] = 0
            elif H[j][i] < bottom: H[j][i] = bottom; K[j][i] = 0
    # ---- 道を地面に刻む (一番近い道の点の高さ)。両側の路肩は、地面との差が小さければ道と同じ高さの平らな帯にする ----
    for j in range(N):
        for i in range(N):
            d, rh, n = near[j][i]
            if n < 0: continue
            if d <= 0:
                if K[j][i] in (4, 5) and roads[n]["kind"] != "main": continue
                # 交差点: 別の道が近ければ、2 本の道の高さを距離でまぜる (同じ距離なら半分ずつ)
                d2, rh2, n2 = near2[j][i]
                if n2 >= 0 and d2 - d < JUNCTION_BLEND:
                    w = 0.5 + 0.5 * (d2 - d) / JUNCTION_BLEND; rh = rh * w + rh2 * (1 - w)
                H[j][i] = rh; K[j][i] = 2; R[j][i] = n
            elif d <= VERGE_WIDTH and K[j][i] in (0, 6) and abs(H[j][i] - rh) < VERGE_MAX:
                H[j][i] = rh; K[j][i] = 0
    # ---- 細い溝を埋める: 両側を高い地面に挟まれた幅 TRENCH_MAX マスまでの溝 (敷地と敷地の間・石段の横に残った段々畑) を、低い方の側の高さまで上げる ----
    def trench_pass():
        changed = 0
        for j in range(N):
            for i in range(N):
                if K[j][i] not in (0, 6): continue
                h = H[j][i]; best = None
                for di, dj in ((1, 0), (0, 1)):
                    sides = []
                    for s in (1, -1):
                        found = None
                        for k in range(1, TRENCH_MAX + 1):
                            a, b = i + di * k * s, j + dj * k * s
                            if not inside(a, b): break
                            if K[b][a] in (0, 6) and abs(H[b][a] - h) < TRENCH_LEVEL: continue
                            found = H[b][a]; break
                        sides.append(found)
                    if None not in sides and min(sides) > h + .5: best = max(best or -1e9, min(sides))
                if best is not None: H[j][i] = best; K[j][i] = 0; changed += 1
        return changed
    for _ in range(4):
        if not trench_pass(): break
    # ---- 小さなくぼみを埋める: 道を刻んだあとに残った、まわりより低い 1〜数マスの地面 (道の横の穴) を、まわりの一番低い高さまで上げる ----
    for _ in range(3):
        seen = [[False] * N for _ in range(N)]
        for j in range(N):
            for i in range(N):
                if seen[j][i] or K[j][i] not in (0, 6): continue
                h = H[j][i]; group = [(i, j)]; seen[j][i] = True; q = [(i, j)]; edge = 1e9; ok = True
                while q:
                    a, b = q.pop()
                    for da, db in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        c, e = a + da, b + db
                        if not inside(c, e): continue   # 町の端は塀なので、高い地面として扱う
                        if K[e][c] in (0, 6) and abs(H[e][c] - h) < .05:
                            if not seen[e][c]: seen[e][c] = True; group.append((c, e)); q.append((c, e))
                        else: edge = min(edge, H[e][c])
                if ok and len(group) <= HOLE_MAX and edge > h + .05 and edge < 1e8:
                    for a, b in group: H[b][a] = edge; K[b][a] = 0
    # ---- 石段 (まっすぐ、両端の高さを結ぶ) ----
    stairs = []
    for s in STAIRS:
        (x0, z0), (x1, z1) = s["from"], s["to"]
        # 石段の両端が道に入っていたら、石段の幅の中のマスが道でなくなる所まで縮める。
        # 道の中まで石段の箱を描くと、道の上に段の板がはみ出し、道の高さも段の高さになっていた
        L0 = math.hypot(x1 - x0, z1 - z0); ux0, uz0 = (x1 - x0) / L0, (z1 - z0) / L0
        def on_road(a):
            for k in range(-4, 5):
                b = k / 4 * s["width"] / 2; i, j = cell(x0 + ux0 * a - uz0 * b, z0 + uz0 * a + ux0 * b)
                if inside(i, j) and K[j][i] == 2: return True
            return False
        a0 = 0.0
        while a0 < L0 / 2 and on_road(a0): a0 += .5
        a1 = L0
        while a1 > L0 / 2 and on_road(a1): a1 -= .5
        # (縮めた端の高さは、すぐ外の道の高さにする。端のマスの高さだと、切り通しの上の高さになって道から段の壁が立っていた)
        ir0, jr0 = cell(x0 + ux0 * (a0 - .5), z0 + uz0 * (a0 - .5)); ir1, jr1 = cell(x0 + ux0 * (a1 + .5), z0 + uz0 * (a1 + .5))
        x0, z0, x1, z1 = round(x0 + ux0 * a0, 2), round(z0 + uz0 * a0, 2), round(x0 + ux0 * a1, 2), round(z0 + uz0 * a1, 2)
        s = {**s, "from": (x0, z0), "to": (x1, z1)}
        i0, j0 = cell(x0, z0); i1, j1 = cell(x1, z1)
        h0, h1 = H[j0][i0], H[j1][i1]
        if a0 > 0 and K[jr0][ir0] == 2: h0 = H[jr0][ir0]
        if a1 < L0 and K[jr1][ir1] == 2: h1 = H[jr1][ir1]
        for name, px0, pz0, r in PLAZAS:
            if math.hypot(x1 - px0, z1 - pz0) <= r + 2: ci, cj = cell(px0, pz0); h1 = H[cj][ci]
        L = math.hypot(x1 - x0, z1 - z0); ux, uz = (x1 - x0) / L, (z1 - z0) / L
        steps = max(4, int(abs(h1 - h0) / 0.18))
        # 石段の幅に、両側の平らな肩 (STAIRS_SHOULDER m) を足した範囲を段の高さにする。高さはゲームの石段の箱と同じ決まり
        # (段 k の上の面 = h0 + (h1 - h0) * (k + 1) / steps)。肩が無いと、斜めの石段の横で段々畑の角が段の上へはみ出していた
        reach = s["width"] / 2 + STAIRS_SHOULDER
        ia, ja = cell(min(x0, x1) - reach - 1, min(z0, z1) - reach - 1); ib, jb = cell(max(x0, x1) + reach + 1, max(z0, z1) + reach + 1)
        for j in range(ja, jb + 1):
            for i in range(ia, ib + 1):
                if not inside(i, j) or K[j][i] == 2: continue
                dx, dz = i - HALF - x0, j - HALF - z0; a = dx * ux + dz * uz; b = -dx * uz + dz * ux
                if not (0 <= a <= L and abs(b) <= reach): continue
                k = min(steps - 1, math.floor(a / L * steps)); H[j][i] = h0 + (h1 - h0) * (k + 1) / steps; K[j][i] = 3
        # 上の端は地面に合わせる
        stairs.append({**s, "h0": h0, "h1": h1, "steps": steps, "length": L})
    return {"half": HALF, "height": H, "base": base, "kind": K,
            "roads": [{k: v for k, v in r.items() if k != "points"} for r in roads], "stairs": stairs, "lots": lots,
            "places": PLACES, "spawn": SPAWN, "rail_z": rail_z, "river": [river_z(x) for x in range(-HALF, HALF + 1)],
            "plazas": [{"name": n, "x": x, "z": z, "r": r, "h": H[cell(x, z)[1]][cell(x, z)[0]]} for n, x, z, r in PLAZAS]}

def export_game(data, path):
    """ゲーム用の地形: 高さ (cm の整数、z の行ごと)・マスの種類 (数字の文字列)・道 (2m ごとの点と高さ)・石段・家の敷地・境内"""
    N = 2 * data["half"] + 1
    game = {
        "_comment": "坂の町の地形 (scripts/terrain/slope_town_design.py --game が書き出す。手で直さない)。長さはメートル。heights は 1m のマスの高さ (cm)、z の小さい行から順に x の小さい順。kinds はマスの種類の数字 (0 地面 / 1 家の敷地 / 2 道 / 3 石段 / 4 川 / 5 線路 / 6 段々畑・庭 / 7 境内)",
        "half": data["half"],
        "heights": [int(round(h * 100)) for row in data["height"] for h in row],
        "kinds": "".join(str(k) for row in data["kind"] for k in row),
        "roads": [{"id": r["id"], "name": r["name"], "kind": r["kind"], "width": r["width"],
                   "path": [[round(x, 2), round(z, 2), round(h, 2)] for (x, z), h in list(zip(r["path"], r["height"]))[::2] + [(r["path"][-1], r["height"][-1])]]} for r in data["roads"]],
        "stairs": [{"id": s["id"], "name": s["name"], "from": list(s["from"]), "to": list(s["to"]), "width": s["width"], "h0": round(s["h0"], 2), "h1": round(s["h1"], 2), "steps": s["steps"]} for s in data["stairs"]],
        "lots": [{"x": round(l["x"], 2), "z": round(l["z"], 2), "yaw": round(l["yaw"], 4), "h": l["h"], "road": l["road"], "w": l["w"], "d": l["d"]} for l in data["lots"]],
        "plazas": [{"name": p["name"], "x": p["x"], "z": p["z"], "r": p["r"], "h": p["h"]} for p in data["plazas"]],
        "railZ": data["rail_z"],
    }
    assert len(game["heights"]) == N * N and len(game["kinds"]) == N * N
    with open(path, "w", encoding="utf-8") as f: json.dump(game, f, ensure_ascii=False, separators=(",", ":"))

if __name__ == "__main__":
    args = sys.argv[1:]; game = None
    if "--game" in args: k = args.index("--game"); game = args[k + 1]; del args[k:k + 2]
    out = args[0] if args else os.path.join(os.path.dirname(__file__), "..", "..", "Docs", "SlopeTownDesign", "terrain.json")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    data = build()
    with open(out, "w", encoding="utf-8") as f: json.dump(data, f)
    if game: export_game(data, game); print("game terrain", game, os.path.getsize(game), "bytes")
    H = data["height"]
    print("height", min(min(r) for r in H), max(max(r) for r in H), "lots", len(data["lots"]))
    for r in data["roads"]:
        hs = r["height"]; g = max(abs(b - a) for a, b in zip(hs, hs[1:]))
        print(r["id"], "len", len(r["path"]), "h", round(hs[0], 1), "->", round(hs[-1], 1), "max grade", round(g, 3))
    for s in data["stairs"]: print("stairs", s["id"], round(s["h0"], 1), "->", round(s["h1"], 1), "steps", s["steps"])
