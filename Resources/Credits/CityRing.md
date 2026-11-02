# Ring city assets (absorb mode) — 2026-11-02

All models and textures added for the ring city (alley → houses → shops → offices → towers) are CC0 1.0:
commercial use, modification and redistribution are permitted, with no attribution requirement.

- Poly Haven — [License](https://polyhaven.com/license)
- ambientCG — [License](https://docs.ambientcg.com/license/)
- [CC0 deed](https://creativecommons.org/publicdomain/zero/1.0/)

`scripts/Fetch-CityRingAssets.ps1` downloads every file. Poly Haven files are verified against the official
MD5; ambientCG zips have no published hash, so their byte size is checked against the API and only the
colour, DirectX normal, roughness, AO and metalness maps are kept. Source URLs, MD5 and SHA-256 of each file
are in `CityRingSources.json`. Only 2K FBX models and 2K JPG/PNG maps are packaged.

## Models (Poly Haven, https://polyhaven.com/a/<id>)
planter_box_01, planter_box_02, potted_plant_01, potted_plant_02, painted_wooden_bench, wooden_picnic_table,
plastic_monobloc_chair_01, metal_trash_can, modular_electricity_poles, tree_small_02, concrete_cat_statue,
exterior_aircon_unit, street_lamp_01, street_lamp_02, modular_street_seating, outdoor_table_chair_set_01,
CoffeeCart_01, rollershutter_door, rollershutter_window_01, modular_urban_apartments_facade,
modular_fire_escape, Barrel_01, wooden_barrels_01, security_light, island_tree_01, jacaranda_tree,
horse_statue_01, bronze_whale_statue, portable_generator, fire_hydrant, modular_chainlink_fence, cement_bag

The three tree models are downloaded but not placed: their foliage is very dense and takes tens of
seconds to load and simplify.

## Surfaces
Poly Haven: brick_wall_02, plastered_wall_02, weathered_plank_siding, clay_roof_tiles, grey_roof_tiles,
roof_slates_02, patterned_paving, square_brick_paving

ambientCG (https://ambientcg.com/view?id=<id>): Facade001, Facade003, Facade005, Facade006, Facade012,
Facade014, Facade018A, Facade019A, Facade020A

Houses, shops, offices and towers are generated in `Game/Systems/RingCity.cpp` from these surfaces.
