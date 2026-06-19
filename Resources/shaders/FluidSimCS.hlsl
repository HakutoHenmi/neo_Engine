// ========================================================================
// SPH流体シミュレーション v3 — パラメータバランス完全再計算版
// 
// 設計根拠:
//   6144粒子を半径1.0の球に詰めた場合:
//     平均粒子間隔 ≈ (4/3 π / 6144)^(1/3) ≈ 0.088
//     smoothingLength = 3.5 × 間隔 ≈ 0.30（近傍約30粒子）
//     particleMass = 0.1 → 総質量 614.4
//     restDensity ≈ 総質量 / 体積 ≈ 147 → 150に設定
//
//   圧力: Tait式（指数4、7より柔らかくプルプルする）
//   表面張力: 粒同士の引力で自然な丸みを保つ
//   境界拘束: なし（SPH自体の表面張力と圧力で形を保つ）
// ========================================================================

struct Particle {
    float3 position;
    float density;
    float3 velocity;
    float pressure;
    float3 force;
    float pad1;
    float3 restPosition;
    float pad2;
};

cbuffer FluidConstants : register(b0) {
    float3 corePosition;
    uint numParticles;
    
    float deltaTime;
    float smoothingLength;
    float particleMass;
    float restDensity;
    
    float gasStiffness;
    float viscosity;
    float gravity;
    float damping;
    
    float floorWorldY;
    uint passType;
    uint simMode;
    float pad0;
    float3 blobRadii;
    float pad1;
    float3 playerInputForce;
    float pad2;
};

RWStructuredBuffer<Particle> Particles : register(u0);

#define PI 3.14159265359f

// ---- SPHカーネル関数 ----
float Poly6(float r2, float h) {
    float h2 = h * h;
    if (r2 < 0.0001f || r2 > h2) return 0.0f;
    float t = h2 - r2;
    return (315.0f / (64.0f * PI * pow(h, 9))) * t * t * t;
}

float3 SpikyGrad(float3 dir, float r, float h) {
    if (r < 0.0001f || r > h) return float3(0, 0, 0);
    float t = h - r;
    return dir * (-45.0f / (PI * pow(h, 6))) * t * t;
}

float ViscosityLap(float r, float h) {
    if (r < 0.0001f || r > h) return 0.0f;
    return (45.0f / (PI * pow(h, 6))) * (h - r);
}

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint id = DTid.x;
    if (id >= numParticles) return;

    Particle p = Particles[id];
    float h = smoothingLength;

        // ==== パス0: 密度計算 + Tait圧力 ====
    if (passType == 0) {
        float density = 0.0f;
        for (uint i = 0; i < numParticles; i++) {
            float3 diff = p.position - Particles[i].position;
            density += particleMass * Poly6(dot(diff, diff), h);
        }
        density = max(density, 0.001f);
        p.density = density;
        
        // Tait式（指数を下げて爆発を防ぎつつ、上限クランプを外して特異点への圧潰を防ぐ）
        float ratio = density / restDensity;
        // 密度が低い時に引力にならないよう max(..., 0.0f)
        float effGasStiffness = gasStiffness * 1.5f; // 反発力を底上げ
        p.pressure = max(0.0f, effGasStiffness * (ratio * ratio - 1.0f));
        
        Particles[id] = p;
    }
    // ==== パス1: 力の計算 + 位置更新 ====
    else if (passType == 1) {
        float3 pressureForce = float3(0, 0, 0);
        float3 viscosityForce = float3(0, 0, 0);
        float3 surfaceTensionForce = float3(0, 0, 0);
        
        // 表面張力係数（1.5は強すぎて1点に潰れる原因だったので下げる）
        float cohesion = simMode == 0 ? 0.15f : 0.02f;
        
        for (uint i = 0; i < numParticles; i++) {
            if (i == id) continue;
            Particle neighbor = Particles[i];
            float3 diff = p.position - neighbor.position;
            float r = length(diff);
            
            if (r > 0.0001f && r < h) {
                float3 dir = diff / r;
                
                // 圧力
                float pTerm = (p.pressure / max(p.density * p.density, 0.01f))
                            + (neighbor.pressure / max(neighbor.density * neighbor.density, 0.01f));
                pressureForce -= particleMass * pTerm * SpikyGrad(dir, r, h);
                
                // 粘性
                float3 velDiff = neighbor.velocity - p.velocity;
                viscosityForce += velDiff * (particleMass / max(neighbor.density, 0.01f)) * ViscosityLap(r, h);
                
                // 表面張力（引力）
                float w = Poly6(r * r, h);
                surfaceTensionForce -= dir * cohesion * particleMass * w;
            }
        }
        viscosityForce *= viscosity;
        
        // 中心への引き戻し力
        float3 coreForce = float3(0, 0, 0);
        float distToCore = length(p.position);
        float3 dirToCore = distToCore > 0.001f ? -p.position / distToCore : float3(0,0,0);
        
        if (simMode == 0) {
            // ★ アプローチ変更：形状記憶バネ (Shape Matching)
            // 粒子ごとに記憶された初期位置(restPosition)へ強力に引き戻す
            float3 diffToRest = p.restPosition - p.position;
            // 弾力のあるゼリー感を出すため、距離に比例したバネの力をかける
            coreForce = diffToRest * 35.0f; 
        } else {
            // 液状化
            float pullStrength = 0.3f;
            coreForce = -p.position * pullStrength;
        }
        
        // プレイヤー入力（外力）の適用
        // スライムの質量・密度に応じて適用し、全体としてドロっと動かす
        float3 inputF = playerInputForce * p.density * 2.0f;
        
        // 重力
        float3 gravityForce = float3(0, gravity * p.density, 0);
        
        // 力の合成
        // 旧来は totalForce を p.density で割っていたため、密度が低い（離れた）粒子の加速度が異常に大きくなり
        // 飛び散る原因となっていた。SPH力は一定の restDensity で割り、他はそのまま足すことで安定させる。
        float3 sphAccel = (pressureForce + viscosityForce + surfaceTensionForce) / restDensity;
        float3 accel = sphAccel + coreForce + float3(0, gravity, 0) + (playerInputForce * 2.0f);
        
        // 速度・位置更新
        p.velocity += accel * deltaTime;
        // スライム時は集まる際の勢いを殺す（ミルククラウン爆発防止）
        float currentDamping = simMode == 0 ? 0.92f : damping;
        p.velocity *= currentDamping;
        
        // 速度制限
        float maxSpeed = simMode == 0 ? 12.0f : 8.0f;
        float speed = length(p.velocity);
        if (speed > maxSpeed) {
            p.velocity *= maxSpeed / speed;
        }
        
        p.position += p.velocity * deltaTime;
        
        // オーバーシュート防止
        if (simMode == 0) {
            float dist = length(p.position);
            float maxRadius = 10.0f; // 広く許容（形状記憶を阻害しないため）
            if (dist > maxRadius) {
                p.position = p.position * (maxRadius / dist);
                p.velocity *= 0.5f;
            }
        }
        
        // 地面衝突と吸着感（Adhesion）
        float colRadius = 0.08f;
        float worldY = corePosition.y + p.position.y;
        
        // 床に近いときの吸着力（張り付くような挙動）は、スライムの丸みを保つため無効化
        /*
        float distToFloor = worldY - floorWorldY;
        if (simMode == 0 && distToFloor > 0.0f && distToFloor < 0.2f) {
            // 床に近いほど床方向へ引っぱる
            p.velocity.y -= (0.2f - distToFloor) * 2.0f * deltaTime;
        }
        */

        if (worldY < floorWorldY + colRadius) {
            p.position.y = floorWorldY + colRadius - corePosition.y;
            p.velocity.y *= -0.05f;  // ほぼ跳ねない（ベチャッとする）
            float friction = simMode == 0 ? 0.6f : 0.95f; // 床の摩擦
            p.velocity.x *= friction;
            p.velocity.z *= friction;
        }
        
        Particles[id] = p;
    }
}
