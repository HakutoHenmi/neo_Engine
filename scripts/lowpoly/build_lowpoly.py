# build_lowpoly.py
# ローポリの小物を Blender で作り、Resources/Models/Lowpoly へ OBJ で書き出す。
# 形はここに書いた数値だけで決まる (外部のモデルは使わない)。大きさの単位は cm で、y が上、底は y=0。
# 色は palette.json の色の名前で指定し、面ごとに色の画像の中のマスを指す。
# 使い方: C:/blender/blender.exe --background --factory-startup --python scripts/lowpoly/build_lowpoly.py -- [--preview 画像のパス] [--only id,id]
import json
import math
import sys
from pathlib import Path

import bpy
import bmesh

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / "Resources/Models/Lowpoly"
PALETTE = ROOT / "Resources/Textures/Lowpoly/palette.png"
SPEC = json.loads((HERE / "palette.json").read_text(encoding="utf-8"))
COLOR_NAMES = list(SPEC["colors"].keys())


def color_uv(name):
    i = COLOR_NAMES.index(name)
    c, n, s = SPEC["cell"], SPEC["columns"], SPEC["size"]
    return ((i % n + .5) * c / s, 1 - (i // n + .5) * c / s)


def glyph_rect(key):
    # 文字のマスの範囲 (左下の u, v と幅)
    g, s = SPEC["glyphCell"], SPEC["size"]
    i = list(SPEC["glyphs"].keys()).index(key)
    x, y = i % (s // g) * g, SPEC["glyphRow"] + i // (s // g) * g
    return x / s, 1 - (y + g) / s, g / s


class Shape:
    """面の集まり。座標はゲームと同じ y が上。
    部品が同じ面に重なると描画がチカチカするので、部品ごとに grow の分だけ順に太らせる"""
    EPS = 0.0

    def __init__(self):
        self.verts, self.faces, self.uvs = [], [], []
        self.part = 0

    def grow(self):
        self.part = self.part % 9 + 1
        return Shape.EPS * self.part

    def face(self, points, color, uv=None):
        base = len(self.verts)
        self.verts += [tuple(p) for p in points]
        self.faces.append(list(range(base, base + len(points))))
        self.uvs.append(uv or [color_uv(color)] * len(points))

    def box(self, lo, hi, color, thick=True):
        e = self.grow() if thick else 0
        x, y, z = lo[0] - e, lo[1] - e, lo[2] - e
        X, Y, Z = hi[0] + e, hi[1] + e, hi[2] + e
        p = [(x, y, z), (X, y, z), (X, y, Z), (x, y, Z), (x, Y, z), (X, Y, z), (X, Y, Z), (x, Y, Z)]
        for f in ((0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
            self.face([p[i] for i in f], color)

    def loft(self, rings, color):
        """同じ頂点数の輪を下から順につなぐ。color は色の名前か (帯の番号, 周の番号) を受ける関数"""
        pick = color if callable(color) else (lambda band, i: color)
        n = len(rings[0])
        for band in range(len(rings) - 1):
            a, b = rings[band], rings[band + 1]
            for i in range(n):
                j = (i + 1) % n
                self.face([a[i], a[j], b[j], b[i]], pick(band, i))

    def cap(self, ring, color):
        self.face(ring, color)

    def lathe(self, profile, color, sides=8, at=(0, 0), yaw=0.0):
        """(半径, 高さ) の列を回す。両端の半径が 0 より大きければふたをする"""
        e = self.grow()
        profile = [(r + e if r > 0 else r, y) for r, y in profile]
        profile[0] = (profile[0][0], profile[0][1] - e)
        profile[-1] = (profile[-1][0], profile[-1][1] + e)
        rings = []
        for r, y in profile:
            rings.append([(at[0] + max(r, 1e-4) * math.cos(yaw + 6.2831853 * i / sides), y,
                           at[1] + max(r, 1e-4) * math.sin(yaw + 6.2831853 * i / sides)) for i in range(sides)])
        self.loft(rings, color)
        pick = color if callable(color) else (lambda band, i: color)
        if profile[0][0] > 0:
            self.cap(rings[0], pick(0, 0))
        if profile[-1][0] > 0:
            self.cap(rings[-1], pick(len(profile) - 2, 0))

    def tube(self, p0, p1, r0, r1, color, sides=6):
        """2 点を結ぶ先細りの筒"""
        d = [p1[i] - p0[i] for i in range(3)]
        length = math.sqrt(sum(v * v for v in d))
        d = [v / length for v in d]
        e = self.grow()
        r0, r1 = r0 + e, r1 + e
        p0 = [p0[i] - d[i] * e for i in range(3)]
        p1 = [p1[i] + d[i] * e for i in range(3)]
        up = (0, 1, 0) if abs(d[1]) < .9 else (1, 0, 0)
        u = cross(d, up)
        ul = math.sqrt(sum(v * v for v in u))
        u = [v / ul for v in u]
        v = cross(d, u)

        def ring(p, r):
            return [tuple(p[k] + r * (math.cos(6.2831853 * i / sides) * u[k] + math.sin(6.2831853 * i / sides) * v[k]) for k in range(3)) for i in range(sides)]

        a, b = ring(p0, r0), ring(p1, r1)
        self.loft([a, b], color)
        self.cap(a, color)
        self.cap(b, color)

    def ball(self, center, radius, color, squash=1.0, level=2):
        """少ない面の球。color は色の名前か (面の番号) を受ける関数"""
        pick = color if callable(color) else (lambda i: color)
        mesh = bmesh.new()
        bmesh.ops.create_icosphere(mesh, subdivisions=level, radius=radius + self.grow())
        for i, f in enumerate(mesh.faces):
            self.face([(center[0] + v.co.x, center[1] + v.co.z * squash, center[2] + v.co.y) for v in f.verts], pick(i))
        mesh.free()


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def rounded_rect(hx, hz, corner, y, wave=0.0):
    """角を落とした長方形の輪 (8 頂点)。wave で少しゆがませると布に見える"""
    c = min(corner, hx, hz)
    pts = [(hx, -hz + c), (hx, hz - c), (hx - c, hz), (-hx + c, hz), (-hx, hz - c), (-hx, -hz + c), (-hx + c, -hz), (hx - c, -hz)]
    return [(x * (1 + wave * math.sin(i * 2.4)), y, z * (1 + wave * math.cos(i * 1.7))) for i, (x, z) in enumerate(pts)]


MODELS = {}


def model(name=None):
    def add(fn):
        MODELS[name or fn.__name__] = fn
        return fn
    return add


# ---- 和室 -------------------------------------------------------------------------------------
@model()
def kotatsu():
    # 布団の面はゲームの地面と同じ形 (RoomRules.h の丘)。1cm = 1.2 単位で、裾から天板の下まで同じ傾きで登れる
    s = Shape()
    hems = [98.3, 86, 74, 60, 47, 37]
    height = lambda hw: 33.4 * (98.3 - hw) / 61.3
    rings = [rounded_rect(hw, hw, 14 if hw > 50 else 7, height(hw), .012 if hw > 50 else 0) for hw in hems]
    s.loft(rings, lambda band, i: ("red_dark", "cream", "red", "orange", "red")[band])
    s.cap(list(reversed(rings[0])), "red_dark")
    s.cap(rings[-1], "red")
    # 天板
    s.box((-41, 33.4, -41), (41, 35, 41), "wood_light")
    s.box((-37, 35, -37), (37, 35.1, 37), "wood")
    return s


@model()
def zabuton():
    # 上面の周りがなだらかなので、そのまま乗り越えられる (RoomRules.h の丘)
    s = Shape()
    rings = [rounded_rect(30.8, 30.8, 7, 0), rounded_rect(28, 28, 7, 1.7), rounded_rect(24, 24, 7, 3.8), rounded_rect(21.6, 21.6, 6, 5)]
    s.loft(rings, lambda band, i: "indigo" if band < 2 else "purple")
    s.cap(list(reversed(rings[0])), "indigo")
    s.cap(rings[-1], "purple")
    # 真ん中の綴じと四隅の房
    s.lathe([(1.8, 4.9), (1.2, 5.5), (0, 5.7)], "gold", sides=6)
    for x, z in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        p = (x * 26, 1.6, z * 26)
        s.tube(p, (x * 31, .8, z * 31), 1.1, .4, "gold", sides=4)
    return s


@model()
def shogi_board():
    s = Shape()
    w, d, y0, y1 = 18, 19.5, 7, 19
    s.box((-w, y0, -d), (w, y1, d), "wood_light")
    for x, z in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        s.lathe([(2.2, 0), (3.2, 2.5), (2.2, 5), (3, 7)], "wood_dark", sides=6, at=(x * (w - 5), z * (d - 5)))
    # 9 x 9 の升目の線
    m, t = 1.4, .09
    for i in range(10):
        x = -w + m + (2 * w - 2 * m) * i / 9
        z = -d + m + (2 * d - 2 * m) * i / 9
        s.box((x - t, y1 - .05, -d + m), (x + t, y1 + .1, d - m), "black", thick=False)
        s.box((-w + m, y1 - .1, z - t), (w - m, y1 + .18, z + t), "black", thick=False)
    return s


@model()
def mikan():
    s = Shape()
    r = 3.5
    # 面ごとに色を少し変えて手で塗ったように見せる
    s.ball((0, r * .86, 0), r, lambda i: ("orange", "orange", "orange_light", "orange", "orange_dark")[(i * 7) % 5], squash=.86)
    # へた
    s.lathe([(1.0, r * 1.7), (.8, r * 1.76), (0, r * 1.8)], "green_dark", sides=5)
    return s


@model()
def kago():
    s = Shape()
    # 外側と内側の壁を持つ浅いかご。編み目は交互の色で見せる
    profile = [(9, 0), (12, 2.5), (14.5, 8), (15, 10), (13.6, 10), (13, 8), (10.6, 3), (0, 2.4)]
    weave = lambda band, i: "wood_dark" if band >= 3 and band < 4 else ("wood_light" if (band + i) % 2 else "wood")
    s.lathe(profile, weave, sides=12)
    return s


@model()
def yunomi():
    s = Shape()
    colors = ["wood_dark", "navy", "navy", "sky", "white", "white", "tea"]
    profile = [(2.2, 0), (2.4, .7), (3.5, 1.2), (3.9, 6.8), (4, 8.6), (3.6, 8.6), (3.2, 6.6), (0, 6.6)]
    s.lathe(profile, lambda band, i: colors[band], sides=10)
    return s


@model()
def kyusu():
    s = Shape()
    body = [(3.6, 0), (5.6, 1.2), (6.8, 3.6), (6.4, 6.4), (4.6, 8)]
    s.lathe(body, "wood_red", sides=10)
    # ふたとつまみ
    s.lathe([(4.8, 8), (4.2, 9.1), (1.2, 9.8), (.9, 10.4), (1.4, 11.2), (0, 11.5)], lambda band, i: "wood_dark" if band >= 2 else "wood_red", sides=8)
    # 注ぎ口と、横へ伸びる持ち手
    s.tube((5.2, 3.8, 0), (10.5, 7.2, 0), 2.2, 1.0, "wood_red", sides=6)
    s.tube((0, 4.2, 5.6), (0, 6.2, 15), 1.4, 1.0, "wood_dark", sides=6)
    return s


def koma(key):
    def build():
        s = Shape()
        w, length, t = 1.35, 3.2, .85
        h = length / 2
        # 上から見た五角形 (先が +z)。上の面は少し小さい
        outline = [(-w, -h), (w, -h), (w * .8, h * .42), (0, h), (-w * .8, h * .42)]
        low = [(x, 0, z) for x, z in outline]
        top = [(x * .9, t, z * .94) for x, z in outline]
        s.loft([low, top], "koma")
        s.cap(list(reversed(low)), "koma")
        u0, v0, g = glyph_rect(key)
        span = max(2 * w, length)
        s.face(top, "koma", uv=[(u0 + g * (.5 + x / span), v0 + g * (.5 + z / span)) for x, _, z in top])
        return s
    return build


for key in SPEC["glyphs"]:
    MODELS["koma_" + key] = koma(key)


# ---- 子ども部屋の家具 -----------------------------------------------------------------------------
@model()
def bed():
    # ゲームでは 1cm = 1.2 単位。枕と掛け布団の上面は RoomRules.h の地面と同じ高さにする
    s = Shape()
    L, W = 100, 50
    for x, z in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        s.box((x * (L - 6) - 4, 0, z * (W - 6) - 4), (x * (L - 6) + 4, 25, z * (W - 6) + 4), "wood_dark")
    s.box((-L, 25, -W), (L, 31, W), "wood")
    s.box((-L + 2, 31, -W + 2), (L - 2, 44, W - 2), "white")
    s.box((-L, 25, -W), (-L + 6, 72, W), "wood")
    s.box((L - 6, 25, -W), (L, 45, W), "wood")
    # 枕: なだらかな丘の形
    rings = [rounded_rect(26, 40, 8, 44), rounded_rect(21, 35, 8, 50), rounded_rect(16, 30, 8, 52)]
    pillow = [[(x - 72, y, z) for x, y, z in ring] for ring in rings]
    s.loft(pillow, "sky")
    s.cap(pillow[-1], "white")
    # 掛け布団: 上面と、ベッドの横に少し垂れた裾
    s.box((-44, 43.6, -47), (92, 46.7, 50), "blue")
    s.box((-44, 30, 50), (92, 46.7, 51), "sky")
    return s


@model()
def desk():
    s = Shape()
    w, d, h = 54, 30, 72
    s.box((-w, h - 3, -d), (w, h, d), "wood_light")
    for x, z in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        s.box((x * (w - 3) - 2, 0, z * (d - 3) - 2), (x * (w - 3) + 2, h - 3, z * (d - 3) + 2), "gray_light")
    s.box((w - 34, h - 22, -d + 2), (w - 3, h - 3, d - 2), "wood")
    for i in range(2):
        y = h - 12 - i * 8
        s.box((w - 30, y, d - 2), (w - 7, y + 6.5, d - 1), "wood_light")
        s.box((w - 21, y + 2.5, d - 1), (w - 16, y + 3.8, d - .2), "gray")
    s.box((-w, h, -d), (w, h + 26, -d + 2), "wood")
    return s


@model()
def chair():
    s = Shape()
    w, h = 20, 42
    s.box((-w, h - 3, -w), (w, h, w), "red")
    s.box((-w, h + 12, -w), (w, h + 34, -w + 3), "red")
    for x in (-1, 1):
        s.box((x * (w - 3) - 1.5, h, -w), (x * (w - 3) + 1.5, h + 13, -w + 3), "gray")
    s.lathe([(2, 12), (2, h - 3)], "gray", sides=6)
    for i in range(5):
        a = 6.2831853 * i / 5
        p = (math.cos(a) * 20, 5, math.sin(a) * 20)
        s.tube((0, 12, 0), p, 1.8, 1.4, "gray", sides=4)
        s.lathe([(2.6, 0), (2.6, 5)], "charcoal", sides=6, at=(p[0], p[2]))
    return s


@model()
def bookshelf():
    s = Shape()
    w, d, h = 42, 15, 118
    s.box((-w, 0, -d), (-w + 2, h, d), "wood")
    s.box((w - 2, 0, -d), (w, h, d), "wood")
    s.box((-w + 1, 0, -d - .6), (w - 1, h - 1, -d + 1), "wood_dark")
    colors = ["red", "blue", "yellow", "green_leaf", "orange", "purple", "teal", "pink", "navy", "cream"]
    k = 0
    for i in range(5):
        y = i * 28.5
        s.box((-w + 1, y, -d + 1.4), (w - 1, y + 2.5, d - 1.4), "wood_light")
        if i == 4:
            break
        x = -w + 3
        while x < w - 12:
            t = 2.6 + (k * 7 % 5) * .9
            hgt = 15 + (k * 11 % 7) * 1.2
            s.box((x, y + 2.5, -d + 3), (x + t, y + 2.5 + hgt, d - 4 - (k % 3)), colors[k % len(colors)])
            x += t + .2
            k += 1
            if k % 9 == 8:
                x += 8
    return s


@model()
def toybox():
    s = Shape()
    w, d, h, t = 30, 20, 36, 2.2
    s.box((-w, 0, -d), (w, 2, d), "yellow")
    s.box((-w, 0, -d), (w, h, -d + t), "red")
    s.box((-w, 0, d - t), (w, h, d), "red")
    s.box((-w, 0, -d), (-w + t, h, d), "blue")
    s.box((w - t, 0, -d), (w, h, d), "blue")
    s.box((-w + t, 0, -d + t), (w - t, h - 6, d - t), "charcoal")
    for x, z, c, r in ((-14, 4, "green_leaf", 8), (6, -6, "orange", 7), (18, 7, "pink", 6)):
        s.ball((x, h - 5, z), r, c, level=1)
    return s


# ---- 部屋の小物 --------------------------------------------------------------------------------
@model()
def marble():
    s = Shape()
    s.ball((0, 1, 0), 1, "white", level=1)
    return s


@model()
def coin():
    s = Shape()
    s.lathe([(1.3, 0), (1.3, .2), (1.0, .24), (0, .24)], "gold", sides=10)
    return s


@model()
def candy():
    s = Shape()
    s.ball((0, 1, 0), 1, "pink", squash=.9, level=1)
    for x in (-1, 1):
        s.tube((x * .9, 1, 0), (x * 1.9, 1, 0), .15, .7, "yellow", sides=4)
    return s


@model()
def sock():
    s = Shape()
    s.tube((-9, 2, 0), (-6, 2, 0), 2.6, 2.7, "red", sides=6)
    s.tube((-6, 2, 0), (-3, 2, 0), 2.7, 2.8, "white", sides=6)
    s.tube((-3, 2, 0), (-1, 2, 0), 2.8, 2.85, "blue", sides=6)
    s.tube((-1, 2, 0), (1, 2, 0), 2.85, 2.9, "white", sides=6)
    s.tube((1, 2, 0), (5, 2, 8), 2.9, 2.2, "white", sides=6)
    s.ball((5, 2, 8), 2.3, "gray_light", squash=.8, level=1)
    return s


@model()
def tsumiki():
    s = Shape()
    b = .35
    rings = [rounded_rect(2 - b, 2 - b, .3, 0), rounded_rect(2, 2, .3, b), rounded_rect(2, 2, .3, 4 - b), rounded_rect(2 - b, 2 - b, .3, 4)]
    s.loft(rings, "white")
    s.cap(rings[0], "white")
    s.cap(rings[-1], "white")
    return s


def wheel(s, x, z, y, r, width, tire="black", hub="gray_light", sides=8):
    s.tube((x, y, z - width), (x, y, z + width), r, r, tire, sides=sides)
    s.tube((x, y, z - width * 1.15), (x, y, z + width * 1.15), r * .5, r * .5, hub, sides=6)


@model()
def minicar():
    s = Shape()
    s.box((-3.8, .7, -1.6), (3.8, 1.9, 1.6), "red")
    s.box((-2, 1.9, -1.4), (1.5, 2.9, 1.4), "sky")
    s.box((-2.1, 2.9, -1.45), (1.6, 3.1, 1.45), "red")
    s.box((3.8, 1.1, -1.2), (3.9, 1.6, 1.2), "yellow")
    for x in (-2.3, 2.3):
        for z in (-1.6, 1.6):
            wheel(s, x, z, .75, .75, .3, sides=6)
    return s


@model()
def nuigurumi():
    s = Shape()
    c = "wood_light"
    s.ball((0, 4.5, 0), 4.5, c, level=1)
    s.ball((0, 11, 0), 3.9, c, level=2)
    for x in (-1, 1):
        s.ball((x * 3, 14.2, 0), 1.3, "wood", level=1)
        s.tube((x * 3.6, 7, 0), (x * 6, 4.5, 2), 1.5, 1.2, c, sides=5)
        s.tube((x * 2.6, 1.8, 2), (x * 3.8, 1.6, 7), 1.8, 1.8, c, sides=5)
        s.ball((x * 1.4, 11.8, 3.5), .42, "black", level=1)
    s.ball((0, 10.2, 3.6), 1.5, "cream", squash=.8, level=1)
    s.ball((0, 10.6, 4.9), .5, "black", level=1)
    s.box((-2.5, 7.6, -2.5), (2.5, 8.4, 3.4), "red")
    return s


@model()
def toy_ball():
    s = Shape()
    s.ball((0, 7, 0), 7, lambda i: ("white", "red", "white", "yellow", "white", "blue")[i % 6], level=2)
    return s


@model()
def enpitsu():
    s = Shape()
    r = .42
    s.tube((-6, r, 0), (8, r, 0), r, r, "green_dark", sides=6)
    s.tube((8, r, 0), (9.6, r, 0), r, .16, "wood_light", sides=6)
    s.tube((9.6, r, 0), (10, r, 0), .16, .02, "black", sides=6)
    s.tube((-6.8, r, 0), (-6, r, 0), r * 1.14, r * 1.14, "gold", sides=6)
    s.tube((-7.6, r, 0), (-6.8, r, 0), r * .9, r * .9, "pink", sides=6)
    return s


@model()
def keshigomu():
    s = Shape()
    s.box((-2.6, 0, -1.1), (2.6, 1.2, 1.1), "white")
    s.box((-1.6, -.02, -1.15), (2.65, 1.25, 1.15), "blue")
    s.box((-.2, 1.25, -1.15), (.5, 1.27, 1.15), "white")
    return s


@model()
def note():
    s = Shape()
    w, d = 9, 12.5
    s.box((-w, 0, -d), (w, .1, d), "sky")
    s.box((-w + .1, .1, -d + .1), (w - .1, .7, d - .1), "white")
    s.box((-w, .7, -d), (w, .8, d), "sky")
    s.box((-w, .8, -d), (-w + 1.4, .82, d), "navy")
    s.box((-5, .8, 3), (6, .82, 8), "white")
    return s


@model()
def manga():
    s = Shape()
    w, d = 5.6, 8.8
    s.box((-w, 0, -d), (w, 2, d), "white")
    s.box((-w, 0, -d), (w, .15, d), "red")
    s.box((-w, 1.85, -d), (w, 2, d), "red")
    s.box((-w, 0, -d), (-w + .3, 2, d), "yellow")
    s.box((-3.5, 2, 2.5), (4, 2.02, 7), "yellow")
    s.box((-3.5, 2, -6), (4, 2.02, 1), "white")
    return s


@model()
def remocon():
    s = Shape()
    s.box((-2.5, 0, -8.5), (2.5, 2, 8.5), "charcoal")
    s.box((-1.5, 2, 5.5), (-.3, 2.15, 6.8), "red")
    for row in range(5):
        for col in range(3):
            x, z = -1.8 + col * 1.4, 2.8 - row * 1.8
            s.box((x, 2, z), (x + .85, 2.12, z + 1), "gray_light" if row < 4 else "blue")
    return s


def trump(color):
    def build():
        s = Shape()
        w, d, t = 3.15, 4.45, .12
        s.box((-w, 0, -d), (w, t, d), "white" if color else "navy")
        if color:
            s.face([(0, t + .06, 2.2), (1.5, t + .06, 0), (0, t + .06, -2.2), (-1.5, t + .06, 0)], color)
            for k in (-1, 1):
                x, z = k * 2.3, k * 3.5
                s.face([(x, t + .06, z + .5), (x + .35, t + .06, z), (x, t + .06, z - .5), (x - .35, t + .06, z)], color)
        else:
            s.box((-2.5, t, -3.8), (2.5, t + .06, 3.8), "red_dark", thick=False)
            s.face([(0, t + .13, 2.6), (1.6, t + .13, 0), (0, t + .13, -2.6), (-1.6, t + .13, 0)], "white")
        return s
    return build


MODELS["trump_red"] = trump("red")
MODELS["trump_black"] = trump("black")
MODELS["trump_back"] = trump(None)


@model()
def maneki():
    s = Shape()
    s.lathe([(4.8, 0), (5.2, 1), (4.8, 6), (3.2, 8.6)], "white", sides=8)
    s.ball((0, 11, 0), 3.6, "white", squash=.92, level=1)
    for x in (-1, 1):
        s.tube((x * 2.2, 13.4, 0), (x * 2.5, 15.6, 0), 1.2, .15, "red" if x > 0 else "white", sides=3)
        s.ball((x * 1.4, 11.6, 3.1), .36, "black", level=1)
    s.tube((-3.8, 6.4, 1), (-4.7, 11.6, 1.2), 1.3, 1.1, "white", sides=5)
    s.ball((-4.7, 11.8, 1.2), 1.4, "white", level=1)
    s.tube((3.6, 6.6, 1.6), (2, 4.6, 4.8), 1.3, 1.1, "white", sides=5)
    s.lathe([(3.5, 8.3), (3.6, 9), (3.2, 9)], "red", sides=8)
    s.ball((0, 8, 3.5), .8, "gold", level=1)
    s.box((-1.4, 3, 3.9), (1.4, 6.4, 5), "gold")
    s.ball((0, 10.6, 3.5), .3, "pink", level=1)
    return s


@model()
def daruma():
    s = Shape()
    s.lathe([(3, 0), (5.5, 1.6), (6, 4.6), (5, 8), (3.2, 10.6), (0, 12)], "red", sides=10)
    s.ball((0, 7.4, 2.4), 3.1, "white", squash=1.05, level=2)
    for x in (-1, 1):
        s.ball((x * 1.3, 7.8, 5.2), .5, "black", level=1)
        s.tube((x * .4, 9.3, 5.2), (x * 2.2, 9, 4.7), .24, .1, "black", sides=4)
    s.box((-1.7, 2.2, 5), (1.7, 3.6, 5.7), "gold")
    return s


@model()
def tissue():
    s = Shape()
    w, d, h = 11.5, 6, 6
    s.box((-w, 0, -d), (w, h, d), "white")
    s.box((-w - .02, 0, -d - .02), (w + .02, 1.6, d + .02), "sky")
    s.box((-6, h, -1.6), (6, h + .05, 1.6), "gray_light")
    s.face([(-3, h, 0), (3.2, h, -.6), (2, h + 3, .3), (-2.4, h + 3.4, -.3)], "white")
    s.face([(-3, h, 0), (-2.4, h + 3.4, -.3), (2, h + 3, .3), (3.2, h, -.6)], "white")
    return s


# ---- 住宅街の小物 ------------------------------------------------------------------------------
@model()
def gomibukuro():
    s = Shape()
    s.ball((0, 22, 0), 26, lambda i: ("white", "gray_light", "white")[i % 3], squash=.8, level=2)
    s.lathe([(5, 38), (2.4, 44), (7, 52), (5, 54), (0, 53)], "gray_light", sides=6)
    return s


@model()
def danboru():
    s = Shape()
    s.box((-25, 0, -20), (25, 35, 20), "wood_light")
    s.box((-25.1, 34.9, -3), (25.1, 35.2, 3), "cream")
    s.box((-25.1, 24, -3), (-24.9, 35, 3), "cream")
    s.box((24.9, 24, -3), (25.1, 35, 3), "cream")
    s.box((-12, 16, 20), (-3, 26, 20.1), "wood_dark")
    return s


@model()
def gomibako():
    s = Shape()
    s.lathe([(19, 0), (23, 58), (24.5, 58), (24.5, 62), (20, 66), (5, 68), (0, 68)], lambda band, i: "charcoal" if band >= 2 else "blue", sides=8)
    return s


@model()
def karasu():
    s = Shape()
    s.ball((0, 17, 0), 10, "black", squash=.8, level=1)
    s.ball((13, 26, 0), 6, "black", level=1)
    s.tube((17, 26, 0), (26, 24, 0), 2.6, .2, "charcoal", sides=4)
    s.tube((-6, 17, 0), (-26, 10, 0), 6, 1.5, "charcoal", sides=4)
    for z in (-1, 1):
        s.tube((0, 10, z * 3), (0, 0, z * 3), .8, .7, "charcoal", sides=4)
        s.ball((15, 28, z * 4.6), 1, "white", level=1)
        s.tube((6, 22, z * 8), (-16, 15, z * 8), 5, 1, "charcoal", sides=4)
    return s


@model()
def uekibachi():
    s = Shape()
    s.lathe([(10, 0), (15, 20), (16.5, 20), (16.5, 24), (0, 24)], lambda band, i: "wood_dark" if band == 3 else "wood_red", sides=8)
    s.ball((0, 42, 0), 16, "green_leaf", level=1)
    s.ball((9, 34, 5), 9, "green_dark", level=1)
    s.ball((-8, 36, -6), 10, "green_dark", level=1)
    return s


@model()
def joro():
    s = Shape()
    s.lathe([(10, 0), (11, 16), (7, 22), (0, 22)], "green_leaf", sides=8)
    s.tube((9, 6, 0), (24, 24, 0), 2.6, 1.6, "green_leaf", sides=5)
    s.tube((24, 24, 0), (27, 28, 0), 1.6, 4, "yellow", sides=6)
    s.tube((-10, 16, 0), (-9, 27, 0), 1.2, 1.2, "green_dark", sides=4)
    s.tube((-9, 27, 0), (7, 24, 0), 1.2, 1.2, "green_dark", sides=4)
    return s


@model()
def monohoshi():
    s = Shape()
    for x in (-100, 100):
        s.box((x - 8, 0, -32), (x + 8, 6, 32), "gray")
        s.tube((x, 6, 0), (x, 170, 0), 2.2, 2, "gray_light", sides=5)
        s.tube((x, 150, -22), (x, 150, 22), 1.6, 1.6, "gray_light", sides=4)
    for z in (-20, 20):
        s.tube((-108, 150, z), (108, 150, z), 1.4, 1.4, "sky", sides=4)
    return s


@model()
def sentakumono():
    s = Shape()
    # 物干し竿から垂れる T シャツ (上端が高さ 72)
    p = [(-14, 72, 0), (14, 72, 0), (26, 58, 0), (19, 50, 0), (14, 55, 0), (14, 2, 0), (-14, 2, 0), (-14, 55, 0), (-19, 50, 0), (-26, 58, 0)]
    s.face(p, "white")
    s.box((-13, 30, -.4), (13, 42, .4), "blue")
    s.tube((0, 72, 0), (0, 84, 0), .8, .6, "gray", sides=4)
    s.tube((-16, 72, 0), (16, 72, 0), .7, .7, "gray", sides=4)
    return s


@model()
def inugoya():
    s = Shape()
    w, d, h, top = 38, 46, 50, 86
    s.box((-w, 0, -d), (w, h, d), "wood_light")
    s.face([(-w, h, d), (w, h, d), (0, top, d)], "wood_light")
    s.face([(-w, h, -d), (0, top, -d), (w, h, -d)], "wood_light")
    s.face([(-w - 6, h - 3, -d - 5), (-w - 6, h - 3, d + 5), (0, top + 2, d + 5), (0, top + 2, -d - 5)], "red")
    s.face([(w + 6, h - 3, d + 5), (w + 6, h - 3, -d - 5), (0, top + 2, -d - 5), (0, top + 2, d + 5)], "red")
    s.box((-16, 0, d), (16, 38, d + .4), "charcoal")
    s.box((-9, 50, d + .1), (9, 58, d + .6), "cream")
    return s


@model()
def inu():
    s = Shape()
    c = "orange_light"
    s.ball((0, 28, 0), 17, c, squash=.74, level=1)
    s.ball((20, 40, 0), 12, c, level=1)
    s.tube((27, 37, 0), (34, 36, 0), 6, 4.4, "cream", sides=5)
    s.ball((35, 36.6, 0), 2, "black", level=1)
    for z in (-1, 1):
        s.tube((18, 50, z * 6), (17, 58, z * 8), 4, .3, c, sides=3)
        s.ball((28, 43, z * 5), 1.3, "black", level=1)
        for x in (-11, 11):
            s.tube((x, 20, z * 8), (x, 0, z * 8), 3.6, 3, c, sides=5)
    s.tube((-16, 34, 0), (-20, 48, 0), 3.6, 2.6, "cream", sides=5)
    s.ball((-16, 49, 0), 3.4, "cream", level=1)
    return s


@model()
def yubinuke():
    s = Shape()
    s.box((-3, 0, -3), (3, 96, 3), "wood_dark")
    s.box((-17, 96, -13), (17, 124, 13), "red")
    s.box((-19, 124, -15), (19, 127, 15), "charcoal")
    s.box((-11, 112, 13), (11, 116, 13.3), "charcoal")
    return s


@model()
def sanrinsha():
    s = Shape()
    wheel(s, 24, 0, 13, 13, 1.8, tire="charcoal", hub="red")
    for z in (-14, 14):
        wheel(s, -22, z, 8, 8, 1.4, tire="charcoal", hub="red", sides=6)
    s.tube((-22, 8, -14), (-22, 8, 14), 1, 1, "red", sides=4)
    s.tube((-22, 8, 0), (22, 27, 0), 1.8, 1.8, "red", sides=4)
    s.tube((24, 13, 0), (20, 42, 0), 1.6, 1.6, "red", sides=4)
    s.tube((20, 42, -14), (20, 42, 14), 1.2, 1.2, "charcoal", sides=4)
    s.box((-16, 22, -8), (4, 26, 8), "blue")
    s.box((-18, 26, -8), (-15, 38, 8), "blue")
    return s


@model()
def jitensha():
    s = Shape()
    for x in (-54, 54):
        wheel(s, x, 0, 33, 33, 1.2, tire="charcoal", hub="gray", sides=10)
    c = "sky"
    seat, head, bb = (-18, 66, 0), (34, 74, 0), (-2, 33, 0)
    s.tube((-54, 33, 0), seat, 1.5, 1.5, c, sides=4)
    s.tube(bb, seat, 1.7, 1.7, c, sides=4)
    s.tube(bb, (32, 60, 0), 2, 2, c, sides=4)
    s.tube((-54, 33, 0), bb, 1.5, 1.5, c, sides=4)
    s.tube((54, 33, 0), head, 1.7, 1.7, "gray_light", sides=4)
    s.tube((34, 74, 0), (30, 92, 0), 1.5, 1.5, "gray_light", sides=4)
    s.tube((30, 92, -26), (30, 92, 26), 1.2, 1.2, "charcoal", sides=4)
    s.box((-30, 74, -7), (-6, 79, 7), "black")
    s.tube(seat, (-18, 74, 0), 1.3, 1.3, "gray", sides=4)
    s.box((46, 66, -14), (74, 84, 14), "gray_light")
    s.box((48, 68, -12), (72, 84.2, 12), "charcoal")
    s.box((-64, 60, -13), (-30, 63, 13), "gray")
    return s


@model()
def denchu():
    s = Shape()
    s.lathe([(17, 0), (13, 800), (0, 804)], "gray_light", sides=6)
    for y, w in ((720, 90), (660, 70)):
        s.box((-w, y, -6), (w, y + 10, 6), "gray")
        for x in (-w + 6, 0, w - 6):
            s.lathe([(4, y + 10), (2.4, y + 26)], "white", sides=5, at=(x, 0))
    s.lathe([(32, 560), (32, 640), (0, 646)], "charcoal", sides=6, at=(0, 42))
    s.box((-22, 180, 13), (22, 230, 16), "yellow")
    return s


@model()
def curve_mirror():
    s = Shape()
    s.lathe([(5, 0), (4, 300), (0, 302)], "orange", sides=6)
    p, q = (0, 330, 7), (0, 330, 10)
    s.tube(p, q, 44, 44, "orange", sides=10)
    s.tube((0, 330, 10), (0, 330, 16), 39, 34, "sky", sides=10)
    s.box((-4, 280, 0), (4, 300, 7), "orange")
    return s


@model()
def cone():
    s = Shape()
    s.box((-20, 0, -20), (20, 4, 20), "charcoal")
    colors = ["red", "white", "red", "white", "red"]
    s.lathe([(14, 4), (10.8, 20), (8.2, 32), (6, 44), (3.5, 58), (1.6, 70)], lambda band, i: colors[band], sides=8)
    return s


@model()
def yubin_post():
    s = Shape()
    s.box((-22, 0, -22), (22, 5, 22), "charcoal")
    s.lathe([(20, 5), (22, 10), (22, 108), (20, 112), (0, 128)], lambda band, i: "red_dark" if band >= 2 else "red", sides=8)
    s.box((-14, 88, 21), (14, 94, 23.5), "charcoal")
    s.box((-11, 54, 21.5), (11, 74, 22.6), "white")
    return s


@model()
def jihanki():
    s = Shape()
    w, d, h = 50, 35, 183
    s.box((-w, 0, -d), (w, h, d), "red")
    s.box((-w + 5, 76, d), (w - 5, h - 10, d + .4), "white")
    colors = ["blue", "orange", "green_leaf", "yellow", "red", "sky", "teal", "pink"]
    for row in range(3):
        y = 86 + row * 30
        s.box((-w + 7, y - 2, d + .4), (w - 7, y - 1, d + 1), "gray")
        for col in range(8):
            s.lathe([(4, y), (4, y + 18), (2.6, y + 23)], colors[(row * 3 + col) % 8], sides=5, at=(-w + 12 + col * 10.8, d + .6))
    s.box((-w + 7, 52, d), (w - 30, 72, d + .6), "navy")
    s.box((w - 26, 52, d), (w - 8, 74, d + .6), "gray_light")
    s.box((-w + 12, 12, d), (w - 12, 32, d + .5), "black")
    return s


@model()
def akikan():
    s = Shape()
    s.lathe([(2.6, 0), (3.3, .8), (3.3, 10.4), (2.8, 12), (2.6, 12), (0, 11.6)], lambda band, i: "gray_light" if band in (0, 3, 4) else ("red" if i % 2 else "white"), sides=8)
    return s


@model()
def bench():
    s = Shape()
    for i in range(4):
        z = -18 + i * 11
        s.box((-75, 40, z), (75, 43, z + 8), "wood")
    for i in range(3):
        y = 52 + i * 11
        s.box((-75, y, -24), (75, y + 8, -21), "wood")
    for x in (-62, 62):
        s.box((x - 2.5, 0, -22), (x + 2.5, 40, 24), "charcoal")
        s.box((x - 2.5, 40, -26), (x + 2.5, 86, -21), "charcoal")
    return s


@model()
def suberidai():
    s = Shape()
    w = 32
    s.box((-90, 170, -w), (0, 176, w), "yellow")
    for x in (-86, -4):
        for z in (-w + 4, w - 4):
            s.tube((x, 0, z), (x, 240, z), 4, 4, "blue", sides=5)
    s.box((-92, 236, -w - 4), (2, 244, w + 4), "red")
    s.face([(-92, 244, -w - 4), (2, 244, -w - 4), (2, 244, w + 4), (-92, 244, w + 4)], "red")
    s.box((0, 168, -24), (6, 176, 24), "blue")
    # すべる面
    s.box((-4, 170, -24), (6, 180, -21), "orange")
    s.box((-4, 170, 21), (6, 180, 24), "orange")
    pts = [(0, 174), (120, 80), (190, 28), (250, 16)]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        s.face([(x0, y0, -24), (x1, y1, -24), (x1, y1, 24), (x0, y0, 24)], "sky")
        s.face([(x0, y0 - 5, -24), (x0, y0 - 5, 24), (x1, y1 - 5, 24), (x1, y1 - 5, -24)], "blue")
        for z, c in ((-24, "orange"), (21, "orange")):
            s.face([(x0, y0 - 5, z), (x1, y1 - 5, z), (x1, y1 + 9, z), (x0, y0 + 9, z)], c)
            s.face([(x0, y0 - 5, z + 3), (x0, y0 + 9, z + 3), (x1, y1 + 9, z + 3), (x1, y1 - 5, z + 3)], c)
    for i in range(6):
        y = 22 + i * 26
        s.tube((-90 - 62 + i * 10, y, -20), (-90 - 62 + i * 10, y, 20), 2.4, 2.4, "gray_light", sides=4)
    for z in (-20, 20):
        s.tube((-160, 0, z), (-90, 176, z), 3, 3, "blue", sides=4)
    return s


@model()
def soccer_ball():
    s = Shape()
    s.ball((0, 11, 0), 11, lambda i: "black" if i % 5 == 0 else "white", level=2)
    return s


@model()
def kei_car():
    s = Shape()
    w, l = 74, 170
    s.box((-l, 22, -w), (l, 76, w), "white")
    s.box((-l + 8, 76, -w + 4), (l - 80, 140, w - 4), "navy")
    s.box((-l + 6, 138, -w + 3), (l - 78, 150, w - 3), "white")
    for x in (-l + 12, l - 92):
        for z in (-w + 6, w - 10):
            s.box((x, 76, z), (x + 10, 140, z + 4), "white")
    s.box((l - 2, 40, -60), (l + 2, 56, -38), "yellow")
    s.box((l - 2, 40, 38), (l + 2, 56, 60), "yellow")
    s.box((l - 2, 21, -w - 1.5), (l + 3, 34, w + 1.5), "charcoal")
    s.box((-l - 3, 21, -w - 1.5), (-l + 2, 34, w + 1.5), "charcoal")
    s.box((-l - 2, 44, -68), (-l + 2, 58, -46), "red")
    s.box((-l - 2, 44, 46), (-l + 2, 58, 68), "red")
    for x in (-100, 100):
        for z in (-w + 2, w - 2):
            wheel(s, x, z, 28, 28, 8, sides=8)
    return s


@model()
def shitsugaiki():
    s = Shape()
    s.box((-40, 6, -15), (40, 62, 15), "white")
    s.box((-36, 0, -13), (-28, 6, 13), "gray")
    s.box((28, 0, -13), (36, 6, 13), "gray")
    s.tube((-12, 34, 15), (-12, 34, 16), 23, 23, "charcoal", sides=10)
    s.tube((-12, 34, 16), (-12, 34, 16.6), 5, 5, "gray", sides=6)
    for i in range(3):
        y = 16 + i * 14
        s.box((16, y, 15), (34, y + 6, 15.5), "gray_light")
    return s


@model()
def neko():
    s = Shape()
    c = "orange_light"
    s.lathe([(9, 0), (12, 4), (10, 18), (6, 26)], c, sides=8)
    s.ball((0, 32, 2), 8.6, c, squash=.9, level=1)
    for x in (-1, 1):
        s.tube((x * 5, 37, 1.5), (x * 6, 46, 1), 3.6, .2, c, sides=3)
        s.ball((x * 3.2, 33.6, 9.6), 1, "green_dark", level=1)
        s.tube((x * 3.6, 18, 8), (x * 3.8, 0, 11), 2.2, 2, "white", sides=4)
    s.ball((0, 30.6, 10.4), 1.4, "pink", level=1)
    s.tube((0, 3, -10), (14, 2, -14), 2.2, 1.8, c, sides=4)
    s.tube((14, 2, -14), (19, 14, -12), 1.8, 1.2, "orange_dark", sides=4)
    return s


@model()
def my_house():
    # ステージ 1 の部屋がある家。赤い屋根と青いカーテンが目印
    s = Shape()
    w, d, h, top = 420, 340, 560, 780
    s.box((-w, 0, -d), (w, h, d), "cream")
    s.box((-w - 10, 0, -d - 10), (w + 10, 40, d + 10), "gray_light")
    s.box((-w - 6, 278, -d - 6), (w + 6, 288, d + 6), "wood")
    s.face([(-w - 40, h - 20, d + 50), (w + 40, h - 20, d + 50), (w + 40, top, 0), (-w - 40, top, 0)], "red")
    s.face([(w + 40, h - 20, -d - 50), (-w - 40, h - 20, -d - 50), (-w - 40, top, 0), (w + 40, top, 0)], "red_dark")
    s.face([(w, h, -d), (w, h, d), (w, top - 8, 0)], "cream")
    s.face([(-w, h, d), (-w, h, -d), (-w, top - 8, 0)], "cream")
    s.box((-60, 40, d), (60, 240, d + 6), "wood_dark")
    s.box((-90, 240, d), (90, 252, d + 70), "red_dark")
    for x, y, c in ((-250, 100, "sky"), (250, 100, "sky"), (-250, 360, "blue"), (0, 360, "sky"), (250, 360, "sky")):
        s.box((x - 76, y, d), (x + 76, y + 120, d + 4), "white")
        s.box((x - 66, y + 10, d + 4), (x + 66, y + 110, d + 6), c)
        s.box((x - 2, y + 10, d + 4), (x + 2, y + 110, d + 7), "white")
    s.box((200, h + 60, -120), (280, top + 10, -40), "wood_red")
    return s


@model()
def storage_case():
    # ベッド下の収納ケース。ふたの上は RoomRules.h の RoomCaseHeight (7cm)
    s = Shape()
    w, d, h = 20, 5, 7
    s.box((-w + .4, 0, -d + .4), (w - .4, h - 1, d - .4), "white")
    s.box((-w, h - 1, -d), (w, h, d), "white")
    s.box((-6, 2, d - .4), (6, 4, d), "gray_light")
    return s


def book_stack(steps, half_width, rise, depth):
    def build():
        # 大きな本を積んだ階段。原点が一番高い段の端で、+z へ低くなる
        s = Shape()
        colors = ["red", "blue", "yellow", "green_leaf", "purple", "orange", "teal", "pink"]
        k = 0
        for step in range(steps):
            z0, z1 = step * depth, (step + 1) * depth
            for level in range(steps - step):
                y0, y1 = level * rise, (level + 1) * rise
                c = colors[k % len(colors)]
                k += 1
                # 表紙で挟んだ白いページ。段ごとに幅を少しずらす
                hw = half_width - (k * 3 % 7) * .22
                front = z1 - (k % 3) * .18
                s.box((-hw + .4, y0 + .5, z0 + .4), (hw - .4, y1 - .5, front - .4), "white", thick=False)
                s.box((-hw, y0, z0), (hw, y0 + .5, front), c, thick=False)
                s.box((-hw, y1 - .5, z0), (hw, y1 - .02, front), c, thick=False)
        return s
    return build


MODELS["book_stairs"] = book_stack(8, 41.67, 5.833, 10)
MODELS["book_steps"] = book_stack(2, 9.17, 6.375, 10)


@model()
def toy_rail():
    # 机へ登るおもちゃのレール。原点が低い端で +x へ上がる (上面は RoomRules.h の坂と同じ)
    s = Shape()
    run, rise, w = 121.67, 72, 15
    y = lambda x: rise * x / run
    s.face([(0, 0, -w), (run, rise, -w), (run, rise, w), (0, 0, w)], "gray_light")
    for z, c in ((-w, "red"), (w, "red")):
        s.face([(0, 0, z), (run, 0, z), (run, rise, z)], "yellow")
        s.face([(0, 0, z), (run, rise, z), (run, 0, z)], "yellow")
    s.face([(run, 0, -w), (run, 0, w), (run, rise, w), (run, rise, -w)], "yellow")
    # 枕木とレール
    for i in range(14):
        x0 = 3 + i * 8.4
        x1 = x0 + 3.6
        s.face([(x0, y(x0) + .25, -w + .4), (x1, y(x1) + .25, -w + .4), (x1, y(x1) + .25, w - .4), (x0, y(x0) + .25, w - .4)], "wood")
    for z in (-w - .8, w + .8):
        s.tube((0, 1.6, z), (run, rise + 1.6, z), 1, 1, "red", sides=4)
    # 横の飾りの丸
    for x, c in ((60, "blue"), (82, "green_leaf"), (104, "orange")):
        for z in (-w - .4, w):
            s.tube((x, 16, z), (x, 16, z + .4), 8, 8, c, sides=8)
    return s


@model()
def ceiling_lamp():
    # 子ども部屋の丸いシーリングライト (y=0 が天井で下へ伸びる)
    s = Shape()
    s.lathe([(26, 0), (30, -4), (26, -8), (0, -9)], "white", sides=12)
    return s


@model()
def washitsu_lamp():
    # 和室の角ばったペンダントライト
    s = Shape()
    s.tube((0, 0, 0), (0, -42, 0), .6, .6, "charcoal", sides=4)
    s.box((-22, -66, -22), (22, -42, 22), "cream")
    for x in (-1, 1):
        for z in (-1, 1):
            s.box((x * 22 - 1, -68, z * 22 - 1), (x * 22 + 1, -40, z * 22 + 1), "wood_dark")
    s.box((-24, -42, -24), (24, -39, 24), "wood_dark")
    return s


@model()
def tire():
    s = Shape()
    s.lathe([(16, 0), (30, 0), (33, 5), (33, 17), (30, 22), (16, 22), (14, 17), (14, 5), (16, 0)], lambda band, i: "gray" if band >= 6 else ("charcoal" if i % 2 else "black"), sides=12)
    s.lathe([(14, 7), (0, 7)], "gray_light", sides=10)
    return s


# ---- ギミック: 共通の目印と、仕掛けの道具 ----------------------------------------------------------
def prism(s, pts, z0, z1, color, y=0.0):
    """板の面 (x, y) に描いた凸の形を z0 から z1 へ押し出す"""
    a = [(x, y + v, z0) for x, v in pts]
    b = [(x, y + v, z1) for x, v in pts]
    s.loft([a, b], color)
    s.cap(a, color)
    s.cap(b, color)


def circle(cx, cy, r, n=10):
    return [(cx + r * math.cos(6.2831853 * i / n), cy + r * math.sin(6.2831853 * i / n)) for i in range(n)]


def rot(pts, cx, cy, a):
    c, n = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * n, cy + (x - cx) * n + (y - cy) * c) for x, y in pts]


# 看板の絵 (板の中心からの位置、半径 7 cm に収める)
def icon_shoot(put):
    put([(-4.5, -.6), (2.2, -.6), (2.2, 2.2), (-4.5, 2.2)], "teal")
    put([(2.2, .4), (6, .4), (6, 1.6), (2.2, 1.6)], "teal")
    put([(-3.8, -4.6), (-1.4, -4.6), (-.8, -.6), (-3.4, -.6)], "teal")
    put(circle(-1.4, 3.6, 1.9), "sky")
    for i, (x, y) in enumerate(((6.9, 1), (6.4, -1.4), (6.2, 3.2))):
        put(circle(x, y, .55 + .1 * i, 6), "sky")


def icon_domino(put):
    base = [(-.9, -4.4), (.9, -4.4), (.9, 2.6), (-.9, 2.6)]
    for i, (x, a) in enumerate(((-5.2, 0), (-2, -.3), (1.2, -.75))):
        put(rot([(px + x, py) for px, py in base], x + .9, -4.4, a), ("red", "blue", "green_leaf")[i])


def icon_bowling(put):
    put([(-3.6, -5.2), (-1.4, -5.2), (-1.2, -2), (-3.8, -2)], "orange")
    put([(-3.8, -2), (-1.2, -2), (-1.9, .4), (-3.1, .4)], "orange")
    put([(-3.1, .4), (-1.9, .4), (-1.9, 1.4), (-3.1, 1.4)], "red")
    put(circle(-2.5, 2.8, 1.5, 8), "orange")
    put(circle(2.4, -2.4, 3.2, 12), "navy")


def icon_paint(put):
    put(rot([(-1, -5.4), (1, -5.4), (1, 1.6), (-1, 1.6)], 0, 0, -.6), "wood_red")
    put(rot([(-1.4, 1.6), (1.4, 1.6), (1.4, 3), (-1.4, 3)], 0, 0, -.6), "gray_light")
    put(rot([(-1.4, 3), (1.4, 3), (0, 6)], 0, 0, -.6), "blue")
    for (x, y), c in (((-4.2, 3.6), "red"), ((-4.8, -.6), "yellow"), ((4.4, -3.2), "green_leaf")):
        put(circle(x, y, 1.4, 8), c)


def icon_liquid(put):
    put(circle(-1.8, -2.4, 3.1, 12), "honey")
    put([(-4.5, -1), (.9, -1), (-1.8, 4.2)], "honey")
    put([(3.6, -4.8), (5, -4.8), (5, 1.6), (3.6, 1.6)], "red")
    put([(2.4, 1.6), (6.2, 1.6), (4.3, 4.6)], "red")


def icon_plane(put):
    put([(-5.6, -2.6), (5.4, 2.6), (-1.6, 1.2)], "sky")
    put([(-5.6, -2.6), (5.4, 2.6), (-.8, -4.4)], "blue")


ICONS = {"shoot": icon_shoot, "domino": icon_domino, "bowling": icon_bowling, "paint": icon_paint, "liquid": icon_liquid, "plane": icon_plane}


def gimmick_sign(kind):
    # どのギミックにも同じ形で立てる目印: 黄と黒の台・紅白の柱・赤い縁の丸い看板。看板の両面に何の仕掛けかを描く
    def build():
        s = Shape()
        s.lathe([(10, 0), (10, 1.6), (8.4, 3), (0, 3)], lambda band, i: "black" if i % 2 else "yellow", sides=12)
        s.lathe([(1.4, 3), (1.4, 36)], "red", sides=8)
        for k in range(4):
            s.lathe([(1.5, 6 + k * 7.6), (1.5, 9.8 + k * 7.6)], "white", sides=8)
        cy = 46
        s.tube((0, cy, -1.6), (0, cy, 1.6), 12.4, 12.4, "red", sides=16)
        for side in (1, -1):
            s.tube((0, cy, side * 1.5), (0, cy, side * 2.1), 10, 10, "white", sides=16)

            layer = [0]

            def put(pts, color, side=side, layer=layer):
                # 重なる絵は少しずつ手前へ出して、面がちらつかないようにする
                layer[0] += 1
                pts = [(x * side * 1.3, y * 1.3) for x, y in pts]
                z0, z1 = side * (1.9 - .03 * layer[0]), side * (2.8 + .06 * layer[0])
                prism(s, pts, min(z0, z1), max(z0, z1), color, y=cy)
            ICONS[kind](put)
        s.ball((0, cy + 13.6, 0), 2.4, "yellow", level=1)
        return s
    return build


for _kind in ICONS:
    MODELS["sign_" + _kind] = gimmick_sign(_kind)


@model()
def gimmick_ring():
    # 仕掛けの範囲を床に描く輪 (外の半径 50 cm。ゲームで範囲の大きさに合わせて縮める)。黄と橙の破線
    s = Shape()
    n = 24
    for i in range(0, n, 2):
        a0, a1 = 6.2831853 * i / n, 6.2831853 * (i + 1.4) / n
        mid = (a0 + a1) / 2
        poly = [(50 * math.cos(a), 50 * math.sin(a)) for a in (a0, mid, a1)] + [(43 * math.cos(a), 43 * math.sin(a)) for a in (a1, mid, a0)]
        top = [(x, .3, z) for x, z in poly]
        bot = [(x, 0, z) for x, z in poly]
        s.face(top, "yellow" if i % 4 == 0 else "orange")
        s.loft([bot + [bot[0]], top + [top[0]]], "orange_dark")
    return s


@model()
def daruma_disc():
    # だるま落としの胴の輪 (白で作り、ゲームで色を付ける)
    s = Shape()
    s.lathe([(6.2, 0), (7, .6), (7, 3.4), (6.2, 4)], "white", sides=12)
    return s


@model()
def daruma_stand():
    s = Shape()
    s.lathe([(10, 0), (10, 1.4), (9, 2)], "wood_light", sides=12)
    return s


@model()
def domino():
    # おもちゃの大きなドミノ (幅 x 6, 高さ 12, 厚み z 2)。白で作り、ゲームで色を付ける
    s = Shape()
    s.box((-3, 0, -1), (3, 12, 1), "white")
    for z in (-1, 1):
        s.box((-2.4, 5.85, z * 1.02 - .05), (2.4, 6.15, z * 1.02 + .05), "black", thick=False)
        for x, y in ((-1.4, 2), (1.4, 4), (0, 3), (-1.2, 8.4), (1.2, 10), (-1.2, 10), (1.2, 8.4)):
            s.box((x - .45, y - .45, z * 1.02 - .06), (x + .45, y + .45, z * 1.02 + .06), "black", thick=False)
    return s


@model()
def toy_bucket():
    # 砂遊びのバケツ。ドミノの最後に倒れて、中のおもちゃがあふれる
    s = Shape()
    s.lathe([(8, 0), (8.6, 4), (11, 15), (12, 16), (10.8, 16), (7.4, 1.4), (0, 1.4)], lambda band, i: "yellow" if band in (2, 3) else "sky", sides=12)
    for x in (-1, 1):
        s.tube((x * 11.6, 13, 0), (x * 7, 22, 0), .5, .5, "blue", sides=4)
    s.tube((-7, 22, 0), (7, 22, 0), .5, .5, "blue", sides=4)
    return s


@model()
def bowling_pin():
    # おもちゃのボウリングのピン (高さ 12 cm)
    s = Shape()
    s.lathe([(1.4, 0), (2.2, 1), (2.6, 3.6), (2.3, 6), (1.3, 8), (1.1, 8.8), (1.15, 9.6), (1.5, 10.6), (1.3, 11.6), (0, 12)],
            lambda band, i: "red" if band in (5, 6) else "white", sides=10)
    return s


@model()
def bowling_lane():
    # 床に敷いたおもちゃのレーン (+x へ 292 cm、幅 26 cm)。手前の線・矢印・ピンを並べる印・奥の枠
    s = Shape()
    L, w = 292, 13
    s.box((0, 0, -w), (L, .5, w), "wood_light")
    for z in (-w, w - 3):
        s.box((0, .5, z), (L - 10, .9, z + 3), "gray")
    s.box((10, .5, -w + 3), (12, .6, w - 3), "red", thick=False)
    for k in range(5):
        z = -7 + k * 3.5
        x = 60 + abs(k - 2) * 6
        s.face([(x, .62, z - 1.2), (x, .62, z + 1.2), (x + 5, .62, z)], "red")
    for row in range(4):
        for k in range(row + 1):
            x, z = 246 + row * 8, (k - row / 2) * 6.4
            s.lathe([(1, .5), (1, .6)], "white", sides=6, at=(x, z))
    s.box((L - 4, .5, -w), (L, .7, w), "wood_red", thick=False)
    return s


PAINT_SPOTS = [(-18, -8, "red"), (-4, -13, "yellow"), (12, -10, "green_leaf"), (22, 3, "blue")]


@model()
def paint_palette():
    # 床に置いた大きな絵の具のパレット (横 x 70 cm・縦 z 46 cm)。4 色の絵の具のたまりと筆
    s = Shape()
    ring = [(35 * math.cos(a) * (1 + .06 * math.sin(3 * a)), 23 * math.sin(a)) for a in [6.2831853 * i / 16 for i in range(16)]]
    top = [(x, .4, z) for x, z in ring]
    bot = [(x, 0, z) for x, z in ring]
    s.face(top, "cream")
    s.face(bot, "cream")
    s.loft([bot + [bot[0]], top + [top[0]]], "wood_light")
    s.lathe([(4, .38), (4, .44)], "wood", sides=10, at=(-24, 10))
    for x, z, c in PAINT_SPOTS:
        s.lathe([(9, .4), (8.6, .7), (5, .85), (0, .9)], c, sides=10, at=(x, z))
    s.tube((-6, 1.4, 14), (24, 1.4, 18), .9, .9, "wood_red", sides=6)
    s.tube((-6, 1.4, 14), (-10, 1.4, 13.4), 1.1, 1.1, "gray_light", sides=6)
    s.tube((-10, 1.4, 13.4), (-15, 1.2, 12.6), 1.1, .2, "blue", sides=6)
    return s


@model()
def paint_tube():
    # 絵の具のチューブ (白で作り、ゲームで色を付ける)
    s = Shape()
    s.box((-3, 0, -1.4), (2.6, 2, 1.4), "white")
    s.tube((2.6, 1, 0), (4.4, 1, 0), .8, .8, "gray_light", sides=6)
    s.box((-3.4, 0, -1.5), (-3, 2, 1.5), "gray_light")
    return s


@model()
def spill():
    # こぼれた液体のたまり (半径 10 cm、白で作り、ゲームで色を付ける)
    s = Shape()
    ring = []
    for i in range(14):
        a = 6.2831853 * i / 14
        r = 10 * (1 + .18 * math.sin(3 * a + .7) + .1 * math.cos(5 * a))
        ring.append((r * math.cos(a), r * math.sin(a)))
    top = [(x, .4, z) for x, z in ring]
    bot = [(x, 0, z) for x, z in ring]
    s.face(top, "white")
    s.loft([bot + [bot[0]], top + [top[0]]], "white")
    return s


@model()
def honey_jar():
    s = Shape()
    s.lathe([(4.6, 0), (5.2, 1), (5.2, 8), (4, 9.4), (3.4, 9.4)], "honey", sides=10)
    s.lathe([(3.6, 9.2), (4, 9.4), (4, 11), (0, 11.2)], lambda band, i: "red" if i % 2 else "white", sides=10)
    s.lathe([(5.25, 3), (5.25, 6)], "cream", sides=10)
    return s


@model()
def glue_bottle():
    s = Shape()
    s.lathe([(3, 0), (3.2, .6), (3.2, 10), (2.2, 12), (1, 12.6)], "white", sides=10)
    s.lathe([(1.4, 12.2), (1.4, 14), (.5, 16), (0, 16.4)], "orange", sides=8)
    s.lathe([(3.25, 3), (3.25, 7.4)], "blue", sides=10)
    return s


@model()
def juice_cup():
    s = Shape()
    s.lathe([(3, 0), (4, 9), (3.6, 9), (2.6, .8), (0, .8)], "pink", sides=10)
    s.tube((1, 2, 0), (2.6, 14, 1), .35, .35, "white", sides=4)
    return s


@model()
def kami_hikoki():
    # 紙飛行機 (先が +x)。翼と胴の折り目を薄い板で作る
    s = Shape()
    wing = [(8, 0), (-6, -5.2), (-6, 5.2)]
    a = [(x, 1.5, z) for x, z in wing]
    b = [(x, 1.9, z) for x, z in wing]
    s.loft([a + [a[0]], b + [b[0]]], "paper")
    s.cap(a, "paper")
    s.cap(b, "white")
    keel = [(8, 1.6), (-6, 1.6), (-6, 0)]
    prism(s, [(7.6, 1.45), (-6, 1.45), (-6, 0)], -.2, .2, "paper")
    return s


# ---- 書き出し -----------------------------------------------------------------------------------
def make_object(name, shape):
    # Blender は z が上なので、y が上の座標を回して入れる。書き出しで元に戻る
    # (エンジンは読み込むときに x を反転するので、ここで先に反転してゲームの座標と合わせる)
    verts = [(-x, -z, y) for x, y, z in shape.verts]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], shape.faces)
    mesh.update()
    layer = mesh.uv_layers.new(name="UVMap")
    k = 0
    for poly, uvs in zip(mesh.polygons, shape.uvs):
        for li, uv in zip(poly.loop_indices, uvs):
            layer.data[li].uv = uv
            k += 1
    # 同じ位置の頂点をつなげ、面の向きを外へそろえる (色の境目は UV が別なので残る)
    b = bmesh.new()
    b.from_mesh(mesh)
    bmesh.ops.remove_doubles(b, verts=b.verts, dist=1e-4)
    bmesh.ops.recalc_face_normals(b, faces=b.faces)
    b.to_mesh(mesh)
    b.free()
    for poly in mesh.polygons:
        poly.use_smooth = False
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def export(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.wm.obj_export(filepath=str(OUT / f"{obj.name}.obj"), export_selected_objects=True, export_materials=False,
                          export_uv=True, export_normals=True, export_triangulated_mesh=True,
                          forward_axis="NEGATIVE_Z", up_axis="Y")


def preview(objects, path):
    """全部を並べて 1 枚の画像にする"""
    image = bpy.data.images.load(str(PALETTE))
    image.colorspace_settings.name = "sRGB"
    material = bpy.data.materials.new("palette")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Closest"
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = .8
    material.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    cols = math.ceil(math.sqrt(len(objects)))
    slot = 1.0
    for i, obj in enumerate(objects):
        obj.data.materials.append(material)
        size = max(obj.dimensions)
        obj.scale = (slot * .8 / size,) * 3
        obj.location = ((i % cols) * slot * 1.15, -(i // cols) * slot * 1.15, 0)
        obj.rotation_euler = (0, 0, .6)
    rows = math.ceil(len(objects) / cols)
    mid = ((cols - 1) * slot * .575, -(rows - 1) * slot * .575, 0)
    span = max(cols, rows) * slot * 1.15
    bpy.ops.object.camera_add(location=(mid[0], mid[1] - span * .95, span * .95))
    cam = bpy.context.object
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = span * 1.05
    cam.rotation_euler = (math.radians(45), 0, 0)
    bpy.context.scene.camera = cam
    bpy.ops.object.light_add(type="SUN", rotation=(math.radians(50), 0, math.radians(30)))
    bpy.context.object.data.energy = 3
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (.82, .86, .9, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = .9
    bpy.context.scene.world = world
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 12
    scene.cycles.device = "CPU"
    scene.view_settings.view_transform = "Standard"
    scene.render.resolution_x = scene.render.resolution_y = 1400
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = args[args.index("--only") + 1].split(",") if "--only" in args else None
    preview_path = args[args.index("--preview") + 1] if "--preview" in args else None
    bpy.ops.wm.read_factory_settings(use_empty=True)
    OUT.mkdir(parents=True, exist_ok=True)
    objects = []
    for name, build in MODELS.items():
        if only and name not in only:
            continue
        # 太らせる量はモデルの大きさに合わせる (一度作って大きさを測る)
        Shape.EPS = 0
        first = build()
        size = max(max(v[i] for v in first.verts) - min(v[i] for v in first.verts) for i in range(3))
        Shape.EPS = max(size * .0008, .02)
        obj = make_object(name, build())
        export(obj)
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        d = obj.dimensions
        print(f"LOWPOLY {name}: {tris} tris, {d.x:.1f} x {d.z:.1f} x {d.y:.1f} cm")
        objects.append(obj)
    if preview_path:
        # 文字や柄だけ違う物は 1 つだけ見せる (残りは元の大きさのまま映らないよう消す)
        for o in [o for o in objects if (o.name.startswith("koma_") and o.name != "koma_ou") or o.name in ("trump_back", "trump_black")]:
            objects.remove(o)
            bpy.data.objects.remove(o)
        preview(objects, preview_path)


main()
