Texture2D<float4> tex : register(t0);
Texture2D<float> depthTex : register(t1);
SamplerState smp : register(s0);

struct PSIn {
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 color = tex.Sample(smp, input.uv);
    
    // 完全に透明なら破棄
    if (color.a < 0.01) {
        discard;
    }

    // アルファ値の閾値でカットアウト
    float threshold = 0.5;
    
    if (color.a < threshold) {
        discard; // 閾値以下は捨てる
    }

    // 滑らかなエッジを作る（エイリアシング対策）
    float alphaEdge = smoothstep(threshold, threshold + 0.05, color.a);
    float3 baseColor = color.rgb; 
    
    // --- 立体感を出すための深度ベース法線計算 ---
    
    // オフセットサイズ（ピクセル幅相当）
    float2 offX = float2(0.002, 0.0);
    float2 offY = float2(0.0, 0.002);
    
    // 周辺の深度を広くサンプリングして平滑化する（ノイズによる白飛びを防ぐ）
    float d1 = depthTex.Sample(smp, input.uv + offX * 2.0).r;
    float d2 = depthTex.Sample(smp, input.uv - offX * 2.0).r;
    float d3 = depthTex.Sample(smp, input.uv + offY * 2.0).r;
    float d4 = depthTex.Sample(smp, input.uv - offY * 2.0).r;
    float d0 = depthTex.Sample(smp, input.uv).r;
    
    // 背景(10000.0) をサンプリングした場合は、球の縁として奥へ緩やかに曲がっているとみなす
    float edgeDepth = 0.5;
    if (d1 > 9999.0) d1 = d0 + edgeDepth;
    if (d2 > 9999.0) d2 = d0 + edgeDepth;
    if (d3 > 9999.0) d3 = d0 + edgeDepth;
    if (d4 > 9999.0) d4 = d0 + edgeDepth;

    // 深度の勾配（平滑化）
    float dzdx = (d1 - d2) * 0.5;
    float dzdy = (d3 - d4) * 0.5;
    
    // 法線の計算
    // 強度を適切なレベルに戻し、ノイズで全体が真っ白になるのを防ぐ
    float normalStrength = 20.0; 
    float3 normal = normalize(float3(-dzdx * normalStrength, dzdy * normalStrength, -1.0));
    
    // 光源と視線ベクトル（ハイライトが中央付近に出るように真正面寄りに調整）
    float3 lightDir = normalize(float3(-0.3, 0.5, -1.0));
    float3 viewDir = float3(0, 0, -1);
    
    // フレネル（リムライト）
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.0);
    
    // スペキュラ（ハイライト） - 適度な広さと強さに戻す
    float3 halfVector = normalize(lightDir + viewDir);
    float specular = pow(max(dot(normal, halfVector), 0.0), 48.0);
    
    // --- 色の計算（参考画像のような透明感のある緑色の液体にする） ---
    
    baseColor = float3(0.05, 0.85, 0.3);
    
    // 陰影
    float nDotL = max(dot(normal, lightDir), 0.0);
    float3 diffuseColor = lerp(float3(0.0, 0.4, 0.2), baseColor, nDotL * 0.5 + 0.5);
    
    // 透明度
    float transparency = 0.75;
    
    // ハイライト（強いツヤ） - 5.0倍は強すぎたので2.0倍に抑える
    float3 specColor = float3(0.9, 1.0, 0.95) * specular * 2.0;
    
    // リムライト（エッジの強い発光）
    float3 rimColor = fresnel * float3(0.4, 1.0, 0.6) * 1.5;
    
    // 空の反射（上を向いている面を少し明るく）
    float skyReflection = max(normal.y, 0.0) * 0.35;
    rimColor += float3(0.2, 0.6, 0.4) * skyReflection;
    
    // 合成
    float3 finalColor = diffuseColor * transparency + specColor + rimColor;
    float finalAlpha = alphaEdge * 0.85; // 全体の不透明度
    
    return float4(finalColor, finalAlpha);
}
