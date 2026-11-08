# render_sprites.py
# タイトル・セレクト・ロードに並べる物の画像を、ゲームのローポリのモデルから撮影する。
# 使い方: C:/blender/blender.exe --background --factory-startup --python scripts/lowpoly/render_sprites.py
# 書き出した後、python scripts/lowpoly/finish_sprites.py で切り抜きと影の画像を作る
import math
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "../Generated/SpriteRaw"
PALETTE = ROOT / "Resources/Textures/Lowpoly/palette.png"
MODELS = ROOT / "Resources/Models/Lowpoly"
# 画像の名前・モデル・左右の向き
SPRITES = [("tsumiki", 20), ("mikan", 20), ("nuigurumi", 18), ("kago", 15), ("danboru", 32), ("shitsugaiki", 26), ("jihanki", 26),
           ("kei_car", 38), ("my_house", 26), ("jitensha", 30), ("neko", 16), ("inugoya", 30)]
RAW.mkdir(parents=True, exist_ok=True)
for name, yaw in SPRITES:
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
    bsdf.inputs["Roughness"].default_value = .75
    tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    obj.data.materials.append(material)
    for p in obj.data.polygons:
        p.use_smooth = False
    obj.rotation_euler = (0, 0, math.radians(yaw))
    bpy.context.view_layer.update()
    corners = [obj.matrix_world @ __import__("mathutils").Vector(c) for c in obj.bound_box]
    lo = [min(c[i] for c in corners) for i in range(3)]
    hi = [max(c[i] for c in corners) for i in range(3)]
    size = max(hi[i] - lo[i] for i in range(3))
    mid = [(lo[i] + hi[i]) / 2 for i in range(3)]
    pitch = math.radians(14)
    bpy.ops.object.camera_add(location=(mid[0], mid[1] - size * 3 * math.cos(pitch), mid[2] + size * 3 * math.sin(pitch)), rotation=(math.pi / 2 - pitch, 0, 0))
    cam = bpy.context.object
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = size * 1.5
    cam.data.clip_end = size * 12
    bpy.context.scene.camera = cam
    bpy.ops.object.light_add(type="SUN", rotation=(math.radians(52), math.radians(8), math.radians(-34)))
    bpy.context.object.data.energy = 3.2
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[1].default_value = .95
    scene = bpy.context.scene
    scene.world = world
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 24
    scene.cycles.device = "CPU"
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    scene.render.resolution_x = scene.render.resolution_y = 768
    scene.render.filepath = str(RAW / f"{name}.png")
    bpy.ops.render.render(write_still=True)
