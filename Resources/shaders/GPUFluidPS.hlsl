Texture2D<float4> tex : register(t0);
Texture2D<float> frontDepthTex : register(t1);
SamplerState smp : register(s0);

uint GetFluidPhase(float type) {
    if (type < 0.5f || (type > 2.5f && type < 3.5f)) return 0U;
    if (type > 1.5f && type < 2.5f) return 1U;
    return 2U;
}

float DecodeSurfaceDepth(float surfaceKey) {
    return floor(surfaceKey * 0.25f) / 1024.0f;
}

uint DecodeSurfacePhase(float surfaceKey) {
    return (uint)fmod(surfaceKey, 4.0f);
}

float4 RenderPhase(float4 svpos, float2 uv, float viewZ, float4 color,
                   float type, uint targetPhase) {
    if (GetFluidPhase(type) != targetPhase) {
        discard;
    }
    // 距離を計算 (UV: 0.0 ~ 1.0) -> 中心(0.5, 0.5) からの距離
    float2 centerOffset = uv - float2(0.5f, 0.5f);
    float distSq = dot(centerOffset, centerOffset);
    
    // 半径0.5 (二乗で0.25) の外側は破棄
    if (distSq > 0.25f) { discard; }
    
    // 中心はアルファ1.0、端は0.0になるようなソフトな減衰
    float dist = sqrt(distSq);
    // パーティクル同士が滑らかに溶け合い、アウトラインの「凸凹」をなくすため、
    // powによる極端な減衰をなくし、ふっくらとした広いグラデーション（ガウス分布に近い形）にします
    float alpha = saturate(1.0f - (dist / 0.5f));
    alpha = alpha * alpha * (3.0f - 2.0f * alpha); // Smoothstep曲線で滑らかに繋げる
    
    float sphereRadius = 0.90f;
    if (type > 2.5f && type < 3.5f) {
        sphereRadius = 0.65f;
    } else if (type >= 0.5f) {
        sphereRadius = 0.72f;
    }

    float normalizedDistSq = distSq * 4.0f;
    float z_offset = sqrt(max(0.0f, 1.0f - normalizedDistSq)) * sphereRadius;
    float fragmentDepth = viewZ - z_offset;
    float surfaceKey = frontDepthTex.Load(int3(int2(svpos.xy), 0));
    float frontDepth = DecodeSurfaceDepth(surfaceKey);
    uint frontPhase = DecodeSurfacePhase(surfaceKey);

    // Accumulate the thickness behind the resolved front surface.  The old
    // symmetric 0.14 band kept only a thin shell, exposing every individual
    // particle and leaving water below its visibility threshold.
    float layerThickness = (frontPhase == 2U) ? 1.6f : 2.3f;
    float depthBehindFront = fragmentDepth - frontDepth;
    if (surfaceKey >= 1.0e19f || frontPhase != GetFluidPhase(type) ||
        depthBehindFront < -0.03f || depthBehindFront > layerThickness) {
        discard;
    }

    // メタボール合成用に、アルファ（厚み）を蓄積する
    float4 outColor = color;
    bool isPlayerSlime = (type < 0.5f);
    bool isDecoySlime = (type > 1.5f && type < 2.5f);
    bool isLostPlayerSlime = (type > 2.5f && type < 3.5f);
    bool isSlime = (isPlayerSlime || isDecoySlime || isLostPlayerSlime);
    
    if (isSlime) {
        if (isPlayerSlime || isLostPlayerSlime) {
            // プレイヤー：白飛びを防ぎつつ鮮やかなエメラルドグリーンにする
            outColor.r = 0.05f;
            outColor.g = 0.8f;
            outColor.b = 0.2f;
        } else {
            // デコイ：はっきりとした鮮やかな黄色
            outColor.r = 1.0f;
            outColor.g = 0.9f;
            outColor.b = 0.05f;
        }
        // ★重要: 密度が1.0に張り付いて巨大化する（透明な隙間ができる）のを防ぐため、
        // 1粒あたりの密度を下げて、複数重なった中心部分だけが濃くなるようにします。
        outColor.a = alpha * (isLostPlayerSlime ? 0.55f : 0.2f) * color.a;
    } else {
        // カエルの卵のように黒く濁らないよう、元の明るい色をそのまま使う
        outColor.rgb = color.rgb;
        // The larger reconstruction footprint closes gaps; keep each sample
        // light so the accumulated surface does not saturate into white balls.
        // 完全に1枚の水たまりだけが残るようになります。
        outColor.a = alpha * 0.055f * color.a;
    }
    
    // 加算ブレンド(ONE)で正しく色を乗せるための事前乗算アルファ (Premultiplied Alpha)
    outColor.rgb *= outColor.a;
    
    if (outColor.a <= 0.0f) { discard; }
    
    return outColor;
}

float4 mainPhase0(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0,
                  float viewZ : TEXCOORD1, float4 color : COLOR0,
                  float type : TEXCOORD2) : SV_TARGET0 {
    return RenderPhase(svpos, uv, viewZ, color, type, 0U);
}

float4 mainPhase1(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0,
                  float viewZ : TEXCOORD1, float4 color : COLOR0,
                  float type : TEXCOORD2) : SV_TARGET0 {
    return RenderPhase(svpos, uv, viewZ, color, type, 1U);
}

float4 mainPhase2(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0,
                  float viewZ : TEXCOORD1, float4 color : COLOR0,
                  float type : TEXCOORD2) : SV_TARGET0 {
    return RenderPhase(svpos, uv, viewZ, color, type, 2U);
}
