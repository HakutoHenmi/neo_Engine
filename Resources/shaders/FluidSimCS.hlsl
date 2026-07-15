struct Particle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
    float type; float3 pad;
};

RWStructuredBuffer<Particle> Particles : register(u0);

cbuffer CBCompute : register(b0) {
    float dt;
    uint emitCursor;
    uint emitCount;
    uint maxParticles;
    float3 emitPos; float emitType;
    float3 emitDir; uint emitStartIndex;
    float4 emitColor;
    float3 corePos; float coreAttraction;
    uint emitEndIndex; float3 pad3;
    float3 coreScale; float pad4;
    float3 coreForward; float pad5;
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
        if (emitCursor + emitCount <= emitEndIndex) {
            shouldEmit = (i >= emitCursor && i < emitCursor + emitCount);
        } else {
            uint overflow = emitStartIndex + ((emitCursor + emitCount) - emitEndIndex);
            shouldEmit = (i >= emitCursor && i < emitEndIndex) || (i >= emitStartIndex && i < overflow);
        }
        
        if (shouldEmit) {
            Particle p = Particles[i];
            
            // 乱数で散らす (-1.0 ～ 1.0)
            float rx = (hash(i * 123) - 0.5f) * 2.0f;
            float ry = (hash(i * 456) - 0.5f) * 2.0f;
            float rz = (hash(i * 789) - 0.5f) * 2.0f;
            
            // ★位置をばらけさせる (超重要: 同一座標に重なると圧力が爆発する)
            float offsetScale = (emitType < 0.5f) ? 1.5f : 0.4f;
            float3 localOffset = float3(rx, ry, rz) * offsetScale;
            p.position = emitPos + localOffset;
            
            // 速度もランダムに
            float velScale = (emitType < 0.5f) ? 5.0f : 2.0f;
            p.velocity = emitDir * 5.0f + float3(rx, abs(ry) + 0.2f, rz) * velScale;
            p.color = emitColor;
            p.type = emitType;
            
            // 物理初期値
            p.density = 1.0f;
            p.pressure = 0.0f;
            
            if (emitType < 0.5f) {
                // スライムの場合: 初期ローカル座標を記憶して Shape Matching（形状維持）の目標にする
                p.pad = localOffset; 
            } else {
                // 水飛沫の場合: タイマー初期化
                p.pad = float3(0, 0, 0);
            }
            
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
    // type == 0.0f（スライム自身）のみ適用。水しぶき(type == 1.0f)は自由落下する
    if (coreAttraction > 0.0f && pi.type < 0.5f) {
        float3 toCore = corePos - pi.position;
        float distToCore = length(toCore);
        
        if (distToCore > 0.001f) {
            // 前方ベクトルと上ベクトルからローカル座標系を作成
            float3 forwardDir = normalize(coreForward);
            float3 upDir = float3(0, 1, 0);
            if (abs(forwardDir.y) > 0.99f) {
                upDir = float3(0, 0, 1);
            }
            float3 rightDir = normalize(cross(upDir, forwardDir));
            upDir = normalize(cross(forwardDir, rightDir));

            // ★Shape Matching (目標位置への追従)
            // 記憶しておいた初期ローカル座標 (pi.pad) に現在のスケールを適用
            float3 localTarget = pi.pad * coreScale;
            
            // ★追加: 攻撃時に「とんがらせて」「前方に突き出す」変形
            if (coreScale.z > 2.0f) {
                // 1. 後ろはそのまま残し、前半分だけを大きく前に伸ばす
                if (pi.pad.z > 0.0f) {
                    localTarget.z = pi.pad.z * coreScale.z; // 前方は鋭く伸びる
                } else {
                    localTarget.z = pi.pad.z * 1.5f; // 後方はプレイヤーの足元に留まる
                }
                
                // 2. 先端をとんがらせる（テーパリング）
                // 前方(z > -0.5)に行くほど、xとyを細くして円錐(針)のようにする
                if (pi.pad.z > -0.5f) {
                    // -0.5(根元) ～ 1.5(先端) を 0.0 ～ 1.0 に正規化
                    float normalizedZ = saturate((pi.pad.z + 0.5f) / 2.0f);
                    // 先端に行くほど 0.01(極細) に近づく係数
                    float taper = lerp(1.0f, 0.01f, normalizedZ);
                    localTarget.x *= taper;
                    localTarget.y *= taper;
                }
            }
            // コアの向きと位置を適用してワールドでの「あるべき目標位置」を算出
            float3 targetWorld = corePos + rightDir * localTarget.x + upDir * localTarget.y + forwardDir * localTarget.z;
            
            // 目標位置へ向かうベクトル（これが変形時に「外側」を向く力になり、移動時には前方に引っ張る力になる！）
            float3 toTarget = targetWorld - pi.position;
            
            // バネの力で目標位置へ引き寄せる
            // スケールが小さい軸（潰れる方向）ほどバネ力を強くして形状を保ちつつ、
            // 伸びる方向（スケールが大きい軸）のバネ力も弱めすぎないように最低1.0を保証する。
            float3 localPullStrength = float3(
                max(1.0f, 1.0f / max(coreScale.x, 0.1f)),
                max(1.0f, 1.0f / max(coreScale.y, 0.1f)),
                max(1.0f, 1.0f / max(coreScale.z, 0.1f))
            ) * (coreAttraction * PARTICLE_MASS * 1.5f);
            
            // toTarget をローカル座標に変換して、各軸ごとに強さを適用
            float3 toTargetLocal = float3(dot(toTarget, rightDir), dot(toTarget, upDir), dot(toTarget, forwardDir));
            float3 springForceLocal = toTargetLocal * localPullStrength;
            
            // ワールド座標に戻す
            float3 springForce = rightDir * springForceLocal.x + upDir * springForceLocal.y + forwardDir * springForceLocal.z;
            
            // 2. 床に潰れるのを防ぐ上向きの持ち上げ（重力相殺）
            if (pi.position.y < corePos.y) {
                springForce.y += 20.0f * PARTICLE_MASS;
            }
            
            // 複雑な押し戻しバネは削除し、強制クランプに委ねる
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
    
    // ★追加: 形状の強制クランプ（水風船アプローチ）
    // 力学的なバネで柔らかく形を作るのは破綻しやすいため、
    // 計算の最後に「指定したスケールの楕円体からはみ出したら物理的に表面に戻す」確実なバリアを張る。
    if (coreAttraction > 0.0f && pi.type < 0.5f) {
        float3 toCoreClamp = pi.position - corePos;
        
        float3 forwardDir = normalize(coreForward);
        float3 upDir = float3(0, 1, 0);
        if (abs(forwardDir.y) > 0.99f) upDir = float3(0, 0, 1);
        float3 rightDir = normalize(cross(upDir, forwardDir));
        upDir = normalize(cross(forwardDir, rightDir));
        
        float3 localToCoreC = float3(dot(toCoreClamp, rightDir), dot(toCoreClamp, upDir), dot(toCoreClamp, forwardDir));
        float3 scaledToCoreC = localToCoreC / max(coreScale, float3(0.01f, 0.01f, 0.01f));
        float distC = length(scaledToCoreC);
        
        float maxRadiusC = 1.2f; // 基本の半径
        
        // もし楕円体からはみ出していたら、強制的に表面に引き戻す
        if (distC > maxRadiusC) {
            float3 surfaceLocal = (scaledToCoreC / distC) * maxRadiusC; // 半径1.2の表面のローカル座標
            float3 targetLocal = surfaceLocal * coreScale; // スケールを戻してワールドでの相対座標へ
            float3 targetWorld = corePos + rightDir * targetLocal.x + upDir * targetLocal.y + forwardDir * targetLocal.z;
            
            // はみ出し防止のセーフティーネット（常に柔らかくクランプ）
            // 強制的な引き戻しをなくし、物理的なバネ力による滑らかな変形を優先する
            float lerpFactor = saturate(10.0f * dt); 
            pi.position = lerp(pi.position, targetWorld, lerpFactor);
        }
    }
    
    // コリジョン (簡易的な床バウンド)
    if (pi.position.y < 0.2f) {
        pi.position.y = 0.2f;
        pi.velocity.y *= -0.3f;
        pi.velocity.x *= 0.6f; // 床の摩擦
        pi.velocity.z *= 0.6f;
        
        // ★水飛沫は地面に付いて少ししたら消えるようにする
        if (pi.type > 0.5f) {
            pi.pad.x += dt;
            if (pi.pad.x > 1.5f) { // 地面に触れてから1.5秒で消滅
                pi.position.y = -1000.0f;
            }
        }
    } else {
        // バウンドして宙に浮いてもタイマーを継続
        if (pi.type > 0.5f && pi.pad.x > 0.0f) {
            pi.pad.x += dt;
            if (pi.pad.x > 1.5f) {
                pi.position.y = -1000.0f;
            }
        }
    }
    
    // 空間制限は削除（プレイヤーへの引力で十分なため）
    Particles[i] = pi;
}
