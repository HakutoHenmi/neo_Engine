"""Small, low cost pebble mesh for the scanned Rock 09 surface image."""

from pathlib import Path
import math

root = Path(__file__).resolve().parents[1]
out = root / "Resources/Models/Environment/field-pebble.obj"
phi = (1 + math.sqrt(5)) / 2
points = [(-1, phi, 0), (1, phi, 0), (-1, -phi, 0), (1, -phi, 0),
          (0, -1, phi), (0, 1, phi), (0, -1, -phi), (0, 1, -phi),
          (phi, 0, -1), (phi, 0, 1), (-phi, 0, -1), (-phi, 0, 1)]
faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
         (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
         (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
lines = ["# Small stone carrying the Poly Haven Rock 09 CC0 scan", "o field_pebble"]
index = 0
for face in faces:
    vertices = []
    for idx in face:
        x, y, z = points[idx]
        length = math.sqrt(x * x + y * y + z * z)
        vertices.append((x / length * .5, y / length * .32, z / length * .44))
    a, b, c = vertices
    ux, uy, uz = (b[i] - a[i] for i in range(3))
    vx, vy, vz = (c[i] - a[i] for i in range(3))
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    nlen = math.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / nlen, ny / nlen, nz / nlen
    ids = []
    for x, y, z in vertices:
        index += 1
        lines.append(f"v {x:.6f} {y:.6f} {z:.6f}")
        lines.append(f"vt {x + .5:.6f} {z + .5:.6f}")
        lines.append(f"vn {nx:.6f} {ny:.6f} {nz:.6f}")
        ids.append(f"{index}/{index}/{index}")
    lines.append("f " + " ".join(ids))
out.write_text("\n".join(lines) + "\n", encoding="ascii")
print(out)
