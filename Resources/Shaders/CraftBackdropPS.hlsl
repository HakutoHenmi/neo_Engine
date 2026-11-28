// 変形武器を作る画面の見本の窓の背景: スライムのまわりを囲む箱の内側。光も影も受けない無地 (床は少し暗く、壁は上ほど明るい)
cbuffer CBFrame : register(b0) {
    row_major float4x4 view; row_major float4x4 proj; row_major float4x4 viewProj;
    float3 cameraPos; float time;
};
float4 main(float4 svpos : SV_POSITION, float3 worldPos : TEXCOORD0, float3 normal : TEXCOORD1, float2 uv : TEXCOORD2,
            float4 color : COLOR0) : SV_TARGET {
    float3 n = normalize(normal);
    // 床 (上を向いた面) は少し暗く、中心から外へ少しずつ暗くする。壁は下から上へ明るく
    float floorShade = .78 - .12 * saturate(length(uv - .5) * 1.6);
    float wallShade = lerp(.86, 1.06, uv.y);
    float shade = abs(n.y) > .5 ? (n.y > 0 ? floorShade : 1.06) : wallShade;
    return float4(color.rgb * shade, 1);
}
