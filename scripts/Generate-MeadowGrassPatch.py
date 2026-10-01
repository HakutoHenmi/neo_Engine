"""Make an instanced field patch from Poly Haven's Bermuda grass photo atlas."""

from pathlib import Path
import math
import random

out = Path(__file__).resolve().parents[1] / "Resources/Models/Environment/meadow-grass-patch.obj"
rng = random.Random(4856)
lines = ["# Instanced grass cards using the Poly Haven Grass Bermuda 01 diffuse/alpha atlas",
         "o meadow_grass_patch"]
index = 0
atlas = ((.01, .255, .015, .265), (.255, .50, .015, .265),
         (.50, .755, .015, .265), (.755, .99, .015, .265))


def quad(cx, cz, angle, width, height, uv):
    global index
    dx = math.cos(angle) * width / 2
    dz = math.sin(angle) * width / 2
    u0, u1, v0, v1 = uv
    vertices = ((cx - dx, 0, cz - dz, u0, v1),
                (cx + dx, 0, cz + dz, u1, v1),
                (cx + dx, height, cz + dz, u1, v0),
                (cx - dx, height, cz - dz, u0, v0))
    ids = []
    for x, y, z, u, v in vertices:
        index += 1
        lines.append(f"v {x:.6f} {y:.6f} {z:.6f}")
        lines.append(f"vt {u:.6f} {v:.6f}")
        lines.append("vn 0 0 1")
        ids.append(index)
    lines.append(f"f {ids[0]}/{ids[0]}/{ids[0]} {ids[1]}/{ids[1]}/{ids[1]} {ids[2]}/{ids[2]}/{ids[2]}")
    lines.append(f"f {ids[0]}/{ids[0]}/{ids[0]} {ids[2]}/{ids[2]}/{ids[2]} {ids[3]}/{ids[3]}/{ids[3]}")


for i in range(72):
    x = rng.uniform(-8.5, 8.5)
    z = rng.uniform(-8.5, 8.5)
    size = rng.uniform(.58, 1.15)
    angle = rng.uniform(0, math.tau)
    uv = atlas[i % len(atlas)]
    quad(x, z, angle, size * 1.15, size, uv)
    quad(x, z, angle + math.pi / 2, size * 1.15, size, uv)

out.write_text("\n".join(lines) + "\n", encoding="ascii")
print(out)
