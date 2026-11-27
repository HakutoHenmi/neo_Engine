#ifndef FLUID_VOLUME_COMMON
#define FLUID_VOLUME_COMMON
#include "FluidMaterial.hlsli"
struct VolumeParticle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
    float type; float3 pad;
};
// 世界からカーネルへの変換の各行。w に中心。粒子の保存形式は
// 変えない。描画専用の再構成で、物理には決して使わない。
struct FluidShape { float4 row0; float4 row1; float4 row2; float4 info; };
cbuffer VolumeSettings : register(b1) {
    float3 volumeOrigin; float voxelSize;
    uint3 volumeSize; uint particleCount;
    float3 previousOrigin; float historyValid;
    float frameDt; float isoValue; float filterAxis; float debugMode;
    float4 phaseColor[3];
    float3 playerBodyCenter; float playerDecoration;
    float3 playerForward; float playerFaceCamera;
    float3 playerRadii; float playerMotion;
    float3 playerSlope; float playerAir; // x/z: 地形の傾き。y: 固い飾りの質量の倍率。
#ifdef FLUID_VOLUME_CORE
    float4 playerLiquidCore; // 描画専用。コンピュートは 44 DWORD の並びを保つ。
    float4 playerMorph; // 形の印・物理的な倍率・主な粒子数・有効かどうか。
    float4 sunDirection; // xyz: 日光の進む向き。w: 強さ (0 = なし)。
#endif
};
uint VolumePhase(float type) {
    if (type < 0.5f || (type > 2.5f && type < 3.5f) || type>=4.0f) return 0;
    return type > 1.5f && type < 2.5f ? 1 : 2;
}
uint VolumeIndex(uint3 p) { return (p.z * volumeSize.y + p.y) * volumeSize.x + p.x; }
uint VolumeHash(int3 c) {
    // 変形 / しぶき以外のバケットは FluidSimCS.hlsl と一致させる (持ち主ごとのハッシュのずらし量は 0)。
    // 変形の形はこの探索を使わず、独自の等方的な再構成を使う。
    // BuildShapes は -(バケット数 [+ ホースの水を画面空間で描くときは .5]) を受け取る。
    return ((uint)c.x * 73856093U ^ (uint)c.y * 19349663U ^ (uint)c.z * 83492791U) & (filterAxis<0?uint(-filterAxis)-1U:65535U);
}
float3 ShapeCenter(FluidShape s) { return float3(s.row0.w,s.row1.w,s.row2.w); }
float3 ShapePoint(FluidShape s, float3 d) { return float3(dot(s.row0.xyz,d),dot(s.row1.xyz,d),dot(s.row2.xyz,d)); }
#endif
