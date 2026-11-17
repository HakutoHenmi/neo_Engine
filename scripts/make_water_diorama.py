# make_water_diorama.py
# 水のデモ (箱庭) のシーン Resources/Scenes/water_diorama.json を作る。
# 岩に見立てた cube を積み重ね、いちばん上の岩の割れ目から水専用の流体 (Renderer::kPoolWaterType) を出し続ける。
# 水は岩の段を伝って流れ落ち、石の枠で囲った箱庭の底にたまる。
# 実行: python scripts/make_water_diorama.py   (シーンを開くのは --water-demo)
import json, os, random

CUBE = "Resources/Models/cube/cube.obj"
NONE = 4294967295
objects = []
next_id = [200]


def add(name, center, half, color, collide=True, tag="Wall", parent=NONE):
    """cube (一辺 2) を center に、半分の大きさ half で置く。collide なら水が当たる箱にもする"""
    oid = next_id[0]; next_id[0] += 1
    comps = [{"type": "MeshRenderer", "enabled": True, "modelPath": CUBE, "shaderName": "Default",
              "color": list(color) + [1], "reflectivity": 0, "useCubemap": False}]
    if collide:
        comps.append({"type": "BoxCollider", "enabled": True, "center": [0, 0, 0], "size": [2, 2, 2], "isTrigger": False})
    comps.append({"type": "Tag", "tag": tag})
    objects.append({"id": oid, "parentId": parent, "name": name, "locked": False,
                    "translate": list(center), "rotate": [0, 0, 0], "scale": list(half), "components": comps})
    return oid


rng = random.Random(7)


def stone(base):
    """岩の色に少しばらつきを付ける"""
    k = rng.uniform(.88, 1.1)
    return [round(min(1, c * k), 3) for c in base]


# ---- 箱庭の底と枠 (内側 14 x 14、底の上面が高さ 0、枠の高さ 3.2。岩の上の水が枠を越えない高さ) ----
SIZE, HEIGHT, THICK = 7.0, 3.2, .5
add("Diorama Floor", (0, -.3, 0), (SIZE + THICK, .3, SIZE + THICK), (.36, .32, .26))
wall = (.46, .44, .40)
add("Diorama Wall North", (0, HEIGHT / 2, SIZE + THICK / 2), (SIZE + THICK, HEIGHT / 2, THICK / 2), wall)
add("Diorama Wall South", (0, HEIGHT / 2, -SIZE - THICK / 2), (SIZE + THICK, HEIGHT / 2, THICK / 2), wall)
add("Diorama Wall East", (SIZE + THICK / 2, HEIGHT / 2, 0), (THICK / 2, HEIGHT / 2, SIZE), wall)
add("Diorama Wall West", (-SIZE - THICK / 2, HEIGHT / 2, 0), (THICK / 2, HEIGHT / 2, SIZE), wall)
# 枠の外の台 (箱庭を置いた机)。水は当たらない
add("Diorama Table", (0, -1.1, 0), (SIZE + 2.2, .5, SIZE + 2.2), (.24, .2, .17), collide=False, tag="Default")

# ---- 岩の積み重なり (北西の角)。上から段々に水が落ちる ----
# 枠より高い岩は枠から離して置く (枠に接していると、岩の上の水が枠を越えて外へこぼれる)
rock = (.48, .46, .43)
dark = (.36, .35, .33)
rocks = [
    # 名前, 中心, 半分の大きさ, 色
    ("Rock Base",       (-4.2, 1.0, 5.0), (2.8, 1.0, 2.0), rock),   # 高さ 0〜2、前の縁 z=3.0
    ("Rock Middle",     (-4.4, 2.8, 5.4), (1.8, .8, 1.2), dark),     # 高さ 2〜3.6、前の縁 z=4.2
    ("Rock Top",        (-4.8, 4.2, 5.7), (1.3, .6, .7), rock),      # 高さ 3.6〜4.8 (水の出口)
    ("Rock Cap Left",   (-5.6, 5.2, 5.9), (.5, .4, .5), dark),       # 出口の左右の岩 (割れ目)
    ("Rock Cap Right",  (-4.0, 5.1, 6.0), (.6, .3, .4), dark),
    ("Rock Side",       (-6.3, 1.3, 2.2), (.7, 1.3, 1.0), dark),     # 西の壁沿いの柱
    ("Rock Step",       (-1.4, .55, 4.4), (.8, .55, .9), rock),      # 東の小さな段
    ("Rock Step High",  (-1.9, 1.5, 5.5), (.6, .4, .7), dark),
    ("Rock Back",       (-1.0, 1.2, 6.3), (1.0, 1.2, .7), rock),
]
for name, c, h, col in rocks:
    add(name, c, h, stone(col))
# 水の中の転がった岩
for i, (c, h) in enumerate([((2.2, .4, -2.0), (.6, .4, .5)), ((3.8, .3, 1.6), (.4, .3, .45)),
                            ((-1.8, .35, -3.6), (.55, .35, .6)), ((4.6, .5, -4.4), (.8, .5, .7)),
                            ((.6, .25, 1.0), (.3, .25, .3))]):
    add(f"Pool Stone {i + 1}", c, h, stone(dark))

# ---- 水の出口: いちばん上の岩の前の縁から南 (-z) へ流し出す ----
oid = next_id[0]; next_id[0] += 1
objects.append({"id": oid, "parentId": NONE, "name": "Water Source", "locked": False,
                # (出口は岩の前の面 z=5.0 より手前。粒を出す円が岩の中にかかると、押し出されて空へ飛ぶ)
                "translate": [-4.6, 5.3, 4.75], "rotate": [0, 0, 0], "scale": [1, 1, 1],
                "components": [{"type": "FluidEmitter", "enabled": True,
                                # 1 フレーム (1/60 秒) に出す数。24576 粒で約 20 秒でたまりきり、そのあとは古い水から出口へ戻って流れ続ける
                                "emitCountPerFrame": 20, "retainWater": True, "particleLimit": 0,
                                # 速さは 5 倍して使われる (水平に 4)
                                "color": [.18, .62, .9, .94], "velocity": [0, -.1, -.8],
                                "fluidType": 1.3}]})
# ---- 光 ----
oid = next_id[0]; next_id[0] += 1
objects.append({"id": oid, "parentId": NONE, "name": "Sun", "locked": False,
                "translate": [0, 0, 0], "rotate": [.9, .6, 0], "scale": [1, 1, 1],
                "components": [{"type": "DirectionalLight", "enabled": True, "color": [1, .96, .9], "intensity": 1.1}]})

scene = {"settings": {"postProcessEnabled": True, "ambientColor": [.42, .46, .52]}, "objects": objects}
path = os.path.join(os.path.dirname(__file__), "..", "Resources", "Scenes", "water_diorama.json")
with open(path, "w", encoding="utf-8", newline="\n") as f:
    json.dump(scene, f, ensure_ascii=False, indent=2)
print("wrote", os.path.normpath(path), len(objects), "objects")
