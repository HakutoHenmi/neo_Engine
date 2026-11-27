#include "FluidRenderSupport.hlsli"
struct Particle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
    float type; float3 pad;
};
StructuredBuffer<Particle> Particles : register(t2); // 粒子のバッファ (DrawGPUFluid でルートの SRV として結び付ける)

cbuffer CBFrame : register(b0) { 
    row_major float4x4 gView; 
    row_major float4x4 gProj; 
    row_major float4x4 gViewProj; 
    float3 gCamPos; 
    float gTime; 
};

struct VSIn {
    float4 pos : POSITION; 
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    uint vertexID : SV_VertexID;
};

struct VSOut {
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float viewZ : TEXCOORD1;
    float4 color : COLOR0;
    float type : TEXCOORD2;
};

VSOut RenderPhase(VSIn v, uint instanceID, uint targetPhase) {
    VSOut output = (VSOut)0;
    Particle p = Particles[instanceID];
    
    // ★追加: 非アクティブなパーティクルは頂点を縮退させて描画とラスタライズを完全スキップ
    uint phase = (p.type < 0.5f || (p.type > 2.5f && p.type < 3.5f)) ? 0U :
                 ((p.type > 1.5f && p.type < 2.5f) ? 1U : 2U);
    float support = FluidRenderSupport(p.density);
    bool rejected = support <= 0 || p.color.a < 0.01f || p.position.y < -500.0f ||
                    (targetPhase < 3U && phase != targetPhase);
    
    // 6頂点で1つのQuad(ビルボード)を生成する
    static const float2 quad[6] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f,  1.0f)
    };
    float2 localXY = quad[v.vertexID];
    
    // パーティクルの大きさを設定
    // 画面空間の流体には、物理的な粒子の間隔と重なるだけの再構成の広がりが要る。
    // 当たり判定の半径に近い広がりだと、粒子が 1 つずつ
    // 別々のガラスの球に見えてしまう。
    float size = 0.90f;
    if (p.type > 2.5f && p.type < 3.5f) {
        size = 0.65f;
    } else if (p.type >= 0.5f) {
        size = 0.72f;
    }
    
    float3 right = float3(gView[0][0], gView[1][0], gView[2][0]);
    float3 up = float3(gView[0][1], gView[1][1], gView[2][1]);
    
    // Quadのローカル座標をカメラの向きに合わせて回転
    float3 localPos = right * localXY.x * size + up * localXY.y * size;
    float3 worldPos = p.position + localPos;
    
    output.svpos = mul(float4(worldPos, 1.0f), gViewProj);
    output.viewZ = mul(float4(worldPos, 1.0f), gView).z;
    
    // UV座標の生成
    output.uv = localXY * 0.5f + 0.5f;
    output.uv.y = 1.0f - output.uv.y; // DirectX仕様に合わせてYを反転
    output.color = p.color;
    output.color.a *= support;
    output.type = p.type;
    // ラスタライズの前に捨てる: 他の相はピクセルシェーディングの負荷を生まない。
    if (rejected) output.svpos = float4(0, 0, -1, 1);
    
    return output;
}

VSOut main(VSIn v, uint instanceID : SV_InstanceID) { return RenderPhase(v, instanceID, 3U); }
VSOut mainPhase0(VSIn v, uint instanceID : SV_InstanceID) { return RenderPhase(v, instanceID, 0U); }
VSOut mainPhase1(VSIn v, uint instanceID : SV_InstanceID) { return RenderPhase(v, instanceID, 1U); }
VSOut mainPhase2(VSIn v, uint instanceID : SV_InstanceID) { return RenderPhase(v, instanceID, 2U); }
