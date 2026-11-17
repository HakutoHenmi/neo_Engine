# lowpoly_core.py
# ローポリのモデルを作る共通の部品 (形の組み立て・色・書き出し・一覧の画像)。
# モデルの形はステージごとのファイル (build_lowpoly.py の部屋、lowpoly_town.py の坂の町) に書く。
# 大きさの単位は cm で、y が上、底は y=0。色は palette.json の色の名前で指定し、面ごとに色の画像の中のマスを指す。
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
# モデルの組 (room 部屋・town 坂の町)。--group で組ごとに作れる。set_group の後に足したモデルがその組になる
GROUPS = {}
# 組ごとの「部品を太らせる量」の上限 (cm)。大きな建物で細い手すりなどが太りすぎないようにする
GROUP_EPS = {}
_group = ["room"]


def set_group(name, eps=None):
    _group[0] = name
    if eps is not None:
        GROUP_EPS[name] = eps


def add_model(name, build):
    MODELS[name] = build
    GROUPS[name] = _group[0]


def model(name=None):
    def add(fn):
        add_model(name or fn.__name__, fn)
        return fn
    return add


def wheel(s, x, z, y, r, width, tire="black", hub="gray_light", sides=8):
    s.tube((x, y, z - width), (x, y, z + width), r, r, tire, sides=sides)
    s.tube((x, y, z - width * 1.15), (x, y, z + width * 1.15), r * .5, r * .5, hub, sides=6)


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
    group = args[args.index("--group") + 1] if "--group" in args else None
    bpy.ops.wm.read_factory_settings(use_empty=True)
    OUT.mkdir(parents=True, exist_ok=True)
    objects = []
    for name, build in MODELS.items():
        if only and name not in only:
            continue
        if group and GROUPS.get(name) != group:
            continue
        # 太らせる量はモデルの大きさに合わせる (一度作って大きさを測る)
        Shape.EPS = 0
        first = build()
        size = max(max(v[i] for v in first.verts) - min(v[i] for v in first.verts) for i in range(3))
        Shape.EPS = min(max(size * .0008, .02), GROUP_EPS.get(GROUPS.get(name), 1e9))
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
