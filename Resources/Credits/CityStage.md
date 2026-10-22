# Slime city stage assets — 2026-10-20 (modern city revision)

All models and textures for the back alley / avenue / scramble crossing stage come from the official
Poly Haven distribution and are CC0 1.0: commercial use, modification and
redistribution are permitted, with no attribution requirement.
[License](https://polyhaven.com/license) ·
[CC0 deed](https://creativecommons.org/publicdomain/zero/1.0/).

`scripts/Fetch-CityStageAssets.ps1` downloads every file and verifies the official
MD5. Source URLs, MD5 and SHA-256 of each file are in `CityStageSources.json`.
Only 2K FBX models and 2K JPG maps (diffuse, DirectX normal, roughness, AO) are
packaged; website previews and branding are not.

## Models (https://polyhaven.com/a/<id>)
street_rat, cannon_01, wooden_crate_01, wooden_crate_02, cardboard_box_01,
trashbag, plastic_crate_01, plastic_crate_02, industrial_pastic_container,
plastic_container, metal_jerrycan, propane_tank, old_tyre, WetFloorSign_01,
hand_truck, covered_car, korean_public_payphone_01, utility_box_01,
utility_box_02, power_box_01, concrete_road_barrier, concrete_road_barrier_02,
water_manhole_cover, gothic_statue

## Surfaces (roads, pavements, facades, shutters, barricade, golem)
asphalt_02, large_grey_tiles, concrete_floor_worn_001,
concrete_wall_008, exterior_wall_cladding_02, rectangular_facade_tiles_02,
concrete_tile_facade, concrete_slab_wall, grey_tiles, painted_metal_shutter,
rusty_metal_shutter, white_bricks, medieval_wood, castle_wall_slates

Buildings, towers, street lights, road markings, the shutter gate, the barricade
planks and the stone golem are
generated in `Game/Systems/CityStage.inl` from these surfaces.
