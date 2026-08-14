#include "Obj.hlsli"

VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD)
{
    VSOutput output;

    // UVのV値(y)を用いて長手方向の位置を取得
    float v = uv.y;
    
    // 1. 根本と先端を細くし、中心を太くするベース形状 (sin(v * pi))
    float shape = sin(v * 3.14159265f);
    
    // 2. エネルギーの脈動（時間と位置で波打たせる）
    float pulse = sin(v * 15.0f - time * 25.0f);
    
    // 3. ランダムな揺らぎ（荒れ）
    float noise = sin(pos.y * 20.0f + time * 30.0f) * cos(pos.x * 15.0f - time * 20.0f);
    
    // 法線方向への変位量を計算
    // shapeによって両端は変位を小さくし、中央ほど荒々しく脈動する
    float displacement = (shape * 0.4f) + (pulse * shape * 0.2f) + (noise * shape * 0.15f);
    
    // 頂点座標を法線方向に膨らませる/凹ませる
    float4 newPos = pos;
    // モデルのスケールに依存しないよう、ローカル座標系で少しだけ膨らます
    newPos.xyz += normal * displacement * 0.5f;

    float4 worldPos = mul(newPos, world);
    float3 worldN = normalize(mul(float4(normal, 0), world)).xyz;

    output.svpos = mul(newPos, mul(world, mul(view, projection)));
    output.worldpos = worldPos;
    output.normal = worldN;
    output.uv = uv;

    return output;
}
