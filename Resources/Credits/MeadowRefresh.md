# Meadow scenery refresh — 2026-10-08

Downloaded from the official Poly Haven asset distribution. All four assets are
CC0: commercial use, modification and redistribution are permitted, with no
attribution requirement. Sources and SHA-256/official MD5 checksums for every
download are recorded in `MeadowRefreshSources.json`.

- [Boulder 01](https://polyhaven.com/a/boulder_01) — Rico Cilliers. FBX and 2K diffuse, DirectX normal, roughness, AO.
- [Namaqualand Boulder 02](https://polyhaven.com/a/namaqualand_boulder_02) — Greg Zaal (photography), Rico Cilliers (modeling). FBX and 2K PBR maps.
- [Grass Medium 02](https://polyhaven.com/a/grass_medium_02) — Rico Cilliers. FBX and 2K PBR/alpha maps. `scripts/Pack-MeadowRefresh.py` combines the original diffuse with correctly normalized 16-bit alpha into an 8-bit RGBA PNG; source RGB is preserved.
- [Leafy Grass](https://polyhaven.com/a/leafy_grass) — Charlotte Baglioni. 4K diffuse and 2K DirectX normal, roughness, AO.

[Official license](https://polyhaven.com/license), [CC0 deed](https://creativecommons.org/publicdomain/zero/1.0/).

Only distributed assets are included; website preview renders and branding are
not packaged. `scripts/Fetch-MeadowRefresh.ps1` retrieves files and verifies the
official hashes. Import applies FBX node axis/unit transforms, converts UV V to
DirectX texture coordinates and selects LOD0 rather than overlaying every LOD.

The new rocks replace enlarged rock-face scans around the field. Authored grass
tufts replace the old hand-selected atlas cards. All scenery is seated on the
terrain, while the playable collision/ink plane remains flat.
