# Meadow arena assets

Downloaded from Poly Haven and ambientCG on 2026-09-29. Every asset below is published under CC0 and may be used and redistributed commercially. Attribution is optional; the links document provenance.

- Grass 004 ground material from ambientCG: https://ambientcg.com/view?id=Grass004
- Rock Face 02 scanned rock wall model and diffuse texture: https://polyhaven.com/a/rock_face_02
- Namaqualand Boulders 01 scanned rock model and diffuse texture: https://polyhaven.com/a/namaqualand_boulders_01
- Rock 09 scanned stone model and diffuse texture: https://polyhaven.com/a/rock_09
- Grass Bermuda 01 grass model, diffuse and alpha textures: https://polyhaven.com/a/grass_bermuda_01
- Poly Haven license: https://polyhaven.com/license
- ambientCG CC0 license: https://docs.ambientcg.com/license/

`grass_bermuda_01_rgba_2k.png` combines the original diffuse and alpha images without changing the source colors; `scripts/Pack-MeadowGrass.py` reproduces it. `scripts/Fetch-MeadowAssets.ps1` obtains the official files and verifies Poly Haven MD5 and ambientCG SHA-256 hashes. The game uses local copies and does not call an asset API.

## Skybox (added 2026-09-30)

- Kloppenheim 06 (Pure Sky), Poly Haven: https://polyhaven.com/a/kloppenheim_06_puresky
- Artists: Greg Zaal (original), Jarod Guest (sky edits).
- License: CC0 1.0, https://creativecommons.org/publicdomain/zero/1.0/ . Commercial use, modification and redistribution are permitted; attribution is optional. Verified against https://polyhaven.com/license on 2026-09-30.
- Official 8K linear HDR source: https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/8k/kloppenheim_06_puresky_8k.hdr
- Source MD5: `4e853a7134635b333c1bde6d1019f452` (verified against the official asset API).
- Distributed file: `Textures/PolyHaven/kloppenheim_06_puresky_8k_cube.dds`.
- Conversion: bilinear equirectangular sampling to six 2048 x 2048 Direct3D cube faces, 12 mip levels, BC6H unsigned HDR compression, preserving linear luminance. File size: 33,554,740 bytes (32 MiB). Used for both background and liquid reflections.
- Converted SHA-256: `D4305D83257D555F5F17A3CAD6A4EC7BDF65DD310C87D5928C9708068A26EA5A`.

`scripts/Import-Skybox.ps1` downloads, verifies and converts the source using `scripts/Convert-Skybox.cpp` and the bundled DirectXTex library. Only the converted DDS and these provenance notes are packaged. Website thumbnails, logos and preview renders are not included.
