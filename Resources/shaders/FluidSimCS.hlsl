struct Particle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
};

RWStructuredBuffer<Particle> Particles : register(u0);

cbuffer CBCompute : register(b0) {
    float dt;
    uint emitCursor;
    uint emitCount;
    uint maxParticles;
    float3 emitPos; float pad1;
    float3 emitDir; float pad2;
    float4 emitColor;
    float3 corePos; float coreAttraction;
}

// 疑似乱数ジェネレーター
float hash(uint n) {
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    return float(n & uint(0x7fffffffU)) / float(0x7fffffff);
}

// ==========================================
// Pass 0: Emit (パーティクル放出)
// ==========================================
[numthreads(256, 1, 1)]
void Emit(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    
    if (emitCount > 0) {
        bool shouldEmit = false;
        if (emitCursor + emitCount <= maxParticles) {
            shouldEmit = (i >= emitCursor && i < emitCursor + emitCount);
        } else {
            uint overflow = (emitCursor + emitCount) % maxParticles;
            shouldEmit = (i >= emitCursor) || (i < overflow);
        }
        
        if (shouldEmit) {
            Particle p = Particles[i];
            
            // 乱数で散らす (-1.0 ～ 1.0)
            float rx = (hash(i * 123) - 0.5f) * 2.0f;
            float ry = (hash(i * 456) - 0.5f) * 2.0f;
            float rz = (hash(i * 789) - 0.5f) * 2.0f;
            
            // ★位置をばらけさせる (超重要: 同一座標に重なると圧力が爆発する)
            p.position = emitPos + float3(rx, ry, rz) * 1.5f;
            
            // 速度もランダムに
            p.velocity = emitDir * 5.0f + float3(rx, abs(ry) + 0.2f, rz) * 5.0f;
            p.color = emitColor;
            
            // 物理初期値
            p.density = 1.0f;
            p.pressure = 0.0f;
            Particles[i] = p;
        }
    }
}

// ==========================================
// SPH Parameters (安定化のためにマイルドに調整)
// ==========================================
static const float SMOOTHING_RADIUS = 1.0f;
static const float PARTICLE_MASS = 1.0f;
static const float REST_DENSITY = 3.0f;     // 基準密度を下げる
static const float GAS_CONSTANT = 50.0f;    // 反発力を弱める
static const float VISCOSITY = 15.0f;       // 粘性
static const float GRAVITY = -20.0f;
static const float PI = 3.1415926535f;

// ==========================================
// Pass 1: CalcDensity (密度と圧力の計算)
// ==========================================
[numthreads(256, 1, 1)]
void CalcDensity(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    
    // ★追加: 未放出・未初期化のパーティクルは地底に飛ばして計算から完全除外
    if (Particles[i].color.a < 0.01f || Particles[i].position.y < -500.0f) {
        Particles[i].position = float3(0, -1000.0f, 0);
        Particles[i].density = 0.01f;
        Particles[i].pressure = 0.0f;
        return;
    }
    
    float h = SMOOTHING_RADIUS;
    float h2 = h * h;
    float3 pos_i = Particles[i].position;
    
    float density = 0.0f;
    float poly6 = 315.0f / (64.0f * PI * pow(h, 9));
    
    // O(N^2) 探索
    for (uint j = 0; j < maxParticles; ++j) {
        if (Particles[j].position.y < -500.0f) continue;
        
        float3 diff = pos_i - Particles[j].position;
        float r2 = dot(diff, diff);
        if (r2 < h2) {
            float w = h2 - r2;
            density += PARTICLE_MASS * poly6 * w * w * w;
        }
    }
    
    // 自身の最低密度を保証 (0割り防止)
    if (density < 0.01f) density = 0.01f;
    
    Particles[i].density = density;
    
    // 圧力計算 (マイナスにならないようにmaxを取る)
    Particles[i].pressure = max(GAS_CONSTANT * (density - REST_DENSITY), 0.0f);
}

// ==========================================
// Pass 2: CalcForce (力学計算と座標更新)
// ==========================================
[numthreads(256, 1, 1)]
void CalcForce(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    if (dt <= 0.0f) return;
    
    Particle pi = Particles[i];
    if (pi.position.y < -500.0f) return;
    
    float h = SMOOTHING_RADIUS;
    
    float3 forcePressure = float3(0,0,0);
    float3 forceViscosity = float3(0,0,0);
    
    // ★修正: -45.0f ではなく 45.0f にする (引力ではなく反発力にするため)
    float spiky = 45.0f / (PI * pow(h, 6));
    float visc = 45.0f / (PI * pow(h, 6));
    
    for (uint j = 0; j < maxParticles; ++j) {
        if (i == j) continue;
        Particle pj = Particles[j];
        if (pj.position.y < -500.0f) continue;
        
        float3 diff = pi.position - pj.position;
        float r = length(diff);
        
        // ★修正: 完全に同じ位置にある場合はランダムにずらして反発させる (特異点回避)
        if (r < 0.001f) {
            diff = float3(hash(i) - 0.5f, hash(j) - 0.5f, hash(i^j) - 0.5f) * 0.01f;
            r = length(diff);
            if (r < 0.0001f) { diff = float3(0, 0.01f, 0); r = 0.01f; }
        }
        
        if (r < h) {
            float3 dir = diff / r;
            float w = h - r;
            
            // 圧力項
            float pressureTerm = (pi.pressure + pj.pressure) / (2.0f * pj.density);
            forcePressure += dir * PARTICLE_MASS * pressureTerm * spiky * w * w;
            
            // 粘性項
            float3 velDiff = pj.velocity - pi.velocity;
            forceViscosity += velDiff * PARTICLE_MASS * (1.0f / pj.density) * VISCOSITY * visc * w;
        }
    }
    
    // 加速度
    float3 force = forcePressure + forceViscosity;
    
    // ★追加: プレイヤーのコアへ引っ張る力 (スライムの形を保つ)
    if (coreAttraction > 0.0f) {
        float3 toCore = corePos - pi.position;
        float distToCore = length(toCore);
        
        if (distToCore > 0.001f) {
            // 1. 全体を丸く保つための一定の引力（表面張力）＋距離によるバネ力
            // ダンピングが効いているため、中心でも一定の引力をかけて中身を密にする
            float pull = coreAttraction * PARTICLE_MASS * 1.2f;
            pull += coreAttraction * PARTICLE_MASS * distToCore * 1.5f;
            float3 springForce = (toCore / distToCore) * pull;
            
            // 2. 床に潰れるのを防ぐ上向きの持ち上げ（重力相殺）
            if (pi.position.y < corePos.y) {
                springForce.y += 20.0f * PARTICLE_MASS;
            }
            
            // 3. 球形(雫)の形を保つため、一定の「3D距離(半径)」を超えたパーティクルを強力に中心へ押し戻す
            // これにより、外側に漏れ出してパンケーキ状に張り付くのを完全に防ぐ
            float maxRadius = 1.2f;
            if (distToCore > maxRadius) {
                float excess = distToCore - maxRadius;
                // 半径を超えた分だけ、極めて強力に内側へ押し返す
                springForce += (toCore / distToCore) * (excess * coreAttraction * PARTICLE_MASS * 15.0f);
            }
            
            force += springForce;
        }
        
        // ★ダンピング（超重要：振動を抑えて静止させる）
        // コアに引かれている間は速度を強制的に減衰させることで、プルプル荒ぶるのをピタッと止める
        force -= pi.velocity * (coreAttraction * 0.15f);
    }
    
    float3 acceleration = force / pi.density;
    
    // ★発散防止のため加速度をクランプ
    float accLen = length(acceleration);
    if (accLen > 500.0f) {
        acceleration = (acceleration / accLen) * 500.0f;
    }
    
    // 外力 (重力)
    acceleration.y += GRAVITY;
    
    // 積分 (Symplectic Euler)
    pi.velocity += acceleration * dt;
    
    // ★発散防止のため速度をクランプ
    float speed = length(pi.velocity);
    if (speed > 30.0f) {
        pi.velocity = (pi.velocity / speed) * 30.0f;
    }
    
    pi.position += pi.velocity * dt;
    
    // コリジョン (簡易的な床バウンド)
    if (pi.position.y < 0.2f) {
        pi.position.y = 0.2f;
        pi.velocity.y *= -0.3f;
        pi.velocity.x *= 0.6f; // 床の摩擦
        pi.velocity.z *= 0.6f;
    }
    
    // 空間制限は削除（プレイヤーへの引力で十分なため）
    Particles[i] = pi;
}
