"""Deterministic closed volume meshes and original layered slime Foley (stdlib only)."""
from pathlib import Path
import math
import random
import struct
import wave

ROOT = Path(__file__).resolve().parents[1]
SHAPES = {
    "hammer": [((-.5,.1,-.5),(.5,4,.5)),((-2.7,4,-1.1),(2.7,5.8,1.1))],
    "sword": [((-.5,.1,-2),(.5,1.1,0)),((-1.8,.1,0),(1.8,1.1,.5)),((-.7,.1,.5),(.7,1.5,7.5))],
    "plane": [((-.8,.2,-3),(.8,1.6,4)),((-5,.5,-.5),(-.8,1.3,1.8)),((.8,.5,-.5),(5,1.3,1.8)),((-2.4,.6,-3.2),(-.8,1.2,-2)),((.8,.6,-3.2),(2.4,1.2,-2))],
}

def mesh(name, boxes):
    path = ROOT / "Resources/Models/SlimeLife" / f"{name}.obj"
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# Original closed solids for volume target sampling; part boundaries do not overlap."]
    triangles = [(0,2,1),(0,3,2),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(3,7,6),(3,6,2),(0,4,7),(0,7,3),(1,2,6),(1,6,5)]
    for part, (lo, hi) in enumerate(boxes):
        x,y,z = lo; X,Y,Z = hi
        vertices = [(x,y,z),(X,y,z),(X,Y,z),(x,Y,z),(x,y,Z),(X,y,Z),(X,Y,Z),(x,Y,Z)]
        lines.append(f"g part_{part}")
        lines += [f"v {a} {b} {c}" for a,b,c in vertices]
        lines += ["f " + " ".join(str(part*8+i+1) for i in tri) for tri in triangles]
    path.write_text("\n".join(lines)+"\n",encoding="utf-8")

def sound(name, duration, seed):
    rng = random.Random(seed); rate = 48000; values=[]; filtered=0
    for i in range(int(duration*rate)):
        t=i/rate; filtered += .11*(rng.uniform(-1,1)-filtered)
        pulse=math.exp(-((t-duration*.67)/.025)**2)
        stretch=math.sin(math.pi*min(1,t/duration))**1.3
        tone=math.sin(2*math.pi*(95*t-28*t*t))*math.exp(-t*6)
        value=(filtered*(.18*stretch+.65*pulse)+tone*.09*stretch)*.8
        values.append(struct.pack("<h",int(max(-.9,min(.9,value))*32767)))
    path=ROOT/"Resources/Sound"/f"slime-life-{name}.wav"
    with wave.open(str(path),"wb") as wav:
        wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(rate);wav.writeframes(b"".join(values))

if __name__ == "__main__":
    for name, boxes in SHAPES.items(): mesh(name, boxes)
    for i in range(3): sound(f"split-{i}",.48+i*.03,71+i)
    sound("engulf",.8,104);sound("merge",.44,130)
