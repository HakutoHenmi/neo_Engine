struct Particle {
    float3 position;
    float life;
    float3 velocity;
    float age;
    float4 colorStart;
    float4 colorEnd;
    float3 scale;
    uint particleType;
};

cbuffer cbSystem : register(b0) {
    row_major float4x4 viewProj;
    float3 camPos;
    uint useBillboard;
};

StructuredBuffer<Particle> g_ParticlePool : register(t0);

struct VSOut {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 worldPos : WORLDPOS;
    float3 normal : NORMAL;
    uint particleType : TYPE;
};

VSOut main(uint vId : SV_VertexID, uint instId : SV_InstanceID) {
    VSOut o;
    
    Particle p = g_ParticlePool[instId];
    
    // Quad vertices (-0.5 to 0.5)
    float2 uvs[6] = { float2(0,0), float2(1,0), float2(0,1), float2(0,1), float2(1,0), float2(1,1) };
    float2 offsets[6] = { float2(-0.5,0.5), float2(0.5,0.5), float2(-0.5,-0.5), float2(-0.5,-0.5), float2(0.5,0.5), float2(0.5,-0.5) };
    
    float2 offset = offsets[vId] * p.scale.xy;
    
    float3 worldPos = p.position;
    float3 normal = float3(0, 0, -1);
    
    if (useBillboard == 1) {
        float3 toCam = normalize(camPos - p.position);
        float3 up = float3(0, 1, 0);
        float3 right = float3(1, 0, 0);
        
        if (p.particleType == 1 && dot(p.velocity, p.velocity) > 0.001f) {
            // Trail: Stretch along velocity
            float3 velNorm = normalize(p.velocity);
            up = velNorm;
            right = normalize(cross(up, toCam));
            
            // Stretch the offset Y based on speed
            float speed = length(p.velocity);
            offset.y *= (1.0f + speed * 0.1f); // Stretch multiplier
        } else {
            // Normal Billboard
            right = normalize(cross(up, toCam));
            up = cross(toCam, right);
        }
        
        worldPos += right * offset.x + up * offset.y;
        normal = toCam;
    } else {
        // Flat (X-Y plane)
        worldPos.x += offset.x;
        worldPos.y += offset.y;
    }
    
    o.pos = mul(float4(worldPos, 1.0), viewProj);
    
    float t = saturate(p.age / p.life);
    o.color = lerp(p.colorStart, p.colorEnd, t);
    
    o.uv = uvs[vId];
    o.worldPos = worldPos;
    o.normal = normal;
    o.particleType = p.particleType;
    
    return o;
}
