// HordeCombat.cpp
// 群れの敵との戦闘 (ChronoSystem の一部)。数の決まった群れは共通のメッシュとインスタンス描画を使う。
// 群れの作成: 状態を消し、鎧のシェーダーとメッシュ (大きさの逆数と中心) を用意して、プレイヤーの周りに 72 体を出す
#include "ChronoSystemInternal.h"

namespace Game {
void ChronoSystem::BuildSwarm(entt::registry& r){
    // 使い魔とローグライトのダメージの時間を消す
    familiars_.clear();rogueDamageClock_=0;
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
    // 最初はボスを隠す
    SetSwarmBossVisible(r,false);
}
// ボスの表示を切り替える (depth があれば地面の下へ沈める)。隠すときは弱点と当たり判定も止める
void ChronoSystem::SetSwarmBossVisible(entt::registry& r,bool visible,float depth){
    // ボスの各部位の表示を切り替え、沈める
    for(auto e:r.view<CreaturePart,MeshRendererComponent,TransformComponent>()){
        r.get<MeshRendererComponent>(e).enabled=visible;
        if(visible&&depth>0)r.get<TransformComponent>(e).translate.y-=depth;
    }
    // 弱点も同じようにする
    r.get<MeshRendererComponent>(weakpoint_).enabled=visible;
    if(visible&&depth>0)r.get<TransformComponent>(weakpoint_).translate.y-=depth;
    if(!visible||depth>0){r.get<Target>(weakpoint_).active=false;creatureSolids_.clear();}
}
// 群れの敵にダメージを与える。倒したら経験値・コンボ・体の量を増やす
void ChronoSystem::HitSwarm(entt::registry& r,Player& p,GameContext& ctx,uint32_t id,float damage,bool credit){
    // 生きている敵を探す
    auto found=std::find_if(swarm_.begin(),swarm_.end(),[&](const SwarmEnemy& enemy){return enemy.id==id&&SwarmAlive(enemy);});
    // 水たまりの中なら 1.25 倍のダメージを受け、光る。体力が残れば終わり
    if(found==swarm_.end())return;found->health-=damage*(found->inPool?1.25f:1);found->flash=.12f;if(found->health>0)return;
    // 倒れた
    found->phase=SwarmPhase::Dead;
    // 倒れたエフェクト (自爆の敵は大きな爆発)
    if(swarmEffects_.size()<128)swarmEffects_.push_back({found->at,0,found->bomber?7.f:4.f,found->bomber?1:2});
    auto& ink=r.get<InkPlayer>(player_);
    // 自分の手柄なら経験値・撃破数・コンボを増やし、体の量を回復する
    if(credit){ink.rogue.Gain(1);++ink.swarmKills;++ink.swarmCombo;ink.swarmBestCombo=std::max(ink.swarmBestCombo,ink.swarmCombo);ink.swarmComboAge=3;
        p.mass=std::min(SlimeMaximumMass,p.mass+3.f);
        // 音 (コンボが続くほど高く)
        if(swarmSoundClock_<=0){auto* audio=Engine::Audio::GetInstance();audio->Play(slimeAcidSound_,false,.12f*audio->GetMasterSEVolume(),1.05f+std::min(.55f,ink.swarmCombo*.012f));swarmSoundClock_=.08f;}}
    // 起動した核を壊すと周りで連鎖して爆発する (プレイヤーを傷つける爆発ではない)
    if(found->bomber){V at=found->at;
        for(auto& enemy:swarm_)if(SwarmAlive(enemy)&&Length(enemy.at-at)<7){
            enemy.health=0;enemy.phase=SwarmPhase::Dead;
            if(swarmEffects_.size()<128)swarmEffects_.push_back({enemy.at,0,3.5f,2});
            if(credit){ink.rogue.Gain(1);++ink.swarmKills;++ink.swarmCombo;ink.swarmBestCombo=std::max(ink.swarmBestCombo,ink.swarmCombo);}
        }
    }
    (void)ctx;
}
// 誘導弾の目標を選ぶ (ボスを優先するか・輪の届く範囲だけか)。0 はボス
uint32_t ChronoSystem::ChooseSwarmTarget(entt::registry& r,V from,bool preferBoss,V origin,float reach){
    // ボスと戦えるか
    const auto& ink=r.get<InkPlayer>(player_);bool bossAvailable=ink.battlePhase==BattlePhase::Boss&&!r.get<Boss>(boss_).countered;
    // 群れのモードでなければボスしか狙えない
    if(!ink.hordeEnabled)return bossAvailable?0:SwarmNoTarget;
    if(preferBoss&&bossAvailable)return 0;
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
    // 良い敵がいなければボスを狙う
    if(bossAvailable&&(target==SwarmNoTarget||best>9000))return 0;
    return target;
}
// 群れの更新: 出現・攻撃の順番 (同時に 3 体まで、4 回ごとに休み)・自爆と突進の当たり・押し合い・カメラの透明度・ボスの出現
void ChronoSystem::UpdateSwarm(entt::registry& r,Player& p,GameContext& ctx){
    // 時間 (世界の遅さを掛ける)
    auto& ink=r.get<InkPlayer>(player_);float dt=std::min(ctx.dt,.05f)*p.worldScale;
    // 群れのモードでなければすぐボス戦にする
    if(!ink.hordeEnabled){
        if(ink.battlePhase!=BattlePhase::Boss){ink.battlePhase=BattlePhase::Boss;PoseCreature(r);SetSwarmBossVisible(r,true);swarm_.clear();}
        return;
    }
    // プレイヤーの位置
    V player=Read(r.get<TransformComponent>(player_).translate);
    // 戦闘の時間・コンボの残り時間・音の間隔を進める
    ink.battleAge+=dt;ink.swarmComboAge=std::max(0.f,ink.swarmComboAge-dt);if(ink.swarmComboAge==0)ink.swarmCombo=0;
    swarmSoundClock_=std::max(0.f,swarmSoundClock_-dt);
    // エフェクトを進めて古いものを消す
    for(auto& effect:swarmEffects_)effect.age+=dt;
    swarmEffects_.erase(std::remove_if(swarmEffects_.begin(),swarmEffects_.end(),[](const SwarmEffect& effect){return effect.age>.7f;}),swarmEffects_.end());
    // 倒れた敵と、遠くで長く生きた敵を消す
    swarm_.erase(std::remove_if(swarm_.begin(),swarm_.end(),[&](const SwarmEnemy& enemy){return !SwarmAlive(enemy)||(enemy.age>35&&DomainDistance(enemy.at,player)>170);}),swarm_.end());
    // 一定間隔で 12 体ずつ出す (上限まで)
    swarmSpawnClock_+=dt;float interval=ink.battlePhase==BattlePhase::Boss?.7f:1.f;
    if(swarmSpawnClock_>=interval){swarmSpawnClock_=std::fmod(swarmSpawnClock_,interval);
        for(int i=0;i<12&&swarm_.size()<SwarmCapacity;++i){uint32_t id=swarmNextId_++;
            SwarmEnemy enemy;enemy.id=id;SwarmRole(enemy);
            enemy.at=SwarmSpawnPosition(player,id);
            enemy.at.y=InkGround(enemy.at)+(enemy.flying?18.f+float(id%7)*2:1.f);enemy.heading=Unit(player-enemy.at);swarm_.push_back(enemy);++ink.swarmSpawned;}
    }
    // 攻撃中の数を数え、休みの後に次の攻撃の時間を決める
    swarmAttackClock_=std::max(0.f,swarmAttackClock_-dt);int activeAttacks=0;
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&(enemy.phase==SwarmPhase::Windup||enemy.phase==SwarmPhase::Dash))++activeAttacks;
    // 休みが終わったら次の攻撃までの時間を決める
    if(swarmRestPending_&&activeAttacks==0){swarmRestPending_=false;swarmAttackClock_=2.8f+SwarmVariation(swarmNextId_)*.8f;}
    // 前回攻撃した敵の次から順に更新する
    const size_t first=swarm_.empty()?0:swarmAttackCursor_%swarm_.size();
    // 前回攻撃した敵の次から順に更新する
    for(size_t step=0;step<swarm_.size();++step){size_t index=(first+step)%swarm_.size();auto& enemy=swarm_[index];
        V before=enemy.at;bool dashing=enemy.phase==SwarmPhase::Dash;auto previous=enemy.phase;
        // 敵を 1 フレーム進める (減速・沼で遅くなる。攻撃は同時 3 体まで)
        auto event=TickSwarm(enemy,player,dt*(enemy.slow>0?.5f:enemy.inPool?.65f:1.f),InkGround(enemy.at),!swarmRestPending_&&activeAttacks<3&&swarmAttackClock_<=0);
        // 攻撃の予告に入ったら、次の攻撃の順番と時間を決める
        if(previous!=SwarmPhase::Windup&&enemy.phase==SwarmPhase::Windup){++activeAttacks;
            swarmAttackCursor_=index+1;
            swarmAttackClock_=1+SwarmVariation(enemy.id,enemy.attacks)*.4f;
            if(++swarmAttackBurst_>=4){swarmAttackBurst_=0;swarmRestPending_=true;}}
        // 移動できる範囲に収め、地面 (飛ぶ敵は空中) の高さに合わせる
        enemy.at.x=std::clamp(enemy.at.x,-320.f,320.f);enemy.at.z=std::clamp(enemy.at.z,-330.f,370.f);
        enemy.at.y=enemy.flying?std::max(enemy.at.y,InkGround(enemy.at)+1.2f):InkGround(enemy.at)+1;
        // 自爆はその場で爆発、突進はプレイヤーに当たったらダメージ
        if(event==SwarmEvent::Detonate){enemy.health=0;++ink.swarmDetonations;
            if(swarmEffects_.size()<128)swarmEffects_.push_back({enemy.at,0,8,1});
            if(Length(player-enemy.at)<7){p.damageReason="HIT - CORE DETONATION";Damage(r,p,9,ctx,enemy.at,.24f);}
        // 突進がプレイヤーに当たったらダメージ
        }else if(dashing&&!enemy.struck){V delta=enemy.at-before;float hit;
            if(Length(player-enemy.at)<3||RaySphere(before,Unit(delta),player,3,Length(delta),hit)){
                enemy.struck=true;++ink.swarmStrikes;p.damageReason="HIT - ARMORED SWARM";Damage(r,p,4,ctx,enemy.at,.2f);}
        }
    }
    // 近くの敵同士を押し離して密集しても見分けやすくする。攻撃中は決めた向きを保つ
    for(size_t i=0;i<swarm_.size();++i)for(size_t j=i+1;j<swarm_.size();++j){auto& a=swarm_[i];auto& b=swarm_[j];
        if(!SwarmAlive(a)||!SwarmAlive(b)||a.flying!=b.flying)continue;V delta=a.at-b.at;if(!a.flying)delta.y=0;float distance=Length(delta);
        if(distance<2.7f&&distance>.01f){V push=delta*((2.7f-distance)*dt*1.8f/distance);
            if(a.phase==SwarmPhase::Chase)a.at=a.at+push;if(b.phase==SwarmPhase::Chase)b.at=b.at-push;}}
    // カメラとプレイヤーの間にいる敵は透明にする
    V camera=Read(ctx.camera->Position());float opacityBlend=1-std::exp(-18*std::min(ctx.dt,.05f));
    for(auto& enemy:swarm_){float goal=SwarmCameraOpacity(camera,player,enemy.at,enemy.flying);
        enemy.cameraOpacity+=(goal-enemy.cameraOpacity)*opacityBlend;}
    // 生きている数と攻撃中の数を記録する
    ink.swarmAlive=0;ink.swarmActiveAttacks=0;for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){++ink.swarmAlive;
        if(enemy.phase==SwarmPhase::Windup||enemy.phase==SwarmPhase::Dash)++ink.swarmActiveAttacks;}
    // 倒した数が足りたらボスが出現し始める
    if(ink.battlePhase==BattlePhase::Horde){
        SetSwarmBossVisible(r,false);
        // 倒した数が足りたら出現の段階へ
        if(SwarmBossReady(ink.swarmKills)){
            ink.battlePhase=BattlePhase::Emerging;ink.battleAge=0;ink.lockedOn=false;
            // プレイヤーの近くに出現地点を決める
            swarmEmergenceAt_={std::clamp(player.x+35.f,-230.f,230.f),0,std::clamp(player.z+60.f,-240.f,280.f)};
            // 生き物のボスを蛇の形で出現地点に置き、プレイヤーの方を向ける
            auto& creature=r.get<CreatureBoss>(boss_);creature={};creature.stage=CreatureStage::Snake;creature.form=0;
            creature.offset=swarmEmergenceAt_-EvaluateCreature(0,0).core;creature.heading=std::atan2(swarmEmergenceAt_.x-player.x,swarmEmergenceAt_.z-player.z);
            // ボスと弱点の状態を初期化する
            r.get<Boss>(boss_).countered=false;ink.armor=0;ink.coreHealth=100;ink.downTimer=0;
            // 揺れと音
            ctx.camera->StartImpactShake(.45f,.18f,{0,1,0},7);
            auto* audio=Engine::Audio::GetInstance();audio->Play(slimePressureSound_,false,.4f*audio->GetMasterSEVolume(),.55f);
        }
    }
    // 出現中は地面から少しずつせり上がる
    if(ink.battlePhase==BattlePhase::Emerging){
        PoseCreature(r);float progress=std::clamp(ink.battleAge/SwarmEmergenceSeconds,0.f,1.f);
        SetSwarmBossVisible(r,true,48*(1-Ease(progress)));
        if(progress>=1){ink.battlePhase=BattlePhase::Boss;ink.battleAge=0;ctx.camera->StartImpactShake(.22f,.13f,{0,1,0},11);}
    }
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
        if(slimeLife_&&cityArmor_.texture)ctx.renderer->DrawMeshInstanced(mesh.handle,cityArmor_.texture,t,color,"CityArmor",cityArmor_.extra);
        else ctx.renderer->DrawMeshInstanced(mesh.handle,white_,t,color,"HordeArmor");};
    // 床のエフェクトを描く
    auto floorEffect=[&](V at,float size,int kind,float progress,Engine::Vector4 color){Engine::Transform t;
        t.translate=EV(V{at.x,InkGround(at)+.18f,at.z});t.scale={size,1,size};color.w=kind*2+std::clamp(progress,0.f,.999f);
        ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,t,color,"HordeEffect");};
    // 生きている敵を 1 体ずつ描く
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){
        // 街のネズミ・大砲・守護者は専用のモデルを使う (CityStage.inl)
        auto cityKind=Chrono::CityKind(enemy);
        if(slimeLife_&&(cityKind==Chrono::CityEnemy::Rat||cityKind==Chrono::CityEnemy::Cannon||cityKind==Chrono::CityEnemy::Guardian))continue;
        // カメラとの間の透明度と大きさ
        bodyOpacity=enemy.cameraOpacity;
        modelScale=enemy.scale;modelPivot=enemy.at;
        // 向き・上下の揺れ・色 (被弾の光と攻撃前の警告。状態異常で色を変える)
        float yaw=std::atan2(enemy.heading.x,enemy.heading.z),bob=std::sin(enemy.age*9+float(enemy.id))*.13f;
        float warning=enemy.phase==SwarmPhase::Windup?1.f:0.f;Engine::Vector4 metal{.68f,.77f,.83f,enemy.flash*6+warning*.55f};
        // 街では役割ごとの色にする
        if(slimeLife_){const Engine::Vector4 roles[]={{.68f,.77f,.83f,0},{.45f,.55f,.68f,0},{.52f,.46f,.3f,0},{.72f,.55f,.3f,0},{.86f,.88f,.92f,0},{.3f,.3f,.3f,0},{.62f,.24f,.2f,0},{.5f,.5f,.5f,0}};
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
        if(slimeLife_&&cityKind==Chrono::CityEnemy::Shield&&enemy.stagger<=0)piece(0,base+enemy.heading*2.3f+V{0,.3f,0},{2.9f,2.6f,.35f},yaw,{.58f,.42f,.22f,metal.w});
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
