struct Particle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
    float type; float3 pad;
};
StructuredBuffer<Particle> Particles : register(t2);

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
    float type : TEXCOORD1;
};

VSOut main(VSIn v, uint instanceID : SV_InstanceID) {
    VSOut o;
    Particle p = Particles[instanceID];
    
    if (p.color.a < 0.01f || p.position.y < -500.0f) {
        o.svpos = float4(0, 0, 0, 0);
        o.uv = float2(0, 0);
        o.type = 0.0f;
        return o;
    }
    
    float2 quad[6] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f,  1.0f)
    };
    float2 localXY = quad[v.vertexID];
    
    float size = 0.7f;
    if (p.type >= 0.5f) {
        size = 0.4f;
    }
    // 影の隙間を埋めるため少し大きめに設定
    size *= 1.4f;
    
    // gViewの右・上ベクトルを用いてカメラ向きビルボードを作成
    float3 right = float3(gView[0][0], gView[1][0], gView[2][0]);
    float3 up = float3(gView[0][1], gView[1][1], gView[2][1]);
    
    // シャドウ用なのでY軸回転だけでも十分だが、パーティクルの丸みを出すためにカメラ向きを採用
    float3 localPos = right * localXY.x * size + up * localXY.y * size;
    float3 worldPos = p.position + localPos;
    
    // シャドウ投影行列(gViewProjにはライトのVPがセットされている)
    o.svpos = mul(float4(worldPos, 1.0f), gViewProj);
    
    o.uv = localXY * 0.5f + 0.5f;
    o.type = p.type;
    return o;
}
