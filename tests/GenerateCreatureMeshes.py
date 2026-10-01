"""Generate the small, reusable hard-surface forms of the procedural boss."""
from pathlib import Path
from math import sqrt

ROOT = Path(__file__).resolve().parents[1] / "Resources/Models/Chrono"


def mesh(name, rings):
    """Each station is (z, half width, half height, ridge height)."""
    vertices = []
    for z, width, height, ridge in rings:
        vertices += [(-width, 0, z), (-width * .53, height, z),
                     (0, height + ridge, z), (width * .53, height, z),
                     (width, 0, z), (width * .53, -height, z),
                     (0, -height, z), (-width * .53, -height, z)]
    faces = []
    for station in range(len(rings) - 1):
        for side in range(8):
            a = station * 8 + side
            b = station * 8 + (side + 1) % 8
            c, d = a + 8, b + 8
            faces.extend(((a, b, d), (a, d, c)))
    for side in range(1, 7):
        faces.append((0, side + 1, side))
        back = (len(rings) - 1) * 8
        faces.append((back, back + side, back + side + 1))
    lines = [f"o {name}", "s off"]
    lines += ["v %.6f %.6f %.6f" % vertex for vertex in vertices]
    for normal_index, (a, b, c) in enumerate(faces, 1):
        p, q, r = vertices[a], vertices[b], vertices[c]
        u = tuple(q[i] - p[i] for i in range(3))
        v = tuple(r[i] - p[i] for i in range(3))
        n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        length = sqrt(sum(x*x for x in n)) or 1
        lines.append("vn %.6f %.6f %.6f" % tuple(x/length for x in n))
        lines.append(f"f {a+1}//{normal_index} {b+1}//{normal_index} {c+1}//{normal_index}")
    (ROOT / f"{name}.obj").write_text("\n".join(lines) + "\n", encoding="ascii")


for name, rings in {
    "link": [(-.5,.28,.34,0),(-.35,.85,.82,.04),(.26,1,.9,.04),(.5,.53,.53,0)],
    "armor": [(-.5,.28,.11,.12),(-.3,.88,.17,.33),(.18,1,.16,.42),(.5,.48,.09,.12)],
    "spar": [(-.5,.7,.72,.08),(-.28,.85,.78,.12),(.3,.59,.53,.1),(.5,.35,.34,0)],
    "primary": [(-.5,.11,.09,0),(-.25,.7,.13,.04),(.04,1,.16,.1),(.3,.68,.11,.05),(.5,.045,.035,0)],
    "covert": [(-.5,.24,.12,.02),(-.27,.8,.2,.1),(.05,1,.2,.15),(.3,.72,.13,.07),(.5,.08,.04,0)],
    "beak": [(-.5,.78,.65,.18),(-.18,1,.72,.3),(.22,.55,.42,.1),(.5,.03,.04,0)],
    "horn": [(-.5,.64,.65,0),(-.2,.7,.68,.04),(.18,.33,.38,.02),(.5,.025,.03,0)],
}.items():
    mesh(name, rings)
