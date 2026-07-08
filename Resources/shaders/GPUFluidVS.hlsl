struct Particle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
};
StructuredBuffer<Particle> Particles : register(t2); // In DrawGPUFluid it is mapped to t6 -> which is descriptor table? Wait, no, it's bound as SRV to root parameter 6. So register(t2) might be wrong if 6 is t0. Let's check rootSig3D_.

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
};

VSOut main(VSIn v, uint instanceID : SV_InstanceID) {
    VSOut o;
    Particle p = Particles[instanceID];
    
    // 6頂点で1つのQuad(ビルボード)を生成する
    float2 quad[6] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f,  1.0f)
    };
    float2 localXY = quad[v.vertexID];
    
    // パーティクルの大きさを設定（少し大きめにすることで隙間を埋める）
    float size = 0.7f; 
    
    float3 right = float3(gView[0][0], gView[1][0], gView[2][0]);
    float3 up = float3(gView[0][1], gView[1][1], gView[2][1]);
    
    // Quadのローカル座標をカメラの向きに合わせて回転
    float3 localPos = right * localXY.x * size + up * localXY.y * size;
    float3 worldPos = p.position + localPos;
    
    o.svpos = mul(float4(worldPos, 1.0f), gViewProj);
    o.viewZ = mul(float4(worldPos, 1.0f), gView).z;
    
    // UV座標の生成
    o.uv = localXY * 0.5f + 0.5f;
    o.uv.y = 1.0f - o.uv.y; // DirectX仕様に合わせてYを反転
    o.color = p.color;
    
    return o;
}
