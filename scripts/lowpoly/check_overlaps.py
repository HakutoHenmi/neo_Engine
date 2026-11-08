# check_overlaps.py
# ローポリのモデルの面が重なっていないか調べる。同じ向きの面がほぼ同じ高さで重なると、
# ゲームの中で面がチカチカする (Z ファイティング)。使い方: python scripts/lowpoly/check_overlaps.py
import math
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAP = .015  # これより近い平行な面は重なりと見なす (cm)。大きな物は町の縮尺で見るので大きさに合わせて広げる


def load(path):
    verts, faces = [], []
    for line in path.read_text().splitlines():
        if line.startswith("v "):
            verts.append(tuple(map(float, line.split()[1:4])))
        elif line.startswith("f "):
            faces.append([int(p.split("/")[0]) - 1 for p in line.split()[1:]])
    return verts, faces


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def overlap(a, b):
    # 2D の三角形が内側で重なるか (分離軸)。辺だけ接している物は重なりにしない
    for tri in (a, b):
        for i in range(3):
            p, q = tri[i], tri[(i + 1) % 3]
            n = (q[1] - p[1], p[0] - q[0])
            l = math.hypot(*n) or 1
            n = (n[0] / l, n[1] / l)
            pa = [x * n[0] + y * n[1] for x, y in a]
            pb = [x * n[0] + y * n[1] for x, y in b]
            if max(pa) <= min(pb) + 1e-3 or max(pb) <= min(pa) + 1e-3:
                return False
    return True


def check(path):
    verts, faces = load(path)
    size = max(max(v[i] for v in verts) - min(v[i] for v in verts) for i in range(3))
    gap = max(GAP, size * .0005)
    tris = []
    for f in faces:
        p = [verts[i] for i in f]
        n = cross(sub(p[1], p[0]), sub(p[2], p[0]))
        l = math.sqrt(dot(n, n))
        if l < 1e-6:
            continue
        n = (n[0] / l, n[1] / l, n[2] / l)
        tris.append((n, dot(n, p[0]), p))
    groups = defaultdict(list)
    for t in tris:
        groups[tuple(round(c, 2) for c in t[0])].append(t)
    hits = 0
    worst = None
    for n, group in groups.items():
        u = cross(n, (0, 1, 0) if abs(n[1]) < .9 else (1, 0, 0))
        ul = math.sqrt(dot(u, u))
        u = (u[0] / ul, u[1] / ul, u[2] / ul)
        v = cross(n, u)
        flat = [([(dot(q, u), dot(q, v)) for q in t[2]], t[1]) for t in group]
        for i in range(len(flat)):
            for j in range(i + 1, len(flat)):
                if abs(flat[i][1] - flat[j][1]) < gap and overlap(flat[i][0], flat[j][0]):
                    hits += 1
                    worst = worst or group[i][2][0]
    return hits, worst


bad = 0
for path in sorted((ROOT / "Resources/Models/Lowpoly").glob("*.obj")):
    hits, at = check(path)
    if hits:
        bad += 1
        print(f"OVERLAP {path.stem}: {hits} at {tuple(round(c, 1) for c in at)}")
print("models with overlaps:", bad)
sys.exit(1 if bad else 0)
