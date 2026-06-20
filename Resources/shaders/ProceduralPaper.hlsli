// Resources/shaders/ProceduralPaper.hlsli
#ifndef PROCEDURAL_PAPER_HLSLI
#define PROCEDURAL_PAPER_HLSLI

// 3D Hash
float hash(float3 p) {
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// 3D Value Noise
float noise(float3 x) {
    float3 i = floor(x);
    float3 f = frac(x);
    // 滑らかな補間 (Quintic)
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    
    return lerp(lerp(lerp(hash(i + float3(0,0,0)), hash(i + float3(1,0,0)), f.x),
                     lerp(hash(i + float3(0,1,0)), hash(i + float3(1,1,0)), f.x), f.y),
                lerp(lerp(hash(i + float3(0,0,1)), hash(i + float3(1,0,1)), f.x),
                     lerp(hash(i + float3(0,1,1)), hash(i + float3(1,1,1)), f.x), f.y), f.z);
}

// FBM (Fractal Brownian Motion)
float fbm(float3 p) {
    float f = 0.0;
    float w = 0.5;
    for (int i = 0; i < 4; i++) {
        f += w * noise(p);
        p *= 2.0;
        w *= 0.5;
    }
    return f;
}

// プロシージャル和紙マテリアル適用関数
void ApplyProceduralPaper(float3 worldPos, inout float3 albedo, inout float3 normal, float scale = 1.0, float strength = 1.0) {
    // ユーザーの要望により、床やオブジェクトに乗る「和紙エフェクト（ノイズ）」を完全に無効化します。
    // albedo や normal には何の変更も加えません。
}
#endif // PROCEDURAL_PAPER_HLSLI
