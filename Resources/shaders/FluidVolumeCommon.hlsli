#ifndef FLUID_VOLUME_COMMON
#define FLUID_VOLUME_COMMON
#include "FluidMaterial.hlsli"
struct VolumeParticle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
    float type; float3 pad;
};
// Rows of the world-to-kernel transform, center in w. Particle storage is
// unchanged; this is a rendering-only reconstruction (never feeds physics).
struct FluidShape { float4 row0; float4 row1; float4 row2; float4 info; };
cbuffer VolumeSettings : register(b1) {
    float3 volumeOrigin; float voxelSize;
    uint3 volumeSize; uint particleCount;
    float3 previousOrigin; float historyValid;
    float frameDt; float isoValue; float filterAxis; float debugMode;
    float4 phaseColor[3];
};
uint VolumePhase(float type) {
    if (type < 0.5f || (type > 2.5f && type < 3.5f)) return 0;
    return type > 1.5f && type < 2.5f ? 1 : 2;
}
uint VolumeIndex(uint3 p) { return (p.z * volumeSize.y + p.y) * volumeSize.x + p.x; }
uint VolumeHash(int3 c) {
    // Must match FluidSimCS.hlsl.
    return ((uint)c.x * 73856093U ^ (uint)c.y * 19349663U ^ (uint)c.z * 83492791U) & 65535U;
}
float3 ShapeCenter(FluidShape s) { return float3(s.row0.w,s.row1.w,s.row2.w); }
float3 ShapePoint(FluidShape s, float3 d) { return float3(dot(s.row0.xyz,d),dot(s.row1.xyz,d),dot(s.row2.xyz,d)); }
#endif
