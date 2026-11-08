// GimmickCurtainPS.hlsl
// 部屋のギミックの作動範囲を示す、縁から垂直に立つ薄い光の膜 (半透明。明るい部屋の床でも見えるように加算にしない)。
// uv.y は膜の高さ (0 が床・1 が上の端)。下ほど濃く、上へ消えていき、明るい帯がゆっくり昇る。
// 視線に沿って見える所ほど濃くして、薄い膜の手前と奥の縁が分かるようにする。color.a で全体の濃さを決める
cbuffer CBFrame : register(b0) {
    row_major float4x4 view; row_major float4x4 proj; row_major float4x4 viewProj;
    float3 cameraPos; float time;
};
struct PSIn {
    float4 pos : SV_POSITION; float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0;
};
float4 main(PSIn i) : SV_TARGET {
    float h = saturate(i.uv.y);
    float fade = pow(1 - h, 1.15);
    float edge = smoothstep(.09, 0, h);
    float band = smoothstep(.1, 0, abs(frac(h * 2.2 - time * .45) - .5) - .38);
    float3 direction = normalize(cameraPos - i.worldPos);
    float rim = 1 - abs(dot(normalize(i.normal), direction));
    float strength = fade * (.55 + .45 * rim + .35 * band) + edge;
    // カメラのすぐ前の膜は薄くして、体や物を隠さない
    strength *= smoothstep(3, 10, distance(cameraPos, i.worldPos));
    // 下の縁と昇る帯は白く光らせる
    float3 color = lerp(i.color.rgb, float3(1, 1, .85), saturate(edge * .7 + band * fade * .35));
    return float4(color * (1 + .08 * sin(time * 3)), saturate(strength * i.color.a));
}
