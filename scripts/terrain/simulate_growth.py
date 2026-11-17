# simulate_growth.py
# 坂の町で体がどれくらいの速さで育つかの見積もり (楽観的: 坂・段差・壁・飛びかかりの間を考えず、一番得な物へまっすぐ向かう)。
# 物の一覧は --slime-life-slope-tour が書く tests/out/slope-props.csv (x, y, z と覆うのに要る半径はメートル、reward は成長の量)。
# 規則は Game/Chrono/DevourRules.h と devour_stages.json に合わせる (覆える大きさ・小さい物の値打ち・移動の速さ)。
# 使い方: python scripts/terrain/simulate_growth.py [秒]   → 時間ごとの体の大きさと、食べた物の大きさの内訳を出す
import csv, math, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
TIME = float(sys.argv[1]) if len(sys.argv) > 1 else 300
START_MASS, START_RADIUS, COVER, KNEE = 100.0, 3.0 / 6, .8, .3   # 体の半径はメートル (3 単位 = 0.5m)
BIG_FROM, BIG_BONUS = .7, 2.2   # DevourRules.h の DevourBigFrom / DevourBigBonus
SPEED_1M = 3.0      # 1m の体の速さ (m/秒。--slime-life-slope-perf の road-walk から)
EAT_TIME = float(os.environ.get("EAT_TIME", 1.0))   # 1 個を飲むのにかかる時間 (秒。飛びかかりの間隔 DevourPounceCooldown)
SPAWN = (-8.0, -96.7)
GOALS = {"C": 8, "B": 12, "A": 16, "S": 22}

GROWTH = float(os.environ.get("GROWTH", 1.0))   # ステージの育ちの倍率 (devour_stages.json の growth)
props = [dict(x=float(r["x"]), z=float(r["z"]), radius=float(r["radius"]), reward=float(r["reward"]) * GROWTH) for r in csv.DictReader(open(os.path.join(ROOT, "tests/out/slope-props.csv")))]
def scale(mass): r = mass / START_MASS; return math.sqrt(r) if r > 1 else r ** (1 / 3)
def body_size(mass): return 2 * START_RADIUS * scale(mass)          # 体の大きさ (m。開始 1m)
def cover(mass): return START_RADIUS * scale(mass) * COVER         # 覆える半径 (m)
def food(mass, radius):
    size = radius / cover(mass); u = size / KNEE
    if size > BIG_FROM: return 1 + (BIG_BONUS - 1) * min(1, (size - BIG_FROM) / (1 - BIG_FROM))
    return min(1, max(.1, u * u))
mass, t, x, z = START_MASS, 0.0, *SPAWN
eaten = [False] * len(props); count = 0; marks = []; nextMark = 30; tiers = {}
while t < TIME:
    speed = SPEED_1M * max(1, scale(mass)) ** .55   # 大きいほど速い (DevourSpeedScale: 大きさの 0.55 乗)
    reach = cover(mass)
    best, bestRate = -1, 0
    for i, p in enumerate(props):
        if eaten[i] or p["radius"] > reach: continue
        d = max(0, math.hypot(p["x"] - x, p["z"] - z) - reach)
        rate = p["reward"] * food(mass, p["radius"]) / (d / speed + EAT_TIME)
        if rate > bestRate: best, bestRate = i, rate
    if best < 0: break
    p = props[best]; d = max(0, math.hypot(p["x"] - x, p["z"] - z) - reach)
    t += d / speed + EAT_TIME; x, z = p["x"], p["z"]; eaten[best] = True; count += 1
    gain = p["reward"] * food(mass, p["radius"]); mass += gain
    tier = "%.1fm" % (min(5, round(p["radius"] * 2, 1))); tiers[tier] = tiers.get(tier, 0) + 1
    while t >= nextMark and nextMark <= TIME: marks.append((nextMark, body_size(mass), count)); nextMark += 30
for sec, size, n in marks: print("%4ds  %.2fm  %d 個" % (sec, size, n))
print("最後: %.2fm (%d 個)  目標 %s" % (body_size(mass), count, ", ".join("%s %dm" % kv for kv in GOALS.items())))
print("食べた物の幅:", ", ".join("%s:%d" % kv for kv in sorted(tiers.items(), key=lambda kv: float(kv[0][:-1]))))
