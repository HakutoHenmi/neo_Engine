Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD, float4 color:COLOR) : SV_TARGET {
    float4 texColor = tex.Sample(smp, uv);
    
    // UV座標から中心(0.5, 0.5)からの距離を計算
    float dist = length(uv - float2(0.5, 0.5)) * 2.0;
    
    // 距離に応じたソフトなグラデーション (端で0)
    float radialAlpha = saturate(1.0 - dist);
    // スムースステップで滑らかに
    radialAlpha = radialAlpha * radialAlpha * (3.0 - 2.0 * radialAlpha);
    
    texColor *= color;
    texColor.a *= radialAlpha;
    
    if (texColor.a <= 0.01f) { discard; }
    
    
    return texColor;
}
