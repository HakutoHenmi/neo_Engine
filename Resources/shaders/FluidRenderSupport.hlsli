// CalcDensity includes the particle itself. A particle with no same-phase
// neighbour inside the 0.4 world-unit SPH support has exactly this density.
// Keep this expression synchronized with FluidSimCS.hlsl's poly6 constants.
float FluidRenderSupport(float density) {
    const float selfDensity = (315.0f / (64.0f * 3.1415926535f * 0.000262144f)) *
                              0.16f * 0.16f * 0.16f;
    // Fade weak support instead of popping when a neighbour crosses the radius.
    // This affects rendering only; isolated particles continue simulating.
    return smoothstep(0.001f, 0.5f, density - selfDensity);
}
