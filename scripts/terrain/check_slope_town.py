# check_slope_town.py
# 坂の町の地形と置く物を調べる (ゲームが読む Resources/Data/slope_town_terrain.json と slope_town.json から)。
#   - 家の敷地: 道や石段に削られていないか (敷地のマスが全部「敷地」のままか)
#   - 擁壁: 道の横の段差が上限 (3m) を超えていないか
#   - 道: 道を横切る向きにのこぎり状の段がないか
#   - 穴: 1m の体で出られない所がないか
#   - 1 つずつ置く物と線に沿って並べる物: 足元 (モデルの大きさ) が道にかかっていないか、足元の地面の高さがそろっているか (浮き・埋まり)、家の敷地に重なっていないか
# 使い方: python scripts/terrain/check_slope_town.py   (問題があれば一覧を出して 1 で終わる)
import json, math, os, sys, collections

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
T = json.load(open(os.path.join(ROOT, "Resources/Data/slope_town_terrain.json"), encoding="utf-8"))
D = json.load(open(os.path.join(ROOT, "Resources/Data/slope_town.json"), encoding="utf-8"))
HALF = T["half"]; N = 2 * HALF + 1
H = [h / 100 for h in T["heights"]]; K = T["kinds"]
def at(i, j): return H[j * N + i], K[j * N + i]
def cell(x, z): return int(math.floor(x + .5)) + HALF, int(math.floor(z + .5)) + HALF
KIND = {"0": "地面", "1": "敷地", "2": "道", "3": "石段", "4": "川", "5": "線路", "6": "段々畑", "7": "境内"}
problems = []
def report(kind, text): problems.append((kind, text))

# ---- 家の敷地 ----
lots = T["lots"]
def lot_cells(lot):
    yaw = lot["yaw"]; f = (math.sin(yaw), math.cos(yaw)); r = (math.cos(yaw), -math.sin(yaw)); hw, hd = lot["w"] / 2, lot["d"] / 2
    out = []
    for j in range(N):
        z = j - HALF
        if abs(z - lot["z"]) > 9: continue
        for i in range(N):
            x = i - HALF
            if abs(x - lot["x"]) > 9: continue
            dx, dz = x - lot["x"], z - lot["z"]
            if abs(dx * r[0] + dz * r[1]) <= hw - .5 and abs(dx * f[0] + dz * f[1]) <= hd - .5: out.append((i, j))
    return out
for n, lot in enumerate(lots):
    cells = lot_cells(lot); kinds = collections.Counter(at(i, j)[1] for i, j in cells)
    bad = {KIND[k]: c for k, c in kinds.items() if k != "1"}
    if bad: report("敷地", "敷地 %d (%.0f, %.0f) に %s が入っている" % (n, lot["x"], lot["z"], bad))
    hs = [at(i, j)[0] for i, j in cells if at(i, j)[1] == "1"]
    if hs and max(hs) - min(hs) > .05: report("敷地", "敷地 %d (%.0f, %.0f) の高さがそろっていない (%.2f〜%.2f)" % (n, lot["x"], lot["z"], min(hs), max(hs)))

# ---- 道の横の擁壁と、道を横切るのこぎり ----
walls = collections.Counter(); saw = 0; saw_at = []; worst = (0, None)
for j in range(N - 1):
    for i in range(N - 1):
        h, k = at(i, j)
        for ii, jj in ((i + 1, j), (i, j + 1)):
            h2, k2 = at(ii, jj)
            if k == "2" and k2 == "2" and abs(h - h2) > .35:
                saw += 1
                if len(saw_at) < 8: saw_at.append((i - HALF, j - HALF, round(abs(h - h2), 2)))
            if (k == "2") != (k2 == "2") and "3" not in (k, k2):
                diff = abs(h - h2); walls[min(int(diff), 9)] += 1
                if diff > worst[0]: worst = (diff, (i - HALF, j - HALF))
# (擁壁の上限 3m に、交差点で道の高さをまぜた分を見込む)
if worst[0] > 3.6: report("擁壁", "道の横の段差が %.1fm (%d, %d)" % (worst[0], *worst[1]))
if saw > 20: report("道", "道の中の段差 (0.35m より大きい) が %d か所 (例 %s)" % (saw, saw_at))

# ---- 1m の体で出られない所 ----
si, sj = cell(-8, -97)
seen = [[False] * N for _ in range(N)]; q = collections.deque([(si, sj)]); seen[sj][si] = True
while q:
    i, j = q.popleft()
    for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        a, b = i + di, j + dj
        if 0 <= a < N and 0 <= b < N and not seen[b][a] and at(i, j)[0] - at(a, b)[0] <= 1.0: seen[b][a] = True; q.append((a, b))
# (石段の箱の下になるマスは、ゲームでは石段の高さになるので除く)
def under_stairs(x, z):
    for s in T["stairs"]:
        (x0, z0), (x1, z1) = s["from"], s["to"]; L = math.hypot(x1 - x0, z1 - z0); ux, uz = (x1 - x0) / L, (z1 - z0) / L
        a = (x - x0) * ux + (z - z0) * uz; b = -(x - x0) * uz + (z - z0) * ux
        if -1 <= a <= L + 1 and abs(b) <= s["width"] / 2 + 1.2: return True
    return False
trap = [(i - HALF, j - HALF) for j in range(N) for i in range(N) if not seen[j][i] and abs(i - HALF) < HALF - 1 and abs(j - HALF) < HALF - 1 and not under_stairs(i - HALF, j - HALF)]
if len(trap) > 20: report("穴", "1m の体で出られないマスが %d (例 %s)" % (len(trap), trap[:5]))

# ---- 置く物の足元 ----
size_cache = {}
def model_size(id):
    if id not in size_cache:
        lo = [1e9] * 3; hi = [-1e9] * 3
        for line in open(os.path.join(ROOT, "Resources/Models/Lowpoly", id + ".obj")):
            if line.startswith("v "):
                v = list(map(float, line.split()[1:4]))
                for k in range(3): lo[k] = min(lo[k], v[k]); hi[k] = max(hi[k], v[k])
        size_cache[id] = (lo, hi)
    return size_cache[id]
lot_rects = []
for lot in lots:
    yaw = lot["yaw"]; ex = abs(math.cos(yaw)) * lot["w"] / 2 + abs(math.sin(yaw)) * lot["d"] / 2; ez = abs(math.sin(yaw)) * lot["w"] / 2 + abs(math.cos(yaw)) * lot["d"] / 2
    lot_rects.append((lot["x"] - ex, lot["z"] - ez, lot["x"] + ex, lot["z"] + ez))
def footprint(id, x, z, yaw, scale):
    lo, hi = model_size(id); sx, sz = (hi[0] - lo[0]) / 100 * scale, (hi[2] - lo[2]) / 100 * scale
    mx, mz = (lo[0] + hi[0]) / 200 * scale, (lo[2] + hi[2]) / 200 * scale; cs, sn = math.cos(yaw), math.sin(yaw)
    cx, cz = x + mx * cs + mz * sn, z - mx * sn + mz * cs
    hx, hz = .5 * (abs(cs) * sx + abs(sn) * sz), .5 * (abs(sn) * sx + abs(cs) * sz)
    return cx - hx, cz - hz, cx + hx, cz + hz
# 木は枝が広がっても、地面に立つのは幹だけ (足元は幹のまわりで調べる)。トンネルは山に掘る物なので足元を調べない
TREES = {"sakura", "kaki_no_ki", "matsu", "takeyabu", "goshinboku"}; SKIP = {"tunnel"}
def check(id, x, z, yaw, scale, landmark, label, road_ok):
    if id in SKIP: return
    x0, z0, x1, z1 = footprint(id, x, z, yaw, scale)
    if id in TREES: x0, z0, x1, z1 = x - .8, z - .8, x + .8, z + .8
    cells = [(i, j) for j in range(cell(0, z0)[1], cell(0, z1)[1] + 1) for i in range(cell(x0, 0)[0], cell(x1, 0)[0] + 1) if 0 <= i < N and 0 <= j < N]
    if not cells: return
    kinds = collections.Counter(at(i, j)[1] for i, j in cells); hs = [at(i, j)[0] for i, j in cells]
    big = max(x1 - x0, z1 - z0) >= 1.5
    if big and not road_ok and kinds.get("2", 0) > len(cells) * .15: report("置く物", "%s %s (%.0f, %.0f) が道にかかっている (%d/%d マス)" % (label, id, x, z, kinds["2"], len(cells)))
    if kinds.get("3", 0) and big: report("置く物", "%s %s (%.0f, %.0f) が石段にかかっている" % (label, id, x, z))
    if (big or id in TREES) and max(hs) - min(hs) > (1.3 if id in TREES else .8): report("置く物", "%s %s (%.0f, %.0f) の足元の高さが %.1fm 違う (浮く・埋まる)" % (label, id, x, z, max(hs) - min(hs)))
    if landmark is False and big:
        for n, (a0, b0, a1, b1) in enumerate(lot_rects):
            if x0 < a1 - .5 and x1 > a0 + .5 and z0 < b1 - .5 and z1 > b0 + .5: report("置く物", "%s %s (%.0f, %.0f) が家の敷地 %d に重なる" % (label, id, x, z, n)); break
deg = math.pi / 180
# 道ぞいに置いてよい物 (道の端に立つ・道を走る)
ROADSIDE = {"fumikiri", "bus_tei", "denwa_box", "jihanki", "bench", "gomi_station", "yatai", "endai", "gachagacha", "torii", "sanmon"}
for p in D["props"]:
    scale = 1.0
    if p.get("fit"): lo, hi = model_size(p["id"]); scale = p["fit"] / (max(hi[0] - lo[0], hi[2] - lo[2]) / 100)
    else: scale = p.get("mult", 1)
    check(p["id"], p["at"][0], p["at"][1], p.get("yaw", 0) * deg, scale, p.get("landmark", False), "props", p["id"] in ROADSIDE)
for r in D["rows"]:
    ids = r.get("ids", [r.get("id")]); (fx, fz), (tx, tz) = r["from"], r["to"]; L = math.hypot(tx - fx, tz - fz); k = 0; s = 0.0
    if r.get("fill", 0) == 0: continue
    while s <= L + .01:
        x, z = fx + (tx - fx) * s / max(L, 1e-6), fz + (tz - fz) * s / max(L, 1e-6)
        check(ids[k % len(ids)], x, z, r.get("yaw", 0) * deg, r.get("mult", 1), None, "rows", False); s += r["step"]; k += 1

print("坂の町の確認: 敷地 %d・道の横の段差 %s・道の中の段差 %d・出られないマス %d" % (len(lots), dict(sorted(walls.items())), saw, len(trap)))
for kind, text in problems: print(" [%s] %s" % (kind, text))
print("問題 %d 件" % len(problems))
sys.exit(1 if problems else 0)
