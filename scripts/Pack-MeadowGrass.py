"""Pack Poly Haven's separate Bermuda grass diffuse and alpha maps for the game shader."""

from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1] / "Resources/Textures/PolyHaven"
diffuse = Image.open(root / "grass_bermuda_01_diff_2k.jpg").convert("RGB")
alpha = Image.open(root / "grass_bermuda_01_alpha_2k.png").convert("L")
if diffuse.size != alpha.size:
    raise ValueError("Grass diffuse and alpha dimensions differ")
result = diffuse.copy()
result.putalpha(alpha)
result.save(root / "grass_bermuda_01_rgba_2k.png", optimize=True)
