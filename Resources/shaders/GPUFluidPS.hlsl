Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

struct PSOut {
    float4 color : SV_TARGET0;
    float depth : SV_TARGET1;
};

PSOut main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0, float viewZ:TEXCOORD1, float4 color:COLOR) {
    // 距離を計算 (UV: 0.0 ~ 1.0) -> 中心(0.5, 0.5) からの距離
    float2 centerOffset = uv - float2(0.5f, 0.5f);
    float distSq = dot(centerOffset, centerOffset);
    
    // 半径0.5 (二乗で0.25) の外側は破棄
    if (distSq > 0.25f) { discard; }
    
    // 中心はアルファ1.0、端は0.0になるようなソフトな減衰
    float dist = sqrt(distSq);
    float alpha = saturate(1.0f - (dist / 0.5f));
    alpha = pow(alpha, 1.5f); // ふんわりとしたグラデーション
    
    // メタボール合成用に、アルファ（密度）を少し高めに蓄積する
    float4 outColor = color;
    outColor.a = alpha * 0.15f; 
    
    // MetaballPSでの合成に向けたベースカラー（エメラルドグリーン）
    outColor.r = 0.2f;
    outColor.g = 1.0f;
    outColor.b = 0.5f;
    
    // 加算ブレンド(ONE)で正しく色を乗せるための事前乗算アルファ (Premultiplied Alpha)
    outColor.rgb *= outColor.a;
    
    if (outColor.a <= 0.0f) { discard; }
    
    PSOut o;
    o.color = outColor;
    
    // スライムの立体感を出すため、球体としての丸み（深度のオフセット）を計算する
    // サイズはVSと合わせて0.7fとする。
    float sphereRadius = 0.7f;
    float normalizedDistSq = distSq * 4.0f; // 0.0 ~ 1.0
    float z_offset = sqrt(max(0.0f, 1.0f - normalizedDistSq)) * sphereRadius;
    
    // 丸みを持たせた深度を出力する（これがMetaballPSで法線計算に使われる）
    // 丸みによる重なりのノイズはMetaballPS側の平滑化処理で吸収する
    o.depth = viewZ - z_offset; 
    
    return o;
}
