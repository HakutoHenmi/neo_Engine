Texture2D gScene : register(t0); 
SamplerState gSmp : register(s0);

cbuffer CBPost : register(b0) { 
    float gTime; 
    float gNoiseStrength; 
    float gDistortion; 
    float gChromaShift; 
    float gVignette; 
    float gScanline; 
    float2 pad; 
};

// 定数
static const float BLOOM_THRESH = 1.0;
static const float BLOOM_INTENSITY = 0.6; // ブルーム強度

// 輝度計算
float luminance(float3 rgb) {
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

// 簡単なノイズ
float hash(float2 p) { return frac(sin(dot(p, float2(12.9898,78.233))) * 43758.5453); }

float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
    // -----------------------------------------------------------------
    // 1. Chromatic Aberration (色収差) - レンズの歪みのような色ズレ
    // -----------------------------------------------------------------
    // 画面端ほど色ズレが大きくなるように
    float2 centerOffset = uv - 0.5;
    float dist = length(centerOffset);
    float2 shift = centerOffset * dist * 0.03; // デフォルトの色収差強度

    float r = gScene.Sample(gSmp, saturate(uv + shift)).r;
    float g = gScene.Sample(gSmp, uv).g;
    float b = gScene.Sample(gSmp, saturate(uv - shift)).b;
    float3 baseColor = float3(r, g, b);

    // -----------------------------------------------------------------
    // 2. Simple Bloom (シングルパス・ボックスブルーム)
    // -----------------------------------------------------------------
    // 本来はダウンスケール＆アップスケールが必要ですが、シングルパスのガウスぼかしで疑似ブレンド
    float2 texelSize = float2(1.0 / 1280.0, 1.0 / 720.0); // 画面解像度の近似値
    float3 bloomColor = 0;
    int samples = 3;
    float weightSum = 0;
    
    // 軽量な数ピクセルのぼかし
    for(int x = -samples; x <= samples; x++){
        for(int y = -samples; y <= samples; y++){
            float2 offset = float2(x,y) * texelSize * 2.0;
            float3 c = gScene.Sample(gSmp, uv + offset).rgb;
            // 明度が閾値を超えている部分だけ足す
            if(luminance(c) > BLOOM_THRESH) {
                bloomColor += c;
            }
            weightSum += 1.0;
        }
    }
    bloomColor /= weightSum;
    
    // ブルーム加算 (Tonemappingを簡単にかける)
    baseColor += bloomColor * BLOOM_INTENSITY;

    // -----------------------------------------------------------------
    // 3. Vignette (周辺減光) ＆ Noise
    // -----------------------------------------------------------------
    // Vingette
    float vig = saturate(1.0 - dot(centerOffset, centerOffset) * 2.5);
    baseColor *= vig;
    
    // フィルムノイズ
    baseColor += (hash(uv * 1000.0 + gTime) - 0.5) * 0.03;

    return float4(baseColor, 1.0);
}
