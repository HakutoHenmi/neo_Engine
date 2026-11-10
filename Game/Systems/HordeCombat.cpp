// HordeCombat.cpp
// 群れの敵との戦闘 (ChronoSystem の一部)。数の決まった群れは共通のメッシュとインスタンス描画を使う。
// 群れの作成: 状態を消し、鎧のシェーダーとメッシュ (大きさの逆数と中心) を用意して、プレイヤーの周りに 72 体を出す
#include "ChronoSystemInternal.h"

namespace Game {
void ChronoSystem::BuildSwarm(entt::registry& r){
    // 群れ・エフェクト・出現と攻撃の時間を初期化する
    swarm_.clear();swarmEffects_.clear();swarmNextId_=1;swarmSpawnClock_=0;swarmSoundClock_=0;swarmAttackClock_=2.5f;swarmAttackBurst_=0;
    swarmAttackCursor_=0;swarmRestPending_=false;
    auto* renderer=Engine::Renderer::GetInstance();
    // 鎧と床のエフェクトのシェーダー
    renderer->CreateShaderPipeline("HordeArmor",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/HordeArmorPS.hlsl");
    renderer->CreateShaderPipelineTransparent("HordeEffect",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/HordeEffectPS.hlsl",true,true);
    // 鎧・くちばし・角のメッシュ
    const char* paths[]={"Resources/Models/Chrono/armor.obj","Resources/Models/Chrono/beak.obj","Resources/Models/Chrono/horn.obj"};
    // 各メッシュの範囲から中心と大きさの逆数を求める
    for(size_t i=0;i<swarmMeshes_.size();++i){auto& mesh=swarmMeshes_[i];mesh.handle=renderer->LoadObjMesh(paths[i]);
        V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& v:renderer->GetModel(mesh.handle)->GetData().vertices){
            lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
            hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};}
        mesh.center=(hi+lo)*.5f;V extent=hi-lo;mesh.inverseExtent={1/std::max(.001f,extent.x),1/std::max(.001f,extent.y),1/std::max(.001f,extent.z)};
    }
    // 床のエフェクト用の四角形
    swarmQuad_=renderer->CreateDynamicMesh({{{-1,0,-1,1},{0,0},{0,1,0}},{{1,0,-1,1},{1,0},{0,1,0}},{{1,0,1,1},{1,1},{0,1,0}},{{-1,0,1,1},{0,1},{0,1,0}}},{0,1,2,0,2,3});
    // プレイヤーの位置
    V player=Read(r.get<TransformComponent>(player_).translate);
    // 72 体の敵を作る (役割を決め、プレイヤーの周りに置き、プレイヤーの方を向く)
    for(int i=0;i<72;++i){
        SwarmEnemy enemy;enemy.id=swarmNextId_++;SwarmRole(enemy);
        enemy.at=SwarmSpawnPosition(player,enemy.id);enemy.at.y=InkGround(enemy.at)+(enemy.flying?18.f+float(enemy.id%7)*2:1.f);
        enemy.heading=Unit(player-enemy.at);swarm_.push_back(enemy);}
    // 出した数と生きている数
    auto& ink=r.get<InkPlayer>(player_);ink.swarmSpawned=int(swarm_.size());ink.swarmAlive=int(swarm_.size());
}
// 誘導弾の目標を選ぶ (輪の届く範囲だけか)
uint32_t ChronoSystem::ChooseSwarmTarget(entt::registry& r,V from,bool preferBoss,V origin,float reach){
    (void)preferBoss;
    // 群れのモードでなければ狙えない
    const auto& ink=r.get<InkPlayer>(player_);
    if(!ink.hordeEnabled)return SwarmNoTarget;
    // 届く範囲の指定があるか
    const bool ranged=reach<1e8f;
    // 輪から出た弾は輪の近くの敵を、それ以外の弾はプレイヤーの近くの敵を優先する
    V anchor=ranged?origin:Read(r.get<TransformComponent>(player_).translate);float best=1e9f;uint32_t target=SwarmNoTarget;
    // 生きている敵を、近さと予約済みのダメージで点数を付けて選ぶ
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){
        if(ranged&&DomainDistance(origin,enemy.at)>reach)continue;
        float score=DomainDistance(anchor,enemy.at)+DomainDistance(from,enemy.at)*.04f+(enemy.reserved>=enemy.health?10000.f:0.f);
        // 攻撃の予告中の自爆の敵を優先する
        if(enemy.bomber&&enemy.phase==SwarmPhase::Windup)score-=15;
        if(score<best){best=score;target=enemy.id;}
    }
    return target;
}
// 群れの描画: 鎧の部品・目・自爆の核・攻撃の予告・出現のエフェクト
void ChronoSystem::DrawSwarm(entt::registry& r,GameContext& ctx){
    // 群れのモードでなければ描かない
    const auto& ink=r.get<InkPlayer>(player_);if(!ink.hordeEnabled||finished_)return;
    // 部品の不透明度・拡大率・拡大の中心
    float bodyOpacity=1,modelScale=1;V modelPivot{};
    // 部品 1 つを描く (メッシュを指定の大きさにし、敵の大きさで拡大する)
    auto piece=[&](int index,V at,V size,float yaw,Engine::Vector4 color){const auto& mesh=swarmMeshes_[size_t(index)];
        at=modelPivot+(at-modelPivot)*modelScale;size=size*modelScale;
        Engine::Transform t;t.scale={size.x*mesh.inverseExtent.x,size.y*mesh.inverseExtent.y,size.z*mesh.inverseExtent.z};t.rotate.y=yaw;
        V center{mesh.center.x*t.scale.x,mesh.center.y*t.scale.y,mesh.center.z*t.scale.z};float cs=std::cos(yaw),sn=std::sin(yaw);
        t.translate=EV(at-V{center.x*cs+center.z*sn,center.y,-center.x*sn+center.z*cs});
        // 被弾と起動の光を、不透明度とは別にインスタンスのデータへ入れる
        color.w=2*std::round(std::clamp(color.w,0.f,1.f)*16)+bodyOpacity;
        // 街では鎧を、投影した PBR の塗装・太陽の影・霞付きで描く
        if(cityArmor_.texture)ctx.renderer->DrawMeshInstanced(mesh.handle,cityArmor_.texture,t,color,"CityArmor",cityArmor_.extra);
        else ctx.renderer->DrawMeshInstanced(mesh.handle,white_,t,color,"HordeArmor");};
    // 床のエフェクトを描く
    auto floorEffect=[&](V at,float size,int kind,float progress,Engine::Vector4 color){Engine::Transform t;
        t.translate=EV(V{at.x,InkGround(at)+.18f,at.z});t.scale={size,1,size};color.w=kind*2+std::clamp(progress,0.f,.999f);
        ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,t,color,"HordeEffect");};
    // 生きている敵を 1 体ずつ描く
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){
        // 街のネズミ・大砲・守護者は専用のモデルを使う (CityStage.inl)
        auto cityKind=Chrono::CityKind(enemy);
        if((cityKind==Chrono::CityEnemy::Rat||cityKind==Chrono::CityEnemy::Cannon||cityKind==Chrono::CityEnemy::Guardian))continue;
        // カメラとの間の透明度と大きさ
        bodyOpacity=enemy.cameraOpacity;
        modelScale=enemy.scale;modelPivot=enemy.at;
        // 向き・上下の揺れ・色 (被弾の光と攻撃前の警告。状態異常で色を変える)
        float yaw=std::atan2(enemy.heading.x,enemy.heading.z),bob=std::sin(enemy.age*9+float(enemy.id))*.13f;
        float warning=enemy.phase==SwarmPhase::Windup?1.f:0.f;Engine::Vector4 metal{.68f,.77f,.83f,enemy.flash*6+warning*.55f};
        // 街では役割ごとの色にする
        {const Engine::Vector4 roles[]={{.68f,.77f,.83f,0},{.45f,.55f,.68f,0},{.52f,.46f,.3f,0},{.72f,.55f,.3f,0},{.86f,.88f,.92f,0},{.3f,.3f,.3f,0},{.62f,.24f,.2f,0},{.5f,.5f,.5f,0}};
            auto role=roles[int(cityKind)];metal={role.x,role.y,role.z,metal.w};}
        // 減速は青、炎上は赤、腐食と沼は緑
        if(enemy.slow>0)metal={.3f,.7f,1,metal.w};else if(enemy.burn>0)metal={1,.4f,.15f,metal.w};else if(enemy.corrosion>0||enemy.inPool)metal={.3f,.95f,.4f,metal.w};
        // 上下に揺らし、出現中は地面から出てくる
        V base=enemy.at+V{0,bob,0};if(enemy.phase==SwarmPhase::Spawn)base.y-=2*(1-std::clamp(enemy.clock/.6f,0.f,1.f));
        V side{std::cos(yaw),0,-std::sin(yaw)};
        // 飛ぶ敵は羽を描く
        if(enemy.flying){float flap=std::sin(enemy.age*10+float(enemy.id))*.65f;
            for(float sign:{-1.f,1.f})piece(0,base+side*(sign*2.1f)+V{0,flap,0},{3.6f,.32f,2.1f},yaw,metal);}
        // 胴体 3 節・くちばし
        for(int segment=0;segment<3;++segment)piece(0,base-enemy.heading*(segment*1.15f),{2.2f-segment*.25f,1.8f-segment*.25f,1.6f},yaw,segment==1?Engine::Vector4{.09f,.14f,.2f,metal.w}:metal);
        piece(1,base+enemy.heading*1.2f,{2.1f,1.2f,2.6f},yaw,{.15f,.2f,.27f,metal.w});
        // 盾持ちは前に大きな盾を構える。よろけている間は下ろす
        if(cityKind==Chrono::CityEnemy::Shield&&enemy.stagger<=0)piece(0,base+enemy.heading*2.3f+V{0,.3f,0},{2.9f,2.6f,.35f},yaw,{.58f,.42f,.22f,metal.w});
        // 角と目
        for(float sign:{-1.f,1.f}){piece(2,base+side*(sign*.85f)+V{0,.8f,0},{.4f,1.4f,.6f},yaw,metal);
            // 目 (自爆の敵はオレンジ)
            Engine::Transform eye;eye.translate=EV(modelPivot+(base+enemy.heading*1.35f+side*(sign*.58f)+V{0,.65f,0}-modelPivot)*modelScale);eye.scale={.25f*modelScale,.25f*modelScale,.35f*modelScale};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,eye,enemy.bomber?Engine::Vector4{1,.48f,.08f,bodyOpacity}:Engine::Vector4{.2f,.85f,1,bodyOpacity},"DomainGlow");}
        // 自爆の敵の核
        if(enemy.bomber){Engine::Transform core;core.translate=EV(base+V{0,.6f,0});core.scale={.75f,.75f,.75f};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,core,{1,.25f,.035f,.9f*bodyOpacity},"DomainGlow");}
        // 攻撃の予告 (自爆は範囲、飛ぶ敵は着地点、突進は進む道)
        if(enemy.phase==SwarmPhase::Windup){
            if(enemy.bomber)floorEffect(enemy.at,7,0,enemy.clock/SwarmWindup(enemy),{1,.25f,.035f,1});
            else if(enemy.flying)floorEffect(enemy.attackAt,3.5f,0,enemy.clock/SwarmWindup(enemy),{1,.25f,.035f,1});
            else{V at=(enemy.at+enemy.attackAt)*.5f;Engine::Transform dashCue;
                dashCue.translate=EV(V{at.x,InkGround(at)+.18f,at.z});dashCue.rotate.y=yaw;dashCue.scale={3*modelScale,1,DomainDistance(enemy.at,enemy.attackAt)*.5f+3*modelScale};
                ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,dashCue,{1,.25f,.035f,10+std::clamp(enemy.clock/SwarmWindup(enemy),0.f,.999f)},"HordeEffect");}
        }
        // 出現中の敵の足元の輪
        if(enemy.phase==SwarmPhase::Spawn)floorEffect(enemy.at,3*modelScale,3,enemy.clock/.6f,{.12f,.65f,1,1});
    }
    // 爆発などのエフェクト (床とカメラに向けた板)
    V camera=Read(ctx.camera->Position());
    // 爆発などのエフェクト: 床の輪とカメラに向けた板
    for(const auto& effect:swarmEffects_){float progress=effect.age/.7f;Engine::Vector4 color=effect.kind==1?Engine::Vector4{1,.32f,.05f,1}:Engine::Vector4{.2f,.85f,1,1};
        floorEffect(effect.at,effect.size,effect.kind,progress,color);
        V normal=Unit(camera-effect.at),right=Unit(SwarmCross({0,1,0},normal)),up=SwarmCross(normal,right);Engine::Matrix4x4 world{};
        world.m[0][0]=right.x*effect.size;world.m[0][1]=right.y*effect.size;world.m[0][2]=right.z*effect.size;
        world.m[1][0]=normal.x;world.m[1][1]=normal.y;world.m[1][2]=normal.z;
        world.m[2][0]=up.x*effect.size;world.m[2][1]=up.y*effect.size;world.m[2][2]=up.z*effect.size;
        world.m[3][0]=effect.at.x;world.m[3][1]=effect.at.y+1;world.m[3][2]=effect.at.z;world.m[3][3]=1;color.w=effect.kind*2+progress;
        ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,world,color,"HordeEffect");
    }
    // ボスの出現の大きな輪
    if(ink.battlePhase==BattlePhase::Emerging)floorEffect(swarmEmergenceAt_,36,4,ink.battleAge/SwarmEmergenceSeconds,{.15f,.7f,1,1});
}
} // namespace Game
