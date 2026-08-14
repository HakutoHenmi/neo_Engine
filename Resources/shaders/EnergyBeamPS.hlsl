#include "Obj.hlsli"

// 簡易ノイズ生成
float random(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453123);
}

float4 main(VSOutput input) : SV_TARGET
{
    float u = input.uv.x;
    float v = input.uv.y;
    
    // UVを使った流れるアニメーション（メインのビーム）
    float mainFlow = sin(v * 20.0f - time * 15.0f);
    mainFlow = pow(mainFlow * 0.5f + 0.5f, 3.0f);

    // らせん状のエネルギーエフェクト
    float spiralFlow1 = sin((u + v * 2.0f) * 15.0f - time * 20.0f);
    float spiralFlow2 = sin((u - v * 3.0f) * 10.0f - time * 25.0f);
    float spiral = max(pow(spiralFlow1 * 0.5f + 0.5f, 8.0f), pow(spiralFlow2 * 0.5f + 0.5f, 6.0f));

    // ノイズを使った微細なビリビリ感
    float noise = random(input.uv * float2(50.0f, 10.0f) + float2(time, time * 2.0f));
    float electricity = (noise > 0.95f) ? 1.0f : 0.0f;

    // 根本と先端をフェードさせる
    float base = saturate(1.0f - abs(v - 0.5f) * 2.0f);

    // 視線と法線の角度から、中心(core)と輪郭(edge)を計算
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldpos.xyz);
    float NdotV = saturate(dot(N, V));
    
    // ★重要: 円柱の輪郭（エッジ）を完全に透明にしてポリゴン感を消すマスク
    float edgeMask = smoothstep(0.1f, 0.4f, NdotV); 
    
    // 中心部ほど強く光る
    float core = pow(NdotV, 2.0f);

    // エネルギーの合成
    // らせんやメインフローを中心に構成
    float energy = (mainFlow * 0.8f) + (spiral * 1.5f) + (electricity * 0.8f);
    energy *= (0.8f + 0.2f * sin(time * 30.0f)); // 高速な明滅
    energy *= base; // 両端フェード

    // 色の計算
    // コア部分は白っぽく、外側は指定色に
    float3 beamColor = lerp(color.rgb, float3(1.0f, 1.0f, 1.0f), core * 0.9f);
    float3 finalRgb = beamColor * (energy + core) * 2.5f;

    // アルファの計算
    // energyとcoreを足し合わせ、最後にedgeMaskを掛けて強制的に輪郭を透明にする
    float alpha = saturate(energy + core * base) * edgeMask;

    return float4(finalRgb, alpha * color.a);
}
