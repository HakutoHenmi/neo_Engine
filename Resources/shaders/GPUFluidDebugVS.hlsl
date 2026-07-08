struct Particle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
};

StructuredBuffer<Particle> Particles : register(t2);

cbuffer CBFrame : register(b0) { 
    row_major float4x4 gView; 
    row_major float4x4 gProj; 
    row_major float4x4 gViewProj; 
    float3 gCamPos; 
    float gTime; 
};

struct VSOut {
    float4 svpos : SV_POSITION;
    float4 color : COLOR0;
};

VSOut main(uint vId : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOut o;
    Particle p = Particles[instanceID];
    
    float3 pos = p.position;
    float speed = length(p.velocity);
    
    float3 dir = float3(0,1,0);
    if (speed > 0.01f) dir = p.velocity / speed;
    
    float scale = 0.02f;
    float lineLen = speed * scale;
    // 長すぎると見づらいのでクランプ
    lineLen = min(lineLen, 2.0f);
    
    float3 tip = pos + dir * lineLen;
    
    // カメラの右ベクトル（矢印の羽をカメラに向けるため）
    float3 camDir = normalize(pos - gCamPos);
    float3 right = normalize(cross(dir, camDir));
    if (length(right) < 0.01f) right = float3(1,0,0);
    
    float headLen = 0.08f;
    float headWidth = 0.04f;
    
    float3 currentPos = pos;
    float4 c = float4(0,1,1,1);
    
    // 先端に行くほど色が赤くなる
    float r = min(speed / 20.0f, 1.0f);
    float g = min(speed / 10.0f, 1.0f) * (1.0f - r);
    float b = 1.0f - r - g;
    float4 tipColor = float4(r, max(g,0.0f), max(b,0.0f), 1.0f);
    
    if (vId == 0) { currentPos = pos; c = float4(0,1,1,1); }
    else if (vId == 1) { currentPos = tip; c = tipColor; }
    else if (vId == 2) { currentPos = tip; c = tipColor; }
    else if (vId == 3) { currentPos = tip - dir * headLen + right * headWidth; c = tipColor; }
    else if (vId == 4) { currentPos = tip; c = tipColor; }
    else if (vId == 5) { currentPos = tip - dir * headLen - right * headWidth; c = tipColor; }
    
    o.svpos = mul(float4(currentPos, 1.0f), gViewProj);
    o.color = c;
    
    return o;
}
