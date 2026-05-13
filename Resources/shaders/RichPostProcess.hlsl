// Resources/shaders/RichPostProcess.hlsl
// 改良版リッチ・ポストプロセス（マスターシェーダー）
// ロックオン（アウトライン）とダッシュ（ブラー）の強度を強化

Texture2D gScene : register(t0); 
SamplerState gSmp : register(s0);

cbuffer CBPost : register(b0) { 
    float gTime; 
    float gNoiseStrength; 
    float gDistortion;  // Radial Blur強度
    float gChromaShift; // Outline強度
    float gVignette;    // ビネット強度
    float gScanline;    // グレイスケール・フェード用 (0..1)
    float2 pad; 
};

// 輝度計算
float luminance(float3 rgb) {
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

// 簡単なノイズ
float hash(float2 p) { return frac(sin(dot(p, float2(12.9898,78.233))) * 43758.5453); }

float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
    float2 centerOffset = uv - 0.5;
    float2 texelSize = float2(1.0 / 1920.0, 1.0 / 1080.0);
    
    // -----------------------------------------------------------------
    // 1. Radial Blur (強化)
    // -----------------------------------------------------------------
    float3 baseColor = 0;
    if (gDistortion > 0.001) {
        // 強度を0.04から0.08に倍増
        float blurStrength = gDistortion * 0.08;
        static const int BLUR_SAMPLES = 12; // サンプル数を増やして滑らかに
        float totalWeight = 0;
        for (int s = 0; s < BLUR_SAMPLES; ++s) {
            float t = (float)s / (float)(BLUR_SAMPLES - 1);
            float weight = 1.0 - t * 0.5;
            baseColor += gScene.Sample(gSmp, saturate(uv - centerOffset * t * blurStrength)).rgb * weight;
            totalWeight += weight;
        }
        baseColor /= totalWeight;
    } else {
        baseColor = gScene.Sample(gSmp, uv).rgb;
    }

    // -----------------------------------------------------------------
    // 2. Outline (強化)
    // -----------------------------------------------------------------
    if (gChromaShift > 0.001) {
        float luMC = luminance(baseColor);
        // 隣接ピクセルの距離を広げて太い線にする
        float2 offset = texelSize * 1.5; 
        float luTL = luminance(gScene.Sample(gSmp, uv + float2(-1, -1) * offset).rgb);
        float luTR = luminance(gScene.Sample(gSmp, uv + float2( 1, -1) * offset).rgb);
        float luBL = luminance(gScene.Sample(gSmp, uv + float2(-1,  1) * offset).rgb);
        float luBR = luminance(gScene.Sample(gSmp, uv + float2( 1,  1) * offset).rgb);
        
        float edge = saturate(abs(4.0 * luMC - luTL - luTR - luBL - luBR));
        // 強度倍率を3.0から10.0に大幅強化
        baseColor *= (1.0 - edge * gChromaShift * 10.0);
    }

    // -----------------------------------------------------------------
    // 3. Optimized Bloom
    // -----------------------------------------------------------------
    float3 bloomColor = 0;
    float BLOOM_THRESH = 0.92;
    float BLOOM_INTENSITY = 0.3;
    float weightSum = 0;
    for(int x = -1; x <= 1; x++){
        for(int y = -1; y <= 1; y++){
            float3 c = gScene.Sample(gSmp, uv + float2(x,y) * texelSize * 6.0).rgb;
            float lum = luminance(c);
            if(lum > BLOOM_THRESH) {
                bloomColor += c * (lum - BLOOM_THRESH);
            }
            weightSum += 1.0;
        }
    }
    baseColor += (bloomColor / weightSum) * BLOOM_INTENSITY;

    // -----------------------------------------------------------------
    // 4. Grayscale Fade
    // -----------------------------------------------------------------
    float grey = luminance(baseColor);
    baseColor = lerp(baseColor, grey.xxx, gScanline);

    // -----------------------------------------------------------------
    // 5. Vignette ＆ Noise
    // -----------------------------------------------------------------
    float distSq = dot(centerOffset, centerOffset);
    float vig = saturate(1.0 - distSq * gVignette * 3.0);
    baseColor *= vig;
    
    baseColor += (hash(uv * 1000.0 + gTime) - 0.5) * gNoiseStrength;

    return float4(baseColor, 1.0);
}
