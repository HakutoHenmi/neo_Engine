// GPUParticle Compute Shader
// ルートディスクリプタ方式: u0 のみ使用

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
    float dt;
    float totalTime;
    uint maxParticles;
    uint emitCountThisFrame;
    
    float3 emitPos;
    float emitRate;
    
    float3 emitVel;
    float emitLife;

    uint emitterShape; // 0: Point, 1: Sphere, 2: Box, 3: Mesh
    float3 emitterExtents; // x is used for meshVertexCount if shape is 3
    
    float4 colorStart;
    float4 colorEnd;
    
    uint param24; // fieldType (Update) / particleType (Emit)
    float3 param25_27; // fieldPos (Update) / randomOffset (Emit)
    float4 fieldParams; // fieldParams (Update)
};

#define CB_FieldType param24
#define CB_ParticleType param24
#define CB_FieldPos param25_27

RWStructuredBuffer<Particle> g_ParticlePool : register(u0);

// 簡易ハッシュ関数 (疑似乱数生成用)
float hash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) / 4294967295.0;
}

// 03_01: 初期化 - 全パーティクルを死亡状態にする
[numthreads(256, 1, 1)]
void InitCS(uint3 dtid : SV_DispatchThreadID) {
    if(dtid.x >= maxParticles) return;
    g_ParticlePool[dtid.x].life = -1.0;
    g_ParticlePool[dtid.x].age = 0.0;
    g_ParticlePool[dtid.x].position = float3(0, 0, 0);
    g_ParticlePool[dtid.x].velocity = float3(0, 0, 0);
    g_ParticlePool[dtid.x].colorStart = float4(0, 0, 0, 0);
    g_ParticlePool[dtid.x].colorEnd = float4(0, 0, 0, 0);
    g_ParticlePool[dtid.x].scale = float3(0.3, 0.3, 0.3);
    g_ParticlePool[dtid.x].particleType = 0;
}

// 03_02 & 03_04: パーティクルの発生と使い回し
[numthreads(64, 1, 1)]
void EmitCS(uint3 dtid : SV_DispatchThreadID) {
    if(dtid.x >= emitCountThisFrame) return;
    
    float rOff = param25_27.x;
    uint seed = dtid.x + asuint(totalTime * 1000.0 + rOff);
    uint startIdx = uint(hash(seed) * float(maxParticles));
    
    for(uint i = 0; i < maxParticles; ++i) {
        uint idx = (startIdx + i) % maxParticles;
        
        // life < 0 なら死亡中 → 再利用
        if(g_ParticlePool[idx].life < 0.0) {
            Particle p;
            float r1 = hash(seed + i * 3u + 1u) * 2.0 - 1.0;
            float r2 = hash(seed + i * 7u + 2u) * 2.0 - 1.0;
            float r3 = hash(seed + i * 13u + 3u) * 2.0 - 1.0;
            
            float3 offset = float3(0, 0, 0);
            if (emitterShape == 1) { // Sphere
                float3 dir = normalize(float3(r1, r2, r3));
                offset = dir * hash(seed + i * 23u) * emitterExtents.x;
            } else if (emitterShape == 2) { // Box
                offset = float3(r1, r2, r3) * emitterExtents;
            } else if (emitterShape == 3) { // Mesh fallback
                offset = float3(r1, r2, r3) * 0.1;
            } else { // Point
                offset = float3(r1, r2, r3) * 0.1;
            }
            
            p.position = emitPos + offset;
            p.velocity = emitVel + float3(r1 * 0.5, abs(r2) * 2.0, r3 * 0.5);
            p.life = emitLife + hash(seed + i * 17u) * 0.5;
            p.age = 0.0;
            p.colorStart = colorStart;
            p.colorEnd = colorEnd;
            p.scale = float3(0.3, 0.3, 0.3);
            p.particleType = CB_ParticleType;
            
            g_ParticlePool[idx] = p;
            return; // 1スレッドにつき1パーティクル
        }
    }
}

#define PARTICLES_PER_THREAD 4

// 03_03: パーティクルの更新
[numthreads(256, 1, 1)]
void UpdateCS(uint3 dtid : SV_DispatchThreadID) {
    for (int i = 0; i < PARTICLES_PER_THREAD; ++i) {
        uint pIdx = dtid.x * PARTICLES_PER_THREAD + i;
        if(pIdx >= maxParticles) continue;
        
        Particle p = g_ParticlePool[pIdx];
        if(p.life <= 0.0) continue; // 死亡中はスキップ
        
        // 更新
        p.age += dt;
        
        // フィールドの影響 (Gravity/Wind/Tornado)
        if (CB_FieldType == 1) { // Gravity
            p.velocity.y -= fieldParams.x * dt;
        } 
        else if (CB_FieldType == 2) { // Wind
            p.velocity += fieldParams.xyz * dt;
        }
        else if (CB_FieldType == 3) { // Tornado
            float3 toCenter = CB_FieldPos - p.position;
            toCenter.y = 0;
            float dist = length(toCenter);
            if (dist > 0.01) {
                float3 dir = toCenter / dist;
                float3 tangent = cross(float3(0, 1, 0), dir); // counter-clockwise
                p.velocity += tangent * fieldParams.x * dt;
                p.velocity += dir * fieldParams.y * dt; // suck inward
            }
            p.velocity.y += fieldParams.z * dt; // updraft
        }
        
        p.position += p.velocity * dt;
        
        // 寿命チェック
        float t = p.age / p.life;
        if(t >= 1.0) {
            p.life = -1.0; // 死亡
        } else {
            // スケールのフェード
            p.scale = lerp(float3(0.3, 0.3, 0.3), float3(0.0, 0.0, 0.0), t);
        }
        
        g_ParticlePool[pIdx] = p;
    }
}
