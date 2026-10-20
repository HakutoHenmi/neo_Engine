#ifndef FLUID_MATERIAL_HLSLI
#define FLUID_MATERIAL_HLSLI
static const float PLAYER_REST_RADIUS = 2.80f;
static const float FLUID_SELF_DENSITY = 24.4794f;
static const float WATER_GROUND_LIFETIME = 5.0f;
bool FluidIsWater(float type) { return type > 0.5f && type < 1.5f; }
float FluidRestDensity(float type) { return FluidIsWater(type) ? 125.0f : 275.0f; }
// Legacy reconstruction uses sampled particle volume. Morph reconstruction
// reads its world-space sample volume from metadata, not normalized solver rho.
float FluidReconstructionVolume(float type, float density) {
    return 1.0f / clamp(density, FLUID_SELF_DENSITY, FluidRestDensity(type));
}
#endif
