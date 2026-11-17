# render_slope_town.py
# 坂の町の設計案 (slope_town_design.py の JSON) を Blender で立体にして、鳥瞰と目線の絵を描く。
# 使い方: C:/blender/blender.exe --background --factory-startup --python scripts/terrain/render_slope_town.py -- terrain.json 出力のフォルダ
# 地面は 1m のマスごとの面で、隣と近い高さの角はならして坂をなめらかにし、差が大きい所は縦の壁 (擁壁) にする
import bpy, bmesh, json, math, os, sys, random

argv = sys.argv[sys.argv.index("--") + 1:]
src, out = argv[0], argv[1]
os.makedirs(out, exist_ok=True)
D = json.load(open(src, encoding="utf-8"))
HALF = D["half"]; N = 2 * HALF + 1; H, K = D["height"], D["kind"]
random.seed(7)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

def material(name, rgb, rough=0.8):
    m = bpy.data.materials.new(name); m.diffuse_color = (*rgb, 1); m.roughness = rough
    return m
def srgb(c): return tuple(v / 255 for v in c)
MATS = {
    0: material("ground", srgb((150, 190, 110))), 1: material("lot", srgb((222, 208, 176))), 2: material("road", srgb((92, 94, 104))),
    3: material("stairs", srgb((178, 172, 160))), 4: material("water", srgb((90, 150, 206)), 0.2), 5: material("rail", srgb((112, 104, 98))),
    6: material("field", srgb((136, 176, 96))), 7: material("plaza", srgb((214, 200, 168))),
    "wall": material("wall", srgb((168, 162, 150))), "wall_road": material("wall_road", srgb((190, 188, 182))),
    "roof": [material("roof%d" % i, srgb(c)) for i, c in enumerate([(70, 78, 96), (110, 70, 60), (64, 110, 106), (96, 96, 100)])],
    "house": [material("house%d" % i, srgb(c)) for i, c in enumerate([(238, 230, 214), (226, 214, 190), (210, 196, 176), (232, 226, 222)])],
    "leaf": material("leaf", srgb((70, 130, 70))), "leaf2": material("leaf2", srgb((96, 150, 80))), "trunk": material("trunk", srgb((110, 84, 62))),
    "train": material("train", srgb((236, 236, 232))), "stripe": material("stripe", srgb((60, 110, 190))), "torii": material("torii", srgb((214, 70, 40))),
}
ORDER = [0, 1, 2, 3, 4, 5, 6, 7, "wall", "wall_road"]

def add_object(name, bm, mats):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me); scene.collection.objects.link(ob)
    for m in mats: me.materials.append(m)
    return ob

# ---- 地面 ----
def corner(i, j, ci, cj):
    """マス (i, j) の角 (ci, cj は 0 か 1) の高さ: 角を囲む 4 マスのうち、このマスと 0.7m 以内の高さの平均"""
    h = H[j][i]; total = 0; n = 0
    for di in (ci - 1, ci):
        for dj in (cj - 1, cj):
            ii, jj = i + di, j + dj
            if 0 <= ii < N and 0 <= jj < N and abs(H[jj][ii] - h) < 0.7 and (K[jj][ii] == 3) == (K[j][i] == 3):
                total += H[jj][ii]; n += 1
    return total / n if n else h
bm = bmesh.new()
C = [[[[0.0] * 2 for _ in range(2)] for _ in range(N)] for _ in range(N)]
for j in range(N):
    for i in range(N):
        for ci in (0, 1):
            for cj in (0, 1): C[j][i][ci][cj] = corner(i, j, ci, cj)
def V(x, y, z): return bm.verts.new((x, y, z))
for j in range(N):
    for i in range(N):
        x0, y0 = i - HALF - 0.5, j - HALF - 0.5; c = C[j][i]
        # 石段のマスは描かない (石段は下の「石段」で段の箱として描く)
        if K[j][i] == 3: continue
        f = bm.faces.new((V(x0, y0, c[0][0]), V(x0 + 1, y0, c[1][0]), V(x0 + 1, y0 + 1, c[1][1]), V(x0, y0 + 1, c[0][1])))
        f.material_index = ORDER.index(K[j][i]); f.smooth = False
        # 隣のマスとの間の縦の面 (高い方の側から見える向きにする)
        for di, dj in ((1, 0), (0, 1)):
            ii, jj = i + di, j + dj
            if ii >= N or jj >= N or K[jj][ii] == 3: continue
            o = C[jj][ii]
            if di: a, b, a2, b2 = c[1][0], c[1][1], o[0][0], o[0][1]; p0, p1 = (x0 + 1, y0), (x0 + 1, y0 + 1)
            else: a, b, a2, b2 = c[0][1], c[1][1], o[0][0], o[1][0]; p0, p1 = (x0, y0 + 1), (x0 + 1, y0 + 1)
            if max(abs(a - a2), abs(b - b2)) < 0.05: continue
            f = bm.faces.new((V(p0[0], p0[1], a), V(p1[0], p1[1], b), V(p1[0], p1[1], b2), V(p0[0], p0[1], a2)))
            road = 2 in (K[j][i], K[jj][ii])
            f.material_index = ORDER.index("wall_road" if road else "wall")
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.001)
ground = add_object("ground", bm, [MATS[k] for k in ORDER])
# 外のまわりの地面: 町の端の高さをそのまま外へ伸ばした粗い格子 (町の外が切れて見えないように)
bm = bmesh.new()
AVG = 14
def edge_h(x, z):
    """端の高さから、離れるほど町の平均の高さへなだらかに寄せる"""
    i, j = max(0, min(N - 1, int(x) + HALF)), max(0, min(N - 1, int(z) + HALF)); e = H[j][i] - 0.05
    d = max(0, abs(x) - HALF, abs(z) - HALF); t = min(1, d / 220); t = t * t * (3 - 2 * t)
    return e + (AVG + 0.08 * z - e) * t
G = 8; R = 640
for gz in range(-R, R, G):
    for gx in range(-R, R, G):
        if -HALF - G < gx < HALF and -HALF - G < gz < HALF: continue
        bm.faces.new((V(gx, gz, edge_h(gx, gz)), V(gx + G, gz, edge_h(gx + G, gz)), V(gx + G, gz + G, edge_h(gx + G, gz + G)), V(gx, gz + G, edge_h(gx, gz + G))))
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.001)
add_object("outside", bm, [material("outside", srgb((150, 180, 126)))])

def hgt(x, z):
    i, j = int(math.floor(x + .5)) + HALF, int(math.floor(z + .5)) + HALF
    i, j = max(0, min(N - 1, i)), max(0, min(N - 1, j)); return H[j][i]

# ---- 家 (敷地に 1 軒ずつ、切妻の屋根) ----
bm = bmesh.new()
def box(bm, cx, cy, z0, hx, hy, hz, yaw, mat):
    cs, sn = math.cos(yaw), math.sin(yaw); vs = []
    for z in (z0, z0 + hz):
        for (a, b) in ((-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy)):
            vs.append(bm.verts.new((cx + a * cs - b * sn, cy + a * sn + b * cs, z)))
    for q in ((0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
        f = bm.faces.new([vs[k] for k in q]); f.material_index = mat
def gable(bm, cx, cy, z0, hx, hy, rise, yaw, mat):
    cs, sn = math.cos(yaw), math.sin(yaw)
    def P(a, b, z): return bm.verts.new((cx + a * cs - b * sn, cy + a * sn + b * cs, z))
    o = 0.5
    a0, a1, a2, a3 = P(-hx - o, -hy - o, z0), P(hx + o, -hy - o, z0), P(hx + o, hy + o, z0), P(-hx - o, hy + o, z0)
    r0, r1 = P(-hx - o, 0, z0 + rise), P(hx + o, 0, z0 + rise)
    for q in ((a0, a1, r1, r0), (a2, a3, r0, r1), (a0, r0, a3), (a1, a2, r1)):
        f = bm.faces.new(q); f.material_index = mat
mats = MATS["house"] + MATS["roof"]
for lot in D["lots"]:
    x, z, yaw = lot["x"], lot["z"], lot["yaw"]
    # yaw は +z (北) を正面とする向き。Blender の回転は x 軸から反時計回り
    ang = -yaw
    floors = random.choice((1, 2, 2, 2))
    hz = 3.0 * floors
    box(bm, x, z, lot["h"], 4.2, 3.6, hz, ang, random.randrange(4))
    gable(bm, x, z, lot["h"] + hz, 4.2, 3.6, 2.2, ang, 4 + random.randrange(4))
houses = add_object("houses", bm, mats)

# ---- 木 (段々畑と神社の山にまばらに)・鳥居・電車 ----
bm = bmesh.new()
def tree(x, z, s):
    h = hgt(x, z)
    box(bm, x, z, h, .25 * s, .25 * s, 2.2 * s, 0, 2)
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.8 * s, matrix=__import__("mathutils").Matrix.Translation((x, z, h + 3.2 * s)))
for _ in range(900):
    x, z = random.uniform(-HALF, HALF), random.uniform(-HALF, HALF)
    i, j = int(x) + HALF, int(z) + HALF
    k = K[j][i]
    mountain = math.hypot(x + 78, z - 100) < 40
    if k == 6 and (mountain and random.random() < 0.9 or random.random() < 0.18): tree(x, z, random.uniform(.9, 1.5) if mountain else random.uniform(.7, 1.1))
trees = add_object("trees", bm, [MATS["leaf"], MATS["leaf2"], MATS["trunk"]])
for f in trees.data.polygons:
    if f.material_index == 0: f.material_index = random.choice((0, 1))
bm = bmesh.new()
# 鳥居 (神社の石段の上)
tx, tz = -72, 91; th = hgt(tx, tz)
box(bm, tx - 2.2, tz, th, .3, .3, 5.5, 0, 0); box(bm, tx + 2.2, tz, th, .3, .3, 5.5, 0, 0); box(bm, tx, tz, th + 5.2, 3.4, .35, .5, 0, 0); box(bm, tx, tz, th + 4.3, 2.6, .25, .35, 0, 0)
torii = add_object("torii", bm, [MATS["torii"]])
bm = bmesh.new()
for k in range(3):
    box(bm, -40 + k * 20.5, D["rail_z"], 1.7, 10, 1.45, 3.6, 0, 0); box(bm, -40 + k * 20.5, D["rail_z"], 2.6, 10.05, 1.5, .5, 0, 1)
add_object("train", bm, [MATS["train"], MATS["stripe"]])

# ---- 石段: 1m のマスではなく、石段の向きに沿った段の箱で描く (斜めの石段がぎざぎざにならないように) ----
bm = bmesh.new()
for s in D["stairs"]:
    (x0, z0), (x1, z1) = s["from"], s["to"]; L = s["length"]; ux, uz = (x1 - x0) / L, (z1 - z0) / L; ang = math.atan2(uz, ux)
    n = s["steps"]; run = L / n
    for k in range(n):
        top = s["h0"] + (s["h1"] - s["h0"]) * (k + 1) / n
        cx, cz = x0 + ux * run * (k + .5), z0 + uz * run * (k + .5)
        box(bm, cx, cz, min(s["h0"], s["h1"]) - 1.5, run * .5 + .02, s["width"] * .5 + .3, top - min(s["h0"], s["h1"]) + 1.5, ang, 0)
add_object("stairs", bm, [MATS[3]])

# ---- 光と空 ----
world = bpy.data.worlds.new("sky"); scene.world = world; world.color = srgb((200, 222, 240))
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); scene.collection.objects.link(sun)
sun.rotation_euler = (math.radians(50), 0, math.radians(-35)); sun.data.energy = 4
scene.render.engine = "BLENDER_WORKBENCH"
sh = scene.display.shading
sh.light = "STUDIO"; sh.color_type = "MATERIAL"; sh.show_shadows = True; sh.shadow_intensity = 0.35; sh.show_cavity = True; sh.cavity_type = "WORLD"
sh.cavity_ridge_factor = 0.6; sh.cavity_valley_factor = 1.0; sh.background_type = "WORLD"
scene.display.light_direction = (-0.45, -0.55, 0.7)
scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
scene.render.film_transparent = False
scene.view_settings.view_transform = "Standard"; scene.view_settings.exposure = 0.55; sh.show_specular_highlight = False

def shot(name, eye, at, lens=35):
    cam = bpy.data.objects.new(name, bpy.data.cameras.new(name)); scene.collection.objects.link(cam)
    cam.data.lens = lens; cam.data.clip_end = 3000; cam.location = eye
    d = [at[k] - eye[k] for k in range(3)]
    cam.rotation_euler = (math.atan2(math.hypot(d[0], d[1]), -d[2]), 0, math.atan2(d[1], d[0]) - math.pi / 2)
    scene.camera = cam; scene.render.filepath = os.path.join(out, name + ".png")
    bpy.ops.render.render(write_still=True)

road = {r["id"]: r for r in D["roads"]}
def on(rid, k, up=1.6):
    r = road[rid]; x, z = r["path"][k]; return (x, z, r["height"][k] + up)
shot("r07", (235, -235, 105), (-5, 10, 14), 30)
shot("r08", (-210, 215, 115), (20, -30, 8), 30)
dz = road["dandanzaka"]
shot("r09", on("dandanzaka", 22, 1.7), on("dandanzaka", 75, 4.0), 28)
shot("r10", (175, -60, 62), (96, 18, 16), 32)
shot("r11", (-135, -95, 38), (-38, -18, 10), 32)
print("rendered")
