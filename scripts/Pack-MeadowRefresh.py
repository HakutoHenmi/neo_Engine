"""Pack the CC0 grass atlas; no texture painting or lossy resampling."""
from pathlib import Path
from PIL import Image
import numpy as np
root=Path(__file__).resolve().parents[1]/'Resources/Textures/PolyHaven'
color=Image.open(root/'grass_medium_02_diff_2k.jpg').convert('RGB')
source=Image.open(root/'grass_medium_02_alpha_2k.png')
samples=np.asarray(source)
# Official alpha PNG is 16-bit. convert('L') would saturate almost every edge.
alpha=Image.fromarray(((samples.astype(np.float32)/65535 if samples.dtype==np.uint16 else samples/255)*255).round().astype(np.uint8))
assert color.size==alpha.size
color.putalpha(alpha)
color.save(root/'grass_medium_02_rgba_2k.png',optimize=True)
