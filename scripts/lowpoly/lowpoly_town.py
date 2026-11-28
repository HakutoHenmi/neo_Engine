# lowpoly_town.py
# ステージ 2「坂の町」のローポリのモデル。谷底の川と線路から、駄菓子屋の通り・ひな壇の住宅・山の上の神社と墓地へ登る段々の町。
# 単位は cm の実寸で、y が上、底は y=0、正面は +z。小物 (〜1m)・中くらい (1〜5m)・大物 (5m〜)・地形の部品の順に並べる。
# build_lowpoly.py から読み込まれる (--group town でこの組だけ作れる)。
import math

from lowpoly_core import Shape, circle, model, prism, rot, rounded_rect, set_group, wheel  # noqa: F401

# 大きな建物でも細い部品が太りすぎないよう、太らせる量は 0.6cm までにする
set_group("town", eps=.6)


# ---- 共通の部品 ----------------------------------------------------------------------------------
def bx(s, a, b, color, thick=True):
    """2 つの角から箱を作る (角の順番は問わない)"""
    s.box(tuple(min(a[i], b[i]) for i in range(3)), tuple(max(a[i], b[i]) for i in range(3)), color, thick)


def plate(s, pts, t, color, side=None):
    """上の面 pts を下へ t だけ押し出した板 (屋根・ひさし)"""
    top = [tuple(p) for p in pts]
    bot = [(x, y - t, z) for x, y, z in top]
    s.loft([bot, top], side or color)
    s.cap(top, color)
    s.cap(bot, side or color)


def merge(dst, src, swap=False, at=(0, 0, 0)):
    """別に作った形を足す。swap で x と z を入れ替える (x の向きに作った物を z の向きにする)"""
    for f, uv in zip(src.faces, src.uvs):
        pts = [src.verts[i] for i in f]
        if swap:
            pts = [(z, y, x) for x, y, z in pts]
        dst.face([(x + at[0], y + at[1], z + at[2]) for x, y, z in pts], None, uv=uv)


def gable(s, hw, hd, h, ridge, over, color, wall, t=6, side="wood_old"):
    """切妻の屋根 (棟は x の向き)。壁の上 h から棟 ridge へ上がり、軒を over だけ出す。両端の三角の壁と棟も作る"""
    slope = (ridge - h) / hd
    e = h - slope * over
    W = hw + over
    plate(s, [(-W, e, hd + over), (W, e, hd + over), (W, ridge, 0), (-W, ridge, 0)], t, color, side)
    plate(s, [(W, e, -hd - over), (-W, e, -hd - over), (-W, ridge, 0), (W, ridge, 0)], t, color, side)
    s.face([(hw, h, -hd), (hw, h, hd), (hw, ridge - t, 0)], wall)
    s.face([(-hw, h, hd), (-hw, h, -hd), (-hw, ridge - t, 0)], wall)
    s.tube((-W, ridge + t * .3, 0), (W, ridge + t * .3, 0), t * 1.3, t * 1.3, color, sides=4)


def hip(s, hw, hd, h, rise, over, color, t=6, side="wood_old"):
    """寄棟の屋根 (4 面。棟は x の向き)"""
    W, D = hw + over, hd + over
    e = h - rise / hd * over
    top = h + rise
    r = max(W - D, W * .12)
    plate(s, [(-W, e, D), (W, e, D), (r, top, 0), (-r, top, 0)], t, color, side)
    plate(s, [(W, e, -D), (-W, e, -D), (-r, top, 0), (r, top, 0)], t, color, side)
    plate(s, [(W, e, D), (W, e, -D), (r, top, 0)], t, color, side)
    plate(s, [(-W, e, -D), (-W, e, D), (-r, top, 0)], t, color, side)
    s.tube((-r, top + t * .3, 0), (r, top + t * .3, 0), t * 1.3, t * 1.3, color, sides=4)


def window(s, face, u, y, w, h, glass="glass", frame="white", bars=1):
    """壁の外の面に窓を貼る。face は (軸 "x" か "z", 壁の位置, 外の向き ±1)、u は壁に沿う中心、y は下の端"""
    axis, pos, out = face

    def put(u0, u1, y0, y1, d0, d1, c):
        if axis == "z":
            bx(s, (u0, y0, pos + out * d0), (u1, y1, pos + out * d1), c, thick=False)
        else:
            bx(s, (pos + out * d0, y0, u0), (pos + out * d1, y1, u1), c, thick=False)
    # 枠は窓のまわりの 4 本の帯にして、ガラスと重ねない (重なると遠くでちらつく)。ガラスは枠と桟より 3cm 奥
    put(u - w / 2 - 3, u + w / 2 + 3, y - 3, y, 0, 5.5, frame)
    put(u - w / 2 - 3, u + w / 2 + 3, y + h, y + h + 3, 0, 5.5, frame)
    put(u - w / 2 - 3, u - w / 2, y, y + h, 0, 5.5, frame)
    put(u + w / 2, u + w / 2 + 3, y, y + h, 0, 5.5, frame)
    put(u - w / 2, u + w / 2, y, y + h, 0, 2.5, glass)
    for k in range(1, bars + 1):
        m = u - w / 2 + w * k / (bars + 1)
        put(m - 1.2, m + 1.2, y, y + h, 0, 5.5, frame)


def crown(s, x, y, z, r, color, squash=1.0, level=1):
    s.ball((x, y, z), r, color, squash=squash, level=level)


def leafy(a="green_leaf", b="green_dark"):
    return lambda i: b if i % 3 == 0 else a


# ==== 小物 (〜1m) ===================================================================================
@model()
def bou_gashi():
    # 棒の駄菓子 (長さ 17cm)。両端をねじった包み
    s = Shape()
    s.tube((-6, 1.5, 0), (6, 1.5, 0), 1.5, 1.5, "yellow", sides=8)
    for x in (-1, 1):
        s.tube((x * 6, 1.5, 0), (x * 7.4, 1.5, 0), 1.3, .35, "yellow", sides=6)
        s.tube((x * 7.4, 1.5, 0), (x * 8.4, 1.5, 0), .35, 1.1, "yellow", sides=6)
    s.tube((-2.4, 1.5, 0), (2.4, 1.5, 0), 1.55, 1.55, "red", sides=8)
    s.tube((-.8, 1.5, 0), (.8, 1.5, 0), 1.6, 1.6, "white", sides=8)
    return s


@model()
def ramune():
    # ラムネの瓶 (高さ 21cm)。くびれにビー玉が入る
    s = Shape()
    prof = [(3, 0), (3.3, .6), (3.3, 11), (2.6, 13), (1.8, 15.5), (2.3, 16.5), (1.6, 18), (1.4, 20), (1.6, 21), (0, 21.3)]
    s.lathe(prof, lambda band, i: "blue" if band >= 7 else "glass", sides=8)
    s.lathe([(3.36, 4), (3.36, 8.5)], "sky", sides=8)
    return s


@model()
def dagashi_box():
    # 駄菓子を並べた浅い木の箱 (44 x 30cm)
    s = Shape()
    w, d = 22, 15
    bx(s, (-w, 0, -d), (w, 2, d), "wood_light")
    bx(s, (-w, 0, d - 1.5), (w, 6, d), "wood")
    bx(s, (-w, 0, -d), (w, 10, -d + 1.5), "wood")
    for x in (-w, w - 1.5):
        bx(s, (x, 0, -d), (x + 1.5, 8, d), "wood")
    colors = ["red", "yellow", "pink", "sky", "green_leaf", "orange", "purple", "white"]
    for i in range(5):
        for j in range(3):
            x, z, c = -w + 5 + i * 8.4, -d + 5 + j * 9.2, colors[(i * 3 + j) % len(colors)]
            k = (i + j) % 3
            if k == 0:
                s.ball((x, 4.4, z), 2.6, c, level=1)
            elif k == 1:
                bx(s, (x - 3, 2, z - 3.5), (x + 3, 5 + (i + j) % 2 * 2.5, z + 3.5), c)
            else:
                s.tube((x - 3.4, 3.4, z), (x + 3.4, 3.4, z), 1.3, 1.3, c, sides=6)
    return s


@model()
def gachagacha():
    # カプセルの自動販売機 (高さ 1.1m)。丸い窓の中にカプセルが見える
    s = Shape()
    bx(s, (-24, 0, -22), (24, 8, 22), "charcoal")
    bx(s, (-22, 8, -20), (22, 62, 20), "red")
    bx(s, (-15, 24, 20), (15, 50, 21.4), "white")
    s.tube((0, 40, 21), (0, 40, 25), 7, 7, "gray_light", sides=10)
    bx(s, (-7, 39, 24), (7, 41.5, 28), "gray")
    bx(s, (-9, 11, 19), (9, 20, 23), "charcoal")
    s.ball((0, 86, 0), 23, lambda i: ("glass", "pink", "glass", "yellow", "glass", "sky", "glass", "green_leaf")[i % 8], level=2)
    s.lathe([(16, 61), (18, 64), (0, 64)], "red", sides=10)
    s.lathe([(9, 107), (9, 111), (0, 112)], "red", sides=8)
    return s


@model()
def kasa():
    # 開いて置いた傘 (直径 92cm)
    s = Shape()
    s.tube((0, 10, 0), (0, 93, 0), .9, .9, "charcoal", sides=5)
    s.lathe([(0, 97), (14, 95), (30, 89), (42, 81), (46, 77)], lambda band, i: "red" if i % 2 else "white", sides=8)
    s.tube((0, 10, 0), (0, 4, 0), 1.4, 1.4, "wood", sides=5)
    s.tube((0, 4, 0), (5, 1.5, 0), 1.4, 1.4, "wood", sides=5)
    s.tube((5, 1.5, 0), (8, 5, 0), 1.4, 1.4, "wood", sides=5)
    return s


@model()
def nagagutsu():
    # 黄色い長靴 1 足 (高さ 32cm)
    s = Shape()
    for z in (-7, 7):
        s.lathe([(5.4, 4), (5.8, 30), (6.3, 31.5)], lambda band, i: "charcoal" if band == 1 else "yellow", sides=8, at=(0, z))
        bx(s, (-6, 0, z - 5.4), (13, 5, z + 5.4), "yellow")
        bx(s, (-6.5, 0, z - 5.8), (13.5, 1.4, z + 5.8), "charcoal")
    return s


@model()
def randoseru():
    s = Shape()
    w, h, d = 12, 30, 9
    bx(s, (-w, 0, -d), (w, h, d), "red")
    bx(s, (-w - .6, 5, d - 1), (w + .6, h + 1.4, d + 2), "red_dark")
    bx(s, (-2.5, 6, d + 2), (2.5, 11, d + 3), "gold")
    for x in (-7, 7):
        bx(s, (x - 2, 4, -d - 2.2), (x + 2, h - 2, -d), "black")
    return s


@model()
def bat():
    # 木のバット (長さ 85cm、横に寝かせる)
    s = Shape()
    y = 3.3
    s.tube((-43, y, 0), (-41.5, y, 0), 2.3, 2.3, "wood_dark", sides=6)
    s.tube((-41.5, y, 0), (-22, y, 0), 1.3, 1.5, "charcoal", sides=6)
    s.tube((-22, y, 0), (8, y, 0), 1.5, 3.2, "wood_light", sides=8)
    s.tube((8, y, 0), (42, y, 0), 3.2, 3.3, "wood_light", sides=8)
    return s


@model()
def yakyu_ball():
    s = Shape()
    s.ball((0, 3.7, 0), 3.7, lambda i: "red" if i % 13 == 0 else "white", level=2)
    return s


@model()
def takebouki():
    # 竹ぼうき (高さ 1.35m)
    s = Shape()
    s.tube((0, 24, 0), (0, 135, 0), 1.6, 1.4, "straw", sides=6)
    for y in (60, 100):
        s.tube((0, y, 0), (0, y + 1.5, 0), 1.9, 1.9, "wood", sides=6)
    s.lathe([(18, 0), (15, 12), (6, 24), (2, 30), (0, 30.5)], lambda band, i: "wood_old" if band == 2 else ("straw" if i % 2 else "wood_light"), sides=10)
    return s


@model()
def gyunyu_box():
    # 牛乳びんの木の箱 (びん 8 本)
    s = Shape()
    w, d = 21, 14
    bx(s, (-w, 0, -d), (w, 2, d), "wood")
    for z in (-d, d - 1.5):
        bx(s, (-w, 0, z), (w, 13, z + 1.5), "wood")
    for x in (-w, w - 1.5):
        bx(s, (x, 0, -d), (x + 1.5, 13, d), "wood")
    for i in range(4):
        for k in range(2):
            s.lathe([(3, 2), (3.2, 3), (3.2, 15), (2, 19), (2, 20.5), (2.3, 21), (0, 21.2)],
                    lambda band, j: "sky" if band >= 5 else "white", sides=8, at=(-15 + i * 10, -6.5 + k * 13))
    return s


@model()
def kingyobachi():
    # 金魚鉢 (直径 28cm)。ふちが赤い波形のガラス
    s = Shape()
    colors = ["water", "water", "glass", "glass", "glass", "red", "water"]
    s.lathe([(7, 0), (11, 3), (13.5, 9), (13, 15), (10, 19.5), (11.5, 21), (12.5, 22), (0, 19.6)], lambda band, i: colors[band], sides=10)
    s.ball((2.5, 19.8, 0), 2.2, "orange", squash=.5, level=1)
    s.tube((.5, 19.9, 0), (-2.5, 20.2, 1), .6, 1.6, "orange_light", sides=3)
    return s


@model()
def furin():
    # 小さな台につるした風鈴 (高さ 36cm)
    s = Shape()
    bx(s, (-8, 0, -6), (8, 2, 6), "wood_dark")
    s.tube((-6, 2, 0), (-6, 35, 0), .8, .8, "wood_dark", sides=5)
    s.tube((-6, 35, 0), (5, 35, 0), .8, .8, "wood_dark", sides=5)
    s.tube((4, 35, 0), (4, 33, 0), .2, .2, "gray", sides=4)
    s.lathe([(0, 33.5), (2.6, 32), (4, 28), (4.4, 24.5), (0, 24.5)], lambda band, i: "red" if band == 2 and i % 2 else "glass", sides=10, at=(4, 0))
    s.tube((4, 24.5, 0), (4, 18, 0), .15, .15, "gray", sides=4)
    bx(s, (2.2, 8, -.2), (5.8, 18, .2), "sky")
    return s


@model()
def chochin():
    # 赤い提灯 (高さ 55cm)
    s = Shape()
    b = 4
    s.lathe([(6, b), (13, b + 4), (16, b + 12), (16, b + 30), (13, b + 38), (6, b + 42)], "red", sides=10)
    for y in (10, 16, 22, 28, 34, 40):
        r = 16 if 16 <= y <= 34 else 14.8
        s.lathe([(r + .3, b + y - 4), (r + .3, b + y - 3.4)], "red_dark", sides=10)
    s.lathe([(7, 0), (7, b + 1), (0, b + 1)], "black", sides=8)
    s.lathe([(7, b + 41), (7, b + 46), (0, b + 46)], "black", sides=8)
    s.tube((0, b + 46, 0), (0, b + 52, 0), .6, .6, "black", sides=4)
    return s


@model()
def bonsai():
    s = Shape()
    bx(s, (-22, 0, -14), (22, 9, 14), "navy")
    bx(s, (-20, 7, -12), (20, 9.6, 12), "soil")
    s.tube((0, 9, 0), (-5, 20, 2), 3, 2.4, "bark", sides=5)
    s.tube((-5, 20, 2), (6, 30, -1), 2.4, 1.8, "bark", sides=5)
    s.tube((6, 30, -1), (-2, 38, 0), 1.8, 1.2, "bark", sides=5)
    s.tube((-5, 20, 2), (-12, 23, 4), 1.2, .8, "bark", sides=4)
    s.tube((6, 30, -1), (12, 30, -2), 1.2, .8, "bark", sides=4)
    for x, y, z, r in ((-12, 25, 4, 7), (12, 31, -2, 8), (-3, 40, 1, 7)):
        crown(s, x, y, z, r, "green_dark", squash=.5)
    return s


@model()
def tanuki():
    # たぬきの置物 (高さ 60cm)。笠・とっくり・通い帳
    s = Shape()
    s.ball((0, 18, 0), 17, "wood", squash=.95, level=2)
    s.ball((0, 15, 9), 12, "cream", squash=.9, level=1)
    s.ball((0, 41, 2), 11, "wood", level=2)
    s.ball((0, 39, 11), 4.5, "cream", squash=.8, level=1)
    s.ball((0, 40, 15), 1.6, "black", level=1)
    for x in (-1, 1):
        s.ball((x * 4, 44, 9), 2.6, "wood_dark", squash=.7, level=1)
        s.ball((x * 4, 44.5, 11), 1.1, "white", level=1)
        s.tube((x * 6, 3, 6), (x * 7, 1, 13), 4, 3.6, "wood", sides=5)
    s.lathe([(16, 50), (16, 51.5), (8, 52), (7, 57), (0, 59)], "straw", sides=10)
    s.lathe([(4.5, 10), (5, 16), (3.5, 22), (1.6, 26), (2, 28), (0, 28)], "cream", sides=8, at=(-16, 6))
    s.lathe([(5.05, 14), (5.05, 18)], "black", sides=8, at=(-16, 6))
    s.tube((-12, 22, 4), (-16, 25, 6), 3, 2.6, "wood", sides=5)
    bx(s, (13, 14, 3), (19, 25, 7), "cream")
    s.tube((12, 24, 3), (16, 21, 5), 3, 2.6, "wood", sides=5)
    s.tube((0, 8, -14), (4, 3, -24), 5, 3, "wood_dark", sides=5)
    return s


@model()
def suzume():
    # すずめ (長さ 17cm)
    s = Shape()
    s.ball((0, 6, 0), 4.4, "wood", squash=.85, level=1)
    s.ball((3.8, 9, 0), 3, "wood_dark", level=1)
    s.tube((6.4, 9, 0), (8, 8.6, 0), .8, .1, "charcoal", sides=4)
    s.tube((-3, 6.5, 0), (-9, 8, 0), 1.6, .4, "wood_dark", sides=4)
    for z in (-1, 1):
        s.ball((4.4, 8, z * 2.3), .9, "white", level=1)
        s.ball((5.4, 9.6, z * 1.6), .5, "black", level=1)
        s.tube((0, 3, z * 1.2), (0, 0, z * 1.2), .3, .3, "orange_dark", sides=3)
        s.tube((1, 7, z * 3.6), (-5, 6.5, z * 3), 1.6, .4, "wood_old", sides=4)
    return s


@model()
def hato():
    # はと (長さ 32cm)
    s = Shape()
    s.ball((0, 12, 0), 9, "gray", squash=.8, level=1)
    s.ball((7, 18, 0), 5, lambda i: ("teal", "purple")[i % 2], level=1)
    s.ball((10, 23, 0), 4, "gray_light", level=1)
    s.tube((13, 23, 0), (16, 22, 0), 1, .2, "charcoal", sides=4)
    s.tube((-6, 11, 0), (-16, 9, 0), 3, 1.5, "charcoal", sides=4)
    for z in (-1, 1):
        s.ball((12, 24, z * 2.4), .7, "orange", level=1)
        s.tube((2, 14, z * 6), (-12, 12, z * 4), 4, 1, "gray_light", sides=4)
        s.tube((-3, 13.2, z * 6.4), (-6, 12.8, z * 6), 2.2, 2, "charcoal", sides=4)
        s.tube((1, 6, z * 2), (1, 0, z * 2.4), .6, .6, "pink", sides=3)
    return s


@model()
def planter():
    # 花のプランター (長さ 60cm)
    s = Shape()
    bx(s, (-30, 0, -9), (30, 18, 9), "wood_red")
    bx(s, (-28, 15, -7), (28, 17.5, 7), "soil")
    colors = ("red", "yellow", "pink", "purple", "orange", "white", "red")
    for i, c in enumerate(colors):
        x, h = -24 + i * 8, 32 + (i * 7 % 3) * 6
        s.tube((x, 16, 0), (x, h, 0), .5, .5, "green_dark", sides=4)
        s.ball((x, h + 2, 0), 3.2, c, squash=.7, level=1)
        s.ball((x, h + 3.4, 0), 1.1, "yellow" if c != "yellow" else "orange", level=1)
        s.ball((x + 2, h * .62, 1), 2.4, "green_leaf", squash=.5, level=1)
    return s


@model()
def shokaki():
    # 消火器 (高さ 50cm)
    s = Shape()
    s.lathe([(8, 0), (8.5, 1), (8.5, 38), (6, 44), (2, 46), (0, 46)], "red", sides=10)
    s.lathe([(8.6, 18), (8.6, 30)], "white", sides=10)
    bx(s, (-2, 46, -1.5), (7, 48, 1.5), "charcoal")
    bx(s, (-2, 49, -1.5), (6, 50.5, 1.5), "charcoal")
    s.tube((3, 46, 2), (6, 34, 9), 1.2, 1.2, "black", sides=5)
    s.tube((6, 34, 9), (7, 16, 9.5), 1.2, 1.6, "black", sides=5)
    return s


@model()
def baketsu():
    s = Shape()
    s.lathe([(11, 0), (14, 28), (15, 28.5), (15, 30), (13.6, 30), (12.8, 28), (10, 3), (0, 3)], "sky", sides=10)
    s.tube((-15, 29, 0), (-10, 40, 0), .4, .4, "gray", sides=4)
    s.tube((-10, 40, 0), (10, 40, 0), .4, .4, "gray", sides=4)
    s.tube((10, 40, 0), (15, 29, 0), .4, .4, "gray", sides=4)
    return s


@model()
def mushikago():
    # 虫かご (カブトムシ入り)
    s = Shape()
    w, h, d = 14, 18, 9
    bx(s, (-w, 0, -d), (w, 2, d), "green_leaf")
    bx(s, (-w, h, -d), (w, h + 2.5, d), "green_leaf")
    bx(s, (-w + 1, 2, -d + 1), (w - 1, 4, d - 1), "grass")
    for i in range(8):
        x = -w + .6 + i * (2 * w - 1.2) / 7
        for z in (-d, d):
            bx(s, (x - .5, 2, z - .5), (x + .5, h, z + .5), "green_leaf", thick=False)
    for i in range(5):
        z = -d + .6 + i * (2 * d - 1.2) / 4
        for x in (-w, w):
            bx(s, (x - .5, 2, z - .5), (x + .5, h, z + .5), "green_leaf", thick=False)
    s.ball((0, 5, 0), 2.6, "wood_old", squash=.6, level=1)
    s.tube((2, 5.5, 0), (5, 7, 0), .4, .2, "wood_old", sides=3)
    s.tube((-w, h + 2, 0), (-6, h + 12, 0), .6, .6, "blue", sides=4)
    s.tube((-6, h + 12, 0), (6, h + 12, 0), .6, .6, "blue", sides=4)
    s.tube((6, h + 12, 0), (w, h + 2, 0), .6, .6, "blue", sides=4)
    return s


@model()
def mugiwara():
    s = Shape()
    s.lathe([(20, 0), (20, 1.2), (11, 2), (10.7, 5), (10, 9), (9, 12), (0, 12.5)], lambda band, i: "red" if band == 2 else "straw", sides=12)
    return s


@model()
def suika():
    s = Shape()
    s.ball((0, 13.8, 0), 15, lambda i: "green_dark" if (i // 4) % 2 else "green_leaf", squash=.92, level=2)
    s.tube((0, 27.5, 0), (2, 30, 1), .5, .3, "wood_old", sides=3)
    return s


@model()
def geta():
    # げた 1 足
    s = Shape()
    for z in (-6, 6):
        bx(s, (-11, 5, z - 4.5), (11, 7, z + 4.5), "wood_light")
        for x in (-6, 6):
            bx(s, (x - 1, 0, z - 4), (x + 1, 5, z + 4), "wood")
        s.tube((-4, 7, z - 3.5), (6, 9, z), .6, .6, "red", sides=4)
        s.tube((-4, 7, z + 3.5), (6, 9, z), .6, .6, "red", sides=4)
    return s


# ==== 中くらい (1〜5m) ================================================================================
@model()
def jizo():
    # お地蔵さま (高さ 92cm)。赤い前掛けと帽子
    s = Shape()
    bx(s, (-30, 0, -26), (30, 14, 26), "stone_dark")
    s.lathe([(20, 14), (22, 20), (20, 45), (14, 60), (10, 64)], "stone", sides=8)
    s.lathe([(19.5, 46), (16.4, 57), (11, 64.5)], "red", sides=8)
    s.ball((0, 74, 0), 12, "stone", level=2)
    s.lathe([(11.4, 80), (12.5, 82), (9, 88), (0, 91)], "red", sides=8)
    s.tube((0, 40, 15), (0, 50, 19), 3, 2, "stone", sides=5)
    for x in (-1, 1):
        bx(s, (x * 3.5 - 1.6, 75.5, 11), (x * 3.5 + 1.6, 76.3, 11.8), "stone_dark", thick=False)
    return s


@model()
def ishidoro():
    # 石灯籠 (高さ 2.1m)
    s = Shape()
    s.lathe([(34, 0), (34, 10), (26, 14), (0, 14)], "stone_dark", sides=6)
    s.lathe([(16, 14), (18, 18), (14, 22), (14, 92), (18, 96), (0, 96)], "stone", sides=6)
    s.lathe([(34, 96), (38, 104), (30, 110), (0, 110)], "stone", sides=6)
    s.lathe([(22, 110), (22, 150)], "stone", sides=6)
    for z in (-1, 1):
        bx(s, (-8, 120, z * 17.5), (8, 140, z * 19.8), "black")
    s.lathe([(48, 150), (46, 154), (36, 162), (14, 176), (6, 180)], "stone_dark", sides=6)
    s.lathe([(8, 180), (10, 186), (8, 192), (0, 197)], "stone", sides=6)
    s.ball((0, 202, 0), 7, "stone", level=1)
    return s


@model()
def komainu():
    # 台に座る狛犬 (高さ 1.9m)
    s = Shape()
    c = "stone"
    bx(s, (-36, 0, -46), (36, 70, 46), "stone_dark")
    bx(s, (-40, 70, -50), (40, 78, 50), "stone")
    b = 78
    s.ball((0, b + 28, -14), 26, c, squash=.9, level=2)
    s.ball((0, b + 52, 8), 22, c, level=2)
    s.ball((0, b + 86, 16), 22, c, level=2)
    for i in range(7):
        a = math.pi * (i / 6)
        s.ball((math.cos(a) * 21, b + 86 + math.sin(a) * 18, 2), 8, "stone_dark", level=1)
    s.ball((0, b + 80, 36), 10, c, squash=.8, level=1)
    s.ball((0, b + 84, 45), 4, "stone_dark", level=1)
    s.ball((0, b + 50, -38), 13, "stone_dark", level=1)
    for x in (-1, 1):
        s.ball((x * 9, b + 94, 34), 4, "stone_dark", level=1)
        s.tube((x * 14, b + 50, 18), (x * 15, b + 4, 26), 8, 9, c, sides=5)
        s.ball((x * 15, b + 5, 30), 9, c, squash=.6, level=1)
        s.tube((x * 16, b + 100, 10), (x * 25, b + 95, 4), 6, 2, c, sides=4)
    return s


@model()
def saisenbako():
    # さい銭箱 (幅 1.2m)
    s = Shape()
    w, d, h = 60, 32, 58
    bx(s, (-w - 2, 0, -d - 2), (w + 2, 6, d + 2), "wood_dark")
    bx(s, (-w, 6, -d), (w, h, d), "wood_old")
    bx(s, (-w - 3, h, -d - 3), (w + 3, h + 4, d + 3), "wood")
    for i in range(9):
        x = -w + 8 + i * 13
        prism(s, [(x - 5, h + 4), (x + 5, h + 4), (x, h + 11)], -d + 2, d - 2, "wood")
    bx(s, (-w + 10, 14, d), (w - 10, h - 10, d + .6), "wood")
    for x in (-1, 1):
        bx(s, (x * w - 1, h - 12, d - 7), (x * (w - 7), h, d + 1), "gold")
    return s


@model()
def haka():
    # 墓石 (高さ 1.5m)。花立てと線香立て
    s = Shape()
    bx(s, (-50, 0, -50), (50, 14, 50), "stone_dark")
    bx(s, (-38, 14, -38), (38, 32, 38), "stone")
    bx(s, (-30, 32, -30), (30, 50, 30), "stone_dark")
    bx(s, (-20, 50, -20), (20, 150, 20), "gray")
    bx(s, (-21.5, 149, -21.5), (21.5, 153, 21.5), "gray")
    bx(s, (-12, 14, 40), (12, 22, 48), "stone")
    for x in (-1, 1):
        s.lathe([(4, 14), (5, 18), (5, 32), (6, 34), (0, 34)], "stone_dark", sides=6, at=(x * 36, 42))
        s.ball((x * 36, 41, 42), 6, lambda i: ("yellow", "red", "purple", "white")[i % 4], level=1)
        s.ball((x * 36, 37, 42), 5, "green_leaf", level=1)
    return s


@model()
def sotoba():
    # 卒塔婆の束 (高さ 1.8m)
    s = Shape()
    bx(s, (-62, 0, -10), (62, 10, 10), "stone")
    bx(s, (-64, 100, -14), (64, 106, -8), "wood_old")
    for i in range(6):
        x, h, z = -50 + i * 20, 168 + (i * 5 % 3) * 8, -6 + (i % 2) * 2.6
        pts = [(x - 6, 10), (x + 6, 10), (x + 6, h - 14), (x + 3, h - 10), (x + 6, h - 6), (x, h), (x - 6, h - 6), (x - 3, h - 10), (x - 6, h - 14)]
        prism(s, pts, z - 1, z + 1, "wood_light")
    return s


@model()
def hokora():
    # 道ばたの小さな祠 (高さ 1.5m)
    s = Shape()
    bx(s, (-50, 0, -45), (50, 40, 45), "stone")
    bx(s, (-30, 40, -26), (30, 46, 26), "wood_old")
    bx(s, (-26, 46, -22), (26, 100, 22), "wood")
    bx(s, (-20, 50, 22), (20, 92, 23), "wood_old")
    for i in range(5):
        x = -16 + i * 8
        bx(s, (x - .8, 50, 23), (x + .8, 92, 24), "wood_light", thick=False)
    gable(s, 26, 22, 100, 126, 14, "kawara", "wood", t=4)
    s.lathe([(4, 40), (4, 46), (0, 46)], "white", sides=6, at=(-36, 34))
    s.ball((-36, 49, 34), 4, "white", level=1)
    s.lathe([(3, 40), (3.4, 52), (0, 52)], "green_dark", sides=6, at=(36, 34))
    return s


@model()
def denwa_box():
    # 電話ボックス (高さ 2.3m)
    s = Shape()
    w, h = 48, 230
    bx(s, (-w, 10, -w), (w, h - 25, w), "glass")
    bx(s, (-w - 2, 0, -w - 2), (w + 2, 10, w + 2), "white")
    for x in (-1, 1):
        for z in (-1, 1):
            bx(s, (x * w - 4, 0, z * w - 4), (x * w + 4, h, z * w + 4), "white")
    bx(s, (-w - 3, h - 25, -w - 3), (w + 3, h, w + 3), "white")
    bx(s, (-w - 3.5, h - 20, -w - 3.5), (w + 3.5, h - 8, w + 3.5), "green_dark", thick=False)
    bx(s, (-w - 5, h, -w - 5), (w + 5, h + 4, w + 5), "gray_light")
    bx(s, (-w, 96, -w - 1), (w, 102, w + 1), "white", thick=False)
    bx(s, (w - 14, 100, w), (w - 10, 130, w + 3), "gray")
    return s


@model()
def keijiban():
    # 町内会の掲示板 (幅 1.8m)
    s = Shape()
    for x in (-1, 1):
        bx(s, (x * 82 - 4, 0, -4), (x * 82 + 4, 180, 4), "wood_old")
    bx(s, (-90, 90, -3), (90, 170, 3), "wood")
    bx(s, (-86, 94, 3), (86, 166, 3.6), "cream")
    for x0, x1, y0, y1, c in ((-80, -30, 100, 150, "white"), (-24, 20, 120, 162, "yellow"), (26, 80, 98, 140, "sky"), (30, 72, 144, 163, "pink"), (-76, -36, 152, 163, "white")):
        bx(s, (x0, y0, 3.6), (x1, y1, 4.2), c, thick=False)
    plate(s, [(-98, 186, -14), (98, 186, -14), (98, 178, 16), (-98, 178, 16)], 3, "kawara")
    return s


@model()
def bus_tei():
    # バス停の標識 (高さ 2.3m)
    s = Shape()
    s.lathe([(25, 0), (25, 8), (18, 12), (0, 12)], "concrete", sides=10)
    s.tube((0, 12, 0), (0, 222, 0), 3, 3, "gray_light", sides=6)
    s.tube((0, 205, -2), (0, 205, 2), 26, 26, "yellow", sides=14)
    s.tube((0, 205, -2.5), (0, 205, 2.5), 20, 20, "white", sides=14)
    bx(s, (-16, 201, -2.8), (16, 209, 2.8), "navy")
    bx(s, (-18, 120, 3), (18, 170, 6), "white")
    for k in range(5):
        bx(s, (-14, 126 + k * 8, 6), (14, 128 + k * 8, 6.4), "gray", thick=False)
    return s


@model()
def shokasen():
    # 道ばたの消火栓 (高さ 86cm)
    s = Shape()
    s.lathe([(16, 0), (16, 4), (11, 6), (10, 60), (12, 64), (12, 72), (8, 78), (0, 80)], "red", sides=8)
    for x in (-1, 1):
        s.tube((x * 9, 50, 0), (x * 20, 50, 0), 5, 5, "red", sides=6)
        s.tube((x * 20, 50, 0), (x * 23, 50, 0), 6, 6, "gray", sides=6)
    s.lathe([(4, 79), (4, 86), (0, 86)], "gray", sides=6)
    return s


@model()
def propane():
    # プロパンガスのボンベ 2 本 (高さ 1.35m)
    s = Shape()
    bx(s, (-44, 0, -20), (44, 4, 20), "concrete")
    for x in (-22, 22):
        s.lathe([(17, 4), (18, 8), (18, 100), (14, 112), (8, 116), (8, 124), (0, 124)], "gray_light", sides=10, at=(x, 0))
        s.lathe([(10, 124), (10, 134), (0, 134)], "gray", sides=6, at=(x, 0))
        s.tube((x, 128, 0), (0, 140, -14), 1.5, 1.5, "steel", sides=4)
    s.tube((-42, 90, 18.6), (42, 90, 18.6), .7, .7, "charcoal", sides=4)
    bx(s, (-8, 134, -20), (8, 150, -8), "steel")
    return s


@model()
def ido():
    # 手押しポンプの井戸
    s = Shape()
    s.lathe([(50, 0), (50, 60), (42, 60), (42, 10), (0, 10)], lambda band, i: "stone" if (i + band) % 2 else "stone_dark", sides=10)
    bx(s, (-40, 58, -40), (40, 64, 40), "wood")
    s.lathe([(8, 64), (8, 140), (10, 146), (0, 150)], "green_dark", sides=8)
    s.tube((0, 122, 6), (0, 112, 30), 3, 2.4, "green_dark", sides=6)
    s.tube((0, 146, -4), (0, 162, -60), 2, 1.6, "green_dark", sides=5)
    s.lathe([(11, 64), (13, 80), (14, 81), (0, 81)], "wood_light", sides=8, at=(0, 30))
    return s


@model()
def endai():
    # 縁台 (長さ 1.8m)
    s = Shape()
    w, d, h = 90, 45, 42
    for i in range(7):
        z = -d + i * 13.2
        bx(s, (-w, h - 4, z), (w, h, z + 11), "wood_light")
    for x in (-80, 80):
        for z in (-36, 36):
            bx(s, (x - 3, 0, z - 3), (x + 3, h - 4, z + 3), "wood")
    bx(s, (-82, 14, -2), (82, 18, 2), "wood")
    return s


@model()
def monooki():
    # スチールの物置 (幅 2m)
    s = Shape()
    w, d, h = 100, 60, 190
    bx(s, (-w - 2, 0, -d - 2), (w + 2, 6, d + 2), "concrete_dark")
    bx(s, (-w, 6, -d), (w, h, d), "concrete")
    plate(s, [(-w - 6, h + 12, -d - 8), (w + 6, h + 12, -d - 8), (w + 6, h + 2, d + 8), (-w - 6, h + 2, d + 8)], 4, "tin_teal")
    bx(s, (-w + 4, 10, d), (-1, h - 10, d + 1.2), "plaster", thick=False)
    bx(s, (1, 10, d), (w - 4, h - 10, d + 1.6), "plaster", thick=False)
    for x in range(-80, 81, 20):
        bx(s, (x - .6, 14, d + 1.6), (x + .6, h - 14, d + 2), "concrete_dark", thick=False)
    bx(s, (-10, 90, d + 1.2), (-6, 112, d + 2.6), "charcoal", thick=False)
    bx(s, (6, 90, d + 1.6), (10, 112, d + 2.8), "charcoal", thick=False)
    return s


@model()
def gentsuki():
    # 出前の箱を積んだ原付 (長さ 1.9m)
    s = Shape()
    wheel(s, -60, 0, 24, 24, 5, tire="black", hub="gray")
    wheel(s, 74, 0, 24, 24, 5, tire="black", hub="gray")
    bx(s, (-78, 30, -16), (-10, 62, 16), "sky")
    bx(s, (-74, 62, -14), (-14, 72, 14), "black")
    bx(s, (-14, 26, -14), (30, 32, 14), "charcoal")
    prism(s, [(28, 26), (44, 26), (50, 92), (38, 96)], -18, 18, "sky")
    s.tube((74, 24, 0), (54, 112, 0), 3, 3, "gray", sides=5)
    s.tube((52, 112, -34), (52, 112, 34), 2, 2, "charcoal", sides=4)
    s.tube((56, 102, 0), (63, 102, 0), 7, 7, "yellow", sides=8)
    bx(s, (-96, 72, -20), (-56, 104, 20), "white")
    bx(s, (-97, 82, -12), (-95, 94, 12), "red")
    bx(s, (-81, 40, -8), (-78, 48, 8), "red")
    return s


@model()
def yatai():
    # 屋台 (幅 2.4m)。紅白のひさし・のれん・提灯
    s = Shape()
    w, d = 110, 50
    bx(s, (-w, 30, -d), (w, 100, d), "wood")
    bx(s, (-w - 8, 100, -d - 8), (w + 8, 106, d + 20), "wood_light")
    for z in (-1, 1):
        wheel(s, -60, z * (d + 4), 30, 30, 4, tire="wood_dark", hub="wood")
    bx(s, (70, 0, -40), (80, 30, 40), "wood_dark")
    for x in (-w + 4, w - 4):
        for z in (-d + 4, d - 4):
            bx(s, (x - 3, 106, z - 3), (x + 3, 220, z + 3), "wood_old")
    n = 8
    for i in range(n):
        x0, x1 = -w - 14 + i * (2 * w + 28) / n, -w - 14 + (i + 1) * (2 * w + 28) / n
        plate(s, [(x0, 234, -d - 20), (x1, 234, -d - 20), (x1, 218, d + 32), (x0, 218, d + 32)], 3, "red" if i % 2 else "white")
    for i in range(5):
        x = -w + 10 + i * 44
        bx(s, (x, 176, d + 26), (x + 40, 214, d + 27), "navy")
    s.lathe([(6, 166), (12, 174), (12, 194), (6, 202)], "red", sides=8, at=(w + 12, d + 22))
    s.tube((w + 12, 202, d + 22), (w + 12, 216, d + 22), .6, .6, "black", sides=4)
    s.lathe([(20, 106), (22, 122), (0, 122)], "gray", sides=8, at=(-50, 0))
    return s


@model()
def kei_truck():
    # 白い軽トラック (長さ 3.4m)
    s = Shape()
    l, w = 170, 72
    bx(s, (-l, 22, -w), (l, 48, w), "white")
    bx(s, (60, 48, -w), (l, 170, w), "white")
    bx(s, (l, 100, -w + 6), (l + 1, 160, w - 6), "glass")
    for z in (-1, 1):
        bx(s, (80, 100, z * w), (150, 160, z * (w + 1)), "glass")
    bx(s, (-l, 48, -w), (56, 52, w), "gray")
    for z in (-w, w - 3):
        bx(s, (-l, 52, z), (56, 90, z + 3), "white")
    bx(s, (-l, 52, -w), (-l + 3, 90, w), "white")
    bx(s, (52, 90, -w), (58, 176, w), "gray_light")
    for z in (-1, 1):
        bx(s, (l, 56, z * 40), (l + 2, 70, z * 60), "yellow")
    bx(s, (l, 24, -w - 1), (l + 4, 40, w + 1), "gray")
    for x in (-110, 110):
        for z in (-w + 4, w - 4):
            wheel(s, x, z, 26, 26, 8)
    return s


@model()
def block_bei():
    # ブロック塀 (長さ 4m・高さ 1.6m)
    s = Shape()
    L, t, h = 200, 7.5, 160
    bx(s, (-L, 0, -t), (L, h, t), "concrete")
    bx(s, (-L - 1, h, -t - 1.5), (L + 1, h + 6, t + 1.5), "concrete_dark")
    for row in range(8):
        y = row * 20
        for z in (-t, t):
            if row:
                bx(s, (-L, y - .6, z - .3), (L, y + .6, z + .3), "concrete_dark", thick=False)
            off = 0 if row % 2 else 20
            # 縦の目地は表 (+z) だけ
            for k in range(11 if z > 0 else 0):
                x = -L + off + k * 40
                if -L < x < L:
                    bx(s, (x - .6, y, z - .3), (x + .6, y + 20, z + .3), "concrete_dark", thick=False)
    return s


@model()
def ikegaki():
    # 生け垣 (長さ 3m・高さ 1.2m)
    s = Shape()
    L, d, h = 150, 30, 120
    rings = [rounded_rect(L, d, 14, 0), rounded_rect(L + 4, d + 4, 16, 20), rounded_rect(L + 2, d + 2, 16, h - 14), rounded_rect(L - 6, d - 6, 12, h)]
    s.loft(rings, lambda band, i: "green_dark" if band == 0 else "green_leaf")
    s.cap(rings[0], "green_dark")
    s.cap(rings[-1], "green_leaf")
    for i in range(6):
        s.ball((-L + 25 + i * 50, h - 4, (i % 2) * 8 - 4), 18, "green_leaf", squash=.5, level=1)
    return s


def tree_on(s, trunk, crowns, leaf, branches=(), fruit=None, bark="bark"):
    """幹 (lathe の輪郭)・枝 (始点, 終点, 太さ)・葉の塊 (x, y, z, 半径) から木を作る。fruit は (色, 半径, 数)"""
    s.lathe(trunk, bark, sides=7)
    for a, b, r in branches:
        s.tube(a, b, r, r * .6, bark, sides=5)
    for x, y, z, r in crowns:
        crown(s, x, y, z, r, leaf)
    if fruit:
        color, fr, count = fruit
        for k in range(count):
            x, y, z, r = crowns[k % len(crowns)]
            a, b = k * 2.4, .2 + (k * 1.3) % 1.1
            s.ball((x + r * .92 * math.cos(a) * math.cos(b), y + r * .92 * math.sin(b), z + r * .92 * math.sin(a) * math.cos(b)), fr, color, level=1)


@model()
def kaki_no_ki():
    # 柿の木 (高さ 5m)。橙の実がなる
    s = Shape()
    tree_on(s, [(24, 0), (18, 20), (15, 180), (12, 240)],
            [(-110, 360, 20, 90), (100, 370, -30, 95), (10, 420, 50, 100), (0, 330, -60, 80)], leafy(),
            branches=[((0, 220, 0), (-90, 330, 20), 10), ((0, 230, 0), (80, 340, -30), 10), ((0, 240, 0), (10, 380, 60), 10)],
            fruit=("orange", 9, 18))
    return s


@model()
def sakura():
    # 桜の木 (高さ 6.5m)
    s = Shape()
    crowns = [(-200, 420, 40, 130), (190, 440, -40, 135), (0, 520, 0, 150), (60, 400, 190, 120), (-60, 410, -190, 115), (0, 380, 0, 120)]
    branches = [((0, 260, 0), (-170, 400, 40), 18), ((0, 270, 0), (170, 420, -40), 18), ((0, 280, 0), (0, 480, 0), 18),
                ((0, 260, 0), (50, 380, 160), 16), ((0, 260, 0), (-50, 390, -160), 16)]
    tree_on(s, [(40, 0), (30, 30), (26, 200), (20, 280)], crowns, lambda i: "pink" if i % 5 == 0 else "sakura", branches=branches)
    return s


@model()
def matsu():
    # 曲がった松 (高さ 5m)。平たい葉の塊
    s = Shape()
    pts = [(0, 0, 0), (20, 120, 0), (-10, 240, 10), (30, 360, 0), (10, 450, 0)]
    for i in range(len(pts) - 1):
        s.tube(pts[i], pts[i + 1], 24 - i * 4, 20 - i * 4, "bark", sides=6)
    for x, y, z, r, k in ((-130, 250, 20, 70, 2), (120, 320, -20, 80, 2), (-70, 400, 40, 60, 3), (40, 480, 0, 70, 4), (150, 210, 30, 50, 1)):
        s.tube(pts[k], (x, y, z), 9, 5, "bark", sides=5)
        crown(s, x, y + 10, z, r, "green_dark", squash=.4, level=2)
    return s


@model()
def takeyabu():
    # 竹やぶ (高さ 7〜8.5m)
    s = Shape()
    s.lathe([(220, 0), (200, 8), (0, 10)], "soil", sides=10)
    for k in range(14):
        a = k * 2.39996
        r = 40 + (k * 37) % 140
        x, z = math.cos(a) * r, math.sin(a) * r
        h = 680 + (k * 53) % 180
        prev = (x, 8, z)
        for seg in range(5):
            t = (seg + 1) / 5
            p = (x * (1 + .35 * t * t), 8 + h * t, z * (1 + .35 * t * t))
            s.tube(prev, p, 7, 6.6, "green_leaf", sides=6)
            if seg < 4:
                s.tube((p[0], p[1] - 2, p[2]), (p[0], p[1] + 2, p[2]), 7.8, 7.8, "green_dark", sides=6)
            prev = p
        crown(s, prev[0], prev[1] - 60, prev[2], 80, "green_leaf" if k % 2 else "green_dark", squash=.6)
    return s


@model()
def fumikiri():
    # 踏切の警報機と遮断機 (高さ 3.7m、竿の長さ 6m)
    s = Shape()
    bx(s, (-30, 0, -30), (30, 10, 30), "concrete")
    for k in range(12):
        y0 = 10 + k * 25
        s.tube((0, y0, 0), (0, y0 + 25, 0), 6, 6, "yellow" if k % 2 else "black", sides=6)
    rect = [(-62, -8), (62, -8), (62, 8), (-62, 8)]
    for a in (.7, -.7):
        prism(s, rot([(x, 330 + y) for x, y in rect], 0, 330, a), 6, 9, "yellow")
    s.tube((-50, 272, 7), (50, 272, 7), 3, 3, "black", sides=4)
    for x in (-45, 45):
        s.tube((x, 272, 7), (x, 272, 16), 16, 16, "black", sides=10)
        s.tube((x, 272, 16), (x, 272, 17.5), 11, 11, "red", sides=10)
    s.lathe([(16, 352), (14, 362), (0, 368)], "black", sides=8)
    bx(s, (14, 40, -18), (56, 130, 18), "yellow")
    for k in range(12):
        x0 = 50 + k * 50
        s.tube((x0, 110, 0), (x0 + 50, 110, 0), 5, 5, "yellow" if k % 2 == 0 else "black", sides=6)
    return s


@model()
def gomi_station():
    # ごみの集積所の網の箱 (幅 1.8m)
    s = Shape()
    w, d, h = 90, 60, 110
    bx(s, (-w, 0, -d), (w, h, d), "green_leaf")
    for x in (-1, 1):
        for z in (-1, 1):
            s.tube((x * w, 0, z * d), (x * w, h, z * d), 2.4, 2.4, "green_dark", sides=4)
    for y in (2, 55):
        bx(s, (-w - 1, y, -d - 1), (w + 1, y + 3, d + 1), "green_dark", thick=False)
    plate(s, [(-w - 6, h + 12, -d - 6), (w + 6, h + 12, -d - 6), (w + 6, h + 2, d + 6), (-w - 6, h + 2, d + 6)], 3, "green_dark")
    return s


@model()
def buranko():
    # 公園のぶらんこ (幅 3.8m)
    s = Shape()
    H, L = 230, 180
    for x in (-L, L):
        s.tube((x, 0, -90), (x, H, 0), 5, 5, "red", sides=6)
        s.tube((x, 0, 90), (x, H, 0), 5, 5, "red", sides=6)
    s.tube((-L - 10, H, 0), (L + 10, H, 0), 6, 6, "red", sides=6)
    for x in (-80, 80):
        for dx in (-22, 22):
            s.tube((x + dx, H, 0), (x + dx, 45, 0), 1, 1, "steel", sides=4)
        bx(s, (x - 26, 40, -12), (x + 26, 45, 12), "yellow")
    return s


@model()
def tetsubo():
    # 高さ違いの鉄棒 (幅 3m)
    s = Shape()
    xs, hs = (-150, -50, 50, 150), (90, 115, 140)
    for i, x in enumerate(xs):
        top = hs[min(i, 2)] if i < 3 else hs[2]
        s.tube((x, 0, 0), (x, max(hs[max(i - 1, 0)], top) + 10, 0), 4, 4, "blue", sides=6)
    for i in range(3):
        s.tube((xs[i], hs[i], 0), (xs[i + 1], hs[i], 0), 2, 2, "steel", sides=6)
    return s


@model()
def koinobori():
    # こいのぼり (竿の高さ 10m)。吹き流しと 3 匹のこい
    s = Shape()
    s.lathe([(14, 0), (10, 1000), (0, 1010)], "wood_light", sides=6)
    s.ball((0, 1030, 0), 20, "gold", level=1)
    for k, c in enumerate(("red", "blue", "yellow", "green_leaf", "white")):
        s.tube((14 + k * 70, 960, 0), (14 + (k + 1) * 70, 955 - k * 6, 0), 26 - k * 2, 24 - k * 2, c, sides=8)
    for y, L, c in ((860, 420, "black"), (720, 350, "red"), (590, 290, "blue")):
        r = L * .17
        s.tube((14, y, 0), (14 + L * .6, y - 20, 0), r, r * .8, c, sides=8)
        s.tube((14 + L * .6, y - 20, 0), (14 + L * .92, y - 36, 0), r * .8, r * .2, c, sides=8)
        prism(s, [(14 + L * .9, y - 36), (14 + L * 1.05, y - 4), (14 + L * 1.05, y - 70)], -3, 3, c)
        s.tube((12, y, 0), (20, y, 0), r * 1.05, r * 1.05, "white", sides=8)
        for z in (-1, 1):
            s.ball((14 + L * .12, y + r * .3, z * r * .8), r * .3, "white", level=1)
            s.ball((14 + L * .12, y + r * .3, z * r * .98), r * .15, "black", level=1)
    return s


# ==== 大物 (5m〜) ===================================================================================
@model()
def house_kawara():
    # 2 階建ての瓦屋根の家 (8.4 x 6.6m)。1 階は板張り、玄関は格子戸、1 階の前後にひさし
    s = Shape()
    W, D, h1, h = 420, 330, 290, 560
    bx(s, (-W - 10, 0, -D - 10), (W + 10, 40, D + 10), "stone")
    # (壁は太らせず、1 階の板張りと 2 階のしっくいに分ける。重ねると遠くでちらつく)
    bx(s, (-W, 40, -D), (W, h1, D), "wood_old", thick=False)
    bx(s, (-W, h1, -D), (W, h, D), "plaster", thick=False)
    for z in (-1, 1):
        plate(s, [(-W - 40, h1 + 60, z * D), (W + 40, h1 + 60, z * D), (W + 40, h1 + 20, z * (D + 80)), (-W - 40, h1 + 20, z * (D + 80))], 8, "kawara")
    gable(s, W, D, h, h + 230, 70, "kawara", "plaster", t=10)
    window(s, ("z", D, 1), -200, 60, 260, 190, bars=3)
    for x in (-230, 0, 230):
        window(s, ("z", D, 1), x, 340, 150, 120, bars=1)
        window(s, ("z", -D, -1), x, 340, 150, 120, bars=1)
    window(s, ("z", -D, -1), 0, 90, 220, 140, bars=2)
    for z in (-160, 160):
        window(s, ("x", W, 1), z, 340, 120, 110)
        window(s, ("x", -W, -1), z, 120, 120, 110)
    bx(s, (140, 40, D), (300, 260, D + 3), "charcoal", thick=False)
    for k in range(9):
        x = 150 + k * 17.5
        bx(s, (x - 2, 50, D + 3), (x + 2, 250, D + 6), "wood_light", thick=False)
    bx(s, (120, 0, D + 10), (320, 30, D + 80), "stone")
    return s


@model()
def house_hiraya():
    # 縁側のある平屋 (10.4 x 7.6m)。寄棟の瓦屋根
    s = Shape()
    W, D, h = 520, 380, 330
    bx(s, (-W - 10, 0, -D - 10), (W + 10, 50, D + 10), "stone")
    bx(s, (-W, 50, -D), (W, 120, D), "wood_old", thick=False)
    bx(s, (-W, 120, -D), (W, h, D), "plaster", thick=False)
    xs = [-W + i * (2 * W) / 6 for i in range(7)]
    for x in xs:
        bx(s, (x - 7, 82, D - 1), (x + 7, h, D + 4), "wood_old")
    for a, b in zip(xs, xs[1:]):
        window(s, ("z", D, 1), (a + b) / 2, 90, b - a - 22, 200, glass="glass", frame="wood_light", bars=1)
    bx(s, (-W, 70, D), (W, 82, D + 90), "wood_light")
    for x in (-W + 10, W - 10):
        bx(s, (x - 6, 0, D + 80), (x + 6, 70, D + 90), "wood_old")
    bx(s, (-70, 0, D + 92), (70, 40, D + 150), "stone")
    for z in (-200, 0, 200):
        window(s, ("x", W, 1), z, 130, 110, 110, bars=1)
        window(s, ("x", -W, -1), z, 130, 110, 110, bars=1)
    for x in (-300, 0, 300):
        window(s, ("z", -D, -1), x, 130, 140, 110, bars=1)
    hip(s, W, D, h, 250, 110, "kawara", t=10)
    return s


@model()
def house_tin():
    # トタン張りの細い 2 階建て (5.6 x 6.8m)。正面が妻、2 階に物干しのベランダ
    s = Shape()
    W, D, h = 280, 340, 600
    bx(s, (-W - 6, 0, -D - 6), (W + 6, 30, D + 6), "concrete")
    bx(s, (-W, 30, -D), (W, h, D), "tin_teal", thick=False)
    for x in range(-W + 15, W, 30):
        for z in (-1, 1):
            bx(s, (x - 3, 30, z * D), (x + 3, h, z * (D + 2)), "tin_teal", thick=False)
    g = Shape()
    gable(g, D, W, h, h + 220, 50, "tin_rust", "tin_teal", t=6)
    merge(s, g, swap=True)
    bx(s, (-W + 40, 30, D), (-W + 160, 240, D + 4), "wood_old", thick=False)
    window(s, ("z", D, 1), 120, 110, 160, 110, bars=1)
    window(s, ("z", D, 1), 0, 340, 180, 150, bars=2)
    bx(s, (-W + 30, 300, D), (W - 30, 312, D + 90), "steel")
    bx(s, (-W + 30, 312, D + 84), (W - 30, 400, D + 90), "steel", thick=False)
    s.tube((-W + 40, 470, D + 60), (W - 40, 470, D + 60), 2, 2, "sky", sides=4)
    for x, c in ((-150, "white"), (-70, "pink"), (10, "yellow"), (90, "white")):
        bx(s, (x, 400, D + 59), (x + 60, 468, D + 61), c, thick=False)
    for z in (-200, 0, 200):
        window(s, ("x", W, 1), z, 340, 100, 100)
        window(s, ("x", -W, -1), z, 110, 100, 100)
    bx(s, (W, 40, 60), (W + 30, 100, 140), "white")
    return s


@model()
def apartment():
    # 木造の 2 階建てアパート (14 x 6.6m)。2 階の外廊下と鉄の外階段
    s = Shape()
    W, D, f = 700, 330, 290
    h = 2 * f + 20
    bx(s, (-W - 6, 0, -D - 6), (W + 6, 30, D + 6), "concrete")
    bx(s, (-W, 30, -D), (W, h, D), "plaster", thick=False)
    bx(s, (-W, f + 20, D), (W, f + 36, D + 120), "concrete_dark")
    bx(s, (-W, f + 36, D + 112), (W, f + 120, D + 120), "tin_teal", thick=False)
    for x in range(-W + 10, W, 200):
        bx(s, (x - 5, 30, D + 108), (x + 5, f + 20, D + 118), "steel")
    for i, x in enumerate((-525, -175, 175, 525)):
        for fl, y in enumerate((30, f + 36)):
            bx(s, (x - 40, y, D), (x + 40, y + 200, D + 3), ("wood_red", "navy")[(i + fl) % 2], thick=False)
            bx(s, (x + 26, y + 90, D + 3), (x + 32, y + 110, D + 6), "gold", thick=False)
            window(s, ("z", D, 1), x + 110, y + 100, 80, 70, bars=1)
            window(s, ("z", -D, -1), x, y + 80, 160, 120, bars=1)
            bx(s, (x - 40, y + 160, -D - 2), (x + 40, y + 170, -D - 60), "steel")
    top = f + 20
    for k in range(1, 16):
        y = top - k * (top - 30) / 15
        z0, z1 = D + 120 - (k - 1) * 30, D + 120 - k * 30
        bx(s, (W, y - 10, z1), (W + 100, y, z0), "steel")
    bx(s, (W, top - 10, D), (W + 100, top + 6, D + 120), "steel")
    gable(s, W, D, h, h + 120, 40, "kawara_light", "plaster", t=8)
    for x in (-500, 0, 500):
        s.tube((x, h + 60, 0), (x, h + 140, 0), 2, 2, "steel", sides=4)
        s.tube((x - 40, h + 130, 0), (x + 40, h + 130, 0), 1.5, 1.5, "steel", sides=4)
    return s


@model()
def danchi():
    # 4 階建ての団地 (30 x 10m)。正面にベランダ、裏に階段室
    s = Shape()
    W, D, f, floors = 1500, 500, 280, 4
    h = floors * f + 40
    bx(s, (-W, 0, -D), (W, h, D), "concrete", thick=False)
    bx(s, (-W - 8, h, -D - 8), (W + 8, h + 30, D + 8), "concrete_dark")
    units = 8
    uw = 2 * W / units
    futon = ("sky", "pink", "white", "yellow", "orange")
    for fl in range(floors):
        y0 = fl * f + 40
        for u in range(units):
            x0, x1 = -W + u * uw + 15, -W + (u + 1) * uw - 15
            bx(s, (x0, y0, D), (x1, y0 + 12, D + 120), "concrete")
            bx(s, (x0, y0 + 12, D + 110), (x1, y0 + 110, D + 120), "concrete_dark")
            bx(s, (x1, y0, D), (x1 + 30, y0 + f, D + 120), "concrete")
            bx(s, (x0 + 20, y0 + 12, D), (x1 - 20, y0 + f - 40, D + 3), "glass", thick=False)
            k = fl * units + u
            if k % 3 == 0:
                s.tube((x0 + 20, y0 + 200, D + 80), (x1 - 20, y0 + 200, D + 80), 2, 2, "steel", sides=4)
            if k % 4 == 1:
                bx(s, (x0 + 60, y0 + 40, D + 120), (x0 + 220, y0 + 112, D + 125), futon[k % len(futon)], thick=False)
        for u in range(units):
            x = -W + (u + .5) * uw
            if u % 2 == 0:
                window(s, ("z", -D, -1), x, y0 + 100, 160, 110, bars=1)
    for u in range(1, units, 2):
        x = -W + u * uw
        bx(s, (x - 90, 0, -D - 160), (x + 90, h + 40, -D), "concrete")
        for fl in range(floors):
            bx(s, (x - 30, fl * f + 160, -D - 163), (x + 30, fl * f + 260, -D - 160), "glass", thick=False)
        bx(s, (x - 50, 0, -D - 163), (x + 50, 220, -D - 160), "charcoal", thick=False)
    for z in (-1, 1):
        bx(s, (-W - 3, h - 120, z * 200 - 80), (-W, h - 40, z * 200 + 80), "concrete_dark", thick=False)
    return s


@model()
def dagashiya():
    # 駄菓子屋 (8.4 x 7.6m)。店先に紅白のひさしと駄菓子の台、2 階が住まい
    s = Shape()
    W, D, f, h = 420, 380, 300, 580
    bx(s, (-W - 6, 0, -D - 6), (W + 6, 20, D + 6), "concrete")
    bx(s, (-W, 20, -D), (W, f + 20, D), "wood_old", thick=False)
    bx(s, (-W, f + 20, -D), (W, h, D), "plaster", thick=False)
    bx(s, (-W + 20, 20, D), (W - 170, f - 10, D + 3), "charcoal", thick=False)
    for k in range(3):
        bx(s, (-W + 40, 20, D - 150 + k * 40), (W - 190, 60 + k * 50, D - 110 + k * 40), "wood")
    colors = ["red", "yellow", "pink", "sky", "green_leaf", "orange", "purple", "white"]
    for k in range(3):
        for i in range(10):
            x = -W + 60 + i * 55
            y = 60 + k * 50
            c = colors[(i * 3 + k * 5) % len(colors)]
            bx(s, (x - 18, y, D - 145 + k * 40), (x + 18, y + 14 + (i % 3) * 5, D - 115 + k * 40), c)
    bx(s, (-W + 40, 20, D + 10), (W - 190, 80, D + 90), "wood")
    for i in range(8):
        x = -W + 70 + i * 60
        bx(s, (x - 22, 80, D + 20), (x + 22, 92 + (i % 2) * 8, D + 80), colors[(i * 5) % len(colors)])
    bx(s, (W - 160, 20, D), (W - 20, f - 10, D + 3), "glass", thick=False)
    for x in (W - 160, W - 90, W - 20):
        bx(s, (x - 3, 20, D), (x + 3, f - 10, D + 6), "wood_light", thick=False)
    n = 10
    for i in range(n):
        x0, x1 = -W - 10 + i * (2 * W + 20) / n, -W - 10 + (i + 1) * (2 * W + 20) / n
        plate(s, [(x0, f + 10, D), (x1, f + 10, D), (x1, f - 40, D + 150), (x0, f - 40, D + 150)], 4, "red" if i % 2 else "white")
    bx(s, (-W + 40, f + 20, D + 2), (W - 40, f + 100, D + 14), "red_dark")
    bx(s, (-W + 50, f + 28, D + 14), (W - 50, f + 92, D + 17), "cream", thick=False)
    for i in range(4):
        x = -240 + i * 160
        bx(s, (x - 34, f + 36, D + 17), (x + 34, f + 84, D + 20), "red", thick=False)
    for x in (-200, 200):
        window(s, ("z", D, 1), x, f + 120, 160, 110, bars=1)
    gable(s, W, D, h, h + 200, 60, "kawara", "wood_old", t=10)
    s.tube((-W - 40, 0, D + 140), (-W - 40, 230, D + 140), 2, 2, "wood", sides=4)
    bx(s, (-W - 38, 150, D + 139), (-W + 40, 225, D + 142), "white", thick=False)
    bx(s, (-W - 38, 150, D + 142), (-W + 40, 166, D + 145), "blue", thick=False)
    bx(s, (-W - 14, 172, D + 142), (-W + 18, 218, D + 145), "red", thick=False)
    bx(s, (40, 40, D + 170), (240, 46, D + 210), "red")
    for x in (50, 230):
        bx(s, (x - 3, 0, D + 172), (x + 3, 40, D + 208), "wood")
    return s


@model()
def sento():
    # 銭湯 (14 x 18m)。正面の入口に妻の屋根とのれん、裏に高い煙突
    s = Shape()
    W, D, h = 700, 900, 600
    bx(s, (-W - 10, 0, -D - 10), (W + 10, 40, D + 10), "stone")
    bx(s, (-W, 40, -D), (W, 160, D), "wood_old", thick=False)
    bx(s, (-W, 160, -D), (W, h, D), "plaster", thick=False)
    gable(s, W, D, h, h + 320, 80, "kawara", "plaster", t=12)
    bx(s, (-200, 40, D), (200, 360, D + 200), "wood_old", thick=False)
    g = Shape()
    gable(g, 100, 220, 360, 520, 40, "kawara", "wood_old", t=8)
    merge(s, g, swap=True, at=(0, 0, D + 100))
    for i in range(3):
        x = -150 + i * 102
        bx(s, (x, 210, D + 203), (x + 96, 330, D + 206), "navy", thick=False)
    bx(s, (-150, 40, D + 200), (150, 210, D + 203), "charcoal", thick=False)
    for z in (-600, -200, 200, 600):
        window(s, ("x", W, 1), z, 380, 180, 140, bars=2)
        window(s, ("x", -W, -1), z, 380, 180, 140, bars=2)
    # 煙突は町のどこからでも見える目印 (高さ 21m)
    prof = [(100, 0), (92, 400), (84, 800), (76, 1200), (70, 1600), (66, 1900), (67, 1980), (66, 2060), (68, 2100)]
    s.lathe(prof, lambda band, i: "red" if band in (5, 7) else ("white" if band == 6 else "concrete_dark"), sides=10, at=(W - 180, -D + 180))
    return s


@model()
def sanmon():
    # お寺の山門 (幅 10m)
    s = Shape()
    W, D = 500, 250
    bx(s, (-W - 40, 0, -D - 40), (W + 40, 30, D + 40), "stone")
    for x in (-450, -150, 150, 450):
        for z in (-180, 180):
            s.lathe([(36, 30), (36, 50), (0, 50)], "stone_dark", sides=8, at=(x, z))
            s.lathe([(26, 50), (24, 520)], "wood_red", sides=8, at=(x, z))
    for z in (-180, 180):
        bx(s, (-W + 20, 440, z - 20), (W - 20, 480, z + 20), "wood_red")
    bx(s, (-W, 480, -D + 40), (W, 540, D - 40), "wood_red")
    for x in range(-450, 451, 150):
        bx(s, (x - 40, 540, -D), (x + 40, 600, D), "wood_old")
    for x0, x1 in ((-450, -150), (150, 450)):
        bx(s, (x0 + 20, 30, -10), (x1 - 20, 440, 10), "wood_old")
        for k in range(8):
            x = x0 + 30 + k * (x1 - x0 - 60) / 7
            bx(s, (x - 3, 30, 10), (x + 3, 440, 14), "wood_red", thick=False)
    bx(s, (-150, 30, -180), (-140, 440, -60), "wood_old")
    bx(s, (140, 30, -180), (150, 440, -60), "wood_old")
    hip(s, W, D, 600, 280, 180, "kawara", t=20)
    return s


@model()
def honden():
    # 神社の本殿 (8 x 9m)。高床と赤い手すり、銅板の屋根に千木と鰹木、鈴と鈴の緒
    s = Shape()
    W, D, fl = 400, 450, 150
    h = fl + 380
    for x in (-380, -130, 130, 380):
        for z in (-420, -140, 140, 420):
            bx(s, (x - 14, 0, z - 14), (x + 14, fl, z + 14), "wood_old")
    bx(s, (-W - 60, fl - 20, -D - 60), (W + 60, fl, D + 60), "wood")
    for x0, x1 in ((-W - 60, -110), (110, W + 60)):
        bx(s, (x0, fl, D + 50), (x1, fl + 70, D + 60), "shrine_red", thick=False)
    for x in (-W - 60, W + 50):
        bx(s, (x, fl, -D - 60), (x + 10, fl + 70, D + 60), "shrine_red", thick=False)
    bx(s, (-W, fl, -D), (W, h, D), "wood_light")
    for x in (-W, W - 20):
        bx(s, (x, fl, D - 1), (x + 20, h, D + 6), "shrine_red")
    bx(s, (-200, fl, D), (200, fl + 280, D + 3), "wood_old", thick=False)
    for k in range(11):
        x = -190 + k * 38
        bx(s, (x - 2.5, fl, D + 3), (x + 2.5, fl + 280, D + 5), "wood", thick=False)
    for k in range(4):
        y = fl + 40 + k * 70
        bx(s, (-200, y - 2.5, D + 3), (200, y + 2.5, D + 5), "wood", thick=False)
    for k in range(8):
        z0 = D + 60 + (7 - k) * 28
        bx(s, (-110, 0, z0), (110, (k + 1) * 19, z0 + 28), "wood")
    ridge = h + 330
    gable(s, W, D, h, ridge, 160, "tin_teal", "wood_light", t=16)
    for x in (-W - 160, W + 160):
        s.tube((x, ridge - 30, -70), (x, ridge + 130, 70), 7, 7, "tin_teal", sides=4)
        s.tube((x, ridge - 30, 70), (x, ridge + 130, -70), 7, 7, "tin_teal", sides=4)
    for x in range(-300, 301, 150):
        s.tube((x, ridge + 22, -28), (x, ridge + 22, 28), 13, 13, "gold", sides=6)
    by, bz = h - 40, D + 70
    s.lathe([(0, by + 30), (22, by + 18), (26, by), (20, by - 10)], "gold", sides=10, at=(0, bz))
    for k in range(10):
        y0 = by - 10 - k * 30
        s.tube((0, y0, bz), (0, y0 - 30, bz), 7, 7, "red" if k % 2 else "white", sides=6)
    return s


@model()
def torii():
    # 赤い鳥居 (高さ 6.4m)
    s = Shape()
    H, span = 600, 480
    for x in (-1, 1):
        s.lathe([(36, 0), (36, 40), (0, 40)], "black", sides=10, at=(x * span / 2, 0))
        s.lathe([(32, 40), (30, 80), (26, H - 40)], "shrine_red", sides=10, at=(x * span / 2, 0))
    bx(s, (-span / 2 - 80, H - 160, -14), (span / 2 + 80, H - 120, 14), "shrine_red")
    bx(s, (-16, H - 120, -12), (16, H - 60, 12), "shrine_red")
    bx(s, (-36, H - 116, 12), (36, H - 64, 18), "black")
    bx(s, (-28, H - 108, 18), (28, H - 72, 19), "gold", thick=False)
    bx(s, (-span / 2 - 110, H - 60, -18), (span / 2 + 110, H - 30, 18), "shrine_red")
    L = span / 2 + 170
    xs = [-L + 2 * L * i / 10 for i in range(11)]
    top = [(x, H + 10 + 40 * (x / L) ** 2) for x in xs]
    bot = [(x, H - 30 + 40 * (x / L) ** 2) for x in reversed(xs)]
    prism(s, top + bot, -24, 24, "black")
    return s


@model()
def densha():
    # 電車 1 両 (長さ 20m)。クリーム色に橙の帯、パンタグラフ付き (2 両つなげて走らせる)
    s = Shape()
    L, W, fl, h = 1000, 145, 110, 390
    bx(s, (-L, fl, -W), (L, h - 30, W), "cream")
    g = Shape()
    prism(g, [(-W, h - 30), (W, h - 30), (W - 30, h - 5), (W - 80, h + 8), (-W + 80, h + 8), (-W + 30, h - 5)], -L, L, "gray_light")
    merge(s, g, swap=True)
    bx(s, (-L - 1, fl + 85, -W - 1), (L + 1, fl + 115, W + 1), "orange", thick=False)
    for side in (-1, 1):
        for x in (-600, 0, 600):
            bx(s, (x - 65, fl + 5, side * W), (x + 65, h - 60, side * (W + 1.5)), "gray_light", thick=False)
            bx(s, (x - 55, fl + 140, side * (W + 1.5)), (x + 55, h - 80, side * (W + 2.5)), "glass", thick=False)
        for x in (-860, -300, 300, 860):
            for dx in (-120, 0, 120):
                if abs(x + dx) < L - 60 and min(abs(x + dx - d) for d in (-600, 0, 600)) > 130:
                    bx(s, (x + dx - 52, fl + 150, side * W), (x + dx + 52, fl + 235, side * (W + 2)), "glass", thick=False)
    for e in (-1, 1):
        for z0, z1 in ((-W + 20, -12), (12, W - 20)):
            bx(s, (e * L, fl + 150, z0), (e * (L + 2), h - 60, z1), "glass", thick=False)
        bx(s, (e * L, h - 55, -50), (e * (L + 2.5), h - 35, 50), "black", thick=False)
        for z in (-1, 1):
            bx(s, (e * L, fl + 50, z * 90), (e * (L + 2.5), fl + 70, z * 120), "yellow", thick=False)
    for x in (-L + 250, L - 250):
        bx(s, (x - 130, 40, -W + 20), (x + 130, fl, W - 20), "charcoal")
        for dx in (-80, 80):
            for z in (-W + 30, W - 30):
                wheel(s, x + dx, z, 43, 43, 6, tire="steel", hub="charcoal")
    bx(s, (-300, 60, -W + 30), (300, fl, W - 30), "charcoal")
    x = L - 300
    bx(s, (x - 60, h + 8, -40), (x + 60, h + 20, 40), "charcoal")
    for a, b in (((x - 80, h + 20, 0), (x, h + 120, 0)), ((x + 80, h + 20, 0), (x, h + 120, 0))):
        s.tube(a, b, 2.5, 2.5, "charcoal", sides=4)
    s.tube((x, h + 120, -80), (x, h + 120, 80), 3, 3, "steel", sides=4)
    return s


@model()
def tekkyo():
    # 川をまたぐ鉄の橋 (長さ 30m)。線路つきのトラス
    s = Shape()
    L, W, H = 1500, 260, 700
    bx(s, (-L, 0, -W), (L, 40, W), "concrete_dark")
    for x in range(-L + 30, L, 60):
        bx(s, (x - 12, 40, -90), (x + 12, 48, 90), "wood_old")
    for z in (-54, 54):
        bx(s, (-L, 48, z - 4), (L, 62, z + 4), "steel")
    step = 300
    for z in (-W, W):
        s.tube((-L, 40, z), (L, 40, z), 14, 14, "red_dark", sides=4)
        s.tube((-L + step, H, z), (L - step, H, z), 14, 14, "red_dark", sides=4)
        s.tube((-L, 40, z), (-L + step, H, z), 14, 14, "red_dark", sides=4)
        s.tube((L, 40, z), (L - step, H, z), 14, 14, "red_dark", sides=4)
        for k in range(1, 10):
            x = -L + k * step
            s.tube((x, 40, z), (x, H, z), 9, 9, "red_dark", sides=4)
            if k < 9:
                a, b = ((x, 40, z), (x + step, H, z)) if k < 5 else ((x, H, z), (x + step, 40, z))
                s.tube(a, b, 8, 8, "red_dark", sides=4)
    for k in range(1, 10):
        x = -L + k * step
        s.tube((x, H, -W), (x, H, W), 8, 8, "red_dark", sides=4)
    return s


@model()
def kyokyaku():
    # 橋の脚 (高さ 10m)
    s = Shape()
    prism(s, [(-150, 0), (150, 0), (110, 1000), (-110, 1000)], -120, 120, "concrete")
    bx(s, (-180, 1000, -150), (180, 1060, 150), "concrete_dark")
    return s


@model()
def tunnel():
    # 線路のトンネルの入口 (幅 14m・高さ 10m)。正面が +z、穴は -z へ続く
    s = Shape()
    Wd, H, T, R, spring = 700, 1000, 150, 300, 200
    bx(s, (-Wd, 0, 0), (-R, H, T), "concrete")
    bx(s, (R, 0, 0), (Wd, H, T), "concrete")
    n = 12
    arc = [(R * math.cos(math.pi * i / n), spring + R * math.sin(math.pi * i / n)) for i in range(n + 1)]
    for (x0, y0), (x1, y1) in zip(arc, arc[1:]):
        prism(s, [(x0, y0), (x1, y1), (x1, H), (x0, H)], 0, T, "concrete")
        k = 1.16
        prism(s, [(x0, y0), (x1, y1), (x1 * k, spring + (y1 - spring) * k), (x0 * k, spring + (y0 - spring) * k)], T, T + 8, "stone")
        prism(s, [(x0 * 1.05, spring + (y0 - spring) * 1.05), (x1 * 1.05, spring + (y1 - spring) * 1.05), (x1, y1), (x0, y0)], -500, 0, "concrete_dark")
    for x in (-R - 30, R):
        bx(s, (x, 0, -500), (x + 30, spring, 0), "concrete_dark")
    bx(s, (-R, 0, -510), (R, spring + R, -500), "black")
    bx(s, (-Wd - 10, H, -10), (Wd + 10, H + 40, T + 10), "concrete_dark")
    bx(s, (-100, H - 170, T), (100, H - 90, T + 6), "stone_dark")
    return s


@model()
def hodokyo():
    # 歩道橋 (道の上 20m、両端に階段)
    s = Shape()
    span, H, Wd = 1000, 520, 150
    bx(s, (-span, H - 30, -Wd), (span, H, Wd), "concrete")
    for z in (-Wd, Wd - 20):
        bx(s, (-span, H - 80, z), (span, H - 30, z + 20), "sky")
    for e in (-1, 1):
        for k in range(26):
            y = H - (k + 1) * 20
            bx(s, (e * (span + k * 30), y - 24, -Wd), (e * (span + (k + 1) * 30), y + 20 if k == 0 else y + 4, Wd), "concrete")
        for z in (-Wd, Wd):
            s.tube((e * span, H + 110, z), (e * (span + 780), 110, z), 4, 4, "sky", sides=4)
            s.tube((e * (span + 780), 0, z), (e * (span + 780), 110, z), 4, 4, "sky", sides=4)
    for z in (-Wd, Wd):
        s.tube((-span, H + 110, z), (span, H + 110, z), 4, 4, "sky", sides=4)
        for x in range(-span, span + 1, 125):
            s.tube((x, H, z), (x, H + 110, z), 3, 3, "sky", sides=4)
    for x in (-span + 60, span - 60):
        for z in (-Wd + 30, Wd - 30):
            s.tube((x, 0, z), (x, H - 80, z), 20, 20, "sky", sides=6)
    return s


@model()
def kyusuito():
    # 給水塔 (高さ 21m)。鉄の 4 本脚の上に丸いタンク
    s = Shape()
    H = 1500
    legs = [(x, z) for x in (-1, 1) for z in (-1, 1)]
    for x, z in legs:
        s.tube((x * 230, 0, z * 230), (x * 130, H, z * 130), 14, 12, "steel", sides=6)
    order = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    for k in range(1, 6):
        y0, y1 = (k - 1) * H / 5, k * H / 5
        r0, r1 = 230 - 100 * y0 / H, 230 - 100 * y1 / H
        for (ax, az), (bx_, bz) in zip(order, order[1:] + order[:1]):
            s.tube((ax * r1, y1, az * r1), (bx_ * r1, y1, bz * r1), 6, 6, "steel", sides=4)
            s.tube((ax * r0, y0, az * r0), (bx_ * r1, y1, bz * r1), 4, 4, "steel", sides=4)
    s.lathe([(300, H - 10), (300, H)], "steel", sides=12)
    s.lathe([(260, H), (280, H + 60), (280, H + 420), (260, H + 440), (120, H + 560), (30, H + 600)],
            lambda band, i: "concrete_dark" if band == 1 and i % 3 == 0 else "concrete", sides=12)
    for k in range(20):
        y = H - 400 + k * 20
        s.tube((-20, y, 140 + 100 * (H - y) / H * 0), (20, y, 140), 1.5, 1.5, "steel", sides=4)
    return s


@model()
def tetto():
    # 送電の鉄塔 (高さ 40m)
    s = Shape()
    H = 4000
    top = H * .85

    def half(y):
        return 500 - 420 * min(y, top) / top if y <= top else 80 - 30 * (y - top) / (H - top)
    corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    levels = [0, 400, 900, 1500, 2100, 2700, 3100, top]
    ys = levels + [H]
    for x, z in corners:
        for y0, y1 in zip(ys, ys[1:]):
            s.tube((x * half(y0), y0, z * half(y0)), (x * half(y1), y1, z * half(y1)), 16 - y0 / 400, 16 - y1 / 400, "steel", sides=4)
    for y0, y1 in zip(levels, levels[1:]):
        for (ax, az), (bx_, bz) in zip(corners, corners[1:] + corners[:1]):
            s.tube((ax * half(y1), y1, az * half(y1)), (bx_ * half(y1), y1, bz * half(y1)), 5, 5, "steel", sides=4)
            s.tube((ax * half(y0), y0, az * half(y0)), (bx_ * half(y1), y1, bz * half(y1)), 4, 4, "steel", sides=4)
            s.tube((bx_ * half(y0), y0, bz * half(y0)), (ax * half(y1), y1, az * half(y1)), 4, 4, "steel", sides=4)
    for y, reach in ((2500, 700), (3000, 620), (3450, 540)):
        w = half(y)
        for e in (-1, 1):
            tip = (e * reach, y, 0)
            for z in (-1, 1):
                s.tube((e * w, y, z * w), tip, 5, 5, "steel", sides=4)
            s.tube((e * w, y - 160, 0), tip, 5, 5, "steel", sides=4)
            s.tube(tip, (e * reach, y - 180, 0), 6, 6, "white", sides=6)
            s.ball((e * reach, y - 190, 0), 10, "white", level=1)
    s.tube((-200, H, 0), (200, H, 0), 5, 5, "steel", sides=4)
    return s


@model()
def goshinboku():
    # しめ縄を巻いた御神木 (高さ 16m)
    s = Shape()
    s.lathe([(260, 0), (180, 60), (140, 200), (120, 600), (100, 900), (80, 1050)], "bark", sides=9)
    for k in range(7):
        a = k * .9
        s.tube((math.cos(a) * 120, 60, math.sin(a) * 120), (math.cos(a) * 320, 0, math.sin(a) * 320), 50, 18, "bark", sides=5)
    crowns = [(0, 1300, 0, 380), (-320, 1080, 120, 280), (330, 1120, -100, 300), (100, 980, 340, 250), (-150, 1020, -320, 260)]
    for x, y, z, r in crowns:
        s.tube((0, 850, 0), (x * .8, y - r * .4, z * .8), 50, 30, "bark", sides=6)
        crown(s, x, y, z, r, leafy("grass_dark", "green_dark"))
    n, R, y = 12, 150, 320
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        s.tube((math.cos(a0) * R, y, math.sin(a0) * R), (math.cos(a1) * R, y, math.sin(a1) * R), 18, 18, "straw", sides=6)
    for i in range(4):
        a = 2 * math.pi * (i + .5) / 4
        cx, cz = math.cos(a) * (R + 14), math.sin(a) * (R + 14)
        for k in range(3):
            o = 10 if k % 2 else -10
            bx(s, (cx + o - 9, y - 30 - k * 30, cz - 2), (cx + o + 9, y - 6 - k * 30, cz + 2), "white", thick=False)
    return s


@model()
def bus():
    # 路線バス (長さ 10.8m)
    s = Shape()
    L, W, h = 540, 124, 310
    bx(s, (-L, 40, -W), (L, h, W), "white")
    bx(s, (-L - 1, 90, -W - 1), (L + 1, 130, W + 1), "blue", thick=False)
    bx(s, (-L + 20, h, -W + 10), (L - 20, h + 15, W - 10), "gray_light")
    for side in (-1, 1):
        for i in range(6):
            x = -L + 90 + i * 150
            if side > 0 and x > L - 170:
                continue
            bx(s, (x - 60, 160, side * W), (x + 60, 270, side * (W + 1.5)), "glass", thick=False)
    bx(s, (L - 160, 40, W), (L - 40, 280, W + 2), "glass", thick=False)
    bx(s, (L, 140, -W + 8), (L + 2, 290, W - 8), "glass", thick=False)
    bx(s, (L, 292, -W + 20), (L + 2.5, 306, W - 20), "black", thick=False)
    for z in (-1, 1):
        bx(s, (L, 70, z * (W - 30)), (L + 2, 90, z * (W - 8)), "yellow", thick=False)
    bx(s, (-L - 2, 180, -W + 20), (-L, 280, W - 20), "glass", thick=False)
    for x in (-L + 130, L - 130):
        for z in (-W + 14, W - 14):
            wheel(s, x, z, 48, 48, 12)
    return s


@model()
def gomi_truck():
    # ごみ収集車 (長さ 7m)
    s = Shape()
    bx(s, (-300, 30, -100), (330, 50, 100), "charcoal")
    bx(s, (150, 50, -110), (330, 250, 110), "white")
    bx(s, (330, 140, -100), (332, 235, 100), "glass", thick=False)
    for z in (-1, 1):
        bx(s, (220, 140, z * 110), (310, 230, z * 111.5), "glass", thick=False)
    prism(s, [(-260, 50), (140, 50), (140, 280), (-200, 280), (-330, 200), (-330, 90)], -115, 115, "blue")
    bx(s, (-320, 150, -116), (140, 170, 116), "green_leaf", thick=False)
    bx(s, (-360, 50, -112), (-330, 200, 112), "gray")
    for x in (-200, -80, 230):
        for z in (-95, 95):
            wheel(s, x, z, 45, 45, 12)
    return s


# ==== 地形の部品 =====================================================================================
@model()
def yoheki():
    # 石垣の擁壁 (長さ 10m・高さ 3m)。正面 (+z) が上へ少し下がる
    s = Shape()
    L, H, T, lean = 500, 300, 100, 60
    g = Shape()
    prism(g, [(0, 0), (T, 0), (T - lean, H), (0, H)], -L, L, "stone_dark")
    merge(s, g, swap=True)
    stones = ("stone", "stone_dark", "stone", "moss", "stone")
    for k in range(6):
        y0 = k * 50
        zf = T - lean * (y0 + 25) / H
        for j in range(11):
            x0 = -L + (j - .5 * (k % 2)) * 100
            a, b = max(x0 + 4, -L), min(x0 + 96, L)
            if b - a < 20:
                continue
            bx(s, (a, y0 + 4, zf - 10), (b, y0 + 46, zf + 5), stones[(j * 3 + k * 7) % len(stones)], thick=False)
    bx(s, (-L, H, -10), (L, H + 20, T - lean + 10), "concrete")
    return s


@model()
def yoheki_concrete():
    # コンクリートの擁壁 (長さ 10m・高さ 3m)。水抜きの穴と汚れ
    s = Shape()
    L, H, T, lean = 500, 300, 80, 40
    g = Shape()
    prism(g, [(0, 0), (T, 0), (T - lean, H), (0, H)], -L, L, "concrete")
    merge(s, g, swap=True)
    for x in range(-L + 80, L, 160):
        for y in (70, 200):
            z = T - lean * y / H
            s.tube((x, y, z - 6), (x, y, z + 2), 7, 7, "black", sides=6)
            bx(s, (x - 5, y - 60, z - 2), (x + 5, y - 8, z + 1.6), "concrete_dark", thick=False)
    bx(s, (-L, H, -6), (L, H + 10, T - lean + 6), "concrete_dark")
    return s


@model()
def ishidan():
    # 石段 (10 段・高さ 1.8m・幅 3m)。原点が下の段の前の端で、-z へ登る
    s = Shape()
    steps, rise, run, Wd = 10, 18, 32, 150
    for k in range(steps):
        bx(s, (-Wd, 0, -(k + 1) * run), (Wd, (k + 1) * rise, -k * run), "stone")
        bx(s, (-Wd, (k + 1) * rise - 3, -k * run - 4), (Wd, (k + 1) * rise + .5, -k * run + .5), "stone_dark", thick=False)
    for x in (-Wd - 24, Wd):
        g = Shape()
        prism(g, [(0, 0), (0, 40), (-steps * run, steps * rise + 40), (-steps * run, 0)], x, x + 24, "stone_dark")
        merge(s, g, swap=True)
    s.tube((0, rise + 90, -run * .5), (0, steps * rise + 90, -(steps - .5) * run), 3, 3, "steel", sides=5)
    for k in (0, steps // 2, steps - 1):
        s.tube((0, (k + 1) * rise, -(k + .5) * run), (0, (k + 1) * rise + 90, -(k + .5) * run), 2.5, 2.5, "steel", sides=4)
    return s


@model()
def senro():
    # 線路 (長さ 10m)。砂利の土手・枕木・レール
    s = Shape()
    L = 500
    g = Shape()
    prism(g, [(-160, 0), (160, 0), (110, 30), (-110, 30)], -L, L, "stone_dark")
    merge(s, g, swap=True)
    for x in range(-L + 30, L, 60):
        bx(s, (x - 12, 30, -120), (x + 12, 40, 120), "concrete")
    for z in (-54, 54):
        bx(s, (-L, 40, z - 4), (L, 56, z + 4), "steel")
    return s


# ==== 結果の景色 (積み木の山のまわり) ================================================================
def cloud(puffs, seed):
    """もこもこの雲: 玉 (x, y, z, 半径) を寄せて、底を平らに切る。上は白、底の面は少し灰色"""
    import bmesh
    s = Shape()
    for n, (x, y, z, r) in enumerate(puffs):
        mesh = bmesh.new()
        bmesh.ops.create_icosphere(mesh, subdivisions=2, radius=r)
        for i, f in enumerate(mesh.faces):
            pts = []
            low = True
            for v in f.verts:
                # (面ごとに少しだけ揺らして、手でこねたような凸凹にする)
                k = 1 + .06 * math.sin(v.co.x * .09 + seed + n) * math.cos(v.co.y * .07 + n * 1.7)
                py = y + v.co.z * k
                low = low and py <= 1
                pts.append((x + v.co.x * k, max(py, 0.0), z + v.co.y * k))
            s.face(pts, "gray_light" if low else "white")
        mesh.free()
    return s


@model()
def kumo_a():
    # 横に長い雲 (幅 12m 前後。大きさは使う側で決める)
    return cloud([(-380, 60, 0, 230), (-60, 120, 30, 320), (280, 90, -20, 270), (520, 40, 10, 170), (80, 40, -160, 220),
                  (-200, 30, 150, 200)], 1.0)


@model()
def kumo_b():
    # 背の高い入道雲
    return cloud([(0, 120, 0, 340), (-260, 60, 40, 240), (250, 70, -30, 250), (40, 380, 20, 260), (-90, 560, -10, 190),
                  (120, 300, 160, 200), (60, 50, -200, 210)], 2.0)


@model()
def kumo_c():
    # 小さなちぎれ雲
    return cloud([(-150, 40, 0, 150), (40, 80, 20, 200), (210, 40, -10, 140), (60, 30, -110, 120)], 3.0)


@model()
def ki_maru():
    # 丸い広葉樹 (高さ 6m)
    s = Shape()
    tree_on(s, [(30, 0), (22, 25), (18, 220), (14, 280)],
            [(-120, 380, 30, 140), (120, 400, -30, 145), (0, 500, 0, 160), (40, 360, 150, 120), (-40, 370, -140, 120)],
            leafy("grass", "green_dark"),
            branches=[((0, 240, 0), (-100, 360, 30), 12), ((0, 250, 0), (100, 380, -30), 12), ((0, 260, 0), (0, 460, 0), 12)])
    return s


@model()
def sugi():
    # 杉 (高さ 9m)。とがった葉の段
    s = Shape()
    s.lathe([(26, 0), (20, 30), (12, 300), (6, 820)], "bark", sides=6)
    for k, (y, r) in enumerate(((180, 190), (330, 160), (470, 125), (600, 90), (720, 55))):
        s.lathe([(0, y), (r, y + 20), (r * .55, y + 140), (0, y + 200)], "green_dark" if k % 2 else "moss", sides=8)
    return s
