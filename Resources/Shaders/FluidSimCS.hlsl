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
    uint aabbCount; float3 pad6;
    
    // デコイ用
    float3 decoyPos; float decoyAttraction;
    float3 decoyScale; float pad7;
    float3 decoyForward; float pad8;
}

#include "FluidMorph.hlsli"

// 疑似乱数ジェネレーター
float hash(uint n) {
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    return float(n & uint(0x7fffffffU)) / float(0x7fffffff);
}

// 消火栓の水が面に当たったとき。衝突で法線方向の勢いが、面に沿って扇形に
// 広がる薄い膜になり (かすめるときは狭い前向きの扇、正面からは円全体)、
// しずくの冠を跳ね上げる。
// すでに膜になっている水は、薄膜の抵抗を受けて滑るだけ。
float3 HydrantImpact(float3 velocity,float3 normal,uint seed,float step){
    float normalSpeed=-dot(velocity,normal);
    float3 tangent=velocity+normal*normalSpeed;
    float sliding=length(tangent);
    if(normalSpeed<3)return tangent*pow(.92f,step*60);
    float3 dir=sliding>.01f?tangent/sliding:normalize(cross(normal,abs(normal.y)<.9f?float3(0,1,0):float3(1,0,0)));
    float headOn=normalSpeed/(normalSpeed+sliding);
    float angle=(hash(seed)*2-1)*lerp(.5f,3.14159f,saturate(headOn*1.6f));
    float3 spread=dir*cos(angle)+cross(normal,dir)*sin(angle);
    float3 result=spread*(sliding*.4f+normalSpeed*.35f)*(.45f+.55f*hash(seed+1U));
    if(hash(seed+2U)<.2f)result+=normal*(normalSpeed*(.15f+.25f*hash(seed+3U))+1.5f);
    return result;
}

// 坂の町の放水の地面の高さの表 (AABBs[aabbCount] が見出し: min = (原点 x, 原点 z, 1 マスの幅)、pad0 = 1 辺のマスの数。
// その後ろに高さを 8 個ずつ詰めてある)。高さはマスの中心の値で、その間は直線で補う。slope は高さの傾き
float HydrantTerrainCell(uint n,uint x,uint z){
    uint index=z*n+x;AABB e=AABBs[aabbCount+1+index/8];uint c=index%8;
    return c==0?e.min.x:c==1?e.min.y:c==2?e.min.z:c==3?e.pad0:c==4?e.max.x:c==5?e.max.y:c==6?e.max.z:e.pad1;
}
bool HydrantTerrain(float3 p,out float height,out float2 slope){
    height=0;slope=0;AABB head=AABBs[aabbCount];uint n=(uint)head.pad0;float cell=max(head.min.z,1e-3f);
    float2 g=(p.xz-head.min.xy)/cell-.5f;
    if(n<2||g.x<0||g.y<0||g.x>=float(n-1)||g.y>=float(n-1))return false;
    uint2 c=(uint2)g;float2 f=g-float2(c);
    float h00=HydrantTerrainCell(n,c.x,c.y),h10=HydrantTerrainCell(n,c.x+1,c.y),h01=HydrantTerrainCell(n,c.x,c.y+1),h11=HydrantTerrainCell(n,c.x+1,c.y+1);
    height=lerp(lerp(h00,h10,f.x),lerp(h01,h11,f.x),f.y);
    slope=float2(lerp(h10-h00,h11-h01,f.y),lerp(h01-h00,h11-h10,f.x))/cell;
    return true;
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
            
            // 位置をばらけさせる (超重要: 同一座標に重なると圧力が爆発する)
            float3 randVec = float3(rx, ry, rz);
            if (isSlime && length(randVec) > 0.01f) {
                // 密度の射影の前に、スライムを球の中に広げる。
                float rDist = pow(hash(i * 999), 0.3333f);
                randVec = normalize(randVec) * rDist;
                if (emitType<0.5f) {
                    // 立体角を一様にする: ランダムな立方体の点を正規化すると、
                    // 静止時の分布が対角方向に偏る。
                    float polar=1.0f-2.0f*hash(i*456U+19U);
                    float azimuth=6.283185307f*hash(i*123U+71U);
                    float ring=sqrt(max(0.0f,1-polar*polar));
                    randVec=float3(ring*cos(azimuth),polar,ring*sin(azimuth))*rDist;
                }
            }
            
            // プレイヤーの粒子数の 1.5 倍で、静止時の体積もおよそ 1.5 倍。
            float offsetScale = emitType<0.5f ? PLAYER_REST_RADIUS : (isSlime ? 1.5f : 0.4f);
            float3 localOffset = randVec * offsetScale;
            p.position = emitPos + localOffset;
            
            // 速度もランダムに
            float velScale = isSlime ? 5.0f : 2.0f;
            p.velocity = emitDir * 5.0f + randVec * velScale;
            if (!isSlime) {
                // 密集したランダムな球ではなく、粒子を並べた流れの板を放出する。個数を増やすと
                // 重なりや圧力が増えるのではなく、ノズルが広がる。
                float3 flow=emitDir*5.0f;
                float speed=length(flow);
                float3 along=speed>0.01f ? flow/speed : float3(0,-1,0);
                float3 across=normalize(cross(along,abs(along.y)<0.9f ? float3(0,1,0) : float3(1,0,0)));
                float3 tangent=cross(along,across);
                float slabLength=max(speed*max(dt,1e-4f),0.01f);
                uint side=max(1U,(uint)ceil(sqrt((float)emitCount*0.20f/slabLength)));
                uint perLayer=side*side;
                uint slot=emitCursor-emitStartIndex+localIndex,lane=slot%perLayer;
                // 粒子あたりの面積を保ったひまわり状の配置で、流れの断面を円盤にする。
                // 層ごとに回転させ、噴流に沿って渦巻きの腕が見えないようにする。
                float discRadius=(float)side*0.20f*0.5641896f;
                if(emitType>1.05f&&emitType<1.15f&&pad6.y>0)discRadius=pad6.y;
                float angle=(float)lane*2.399963f+(float)(slot/perLayer)*1.618034f;
                float2 plane=float2(cos(angle),sin(angle))*discRadius*sqrt(((float)lane+0.5f)/(float)perLayer);
                float travel=(emitCount-localIndex-0.5f)*slabLength/emitCount;
                // 消火栓のまとまりは、このフレームの積分より前に放出する。
                // 時刻を前にずらし、最新の水がノズルで終わるようにする
                // (手前に 1 フレーム分の空白を残さない)。
                if(emitType>1.05f&&emitType<1.25f)travel-=slabLength;
                p.position=emitPos+across*plane.x+tangent*plane.y+along*travel;
                p.velocity=flow;
            }
            p.color = emitColor;
            // インクの噴流は効果用のプールを使うが、材質はプレイヤーと共有する。
            // 粒子を 3x3 に並べたリボンは、離れる前に体と重なる。
            if(emitType>=4.0f){
                float3 along=normalize(emitDir+float3(0,0,1e-6f));
                float3 across=normalize(cross(along,abs(along.y)<.9f?float3(0,1,0):float3(1,0,0)));
                float3 up=cross(along,across);
                float axial=1-2*(localIndex+.5f)/emitCount,angle=localIndex*2.399963f;
                // pad6.y > 0 はしずくの大きさ (大きくなったスライムは比例して大きな液体を投げる)。
                // pad6.y < 0 は大きさ -pad6.y のしずくを正確に投げる: 速さのばらつきが無いので、CPU が
                // 予測した場所に落ちる (スライムの縄張りのインクがその場所と時刻に床へ印を付ける)。
                float size=pad6.y!=0?abs(pad6.y):1;
                float radius=sqrt(max(0,1-axial*axial))*.32f*size;
                p.position=emitPos+along*(axial*.8f*size)+across*(cos(angle)*radius)+up*(sin(angle)*radius);
                p.velocity=emitDir*5.0f;
                // 大きさ指定のしずくには粒子ごとに速さのばらつきを付け、流れが 1 つの固いカプセルとして
                // 飛ばずに、別々のしずくに分かれるようにする。
                if(pad6.y>0)p.velocity+=float3(rx,ry*.6f,rz)*(length(emitDir)*5.0f*.24f);
            }
            p.type = emitType;
            
            // 物理初期値
            p.density = 1.0f;
            p.pressure = 0.0f;
            
            if (emitType < 0.5f || isLostSlime) {
                p.pad = float3(0, 0, 0);
            } else if (isSlime) {
                // 分身は自分の形合わせのために静止時の位置を保つ。
                p.pad = localOffset; 
            } else {
                // 水飛沫の場合: タイマー初期化
                p.pad = float3(pad6.x>0.5f ? -1.0f : 0.0f, 0, 0);
                // 放物線を描くスライムは再構成のためにしずくの大きさを保つ (0 = 従来の決まった大きさ)。
                if(emitType>=4.0f&&emitType<8.0f)p.pad.y=abs(pad6.y);
                // 空中の消火栓の水は、接触でこの欄が支える面の高さに置き換わるまで
                // ノズルの幅を入れておく。
                if(emitType>1.05f&&emitType<1.25f){
                    p.pad.y=pad6.y;
                    // 霧のもや用の描画専用の種を、赤の 1000 分の 1 の桁に入れる。
                    // 水の色はこの欄ではなく相 (phase) の色から決まる。
                    p.color.r=floor(emitColor.r*1000.0f)/1000.0f+hash(i*313U+7U)*0.00099f;
                }
            }
            
            Particles[i] = p;
        }
    }
}

// ==========================================
// PBF の基本カーネルの調整 (粒子の質量 1)。変形の持ち主は世界の距離を
// この座標に換算する。従来の流体は世界の単位をそのまま使う。
// ==========================================
static const float SMOOTHING_RADIUS = 0.4f; // 近傍探索と圧力計算に使う半径 (ソルバー座標)
static const float PARTICLE_MASS = 1.0f;
// 粒子の質量 1: 0.2 間隔の格子の密度は 3 ではなく約 125 (自分の分は約 24.48)。
static const float REST_DENSITY = 125.0f;
static const float SLIME_SURFACE_TENSION = 18.0f;
static const float WATER_SURFACE_TENSION = 4.0f;
static const float GRAVITY = -20.0f;
static const float PI = 3.1415926535f;
static const uint NUM_GRID_CELLS = 65536;

// 定数の事前計算 (演算負荷の削減)
static const float H2 = 0.16f; // SMOOTHING_RADIUS^2
static const float H_POWER_6 = 0.004096f; // SMOOTHING_RADIUS^6
static const float H_POWER_9 = 0.000262144f; // SMOOTHING_RADIUS^9
static const float POLY6_COEFF = 315.0f / (64.0f * PI * H_POWER_9);
static const float SPIKY_COEFF = 45.0f / (PI * H_POWER_6);

float RestDensity(float type,uint original) {
    // 変形の密度 / 圧力は粒子間隔の座標で解く。世界空間の静止密度だと、
    // 粒子 1 個の自分の寄与を下回ることがある。
    if(type<.5f&&pad8>.5f)return 275.f;
    // 突撃の質量で変わるのは動いている粒子の数で、粒子 1 つの質量ではない。
    if(type<0.5f && coreMode>=4.0f) return FluidRestDensity(type);
    // 基準の質量を超えたら、粒子の予算は同じまま体積を大きくする。
    float volume = (type < 0.5f && coreMode >= 2.0f) ? (coreMode>=4.0f?clamp(coreFlowSpeed/100.0f,.05f,2.0f):max(1.0f, coreFlowSpeed / 100.0f)) : 1.0f;
    return FluidRestDensity(type) / volume;
}
float ParticleSpacing(float type,uint original){
    return type<.5f&&pad8>.5f?pow(275.f/max(MorphTargets[MorphData[8192+original].target].density,1e-6f),1.f/3.f):1.f;
}
float3 ChronoTransportVelocity() {
    if(coreMode>=4.0f) return pad3*float3(.9f,lerp(.9f,.65f,saturate(coreScale.y)),.9f);
    // 通常の歩きでは、従来の速度追従の力をそのまま使う。
    // 毎秒 30 単位のソルバーが追いつけない高速の分だけを運ぶ。
    return pad3 * saturate((length(pad3) - 20.0f) / 45.0f);
}
bool IsTetherParticle(uint index) {
    // 決まった割り当て: 質量が増減しても腕全体を割り当て直してはいけない。
    return coreMode >= 2.0f && index % 20U >= 13U && index < (uint)pad6.y;
}
// 柔らかい閉じ込め場はシミュレーション上の位置に働く。粒子に表面の枠は割り当てない:
// 圧力・粘性・衝突が自由に配り直せるようにしておく。
float3 SlimeSupportOrigin() {
    return corePos+float3(0,-1.25f+.8f*saturate(coreScale.y),0);
}
float3 SlimeFieldRadii() {
    float scale=pow(clamp(coreFlowSpeed/100.0f,.05f,2.0f),1.0f/3.0f);
    // どちらの姿勢でも体積は同じ。6600 粒子 / 静止密度 275 ≒ 24。
    float3 radii=lerp(coreMode>=5.0f?float3(4.2f,.66f,4.2f):float3(2.55f,1.8f,2.55f),float3(1.8f,1.8f,1.8f),saturate(coreScale.y));
    float aspect=coreScale.x>.1f?clamp(coreScale.x,.55f,1.6f):1.f;
    return radii*float3(rsqrt(aspect),aspect,rsqrt(aspect))*scale;
}
float3 SlimeMotionAxis(){return normalize(float3(coreForward.x,0,coreForward.z)+float3(0,0,1e-6f));}
float SlimeGroundHeight(float3 position){return emitDir.y+dot(position.xz-corePos.xz,emitDir.xz);}
// スライムの粒子の従来の安全な床 (y=0.2)。プレイヤーのスライム (coreMode>=4) は y=0 より下に立てるので、
// その床が自分の地面 (SlimeGroundHeight) より上に来ることはない。地面が 0 以上なら変わらない。
float SlimeFloor(float3 position,float particleType){
    if(coreMode>=4&&particleType<.5f&&emitStartIndex!=0)return min(.2f,SlimeGroundHeight(position)+.18f);
    return .2f;}
float SlimeMotionAmount(){return saturate(coreScale.z)*(1-.75f*saturate(coreScale.y));}
float3 SlimeToField(float3 local){
    local.y-=dot(local.xz,emitDir.xz)*(1-saturate(coreScale.y));
    float3 forward=SlimeMotionAxis(),side=float3(forward.z,0,-forward.x);
    float motion=SlimeMotionAmount(),stretch=1+.55f*motion;
    float along=dot(local,forward)/stretch;
    float width=1+motion*.26f*clamp(along/max(SlimeFieldRadii().z,.1f),-1.f,1.f);
    // 進む前面は広く、後ろは細くすぼめて引きずる。横方向に絞って
    // 軸方向の伸びを打ち消しつつ、粒子は場の中を流れられる。
    return float3(dot(local,side)*sqrt(stretch)/width,local.y*sqrt(stretch)/width,along);
}
float3 SlimeFromField(float3 local){
    float3 forward=SlimeMotionAxis(),side=float3(forward.z,0,-forward.x);
    float motion=SlimeMotionAmount(),stretch=1+.55f*motion;
    float width=1+motion*.26f*clamp(local.z/max(SlimeFieldRadii().z,.1f),-1.f,1.f);
    float3 world=side*(local.x*width/sqrt(stretch))+float3(0,local.y*width/sqrt(stretch),0)+forward*(local.z*stretch);
    world.y+=dot(world.xz,emitDir.xz)*(1-saturate(coreScale.y));return world;
}
float TetherParameter(uint index) {
    uint lane = (index / 20U) * 7U + index % 20U - 13U;
    return frac((lane + 0.5f) * 0.61803398875f);
}
float3 TetherGuide(uint index) {
    if (!IsTetherParticle(index)) return 0;
    float t = TetherParameter(index);
    float3 span = (emitPos - corePos) * emitType;
    float sag = min(length(span) * 0.035f, 0.45f) * sin(t * PI);
    return span * t - float3(0, sag, 0);
}
bool TetherControlled(uint index) { return IsTetherParticle(index) && emitType > 0.001f; }
float TetherShapeWeight() { return smoothstep(1.5f, 6.0f, length(emitPos - corePos) * emitType); }
float3 TetherGoal(uint index, float3 rest) {
    float3 span = (emitPos - corePos) * emitType;
    float distance = length(span), t = TetherParameter(index);
    float3 axis = distance > 1e-5f ? span / distance : float3(0,0,1);
    float3 across = normalize(cross(axis, abs(axis.y) < 0.9f ? float3(0,1,0) : float3(1,0,0)));
    float angle = hash(index * 123U + 71U) * 2 * PI;
    float massScale = pow(max(0.1f, coreFlowSpeed / 100.0f), 1.0f / 3.0f);
    float radius = lerp(0.70f, 0.32f, t) * massScale * sqrt(hash(index * 999U + 3U));
    float3 section = (across * cos(angle) + cross(axis, across) * sin(angle)) * radius;
    // 材質の目標は絶対的なもの。ソルバーの出力を逆に伸ばしたものでは決してない。
    // 手を縮めると、同じ液体が取り込んだ体の形へ混ざって戻る。
    return corePos + lerp(rest, TetherGuide(index) + section, smoothstep(0.0f, 6.0f, distance));
}
float3 ConstrainTether(float3 position, uint index, float3 rest) {
    if (!TetherControlled(index)) return position;
    float3 goal = TetherGoal(index, rest), residual = position - goal;
    float distance = length(residual);
    float3 constrained = goal + residual * min(1.0f, 0.12f / max(distance, 1e-5f));
    return lerp(position, constrained, TetherShapeWeight());
}
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
    // プレイヤーと、そこから離れた塊は同じ材質。分身と水は
    // 別の相 (phase) なので、密度・圧力・粘性を共有してはいけない。
    if(type>=4.0f)return 3U; // 独立した投射物の物理。
    if (type < 0.5f || (type > 2.5f && type < 3.5f)) return 0U;
    if (type > 1.5f && type < 2.5f) return 1U;
    return 2U;
}

bool CanFluidsInteract(float typeA, float typeB) {
    return GetFluidPhase(typeA) == GetFluidPhase(typeB);
}
bool CanParticlePairInteract(uint i,uint j,float a,float b){
    if(!CanFluidsInteract(a,b))return false;
    if(pad8>.5f&&a<.5f&&b<.5f)return MorphData[8192+OriginalIndices[i]].owner==MorphData[8192+OriginalIndices[j]].owner;
    return true;
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

int3 GetCell(float3 pos,float spacing) {
    return int3(floor(pos / (SMOOTHING_RADIUS*spacing)));
}
uint GridSalt(float type,uint original){
    return pad8>.5f&&type<.5f?(MorphData[8192+original].owner+1)*2654435761U:0;
}
uint GetGridHash(int3 cell,uint salt) {
    uint h = (uint(cell.x) * 73856093U) ^ (uint(cell.y) * 19349663U) ^ (uint(cell.z) * 83492791U);
    return (h^salt) & (pad8>.5f?8191U:NUM_GRID_CELLS-1);
}

// ==========================================
// 初期化パス: 粒子を初期化する (1 回だけ)
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
    
    // ゴーストパーティクルの計算を防ぐため、ソート先バッファも安全な値で初期化
    SortedParticles[i] = Particles[i];
}

// ==========================================
// パス 0.0: 元の番号を消す
// ==========================================
[numthreads(64, 1, 1)]
void ClearOriginalIndices(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    OriginalIndices[i] = 0xFFFFFFFF;
}

// ==========================================
// パス 0.1: 格子ごとの個数を消す
// ==========================================
[numthreads(64, 1, 1)]
void ClearGridCount(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= NUM_GRID_CELLS) return;
    GridCount[i] = 0;
}

// ==========================================
// パス 0.2: 粒子を数える
// ==========================================
[numthreads(64, 1, 1)]
void CountParticles(uint3 DTid : SV_DispatchThreadID) {
    uint i = DTid.x;
    if (i >= maxParticles) return;
    
    Particle p = Particles[i];
    if (p.color.a < 0.01f || p.position.y < -500.0f) return;
    
    int3 cell = GetCell(p.position,ParticleSpacing(p.type,i));
    uint gHash = GetGridHash(cell,GridSalt(p.type,i));
    InterlockedAdd(GridCount[gHash], 1);
}

// ==========================================
// パス 0.3: 累積和
// ==========================================
groupshared uint sharedTotal[256];

[numthreads(256, 1, 1)]
void PrefixSum(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex) {
    // 変形の経路は粒子の予算が 6600 に決まっている。詰めたテーブルにすれば、
    // PBF の反復 / サブステップのたびに、ほとんど空の 65536 個のバケットを調べずに済む。
    // (ホースの粒子 15k で計測: 32768 バケットのテーブルは全体では遅かった。
    //  この 1 グループでの走査は毎フレーム約 10 回走り、衝突が増える分より重い。)
    uint elementsPerThread = (pad8>.5f?8192U:NUM_GRID_CELLS) / 256;
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
// パス 0.4: 粒子を並べ替える
// ==========================================
void SortParticle(uint i, bool prepareVelocity) {
    if (i >= maxParticles) return;
    
    Particle p = Particles[i];
    if (p.color.a < 0.01f || p.position.y < -500.0f) return;
    
    int3 cell = GetCell(p.position,ParticleSpacing(p.type,i));
    uint gHash = GetGridHash(cell,GridSalt(p.type,i));
    
    uint localOffset;
    InterlockedAdd(GridCount[gHash], 1, localOffset);
    
    uint destIdx = GridOffset[gHash] + localOffset;
    if (destIdx >= maxParticles) return;
    if (prepareVelocity && p.type<4.0f) {
        float3 v = (p.position - PreviousPositions[i].xyz) / max(dt, 1e-6f);
        p.velocity = v * min(1.0f, MorphSpeedLimit(i,p.type) / max(length(v), 1e-6f));
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
    
    // 未ソート（無効）なスロットなら計算スキップ（ゴーストパーティクルの計算防止）
    if (OriginalIndices[i] == 0xFFFFFFFF) return;
    
    // 未放出・未初期化のパーティクルは地底に飛ばして計算から完全除外
    if (SortedParticles[i].color.a < 0.01f || SortedParticles[i].position.y < -500.0f) {
        SortedParticles[i].position = float3(0, -1000.0f, 0);
        SortedParticles[i].density = 0.01f;
        SortedParticles[i].pressure = 0.0f;
        return;
    }
    
    if(SortedParticles[i].type>=8.0f){SortedParticles[i].density=275;SortedParticles[i].pressure=0;return;}
    float3 pos_i = SortedParticles[i].position;
    float type_i = SortedParticles[i].type;
    
    float density = 0.0f;
    float3 gradientSum = 0;
    float3 surfaceGradientSum = 0;
    float gradientSquaredSum = 0;
    float rest = RestDensity(type_i,OriginalIndices[i]);
    float spacing=ParticleSpacing(type_i,OriginalIndices[i]);
    
    // 空間ハッシュグリッドで近傍のパーティクルのみを探索
    int3 cell = GetCell(pos_i,spacing);uint salt=GridSalt(type_i,OriginalIndices[i]);
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                uint gHash = GetGridHash(cell + int3(x, y, z),salt);
                uint startIdx = GridOffset[gHash];
                if (startIdx >= maxParticles) continue;
                uint count = GridCount[gHash];
                
                for(uint k = 0; k < count; k++) {
                    uint j = startIdx + k;
                    if (j >= maxParticles) break;
                    if (SortedParticles[j].position.y >= -500.0f) {
                        float3 diff = (pos_i - SortedParticles[j].position)/spacing;
                        float r2 = dot(diff, diff);
                        if (r2 < H2) {
                            // 調べる立方体はカーネルの球よりずっと大きい。
                            // ハッシュの当たりを見分けるための floor / 割り算より前に、
                            // 遠い候補を捨てる。
                            if (!CanParticlePairInteract(i,j,type_i, SortedParticles[j].type)) continue;
                            if (any(GetCell(SortedParticles[j].position,spacing) != cell + int3(x,y,z))) continue;
                            float w = H2 - r2;
                            density += PARTICLE_MASS * POLY6_COEFF * w * w * w;
                            float3 gradient = KernelGradient(diff) / rest;
                            gradientSum += gradient;
                            gradientSquaredSum += dot(gradient, gradient);
                            if (i != j) {
                                // 圧力計算と同じ近傍走査で、表面張力に使う勾配も集計する。
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
    // PBF の λ。自由表面では圧縮だけにする: 隣が足りない分の密度を満たすために、
    // 孤立した粒子を引き寄せて不自然な塊にしない。
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
    
    // 未ソート（無効）なスロットなら計算スキップ（ゴーストパーティクルの計算防止）
    if (OriginalIndices[i] == 0xFFFFFFFF) return;
    
    Particle pi = SortedParticles[i];
    if(pad7>.5f && OriginalIndices[i]>=uint(pad6.z)){SolverOutput[i]=pi;return;}
    if (pi.position.y < -500.0f) return;
    
    if(pi.type>=8.0f){SolverOutput[i]=pi;return;} // 床に貼り付いたインクは動かさない (放物線を描くしぶきではない)。
    if(pi.type>=4.0f){
        // まとまった独立のインクの体積。CPU 側で計算した放物線の経路に合わせる。
        pi.position+=pi.velocity*dt+float3(0,-9*dt*dt,0);
        pi.velocity.y-=18*dt;pi.pad.x+=dt;
        float life=max(.02f,(pi.type-4)*10);
        // 大きさ指定のスライムのしずくが着地する: 変形の地面で止まり、平たく広がり
        // (pad.z が着地した印)、少し後に消えていく。
        if(pi.pad.y>0&&pad8>.5f){float ground=MorphTargets[0].material.w+.12f*pi.pad.y;
            if(pi.position.y<ground&&pi.velocity.y<0){pi.position.y=ground;pi.velocity=float3(pi.velocity.x*.15f,0,pi.velocity.z*.15f);
                if(pi.pad.z<.5f){pi.pad.z=1;pi.pad.x=max(pi.pad.x,life-.18f);}}}
        if(pi.pad.x>=life){pi.color.a=0;pi.position.y=-1000;}
        SolverOutput[i]=pi;return;
    }
    // 密度のパスで集計した spiky の勾配から、
    // 重なった粒子の分離も含む表面張力を求める。
    bool isSlimeType = (pi.type < 0.5f || (pi.type > 1.5f && pi.type < 3.5f));
    float surfaceTension = pi.type < 0.5f ? 50.0f :
        (isSlimeType ? SLIME_SURFACE_TENSION : WATER_SURFACE_TENSION);
    if(pad8>.5f&&pi.type<.5f)surfaceTension=MorphTargets[MorphData[8192+OriginalIndices[i]].target].material.z;
    float3 force = SolverOutput[i].position * (surfaceTension / (SPIKY_COEFF * H2))*ParticleSpacing(pi.type,OriginalIndices[i]);

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
    
    // コントローラーは中心を動かす。プレイヤーの粒子は自分の位置を保つ。
    // 密度の射影と衝突が、占める体積と底を決める。
    bool isCurrentLiquefiedPlayer = pi.type < 0.5f && coreMode > 0.5f && coreMode < 1.5f;
    if (pi.type < 0.5f && coreAttraction > 0.0f && pad8<.5f) {
        if(coreMode>=4.0f){
            float3 radii=SlimeFieldRadii();
            float3 local=pi.position-SlimeSupportOrigin();
            float3 field=SlimeToField(local);
            float extent=length(field/radii);
            float3 normal=normalize(SlimeFromField(field/(radii*radii))+float3(0,1e-6f,0));
            float air=saturate(coreScale.y);
            float3 confinement=-normal*min(100.0f,lerp(65.f,24.f,air)*max(extent-.86f,0.0f));
            // 重力の大半を打ち消して頭頂を支える。残った重力で底を
            // 濡らす。相対的な減衰で、動いた後の小さな波を残す。
            confinement.y+=lerp(18.f,20.f,air);
            float3 follow=pad3-ChronoTransportVelocity();
            force+=(confinement+(follow-pi.velocity)*lerp(5.f,2.8f,air))*pi.density;
        }else if (!isCurrentLiquefiedPlayer) {
            uint original = OriginalIndices[i];
            bool tether = TetherControlled(original);
            float3 guide = tether ? TetherGuide(original) : float3(0,0,0);
            float3 fromCore = pi.position - corePos - guide;
            float distanceToCore = length(fromCore);
            float3 radialDir = distanceToCore > 1e-4f ? fromCore / distanceToCore : float3(0.0f, 1.0f, 0.0f);
            float fieldDistance = distanceToCore;
            if (coreMode >= 2.0f) fieldDistance /= pow(max(1.0f, coreFlowSpeed / 100.0f), 1.0f/3.0f);
            // 攻撃は前方の場を開くだけ。粒子の割り当てはせず、コントローラーが
            // 向きを変えても体を回さない。
            if (coreScale.z > 2.0f) {
                float3 forwardDir = normalize(float3(coreForward.x, 0.0f, coreForward.z) + float3(0.0f, 0.0f, 1e-5f));
                float front = max(dot(fromCore, forwardDir), 0.0f);
                fieldDistance = length(fromCore - forwardDir * front * (1.0f - 1.0f / coreScale.z));
            }

            // --- 圧力: 本当に圧縮されたときだけ ---
            // 外へ押す力の大半は PBF が受け持つ。この項は強く圧縮されたときにそれを補うが、
            // 頭頂をくぼませるような常時の反発は
            // 生まない。
            float compression = max(pi.density / RestDensity(pi.type,OriginalIndices[i]) - 1.0f, 0.0f);
            float outwardPressure = min(coreAttraction * 0.5f * compression, 12.0f);

            // --- 凝集: 不感帯なしで常に働く ---
            // どの距離でも弱い引き寄せがあるので、縦の支えで粒子が
            // 持ち上がっても層が分かれない。
            // 1.55 を超えると硬さを急に上げて、体をまとまったままにする。
            float cohesionMag = min(3.0f * fieldDistance
                                  + 15.0f * max(fieldDistance - 1.55f, 0.0f), 45.0f);
            float3 coreForce = radialDir * (outwardPressure - cohesionMag);
            if (tether) {
                // 伸ばした液体は圧力と隣との相互作用を保つ。
                // 柔らかい場が中心線を保ち、固いメッシュは描かない。
                float pull = min(90.0f * max(distanceToCore - 0.35f, 0.0f), 120.0f);
                coreForce = radialDir * (outwardPressure - pull);
            }

            // --- 縦の支え: ドーム形に整える ---
            // Moderate baseline keeps floor particles grounded (net −6).
            // 頭頂にはドームができるだけの持ち上げを与える (差し引き +4)。
            // 常に働く凝集が、不感帯があると起きる分離を
            // 防ぐ。
            float aboveCore = saturate(fromCore.y / 2.1f);
            coreForce.y += tether ? 20.0f : 14.0f + 10.0f * aboveCore;
            // 本編の着地 / 空中の変形は力を変えるだけで、粒子を床の向こうへ
            // 瞬間移動させない。縦横比 1 なら通常の SPH のまま。
            if(coreMode>=2.0f && !tether && emitType<0.05f){
                float massScale=pow(clamp(coreFlowSpeed,1.0f,200.0f)/100.0f,1.0f/3.0f);
                float aspect=clamp(coreScale.y/max(massScale,0.01f),coreMode>=5.0f?.08f:.62f,1.18f);
                coreForce.y+=clamp((aspect-1.0f)*max(fromCore.y,0.0f)*90.0f,-48.0f,12.0f);
                coreForce.xz+=fromCore.xz*clamp((rsqrt(aspect)-1.0f)*25.0f,-4.0f,9.0f);
            }

            // どの面にも等しく追従させる。前後で強さが違うと、コントローラーが
            // 向きを反転したときに流体が壁のように積み上がる。
            float3 followVelocity = coreMode >= 2.0f ? pad3 - ChronoTransportVelocity() : pad3;
            float coreSpeed = length(followVelocity);
            float baseFollow = 1.2f;
            float distanceFollow = min(6.0f * saturate((fieldDistance - 1.8f) / 2.3f), 6.0f);
            float speedFollow = 0.2f * min(coreSpeed, 20.0f);
            float planarCoreSpeed = length(followVelocity.xz);
            float opposingSpeed = planarCoreSpeed > 0.5f
                ? max(-dot(pi.velocity.xz, followVelocity.xz) / planarCoreSpeed, 0.0f) : 0.0f;
            float follow = baseFollow + distanceFollow + speedFollow + 0.35f * min(opposingSpeed, 20.0f);
            float3 velocityFollow = (followVelocity - pi.velocity) * follow;
            // 速く動いたときに体が縦に流れないよう、縦の結び付きは
            // 少なくとも横と同じ強さにする。
            velocityFollow.y *= max(1.0f, 2.0f * aboveCore);

            force += (coreForce + velocityFollow) * pi.density;
        } else {
            // 入るときの勢いで水たまりが一瞬広がる。コントローラーは固定点に
            // 留まるので、柔らかい外周を残して動きを消していく。
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
        // 分身は自分の静止時の形を使い続ける。
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
    
    // 発散防止のため加速度をクランプ
    float accLen = length(acceleration);
    if (accLen > 500.0f) {
        acceleration = (acceleration / accLen) * 500.0f;
    }
    
    // 外力 (重力)
    acceleration.y += pi.type>=4.0f ? -18.0f : GRAVITY;
    
    // 積分 (Symplectic Euler)
    if(pad8>.5f&&pi.type<.5f){
        pi.velocity=MorphIntegrate(OriginalIndices[i],pi.position,pi.velocity,acceleration,dt);
        MorphTarget visual=MorphTargets[MorphData[8192+OriginalIndices[i]].target];
        pi.color=visual.color;
        // 描画専用の持ち主・サンプル体積・形。通常のプレイヤー / 水は
        // 既存の pad の並びを保ち、変形のソルバーがそれを縛ることはない。
        pi.pad=float3(1000+visual.forward.y,visual.density,visual.shape.w);
    }else pi.velocity += acceleration * dt;
    
    // 発散防止のため速度をクランプ
    float speed = length(pi.velocity);
    float speedLimit=MorphSpeedLimit(OriginalIndices[i],pi.type);
    if (speed > speedLimit) {
        pi.velocity = (pi.velocity / speed) * speedLimit;
    }
    
    pi.position += pi.velocity * dt;
    pi.position = (pad8>.5f?pi.position:ConstrainTether(pi.position, OriginalIndices[i], pi.pad));
    
    // 静止時の形の安全な境界を持つのは分身だけ。
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
    // プレイヤーは地面に触れたまま、動く中心について行けなければならない。
    // 分身・離れたスライム・水は既存の摩擦のままにする。
    float friction = pow(isCurrentLiquefiedPlayer ? 0.92f :
        (pi.type < 0.5f ? 0.97f : (isSlimeTypeForFriction ? 0.6f : 0.98f)), dt * 60.0f);
    bool groundContact=false;
    bool hydrantWater=pi.type>1.05f&&pi.type<1.25f;
    float3 incoming=pi.velocity,hitNormal=0;
    // 消火栓の水は平らな街の通りに属する (PBF の射影も含む)。
    // 他の水はシーンの当たり判定に支えられ、段差から落ちられる。
    // (坂の町の放水: emitColor.w が 1 のとき、AABBs の箱と球の後ろにゲームが町の地面の高さの表を置く
    //  (建物と、まだ飲めない大きな物の高さも入れてある)。水はその上に落ちて坂を流れ下り、高い段や建物の壁で止まって跳ね返る。
    //  表の外へ出た水は消える。表が無ければ今まで通り高さ 0.22 の平らな床)
    if(hydrantWater&&emitColor.w>.5f&&pi.position.y>-500&&pi.color.a>0){
        float height;float2 slope;
        if(!HydrantTerrain(pi.position,height,slope)){pi.color.a=0;pi.position.y=-1000;}
        else if(pi.position.y<height+.22f){
            // 前の位置から大きな段を上がっていれば壁: 横へ跳ね返す (段の上へ持ち上げない)
            float3 before=pi.position-pi.velocity*dt;float lower;float2 lowerSlope;
            if(HydrantTerrain(before,lower,lowerSlope)&&height-lower>1.2f&&before.y<height-.4f){
                float2 away=normalize(-slope+float2(1e-5f,0));pi.position.xz=before.xz;pi.position.y=max(pi.position.y,lower+.22f);
                hitNormal=float3(away.x,0,away.y);pi.velocity.xz*=-.25f;
            }else{
                groundContact=true;pi.position.y=height+.22f;hitNormal=normalize(float3(-slope.x,1,-slope.y));
                float into=dot(pi.velocity,hitNormal);if(into<0)pi.velocity-=hitNormal*into;pi.velocity*=friction;
            }
        }
    } else if(hydrantWater&&pi.position.y<.22f&&pi.position.y>-500&&pi.color.a>0){
        groundContact=true;pi.position.y=.22f;pi.velocity.y=abs(pi.velocity.y)*.12f;pi.velocity.xz*=friction;hitNormal=float3(0,1,0);
    }
    
    // スライムは従来の安全な床を保つ。水はシーンの当たり判定を使い、
    // 段差から離れて重力で落ちられるようにする。
    // プレイヤーのスライムの床は自分の地面 (SlimeFloor) に従う: y=0 より下 (坂の街の川と
    // くぼみ) では、決まった 0.2 の床がサブステップごとに体とぶつかり合い、フレームが
    // 2.5 倍遅くなっていた。
    float safetyFloor=SlimeFloor(pi.position,pi.type);
    if (!FluidIsWater(pi.type) && pi.position.y < safetyFloor) {
        groundContact=true;
        pi.position.y = safetyFloor;
        pi.velocity.y *= -0.3f;
        pi.velocity.x *= friction; // 床の摩擦
        pi.velocity.z *= friction;
        
    }
    
    // 自由配置されたAABB（Cube等）との衝突判定
    uint safeAABBCount = min(aabbCount, 160U);
    for (uint k = 0; k < safeAABBCount; ++k) {
        float3 bmin = AABBs[k].min;
        float3 bmax = AABBs[k].max;
        if(AABBs[k].pad0>.5f){
            // 球の項目 (敵) がさえぎるのは消火栓の水だけ。
            if(!hydrantWater)continue;
            float3 centre=(bmin+bmax)*.5f,offset=pi.position-centre;
            float radius=(bmax.x-bmin.x)*.5f+.25f,distance=length(offset);
            if(distance<radius&&distance>1e-4f){hitNormal=offset/distance;pi.position=centre+hitNormal*radius;pi.pad.x=max(pi.pad.x,1e-3f);}
            continue;
        }
        // 上面の接触は連続した支えの平面が受け持つ。広げた塔の
        // 正面が、登っている粒子を何度も後ろへ蹴ってはいけない。
        if(coreMode>=4&&pi.type<.5f&&emitStartIndex!=0&&bmax.y<=SlimeGroundHeight(pi.position)+.4f)continue;
        
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
            pi.position.z > bmin.z - pRadius && pi.position.z < bmax.z + pRadius) {
            // どの面に最も近いかを計算して押し出す
            float dLeft = pi.position.x - (bmin.x - pRadius);
            float dRight = (bmax.x + pRadius) - pi.position.x;
            float dBottom = pi.position.y - (bmin.y - pRadius);
            float dTop = (bmax.y + pRadius) - pi.position.y;
            float dBack = pi.position.z - (bmin.z - pRadius);
            float dFront = (bmax.z + pRadius) - pi.position.z;
            
            float minDist = min(min(min(dLeft, dRight), min(dBottom, dTop)), min(dBack, dFront));
            
            if (minDist == dTop) {
                hitNormal=float3(0,1,0);
                pi.position.y = bmax.y + pRadius;
                pi.velocity.y *= -0.3f;
                pi.velocity.x *= friction;
                pi.velocity.z *= friction;
                groundContact=true;
            } else if (minDist == dBottom) {
                hitNormal=float3(0,-1,0);
                pi.position.y = bmin.y - pRadius;
                pi.velocity.y *= -0.3f;
                pi.velocity.x *= friction;
                pi.velocity.z *= friction;
            } else if (minDist == dLeft) {
                hitNormal=float3(-1,0,0);
                pi.position.x = bmin.x - pRadius;
                pi.velocity.x *= -0.3f;
                pi.velocity.y *= friction;
                pi.velocity.z *= friction;
            } else if (minDist == dRight) {
                hitNormal=float3(1,0,0);
                pi.position.x = bmax.x + pRadius;
                pi.velocity.x *= -0.3f;
                pi.velocity.y *= friction;
                pi.velocity.z *= friction;
            } else if (minDist == dBack) {
                hitNormal=float3(0,0,-1);
                pi.position.z = bmin.z - pRadius;
                pi.velocity.z *= -0.3f;
                pi.velocity.x *= friction;
                pi.velocity.y *= friction;
            } else if (minDist == dFront) {
                hitNormal=float3(0,0,1);
                pi.position.z = bmax.z + pRadius;
                pi.velocity.z *= -0.3f;
                pi.velocity.x *= friction;
                pi.velocity.y *= friction;
            }
        }
    }
    
    // 位置ベースの速度はサブステップの変位から求め直すので、速度を変えるだけでは
    // 捨てられてしまう。サブステップの始点を書き換え、求めた速度が
    // 衝突の反応になるようにする。
    if(hydrantWater&&dot(hitNormal,hitNormal)>0){
        uint seed=uint(frac(pi.color.r*1000)*1048576)*7U+(asuint(pi.position.x+pi.position.z)&1023U);
        pi.velocity=HydrantImpact(incoming,hitNormal,seed,dt);
        PreviousPositions[OriginalIndices[i]].xyz=pi.position-pi.velocity*dt;
    }

    // 水のメタデータ: 接触してからの時間・支えの高さ・最近の接触の猶予。
    // 床と AABB の両方に触れても、時間は 1 回だけ進める。
    if (FluidIsWater(pi.type)) {
        pi.pad.z=max(0,pi.pad.z-dt);
        if (groundContact) { pi.pad.y=pi.position.y; pi.pad.z=0.08f; }
        // 接触してからの時間が負なら、プレビューの粒子は寿命で消えない。
        // 支えの高さと接触の猶予は、表面の再構成のために更新を続ける。
        if (pi.pad.x>=0) {
            if (groundContact || pi.pad.x>0) pi.pad.x+=dt;
            if (pi.pad.x>WATER_GROUND_LIFETIME) { pi.color.a=0; pi.position.y=-1000; }
        }
    }
    // 液状化したデコイ（引力がなくなったデコイ）の消滅処理
    if (pi.type > 1.5f && pi.type < 2.5f && decoyAttraction <= 0.01f) {
        pi.color.a -= dt * 0.5f; // 約2秒かけてじわじわ消える
        if (pi.color.a <= 0.01f) {
            pi.position.y = -1000.0f;
        }
    }
    
    // プレイヤーへの引力で体を保持するため、固定の空間境界は設けない。
    if (pi.type > 2.5f && pi.type < 3.5f) {
        pi.pad.x += dt;
    }

    // この実行の間、すべての隣と元のグループの位置は変えない。
    // 予測は別の書き戻しのパスで反映する。
    SolverOutput[i] = pi;
}

// ==========================================
// パス 3: 書き戻し
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
    if (id.x >= maxParticles) return;
    if(pad8>.5f&&id.x<(uint)pad6.z){
        Particle p=Particles[id.x];
        if(p.color.a<=.01f||p.position.y < -500){MorphTarget t=MorphTargets[MorphData[8192+id.x].target];p.position=MorphWorld(t.local,t);p.velocity=0;p.color=t.color;p.type=0;Particles[id.x]=p;}
        else {MorphTarget t=MorphTargets[MorphData[8192+id.x].target];if(t.shape.w>.5f){p.position.y+=t.motion.y*lerp(.9f,.65f,t.motion.w)*dt;Particles[id.x]=p;}}
    }
    if (pad8<.5f && pad6.x > 0.5f && id.x < (uint)pad6.z) {
        Particle p = Particles[id.x];
        if (id.x >= (uint)pad6.y) { p.position=float3(0,-1000,0); p.color.a=0; }
        else {
            if (p.position.y < -500 || p.color.a <= 0) {
                float polar=1-2*hash(id.x*456U+19U), angle=6.2831853f*hash(id.x*123U+71U);
                float ring=sqrt(max(0,1-polar*polar));
                float3 sample=float3(ring*cos(angle),polar,ring*sin(angle))*pow(hash(id.x*999U),0.333333f);
                p.position=corePos+sample*PLAYER_REST_RADIUS;
                if(coreMode>=4.0f){sample.y=abs(sample.y);p.position=SlimeSupportOrigin()+sample*SlimeFieldRadii();}
                p.velocity=0;p.pad=0;
            } else p.position+=ChronoTransportVelocity()*dt;
            if (TetherControlled(id.x)) {
                if (dot(p.pad,p.pad) < 1e-10f) {
                    float3 rest = p.position - corePos;
                    // 速い連鎖の後に残った遠くの切れ端を、静止時の位置として取り込まない。
                    float limit = PLAYER_REST_RADIUS * pow(max(0.1f,coreFlowSpeed/100.0f),1.0f/3.0f);
                    p.pad = rest * min(1.0f, limit / max(length(rest),1e-5f)) + float3(0,0.0001f,0);
                }
                // 腕が体に戻ったら、PBF に配り直させる。動いている体の中に
                // 取り込んだ位置を固定すると、隣を何度も圧縮し、
                // 離したときに外へ打ち出していた。
                p.position = lerp(p.position, TetherGoal(id.x, p.pad), TetherShapeWeight());
                p.velocity *= exp(-24.0f * dt);
            } else p.pad = 0;
            p.type=0; p.color=float4(0.4f,0.8f,0.1f,1);
        }
        Particles[id.x]=p;
    }
    PreviousPositions[id.x] = float4(Particles[id.x].position, 1);
}

// ヤコビ法: この実行の間、λ と予測位置は読むだけ。
[numthreads(64, 1, 1)]
void CalcDeltaP(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= maxParticles || OriginalIndices[i] == 0xffffffffU) return;
    Particle p = SortedParticles[i];
    float3 correction = 0;
    float rest = RestDensity(p.type,OriginalIndices[i]);
    float spacing=ParticleSpacing(p.type,OriginalIndices[i]);
    int3 cell = GetCell(p.position,spacing);uint salt=GridSalt(p.type,OriginalIndices[i]);
    [loop] for (int z=-1; z<=1; ++z) [loop] for (int y=-1; y<=1; ++y) [loop] for (int x=-1; x<=1; ++x) {
        int3 query = cell + int3(x,y,z);
        uint h = GetGridHash(query,salt);
        uint end = min(GridOffset[h]+GridCount[h], maxParticles);
        [loop] for (uint j=GridOffset[h]; j<end; ++j) {
            if (i==j) continue;
            Particle q = SortedParticles[j];
            float3 d = (p.position-q.position)/spacing;
            if (dot(d,d)>=H2) continue;
            if (!CanParticlePairInteract(i,j,p.type,q.type)||any(GetCell(q.position,spacing)!=query)) continue;
            // 小さな人工的な圧力で引っ張りによる粒子の対を防ぎつつ、
            // 薄い所の粒子を大きな反発の泡にしない。
            float ratio = Kernel(d) / Kernel(float3(0.12f,0,0));
            float scorr = -0.0001f * ratio*ratio*ratio*ratio;
            correction += (p.pressure+q.pressure+scorr) * KernelGradient(d) / rest;
        }
    }
    correction *= min(1.0f, 0.04f / max(length(correction),1e-6f));
    SolverOutput[i].position = correction*spacing;
}

float3 ProjectCollisions(float3 position, float particleType) {
    if (!FluidIsWater(particleType)) position.y = max(position.y, SlimeFloor(position,particleType));
    // (坂の町の放水では地面の高さの表が床。平らな床 0.22 に持ち上げると川や窪みの上に水が浮く)
    if(particleType>1.05f&&particleType<1.25f&&emitColor.w<.5f)position.y=max(position.y,.22f);
    [loop] for (uint k=0; k<min(aabbCount,128U); ++k) {
        if(coreMode>=4&&particleType<.5f&&emitStartIndex!=0&&AABBs[k].max.y<=SlimeGroundHeight(position)+.4f)continue;
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
    if(pad8<.5f&&coreMode>=4&&particleType<.5f&&emitStartIndex!=0)position.y=max(position.y,SlimeGroundHeight(position)+.18f);
    return position;
}

[numthreads(64, 1, 1)]
void ApplyDeltaP(uint3 id : SV_DispatchThreadID) {
    uint i=id.x;
    if (i>=maxParticles || OriginalIndices[i]==0xffffffffU) return;
    Particle p=SortedParticles[i];
    if (p.position.y < -500 || p.color.a < 0.01f) return;
    if((pad7>.5f&&OriginalIndices[i]>=uint(pad6.z))||p.type>=4.0f){Particles[OriginalIndices[i]]=p;return;}
    float3 corrected = p.position+SolverOutput[i].position;
    if(pad8<.5f)corrected=ConstrainTether(corrected, OriginalIndices[i], p.pad);
    p.position=ProjectCollisions(corrected,p.type);
    // 坂の町の放水: 押し縮めない補正でも地面の高さの表の下へ潜らせない (CalcForce と同じく、大きな段は壁として横で止める)
    if(p.type>1.05f&&p.type<1.25f&&emitColor.w>.5f){float height,lower;float2 slope,lowerSlope;
        if(HydrantTerrain(p.position,height,slope)&&p.position.y<height+.22f){float3 before=PreviousPositions[OriginalIndices[i]].xyz;
            if(HydrantTerrain(before,lower,lowerSlope)&&height-lower>1.2f&&before.y<height-.4f)p.position.xz=before.xz;else p.position.y=height+.22f;}}
    if(pad8>.5f&&p.type<.5f){MorphTarget body=MorphTargets[MorphData[8192+OriginalIndices[i]].target];
        if(body.shape.w>.5f){
            float3 field=MorphField(MorphLocal(p.position,body),body);float extent=length(field/max(body.shape.xyz,.1f));
            if(extent>1)field=lerp(field,field/extent,lerp(.10f,.025f,body.motion.w));
            p.position=MorphWorld(MorphFromField(field,body),body);
        }
        // 跳んでいる体の下にも実際の地面はある。動く原点に合わせて押さえつけてはいけない。
        // (坂では傾いた地面。平らな所では今まで通りの高さ)
        p.position.y=max(p.position.y,MorphGround(p.position,body)+.15f);
    }
    if(coreMode>=4.0f&&p.type<.5f&&pad8<.5f){
        float3 origin=SlimeSupportOrigin(),local=p.position-origin;
        float3 field=SlimeToField(local);float extent=length(field/SlimeFieldRadii());
        // 柔らかい PBF の閉じ込めは、粒子を正確な楕円体に吸着させず、柔らかく動く
        // 表面を残す。最終的には衝突が決める。
        float air=saturate(coreScale.y);
        if(extent>1)local=lerp(local,SlimeFromField(field/extent),lerp(.14f,.025f,air));
        // 空中では支えの平面が無くなるので、下側が丸くなり、
        // 個々の粒子は着地まで遅れたり伸びたりできる。
        local.y=max(local.y,dot(local.xz,emitDir.xz)*(1-air)-SlimeFieldRadii().y*air);
        p.position=ProjectCollisions(origin+local,p.type);
    }
    Particles[OriginalIndices[i]]=p;
}

[numthreads(64, 1, 1)]
void UpdateVelocity(uint3 id : SV_DispatchThreadID) {
    uint i=id.x;
    if (i>=maxParticles || OriginalIndices[i]==0xffffffffU) return;
    Particle p=SortedParticles[i];
    if((pad7>.5f&&OriginalIndices[i]>=uint(pad6.z))||p.type>=4.0f){SolverOutput[i]=p;return;}
    float3 velocity=p.velocity, sum=0;
    float weightSum=0;
    float spacing=ParticleSpacing(p.type,OriginalIndices[i]);
    int3 cell=GetCell(p.position,spacing);uint salt=GridSalt(p.type,OriginalIndices[i]);
    [loop] for (int z=-1; z<=1; ++z) [loop] for (int y=-1; y<=1; ++y) [loop] for (int x=-1; x<=1; ++x) {
        int3 query=cell+int3(x,y,z);
        uint h=GetGridHash(query,salt), end=min(GridOffset[h]+GridCount[h],maxParticles);
        [loop] for(uint j=GridOffset[h]; j<end; ++j) {
            if (j==i) continue;
            Particle q=SortedParticles[j];
            float3 d=(p.position-q.position)/spacing;
            if (dot(d,d)>=H2) continue;
            if (!CanParticlePairInteract(i,j,p.type,q.type)||any(GetCell(q.position,spacing)!=query)) continue;
            float w=Kernel(d);
            sum += q.velocity*w;
            weightSum += w;
        }
    }
    bool water=p.type>0.5f && p.type<1.5f;
    float viscosity=water?2.f:40.f;
    if(pad8>.5f&&p.type<.5f)viscosity=MorphTargets[MorphData[8192+OriginalIndices[i]].target].material.y;
    float blend=1.0f-exp(-viscosity*dt);
    p.velocity=weightSum>1e-6f ? lerp(velocity,sum/weightSum,blend) : velocity;
    if (coreMode >= 2.0f && p.type < 0.5f && emitType > 0.001f) {
        float recovery = 1.0f - TetherShapeWeight();
        float3 follow = pad3 - ChronoTransportVelocity();
        float3 relative = p.velocity - follow;
        float speed = length(relative);
        p.velocity = lerp(p.velocity, follow + relative * min(1.0f, 8.0f / max(speed,1e-5f)), recovery);
    }
    SolverOutput[i]=p;
}
