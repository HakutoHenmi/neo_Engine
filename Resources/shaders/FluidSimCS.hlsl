#include "FluidMaterial.hlsli"
struct Particle {
    float3 position; float density;
    float3 velocity; float pressure;
    float4 color;
    float type; float3 pad;
};

struct AABB {
    float3 min; float pad0;
    float3 max; float pad1;
};

RWStructuredBuffer<Particle> Particles : register(u0);
RWStructuredBuffer<uint> GridCount : register(u1);
RWStructuredBuffer<uint> GridOffset : register(u2);
RWStructuredBuffer<Particle> SortedParticles : register(u3);
RWStructuredBuffer<uint> OriginalIndices : register(u4);
RWStructuredBuffer<float4> PreviousPositions : register(u5);
RWStructuredBuffer<Particle> SolverOutput : register(u6);
StructuredBuffer<AABB> AABBs : register(t0);

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
    float3 coreScale; float coreFlowSpeed;
    float3 coreForward; float coreMode;
    uint aabbCount; float3 pad6; // ★追加
    
    // ★追加: デコイ用
    float3 decoyPos; float decoyAttraction;
    float3 decoyScale; float pad7;
    float3 decoyForward; float pad8;
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
[numthreads(64, 1, 1)]
void Emit(uint3 DTid : SV_DispatchThreadID) {
    uint localIndex = DTid.x;
    if (localIndex >= emitCount) return;
    
    if (emitCount > 0) {
        uint range = emitEndIndex - emitStartIndex;
        if (range == 0) return;
        
        uint i = emitStartIndex + ((emitCursor - emitStartIndex + localIndex) % range);
        if (i < maxParticles) {
            Particle p = Particles[i];
            
            // 乱数で散らす (-1.0 ～ 1.0)
            float rx = (hash(i * 123) - 0.5f) * 2.0f;
            float ry = (hash(i * 456) - 0.5f) * 2.0f;
            float rz = (hash(i * 789) - 0.5f) * 2.0f;
            
            bool isLostSlime = (emitType > 2.5f && emitType < 3.5f);
            bool isSlime = (emitType < 0.5f || (emitType > 1.5f && emitType < 2.5f) || isLostSlime);
            
            // ★位置をばらけさせる (超重要: 同一座標に重なると圧力が爆発する)
            float3 randVec = float3(rx, ry, rz);
            if (isSlime && length(randVec) > 0.01f) {
                // Spread slime through a sphere before density projection.
                float rDist = pow(hash(i * 999), 0.3333f);
                randVec = normalize(randVec) * rDist;
                if (emitType<0.5f) {
                    // Uniform solid angle: normalizing a random cube biases
                    // the rest distribution toward diagonal directions.
                    float polar=1.0f-2.0f*hash(i*456U+19U);
                    float azimuth=6.283185307f*hash(i*123U+71U);
                    float ring=sqrt(max(0.0f,1-polar*polar));
                    randVec=float3(ring*cos(azimuth),polar,ring*sin(azimuth))*rDist;
                }
            }
            
            // 1.5x player particle count with approximately 1.5x rest volume.
            float offsetScale = emitType<0.5f ? PLAYER_REST_RADIUS : (isSlime ? 1.5f : 0.4f);
            float3 localOffset = randVec * offsetScale;
            p.position = emitPos + localOffset;
            
            // 速度もランダムに
            float velScale = isSlime ? 5.0f : 2.0f;
            p.velocity = emitDir * 5.0f + randVec * velScale;
            if (!isSlime) {
                // Emit a resolved stream slab, not a dense random ball. Increasing
                // count widens the nozzle instead of increasing overlap/pressure.
                float3 flow=emitDir*5.0f;
                float speed=length(flow);
                float3 along=speed>0.01f ? flow/speed : float3(0,-1,0);
                float3 across=normalize(cross(along,abs(along.y)<0.9f ? float3(0,1,0) : float3(1,0,0)));
                float3 tangent=cross(along,across);
                float slabLength=max(speed*max(dt,1e-4f),0.01f);
                uint side=max(1U,(uint)ceil(sqrt((float)emitCount*0.20f/slabLength)));
                uint perLayer=side*side;
                uint lane=(emitCursor-emitStartIndex+localIndex)%perLayer;
                float2 plane=(float2(lane%side,lane/side)-((float)side-1)*0.5f)*0.20f;
                // Earlier emissions have travelled farther by the frame end.
                p.position=emitPos+across*plane.x+tangent*plane.y+along*((emitCount-localIndex-0.5f)*slabLength/emitCount);
                p.velocity=flow;
            }
            p.color = emitColor;
            p.type = emitType;
            
            // 物理初期値
            p.density = 1.0f;
            p.pressure = 0.0f;
            
            if (emitType < 0.5f || isLostSlime) {
                p.pad = float3(0, 0, 0);
            } else if (isSlime) {
                // Decoys retain their rest positions for their own shape matching.
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
// PBF kernel calibration (world units, unit particle mass).
// ==========================================
static const float SMOOTHING_RADIUS = 0.4f; // ★1.0fから0.4fへ最適化: グリッドセルサイズが縮小され、O(N)の真価を発揮する
static const float PARTICLE_MASS = 1.0f;
// Unit particle mass: a 0.2 lattice has density ~= 125, not 3 (self ~= 24.48).
static const float REST_DENSITY = 125.0f;
static const float SLIME_SURFACE_TENSION = 18.0f;
static const float WATER_SURFACE_TENSION = 4.0f;
static const float GRAVITY = -20.0f;
static const float PI = 3.1415926535f;
static const uint NUM_GRID_CELLS = 65536;

// ★定数の事前計算 (演算負荷の削減)
static const float H2 = 0.16f; // SMOOTHING_RADIUS^2
static const float H_POWER_6 = 0.004096f; // SMOOTHING_RADIUS^6
static const float H_POWER_9 = 0.000262144f; // SMOOTHING_RADIUS^9
static const float POLY6_COEFF = 315.0f / (64.0f * PI * H_POWER_9);
static const float SPIKY_COEFF = 45.0f / (PI * H_POWER_6);

float RestDensity(float type) { return FluidRestDensity(type); }
float3 KernelGradient(float3 d) {
    float r = length(d);
    if (r < 1e-5f || r >= SMOOTHING_RADIUS) return 0;
    return -SPIKY_COEFF * (SMOOTHING_RADIUS-r) * (SMOOTHING_RADIUS-r) * d/r;
}
float Kernel(float3 d) {
    float w = max(0.0f, H2-dot(d,d));
    return POLY6_COEFF*w*w*w;
}

uint GetFluidPhase(float type) {
    // Player and its detached mass are the same material.  Decoys and water
    // are separate phases and must not share density, pressure or viscosity.
    if (type < 0.5f || (type > 2.5f && type < 3.5f)) return 0U;
    if (type > 1.5f && type < 2.5f) return 1U;
    return 2U;
}

bool CanFluidsInteract(float typeA, float typeB) {
    return GetFluidPhase(typeA) == GetFluidPhase(typeB);
}

// ==========================================
// 空間ハッシュグリッド用関数群
// ==========================================
[numthreads(64, 1, 1)]
void ExtractLostFluid(uint3 DTid : SV_DispatchThreadID) {
    uint localIndex = DTid.x;
    if (localIndex >= emitCount) return;

    uint range = emitEndIndex - emitStartIndex;
    if (range == 0) return;

    uint i = emitStartIndex + ((emitCursor - emitStartIndex + localIndex) % range);
    if (i >= maxParticles) return;

    Particle p = Particles[i];
    if (p.color.a < 0.01f || p.position.y < -500.0f || p.type > 0.5f) return;

    uint groupIndex = localIndex / 10;
    uint memberIndex = localIndex - groupIndex * 10;

    float groupCount = max(1.0f, ceil((float)emitCount / 10.0f));
    float groupAngle = ((float)groupIndex / groupCount) * (PI * 2.0f) + (hash(groupIndex * 193 + 7) - 0.5f) * 0.75f;
    float groupLift = 0.22f + hash(groupIndex * 271 + 11) * 0.32f;
    float groupDist = 0.45f + hash(groupIndex * 313 + 17) * 0.35f;
    float3 groupOut = normalize(float3(cos(groupAngle), groupLift, sin(groupAngle)));

    float3 right = cross(float3(0.0f, 1.0f, 0.0f), groupOut);
    if (length(right) < 0.01f) {
        right = float3(1.0f, 0.0f, 0.0f);
    } else {
        right = normalize(right);
    }
    float3 tangent = normalize(cross(groupOut, right));

    float memberAngle = ((float)memberIndex / 10.0f) * (PI * 2.0f) + hash(i * 37 + 5) * 0.55f;
    float memberRadius = 0.10f + hash(i * 67 + 3) * 0.10f;
    float3 memberOffset =
        right * cos(memberAngle) * memberRadius +
        tangent * sin(memberAngle) * memberRadius +
        groupOut * ((hash(i * 97 + 23) - 0.5f) * 0.06f);

    p.position = emitPos + groupOut * groupDist + memberOffset;
    p.velocity = emitDir * 2.4f + groupOut * (7.5f + hash(groupIndex * 401 + 29) * 2.5f) + memberOffset * 3.0f;
    p.color = emitColor;
    p.type = 3.0f;
    p.pad = float3(0, (float)(i / 10U), 0);
    p.density = 1.0f;
    p.pressure = 0.0f;

    Particles[i] = p;
}

[numthreads(64, 1, 1)]
void SyncLostFluidGroup(uint3 DTid : SV_DispatchThreadID) {
    uint localIndex = DTid.x;
    if (localIndex >= 10U) return;

    uint groupId = (uint)(pad3.x + 0.5f);
    uint i = groupId * 10U + localIndex;
    if (i >= maxParticles) return;

    Particle p = Particles[i];
    if (!(p.type > 2.5f && p.type < 3.5f)) return;

    float memberAngle = ((float)localIndex / 10.0f) * (PI * 2.0f) + hash(i * 37 + 5) * 0.35f;
    float memberRadius = (localIndex < 6U) ? 0.18f : 0.10f;
    float memberHeight = (localIndex < 6U) ? 0.02f : 0.18f;
    float3 memberOffset = float3(cos(memberAngle) * memberRadius, memberHeight, sin(memberAngle) * memberRadius);

    p.position = emitPos + memberOffset;
    p.velocity = float3(0.0f, 0.0f, 0.0f);
    p.pad.x = max(p.pad.x, 0.9f);
    p.pad.y = (float)groupId;
    p.color.a = 1.0f;

    Particles[i] = p;
}

[numthreads(64, 1, 1)]
void AbsorbLostFluidGroup(uint3 DTid : SV_DispatchThreadID) {
    uint localIndex = DTid.x;
    if (localIndex >= 10U) return;

    uint groupId = (uint)(pad3.x + 0.5f);
    uint i = groupId * 10U + localIndex;
    if (i >= maxParticles) return;

    Particle p = Particles[i];
    if (!(p.type > 2.5f && p.type < 3.5f)) return;

    float memberAngle = ((float)localIndex / 10.0f) * (PI * 2.0f);
    float memberRadius = (localIndex < 6U) ? 0.18f : 0.10f;
    float memberHeight = (localIndex < 6U) ? 0.02f : 0.18f;
    float3 memberOffset = float3(cos(memberAngle) * memberRadius, memberHeight, sin(memberAngle) * memberRadius);

    p.position = corePos + memberOffset;
    p.velocity = float3(0.0f, 0.0f, 0.0f);
    p.type = 0.0f;
    p.color = float4(0.35f, 0.95f, 0.20f, 1.0f);
    p.pad = float3(0.0f, 0.0f, 0.0f);

    Particles[i] = p;
}

int3 GetCell(float3 pos) {
    return int3(floor(pos / SMOOTHING_RADIUS));
}

uint GetGridHash(int3 cell) {
    uint h = (uint(cell.x) * 73856093U) ^ (uint(cell.y) * 19349663U) ^ (uint(cell.z) * 83492791U);
    return h % NUM_GRID_CELLS;
}

// ==========================================
// Pass Init: Initialize Particles (Run Once)
// ==========================================
[numthreads(64, 1, 1)]
void InitParticles(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    
    Particles[i].position = float3(0, -1000.0f, 0); // 初期は画面外の遠くへ
    Particles[i].density = 0.01f;
    Particles[i].velocity = float3(0,0,0);
    Particles[i].pressure = 0.0f;
    Particles[i].color = float4(1,1,1,0); // a=0で非アクティブ扱い
    Particles[i].type = 0.0f;
    Particles[i].pad = float3(0,0,0);
    
    // ★追加: ゴーストパーティクルの計算を防ぐため、ソート先バッファも安全な値で初期化
    SortedParticles[i] = Particles[i];
}

// ==========================================
// Pass 0.0: Clear Original Indices
// ==========================================
[numthreads(64, 1, 1)]
void ClearOriginalIndices(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    OriginalIndices[i] = 0xFFFFFFFF;
}

// ==========================================
// Pass 0.1: Clear Grid Count
// ==========================================
[numthreads(64, 1, 1)]
void ClearGridCount(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= NUM_GRID_CELLS) return;
    GridCount[i] = 0;
}

// ==========================================
// Pass 0.2: Count Particles
// ==========================================
[numthreads(64, 1, 1)]
void CountParticles(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    
    Particle p = Particles[i];
    if (p.color.a < 0.01f || p.position.y < -500.0f) return;
    
    int3 cell = GetCell(p.position);
    uint gHash = GetGridHash(cell);
    InterlockedAdd(GridCount[gHash], 1);
}

// ==========================================
// Pass 0.3: Prefix Sum
// ==========================================
groupshared uint sharedTotal[256];

[numthreads(256, 1, 1)]
void PrefixSum(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex) {
    uint elementsPerThread = NUM_GRID_CELLS / 256; // 65536 / 256 = 256 elements per thread
    uint startIdx = GI * elementsPerThread;
    
    // 1. 各スレッドが自分の担当ブロック(256個)の合計を計算
    uint localSum = 0;
    for (uint i = 0; i < elementsPerThread; i++) {
        localSum += GridCount[startIdx + i];
    }
    sharedTotal[GI] = localSum;
    
    // 全スレッドのローカル合計計算が終わるのを待つ
    GroupMemoryBarrierWithGroupSync();
    
    // 2. スレッド0が全ブロックのプレフィックスサム（累積和）を計算
    if (GI == 0) {
        uint s = 0;
        for (uint j = 0; j < 256; j++) {
            uint temp = sharedTotal[j];
            sharedTotal[j] = s;
            s += temp;
        }
    }
    
    // スレッド0の計算が終わるのを待つ
    GroupMemoryBarrierWithGroupSync();
    
    // 3. 各スレッドが自分の担当ブロックにオフセットを適用して出力
    uint currentOffset = sharedTotal[GI];
    for (uint k = 0; k < elementsPerThread; k++) {
        uint idx = startIdx + k;
        GridOffset[idx] = currentOffset;
        currentOffset += GridCount[idx];
        GridCount[idx] = 0; // 次のSortParticlesパスのためにリセット
    }
}

// ==========================================
// Pass 0.4: Sort Particles
// ==========================================
void SortParticle(uint i, bool prepareVelocity) {
    if (i >= maxParticles) return;
    
    Particle p = Particles[i];
    if (p.color.a < 0.01f || p.position.y < -500.0f) return;
    
    int3 cell = GetCell(p.position);
    uint gHash = GetGridHash(cell);
    
    uint localOffset;
    InterlockedAdd(GridCount[gHash], 1, localOffset);
    
    uint destIdx = GridOffset[gHash] + localOffset;
    if (destIdx >= maxParticles) return;
    if (prepareVelocity) {
        float3 v = (p.position - PreviousPositions[i].xyz) / max(dt, 1e-6f);
        p.velocity = v * min(1.0f, 30.0f / max(length(v), 1e-6f));
    }
    SortedParticles[destIdx] = p;
    OriginalIndices[destIdx] = i;
}

[numthreads(64, 1, 1)]
void SortParticles(uint3 DTid : SV_DispatchThreadID) {
    SortParticle(DTid.x, false);
}

[numthreads(64, 1, 1)]
void SortParticlesVelocity(uint3 DTid : SV_DispatchThreadID) {
    SortParticle(DTid.x, true);
}

// ==========================================
// Pass 1: CalcDensity (密度と圧力の計算)
// ==========================================
[numthreads(64, 1, 1)]
void CalcDensity(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    
    // ★追加: 未ソート（無効）なスロットなら計算スキップ（ゴーストパーティクルの計算防止）
    if (OriginalIndices[i] == 0xFFFFFFFF) return;
    
    // ★追加: 未放出・未初期化のパーティクルは地底に飛ばして計算から完全除外
    if (SortedParticles[i].color.a < 0.01f || SortedParticles[i].position.y < -500.0f) {
        SortedParticles[i].position = float3(0, -1000.0f, 0);
        SortedParticles[i].density = 0.01f;
        SortedParticles[i].pressure = 0.0f;
        return;
    }
    
    float3 pos_i = SortedParticles[i].position;
    float type_i = SortedParticles[i].type;
    
    float density = 0.0f;
    float3 gradientSum = 0;
    float3 surfaceGradientSum = 0;
    float gradientSquaredSum = 0;
    float rest = RestDensity(type_i);
    
    // 空間ハッシュグリッドで近傍のパーティクルのみを探索
    int3 cell = GetCell(pos_i);
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                uint gHash = GetGridHash(cell + int3(x, y, z));
                uint startIdx = GridOffset[gHash];
                if (startIdx >= maxParticles) continue;
                uint count = GridCount[gHash];
                
                for(uint k = 0; k < count; k++) {
                    uint j = startIdx + k;
                    if (j >= maxParticles) break;
                    if (SortedParticles[j].position.y >= -500.0f) {
                        float3 diff = pos_i - SortedParticles[j].position;
                        float r2 = dot(diff, diff);
                        if (r2 < H2) {
                            // The query cube is much larger than the kernel
                            // sphere. Reject distant candidates before the
                            // floor/division needed to disambiguate hash hits.
                            if (any(GetCell(SortedParticles[j].position) != cell + int3(x,y,z))) continue;
                            if (!CanFluidsInteract(type_i, SortedParticles[j].type)) continue;
                            float w = H2 - r2;
                            density += PARTICLE_MASS * POLY6_COEFF * w * w * w;
                            float3 gradient = KernelGradient(diff) / rest;
                            gradientSum += gradient;
                            gradientSquaredSum += dot(gradient, gradient);
                            if (i != j) {
                                // CalcForce used to walk the same 27 cells just
                                // to sum this gradient for surface tension.
                                if (r2 < 0.000001f) {
                                    float3 separated = float3(hash(i) - 0.5f,
                                        hash(j) - 0.5f, hash(i ^ j) - 0.5f) * 0.01f;
                                    if (dot(separated, separated) < 0.00000001f)
                                        separated = float3(0, 0.01f, 0);
                                    surfaceGradientSum += KernelGradient(separated);
                                } else {
                                    surfaceGradientSum += gradient * rest;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 自身の最低密度を保証 (0割り防止)
    if (density < 0.01f) density = 0.01f;
    
    SortedParticles[i].density = density;
    
    // 圧力計算 (マイナスにならないようにmaxを取る)
    // PBF lambda. Compression-only at free surfaces; do not pull isolated
    // particles into artificial clumps to satisfy a missing-neighbour density.
    float constraint = max(density / rest - 1.0f, 0.0f);
    SortedParticles[i].pressure = -constraint / (gradientSquaredSum + dot(gradientSum,gradientSum) + 1e-4f);
    SolverOutput[i].position = surfaceGradientSum;
}

// ==========================================
// Pass 2: CalcForce (力学計算と座標更新)
// ==========================================
[numthreads(64, 1, 1)]
void CalcForce(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    if (dt <= 0.0f) return;
    
    // ★追加: 未ソート（無効）なスロットなら計算スキップ（ゴーストパーティクルの計算防止）
    if (OriginalIndices[i] == 0xFFFFFFFF) return;
    
    Particle pi = SortedParticles[i];
    if (pi.position.y < -500.0f) return;
    
    // The density pass already collected the same spiky gradients. Its sum
    // converts exactly to the former cohesion force (including overlap jitter).
    bool isSlimeType = (pi.type < 0.5f || (pi.type > 1.5f && pi.type < 3.5f));
    float surfaceTension = pi.type < 0.5f ? 50.0f :
        (isSlimeType ? SLIME_SURFACE_TENSION : WATER_SURFACE_TENSION);
    float3 force = SolverOutput[i].position * (surfaceTension / (SPIKY_COEFF * H2));

    // 加速度
    bool isLostPlayerSlimeForCluster = (pi.type > 2.5f && pi.type < 3.5f);

    if (isLostPlayerSlimeForCluster) {
        uint sourceIndex = OriginalIndices[i];
        uint groupStart = (sourceIndex / 10U) * 10U;
        float3 groupCenter = float3(0.0f, 0.0f, 0.0f);
        uint groupMemberCount = 0U;

        [unroll]
        for (uint m = 0U; m < 10U; ++m) {
            uint memberSource = groupStart + m;
            if (memberSource >= emitEndIndex) break;

            Particle gp = Particles[memberSource];
            if (gp.type > 2.5f && gp.type < 3.5f && gp.position.y > -500.0f) {
                groupCenter += gp.position;
                groupMemberCount += 1U;
            }
        }

        if (groupMemberCount > 0U) {
            groupCenter /= (float)groupMemberCount;

            uint memberIndex = sourceIndex - groupStart;
            float memberAngle = ((float)memberIndex / 10.0f) * (PI * 2.0f);
            float heightLayer = (memberIndex < 6U) ? 0.0f : 0.14f;
            float radius = (memberIndex < 6U) ? 0.18f : 0.10f;
            float3 targetOffset = float3(cos(memberAngle) * radius, heightLayer, sin(memberAngle) * radius);
            float3 clusterTarget = groupCenter + targetOffset;

            force += (clusterTarget - pi.position) * (180.0f * pi.density);
            force -= pi.velocity * (20.0f * pi.density);
        }
    }
    
    // The controller moves the core; player particles keep their own positions.
    // Density projection and collisions determine the occupied volume and base.
    bool isCurrentLiquefiedPlayer = pi.type < 0.5f && coreMode > 0.5f;
    if (pi.type < 0.5f && coreAttraction > 0.0f) {
        if (!isCurrentLiquefiedPlayer) {
            float3 fromCore = pi.position - corePos;
            float distanceToCore = length(fromCore);
            float3 radialDir = distanceToCore > 1e-4f ? fromCore / distanceToCore : float3(0.0f, 1.0f, 0.0f);
            float fieldDistance = distanceToCore;
            // An attack opens the field in front without assigning particle
            // identities or rotating the body when the controller turns.
            if (coreScale.z > 2.0f) {
                float3 forwardDir = normalize(float3(coreForward.x, 0.0f, coreForward.z) + float3(0.0f, 0.0f, 1e-5f));
                float front = max(dot(fromCore, forwardDir), 0.0f);
                fieldDistance = length(fromCore - forwardDir * front * (1.0f - 1.0f / coreScale.z));
            }

            // --- Pressure: only when genuinely compressed ---
            // PBF handles most outward push; this extra term reinforces it
            // under heavy compression without producing a permanent repulsion
            // that would hollow the crown.
            float compression = max(pi.density / RestDensity(pi.type) - 1.0f, 0.0f);
            float outwardPressure = min(coreAttraction * 0.5f * compression, 12.0f);

            // --- Cohesion: always-on with no dead zone ---
            // A gentle pull exists at every distance so that no layer can
            // separate even when vertical support lifts particles upward.
            // Beyond 1.55 the stiffness ramps sharply to keep the body compact.
            float cohesionMag = min(3.0f * fieldDistance
                                  + 15.0f * max(fieldDistance - 1.55f, 0.0f), 45.0f);
            float3 coreForce = radialDir * (outwardPressure - cohesionMag);

            // --- Vertical support: dome shaping ---
            // Moderate baseline keeps floor particles grounded (net −6).
            // Crown gets enough lift to form a dome (net +4).
            // The always-on cohesion prevents the separation that a dead zone
            // would allow.
            float aboveCore = saturate((pi.position.y - corePos.y) / 2.1f);
            coreForce.y += 14.0f + 10.0f * aboveCore;

            // Follow equally on every side. Front/rear gain differences pile
            // fluid into a wall when the controller reverses direction.
            float coreSpeed = length(pad3);
            float baseFollow = 1.2f;
            float distanceFollow = min(6.0f * saturate((fieldDistance - 1.8f) / 2.3f), 6.0f);
            float speedFollow = 0.2f * min(coreSpeed, 20.0f);
            float planarCoreSpeed = length(pad3.xz);
            float opposingSpeed = planarCoreSpeed > 0.5f
                ? max(-dot(pi.velocity.xz, pad3.xz) / planarCoreSpeed, 0.0f) : 0.0f;
            float follow = baseFollow + distanceFollow + speedFollow + 0.35f * min(opposingSpeed, 20.0f);
            float3 velocityFollow = (pad3 - pi.velocity) * follow;
            // Vertical coupling must be at least as strong as horizontal so that
            // the body does not smear vertically during fast movement.
            velocityFollow.y *= max(1.0f, 2.0f * aboveCore);

            force += (coreForce + velocityFollow) * pi.density;
        } else {
            // Entry momentum briefly spreads the puddle. The controller stays
            // at its anchor, so retain a soft perimeter and dissipate motion.
            float3 forwardDir = normalize(float3(coreForward.x, 0.0f, coreForward.z) + float3(0.0f, 0.0f, 1e-5f));
            float3 planarVelocity = float3(pi.velocity.x, 0.0f, pi.velocity.z);
            float3 planarOffset = float3(pi.position.x - corePos.x, 0.0f, pi.position.z - corePos.z);
            float planarDistance = length(planarOffset);
            float puddleRadius = max(2.0f, 0.45f * max(coreScale.x, coreScale.z));
            float excess = max(planarDistance - puddleRadius, 0.0f);
            float3 returnForce = planarDistance > 1e-4f
                ? -planarOffset / planarDistance * min(18.0f * excess, 60.0f)
                : float3(0.0f, 0.0f, 0.0f);
            force += (forwardDir * coreFlowSpeed - planarVelocity) * 3.0f * pi.density;
            force += (returnForce - planarVelocity * 3.0f - float3(0.0f, pi.velocity.y, 0.0f)) * pi.density;
        }
    } else if (pi.type > 1.5f && pi.type < 2.5f && decoyAttraction > 0.0f) {
        // The decoy still uses its own rest shape.
        float3 forwardDir = normalize(decoyForward);
        float3 upDir = abs(forwardDir.y) > 0.99f ? float3(0, 0, 1) : float3(0, 1, 0);
        float3 rightDir = normalize(cross(upDir, forwardDir));
        upDir = normalize(cross(forwardDir, rightDir));
        float3 localTarget = pi.pad * decoyScale;
        float3 targetWorld = decoyPos + rightDir * localTarget.x + upDir * localTarget.y + forwardDir * localTarget.z;
        force += (targetWorld - pi.position) * decoyAttraction * 3.0f * pi.density;
        force -= pi.velocity * pi.density * (2.0f * sqrt(decoyAttraction));
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
    
    // Only the decoy retains a rest-shape safety boundary.
    if (pi.type > 1.5f && pi.type < 2.5f && decoyAttraction > 0.0f) {
        float3 offset = pi.position - decoyPos;
        float distanceToCore = length(offset);
        float maxRadius = 1.2f * max(max(decoyScale.x, decoyScale.y), decoyScale.z);
        if (distanceToCore > maxRadius) {
            pi.position = lerp(pi.position, decoyPos + offset * (maxRadius / distanceToCore), saturate(10.0f * dt));
        }
    }
    // 床や壁に触れた時の摩擦（水は滑りやすく、スライムは滑りにくく）
    bool isSlimeTypeForFriction = (pi.type < 0.5f || (pi.type > 1.5f && pi.type < 2.5f) || (pi.type > 2.5f && pi.type < 3.5f));
    // The player must be able to follow its moving core while touching ground.
    // Keep the existing friction for decoys, detached slime, and water.
    float friction = pow(isCurrentLiquefiedPlayer ? 0.92f :
        (pi.type < 0.5f ? 0.97f : (isSlimeTypeForFriction ? 0.6f : 0.98f)), dt * 60.0f);
    bool groundContact=false;
    
    // コリジョン (簡易的な床バウンド)
    if (pi.position.y < 0.2f) {
        groundContact=true;
        pi.position.y = 0.2f;
        pi.velocity.y *= -0.3f;
        pi.velocity.x *= friction; // 床の摩擦
        pi.velocity.z *= friction;
        
    }
    
    // ★追加: 自由配置されたAABB（Cube等）との衝突判定
    uint safeAABBCount = min(aabbCount, 128U);
    for (uint k = 0; k < safeAABBCount; ++k) {
        float3 bmin = AABBs[k].min;
        float3 bmax = AABBs[k].max;
        
        // 余裕を持たせたAABBの少し外側で判定（めり込み防止）
        float pRadius = 0.3f;
        bool liquefiedFloorHit =
            isCurrentLiquefiedPlayer &&
            pi.position.x > bmin.x - pRadius && pi.position.x < bmax.x + pRadius &&
            pi.position.z > bmin.z - pRadius && pi.position.z < bmax.z + pRadius &&
            pi.position.y < bmax.y + pRadius && pi.position.y > bmax.y - 2.0f;
        if (liquefiedFloorHit) {
            pi.position.y = bmax.y + pRadius;
            if (pi.velocity.y < 0.0f) {
                pi.velocity.y *= -0.25f;
            }
            pi.velocity.x *= friction;
            pi.velocity.z *= friction;
        }
        
        if (!liquefiedFloorHit &&
            pi.position.x > bmin.x - pRadius && pi.position.x < bmax.x + pRadius &&
            pi.position.y > bmin.y - pRadius && pi.position.y < bmax.y + pRadius &&
            pi.position.z > bmin.z - pRadius && pi.position.z < bmax.z + pRadius) 
        {
            // どの面に最も近いかを計算して押し出す
            float dLeft = pi.position.x - (bmin.x - pRadius);
            float dRight = (bmax.x + pRadius) - pi.position.x;
            float dBottom = pi.position.y - (bmin.y - pRadius);
            float dTop = (bmax.y + pRadius) - pi.position.y;
            float dBack = pi.position.z - (bmin.z - pRadius);
            float dFront = (bmax.z + pRadius) - pi.position.z;
            
            float minDist = min(min(min(dLeft, dRight), min(dBottom, dTop)), min(dBack, dFront));
            
            if (minDist == dTop) {
                pi.position.y = bmax.y + pRadius;
                pi.velocity.y *= -0.3f;
                pi.velocity.x *= friction;
                pi.velocity.z *= friction;
                groundContact=true;
            } else if (minDist == dBottom) {
                pi.position.y = bmin.y - pRadius;
                pi.velocity.y *= -0.3f;
                pi.velocity.x *= friction;
                pi.velocity.z *= friction;
            } else if (minDist == dLeft) {
                pi.position.x = bmin.x - pRadius;
                pi.velocity.x *= -0.3f;
                pi.velocity.y *= friction;
                pi.velocity.z *= friction;
            } else if (minDist == dRight) {
                pi.position.x = bmax.x + pRadius;
                pi.velocity.x *= -0.3f;
                pi.velocity.y *= friction;
                pi.velocity.z *= friction;
            } else if (minDist == dBack) {
                pi.position.z = bmin.z - pRadius;
                pi.velocity.z *= -0.3f;
                pi.velocity.x *= friction;
                pi.velocity.y *= friction;
            } else if (minDist == dFront) {
                pi.position.z = bmax.z + pRadius;
                pi.velocity.z *= -0.3f;
                pi.velocity.x *= friction;
                pi.velocity.y *= friction;
            }
        }
    }
    
    // Water metadata: age since contact, support height, recent-contact grace.
    // Advance age once, even when both the floor and an AABB were touched.
    if (FluidIsWater(pi.type)) {
        pi.pad.z=max(0,pi.pad.z-dt);
        if (groundContact) { pi.pad.y=pi.position.y; pi.pad.z=0.08f; }
        if (groundContact || pi.pad.x>0) pi.pad.x+=dt;
        if (pi.pad.x>WATER_GROUND_LIFETIME) { pi.color.a=0; pi.position.y=-1000; }
    }
    // ★追加: 液状化したデコイ（引力がなくなったデコイ）の消滅処理
    if (pi.type > 1.5f && pi.type < 2.5f && decoyAttraction <= 0.01f) {
        pi.color.a -= dt * 0.5f; // 約2秒かけてじわじわ消える
        if (pi.color.a <= 0.01f) {
            pi.position.y = -1000.0f;
        }
    }
    
    // 空間制限は削除（プレイヤーへの引力で十分なため）
    if (pi.type > 2.5f && pi.type < 3.5f) {
        pi.pad.x += dt;
    }

    // All neighbours and the original group positions remain immutable during
    // this dispatch. A separate writeback pass publishes the prediction.
    SolverOutput[i] = pi;
}

// ==========================================
// Pass 3: WriteBack
// ==========================================
[numthreads(64, 1, 1)]
void WriteBack(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    
    uint origIdx = OriginalIndices[i];
    if (origIdx != 0xFFFFFFFF && origIdx < maxParticles) {
        Particles[origIdx] = SolverOutput[i];
        SortedParticles[i] = SolverOutput[i];
    }
}

[numthreads(64, 1, 1)]
void SavePrevious(uint3 id : SV_DispatchThreadID) {
    if (id.x < maxParticles) PreviousPositions[id.x] = float4(Particles[id.x].position, 1);
}

// Jacobi: lambdas and predicted positions are read-only for this dispatch.
[numthreads(64, 1, 1)]
void CalcDeltaP(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= maxParticles || OriginalIndices[i] == 0xffffffffU) return;
    Particle p = SortedParticles[i];
    float3 correction = 0;
    int3 cell = GetCell(p.position);
    float rest = RestDensity(p.type);
    [loop] for (int z=-1; z<=1; ++z) [loop] for (int y=-1; y<=1; ++y) [loop] for (int x=-1; x<=1; ++x) {
        int3 query = cell + int3(x,y,z);
        uint h = GetGridHash(query);
        uint end = min(GridOffset[h]+GridCount[h], maxParticles);
        [loop] for (uint j=GridOffset[h]; j<end; ++j) {
            if (i==j) continue;
            Particle q = SortedParticles[j];
            float3 d = p.position-q.position;
            if (dot(d,d)>=H2) continue;
            if (any(GetCell(q.position)!=query) || !CanFluidsInteract(p.type,q.type)) continue;
            // A small artificial pressure discourages tensile pairing without
            // turning dilute particles into large repulsive bubbles.
            float ratio = Kernel(d) / Kernel(float3(0.12f,0,0));
            float scorr = -0.0001f * ratio*ratio*ratio*ratio;
            correction += (p.pressure+q.pressure+scorr) * KernelGradient(d) / rest;
        }
    }
    correction *= min(1.0f, 0.04f / max(length(correction),1e-6f));
    SolverOutput[i].position = correction;
}

float3 ProjectCollisions(float3 position) {
    position.y = max(position.y, 0.2f);
    [loop] for (uint k=0; k<min(aabbCount,128U); ++k) {
        float3 lo=AABBs[k].min-0.2f, hi=AABBs[k].max+0.2f;
        if (all(position>lo) && all(position<hi)) {
            float3 a=position-lo, b=hi-position;
            float3 distances=min(a,b);
            if (distances.x <= distances.y && distances.x <= distances.z)
                position.x = a.x < b.x ? lo.x : hi.x;
            else if (distances.y <= distances.z)
                position.y = a.y < b.y ? lo.y : hi.y;
            else position.z = a.z < b.z ? lo.z : hi.z;
        }
    }
    return position;
}

[numthreads(64, 1, 1)]
void ApplyDeltaP(uint3 id : SV_DispatchThreadID) {
    uint i=id.x;
    if (i>=maxParticles || OriginalIndices[i]==0xffffffffU) return;
    Particle p=SortedParticles[i];
    if (p.position.y < -500 || p.color.a < 0.01f) return;
    p.position=ProjectCollisions(p.position+SolverOutput[i].position);
    Particles[OriginalIndices[i]]=p;
}

[numthreads(64, 1, 1)]
void UpdateVelocity(uint3 id : SV_DispatchThreadID) {
    uint i=id.x;
    if (i>=maxParticles || OriginalIndices[i]==0xffffffffU) return;
    Particle p=SortedParticles[i];
    float3 velocity=p.velocity, sum=0;
    float weightSum=0;
    int3 cell=GetCell(p.position);
    [loop] for (int z=-1; z<=1; ++z) [loop] for (int y=-1; y<=1; ++y) [loop] for (int x=-1; x<=1; ++x) {
        int3 query=cell+int3(x,y,z);
        uint h=GetGridHash(query), end=min(GridOffset[h]+GridCount[h],maxParticles);
        [loop] for(uint j=GridOffset[h]; j<end; ++j) {
            if (j==i) continue;
            Particle q=SortedParticles[j];
            float3 d=p.position-q.position;
            if (dot(d,d)>=H2) continue;
            if (any(GetCell(q.position)!=query) || !CanFluidsInteract(p.type,q.type)) continue;
            float w=Kernel(d);
            sum += q.velocity*w;
            weightSum += w;
        }
    }
    bool water=p.type>0.5f && p.type<1.5f;
    float blend=1.0f-exp(-(water ? 2.0f : 40.0f)*dt);
    p.velocity=weightSum>1e-6f ? lerp(velocity,sum/weightSum,blend) : velocity;
    SolverOutput[i]=p;
}
