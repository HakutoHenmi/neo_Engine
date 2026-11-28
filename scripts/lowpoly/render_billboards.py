# render_billboards.py
# チャレンジステージ (積み木の街) の遠くの景色に立てる板 (ビルボード) の絵を、ゲームのローポリのモデルから撮影する。
# 使い方: C:/blender/blender.exe --background --factory-startup --python scripts/lowpoly/render_billboards.py
# 書き出した後、python scripts/lowpoly/finish_billboards.py で 1 枚の絵 (Resources/Textures/Lowpoly/billboards.png) にまとめる
import math
from pathlib import Path

import bpy
import mathutils

ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "../Generated/BillboardRaw"
PALETTE = ROOT / "Resources/Textures/Lowpoly/palette.png"
MODELS = ROOT / "Resources/Models/Lowpoly"
# 並べる順 (finish_billboards.py と同じ)。木 5 つと家 3 つ。左右の向き
BILLBOARDS = [("ki_maru", 0), ("sugi", 0), ("sakura", 20), ("matsu", 10), ("kaki_no_ki", 15),
              ("house_hiraya", 28), ("house_kawara", 32), ("my_house", 26)]
RAW.mkdir(parents=True, exist_ok=True)
for name, yaw in BILLBOARDS:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.wm.obj_import(filepath=str(MODELS / f"{name}.obj"), forward_axis="NEGATIVE_Z", up_axis="Y")
    obj = bpy.context.selected_objects[0]
    # エンジンは x を反転して読むので、ゲームと同じ向きにする
    obj.scale = (-1, 1, 1)
    bpy.ops.object.transform_apply(scale=True)
    obj.data.flip_normals()
    material = bpy.data.materials.new("palette")
    material.use_nodes = True
    tree = material.node_tree
    tex = tree.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(str(PALETTE))
    tex.interpolation = "Closest"
    bsdf = tree.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = .8
    tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    obj.data.materials.append(material)
    for p in obj.data.polygons:
        p.use_smooth = False
    obj.rotation_euler = (0, 0, math.radians(yaw))
    bpy.context.view_layer.update()
    corners = [obj.matrix_world @ mathutils.Vector(c) for c in obj.bound_box]
    lo = [min(c[i] for c in corners) for i in range(3)]
    hi = [max(c[i] for c in corners) for i in range(3)]
    size = max(hi[i] - lo[i] for i in range(3))
    mid = [(lo[i] + hi[i]) / 2 for i in range(3)]
    # (遠くから少し見下ろす高さ。地面に立つ板なので、ほぼ真横から)
    pitch = math.radians(6)
    bpy.ops.object.camera_add(location=(mid[0], mid[1] - size * 3 * math.cos(pitch), mid[2] + size * 3 * math.sin(pitch)),
                              rotation=(math.pi / 2 - pitch, 0, 0))
    cam = bpy.context.object
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = size * 1.15
    cam.data.clip_end = size * 12
    bpy.context.scene.camera = cam
    bpy.ops.object.light_add(type="SUN", rotation=(math.radians(50), math.radians(8), math.radians(-30)))
    bpy.context.object.data.energy = 3
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.1
    scene = bpy.context.scene
    scene.world = world
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 16
    scene.cycles.device = "CPU"
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    scene.render.resolution_x = scene.render.resolution_y = 384
    scene.render.filepath = str(RAW / f"{name}.png")
    bpy.ops.render.render(write_still=True)
