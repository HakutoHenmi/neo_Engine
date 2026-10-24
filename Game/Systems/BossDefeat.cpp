// BossDefeat.cpp
// ボスの被弾の反応と倒れる演出 (ChronoSystem の一部)。
#include "ChronoSystemInternal.h"

namespace Game {
namespace {
// 倒れる演出の時間 (破裂まで・結果まで・被弾の光)
constexpr float DefeatBurstSeconds=1.6f,DefeatResultSeconds=6.2f,BossFlashSeconds=.14f;
// 2 色を t で混ぜる (アルファは a のまま)
DirectX::XMFLOAT4 MixColor(DirectX::XMFLOAT4 a,DirectX::XMFLOAT4 b,float t){
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w};
}
}
// 被弾の直後に装甲の全部位を赤くする。PoseCreature は毎フレーム元の色を作り直すので、
// 色は自然に消える。連射すると何度も光り、点滅に見える
void ChronoSystem::FlashBoss(entt::registry& r){
    // 光の強さ (0 なら何もしない)
    float k=std::clamp(bossFlash_/BossFlashSeconds,0.f,1.f);if(k<=0)return;
    // すべての部位の色を赤へ寄せる
    for(auto e:r.view<CreaturePart,MeshRendererComponent>()){auto& mesh=r.get<MeshRendererComponent>(e);
        mesh.color=MixColor(mesh.color,{2.6f,.16f,.1f,1},k*.85f);}
}
// ボスの撃破を始める: プレイヤーを無敵にして攻撃を消し、装甲の各部位を破片として記録し、カメラの構図を決める
void ChronoSystem::StartBossDefeat(entt::registry& r,Player& p,GameContext& ctx){
    // 二重に始めない
    if(bossDefeat_)return;
    // 撃破の演出の状態を初期化する
    bossDefeat_=true;defeatBurst_=false;defeatAge_=0;defeatPulse_=0;bossFlash_=0;bossDebris_.clear();
    auto& ink=r.get<InkPlayer>(player_);
    // プレイヤーを無敵にして止める
    p.invincible=std::max(p.invincible,10.f);p.velocity={};p.action=Action::Free;
    // 溜めと演出を消し、輪と誘導弾と羽を消す
    ink.phase=SlimePhase::Roaming;ink.charge=ink.spent=0;ink.monoAge=ink.perfectAge=-1;
    homingDomains_.clear();domainMissiles_.clear();
    std::vector<entt::entity> feathers;for(auto e:r.view<SlimeFeather>())feathers.push_back(e);
    for(auto e:feathers)r.destroy(e);
    // 弱点を止める
    r.get<Target>(weakpoint_).active=false;
    // 最後の姿勢を記録する。けいれんの基準とカメラの構図の両方に使う
    defeatBounds_={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
    // 各部位を破片として記録し、全体の範囲を求める
    for(auto e:r.view<CreaturePart,TransformComponent,MeshRendererComponent,CameraOccluder>()){
        const auto& t=r.get<TransformComponent>(e);auto& occluder=r.get<CameraOccluder>(e);occluder.opacity=1;
        BossDebris debris;debris.part=e;debris.rest=Read(t.translate);debris.color=r.get<MeshRendererComponent>(e).color;
        debris.clearance=std::max(.25f,std::min({t.scale.x,t.scale.y,t.scale.z})*.5f);
        bossDebris_.push_back(debris);
        V half=occluder.size*.5f;auto& b=defeatBounds_;
        b.min={std::min(b.min.x,debris.rest.x-half.x),std::min(b.min.y,debris.rest.y-half.y),std::min(b.min.z,debris.rest.z-half.z)};
        b.max={std::max(b.max.x,debris.rest.x+half.x),std::max(b.max.y,debris.rest.y+half.y),std::max(b.max.z,debris.rest.z+half.z)};
    }
    // 部位が無ければ核の周りの範囲にする
    if(bossDebris_.empty()){V core=Center(r,boss_);defeatBounds_={core-V{5,5,5},core+V{5,5,5}};}
    // カメラから外向きの水平の向き
    V camera=Read(ctx.camera->Position()),center=(defeatBounds_.min+defeatBounds_.max)*.5f;
    V outward=camera-center;outward.y=0;defeatOutward_=Length(outward)>.1f?Unit(outward):V{0,0,-1};
    // 元のカメラを記録し、画面を揺らして BGM を下げる
    defeatReturn_={camera,Read(ctx.camera->Rotation()),2*std::atan(1/DirectX::XMVectorGetY(ctx.camera->Proj().r[1]))};
    ctx.camera->StopShake();ctx.camera->StartImpactShake(.35f,.25f,{0,1,0},12);
    Engine::Audio::GetInstance()->SetBGMDucked(true);
}
// 撃破の演出の更新: けいれん → 破裂 (止める) → スローから元の速さへ。破片を飛ばし、カメラで追う
void ChronoSystem::UpdateBossDefeat(entt::registry& r,Player& p,GameContext& ctx){
    // 経過時間
    const float dt=std::clamp(ctx.dt,0.f,.05f);defeatAge_+=dt;
    const float burst=DefeatBurstSeconds;
    // ゆっくりけいれんし、破裂でヒットストップ、その後スローから実時間へ戻る
    const float scale=defeatAge_<burst?.6f:defeatAge_<burst+.12f?0.f:.25f+.75f*ShotEase(defeatAge_,burst+.12f,burst+1.4f);
    // 演出用の時間 (けいれん・停止・スローを掛けたもの)
    const float sim=dt*scale;
    // プレイヤーを無敵のまま止める
    p.invincible=std::max(p.invincible,1.f);p.worldScale=p.aimScale=1;p.velocity={};
    Engine::WindowDX::SetCursorVisible(finished_);
    // 演出の中心
    const V center=(defeatBounds_.min+defeatBounds_.max)*.5f;
    auto* audio=Engine::Audio::GetInstance();
    // 生き物の衝撃の時間を進める
    auto& creature=r.get<CreatureBoss>(boss_);creature.impactAge+=sim;
    // 破裂の前: 各部位を揺らして赤白に点滅させ、核を光らせて膨らませる
    if(!defeatBurst_){
        // 破裂までの進み具合
        float throe=std::clamp(defeatAge_/burst,0.f,1.f);
        // 破裂に向けてけいれんが強まる。赤と白の点滅で致命的なダメージを表す
        float strobe=std::fmod(defeatAge_*(5+11*throe),1.f)<.5f?1.f:0.f;
        DirectX::XMFLOAT4 hot=strobe>0?DirectX::XMFLOAT4{2.6f,.16f,.1f,1}:DirectX::XMFLOAT4{1.5f,.5f,.12f,1};
        for(size_t i=0;i<bossDebris_.size();++i){auto& d=bossDebris_[i];if(!r.valid(d.part))continue;
            // 各部位をずらしてけいれんさせ、色を赤白に変える
            float amplitude=.15f+1.1f*throe*throe,phase=defeatAge_*(38+9*SwarmVariation(uint32_t(i),1))+float(i);
            V jitter{std::sin(phase)*amplitude,std::sin(phase*1.31f+1.7f)*amplitude*.6f,std::cos(phase*.87f)*amplitude};
            r.get<TransformComponent>(d.part).translate=Write(d.rest+jitter);
            r.get<MeshRendererComponent>(d.part).color=MixColor(d.color,hot,.5f+.35f*throe);
        }
        // 核を表示し、強く光らせて脈打たせる
        auto& core=r.get<MeshRendererComponent>(weakpoint_);core.enabled=true;
        float glow=2+4*throe;core.color={glow,glow*.55f,.2f,1};
        float coreScale=5*(1+.3f*throe+.08f*std::sin(defeatAge_*40));
        r.get<TransformComponent>(weakpoint_).scale={coreScale,coreScale,coreScale};
        // 一定間隔で液体の粒と揺れと音を出す (破裂が近いほど頻繁に)
        defeatPulse_-=dt;
        if(defeatPulse_<=0&&!bossDebris_.empty()){defeatPulse_=.3f-.18f*throe;
            const auto& d=bossDebris_[size_t(defeatAge_*97)%bossDebris_.size()];
            ctx.renderer->EmitGPUFluid(EV(d.rest),{0,4,0},{SlimeRed,SlimeGreen,SlimeBlue,1},14,4.035f);
            ctx.camera->StartImpactShake(.18f,.06f+.1f*throe,{0,1,0},16);
            if(slimeAcidSound_!=0xffffffff)audio->Play(slimeAcidSound_,false,.18f*audio->GetMasterSEVolume(),.7f+.3f*throe);
        }
        // 破裂の瞬間
        if(defeatAge_>=burst){
            defeatBurst_=true;
            // 装甲の板を核から外側と上へ飛ばす (強さは大蛇の大きさに合わせる)
            const float force=std::clamp(Length(defeatBounds_.max-defeatBounds_.min)/90.f,1.f,2.2f);
            // 各破片に外向き・上向きの速度と回転を与える
            for(size_t i=0;i<bossDebris_.size();++i){auto& d=bossDebris_[i];uint32_t id=uint32_t(i);
                V from=d.rest-center;
                V direction=Unit(from+V{0,Length(from)*.35f+2,0}+V{(SwarmVariation(id,2)-.5f)*6,SwarmVariation(id,3)*4,(SwarmVariation(id,4)-.5f)*6});
                d.velocity=(direction*(20+28*SwarmVariation(id,5))+V{0,10+14*SwarmVariation(id,6),0})*force;
                d.spin={(SwarmVariation(id,7)-.5f)*14,(SwarmVariation(id,8)-.5f)*14,(SwarmVariation(id,9)-.5f)*14};
                if(r.valid(d.part))r.get<TransformComponent>(d.part).translate=Write(d.rest);
            }
            // 核を消し、地面に衝撃を出して大きく揺らす
            r.get<MeshRendererComponent>(weakpoint_).enabled=false;
            V ground=center;ground.y=InkGround(center)+.1f;
            BossImpact(r,ctx,ground,{0,1,0},2.2f);
            ctx.camera->StartImpactShake(.7f,.45f,{0,1,0},12);
            // 周りに液体を 10 方向へ飛ばす
            for(int k=0;k<10;++k){float angle=float(k)*.6283185f;
                ctx.renderer->EmitGPUFluid(EV(center+V{std::cos(angle)*3,0,std::sin(angle)*3}),{std::cos(angle)*12,10,std::sin(angle)*12},
                    {SlimeRed,SlimeGreen,SlimeBlue,1},24,4.035f);}
            // 大きな爆発と、破片の位置の爆発
            if(swarmEffects_.size()<128)swarmEffects_.push_back({center,0,26,1});
            for(size_t k=0;k<8&&!bossDebris_.empty()&&swarmEffects_.size()<128;++k)
                swarmEffects_.push_back({bossDebris_[k*bossDebris_.size()/8].rest,0,12,1});
            // 残った群れもリーダーと一緒に崩れる
            for(auto& enemy:swarm_)if(SwarmAlive(enemy)){enemy.health=0;enemy.phase=SwarmPhase::Dead;
                if(swarmEffects_.size()<128)swarmEffects_.push_back({enemy.at,0,enemy.bomber?7.f:4.f,enemy.bomber?1:2});}
            // 核の音
            if(coreSound_!=0xffffffff)audio->Play(coreSound_,false,.5f*audio->GetMasterSEVolume(),.55f);
        }
    // 破裂の後: 破片を重力で落とし、地面で弾ませて止める
    }else{
        // 冷える進み具合
        const float cool=ShotEase(defeatAge_,burst+.4f,burst+3.6f);
        // 各破片を重力で落とし、床で弾ませて止める
        for(size_t i=0;i<bossDebris_.size();++i){auto& d=bossDebris_[i];if(!r.valid(d.part))continue;
            auto& t=r.get<TransformComponent>(d.part);V at=Read(t.translate);
            d.velocity.y-=32*sim;at=at+d.velocity*sim;
            float floor=InkGround(at);floor=(floor<-500?0.f:floor)+d.clearance;
            if(at.y<floor){at.y=floor;
                if(d.velocity.y<-4){d.velocity.y*=-.32f;d.velocity.x*=.6f;d.velocity.z*=.6f;d.spin=d.spin*.5f;}
                else{float friction=std::exp(-5*sim);d.velocity.y=0;d.velocity.x*=friction;d.velocity.z*=friction;d.spin=d.spin*friction;}}
            t.translate=Write(at);
            // 回転させる
            t.rotate={t.rotate.x+d.spin.x*sim,t.rotate.y+d.spin.y*sim,t.rotate.z+d.spin.z*sim};
            // 破片は白熱から赤い残り火を経て焦げた装甲の色へ冷える
            DirectX::XMFLOAT4 ash{d.color.x*.6f+.05f,d.color.y*.5f,d.color.z*.5f,1};
            r.get<MeshRendererComponent>(d.part).color=MixColor(MixColor({1.8f,.6f,.16f,1},{1.6f,.18f,.06f,1},std::min(1.f,cool*2)),ash,cool);
        }
        // 空中の破片の一部からスライムの跡を出す
        defeatPulse_-=sim;
        if(defeatPulse_<=0&&defeatAge_<burst+2.f&&!bossDebris_.empty()){defeatPulse_=.07f;
            const auto& d=bossDebris_[size_t(defeatAge_*131)%bossDebris_.size()];
            if(r.valid(d.part)&&Length(d.velocity)>6)
                ctx.renderer->EmitGPUFluid(EV(Read(r.get<TransformComponent>(d.part).translate)),{0,1,0},{SlimeRed,SlimeGreen,SlimeBlue,1},6,4.035f);
        }
    }
    // エフェクトの時間を進めて古いものを消す
    for(auto& effect:swarmEffects_)effect.age+=sim;
    swarmEffects_.erase(std::remove_if(swarmEffects_.begin(),swarmEffects_.end(),[](const SwarmEffect& effect){return effect.age>.7f;}),swarmEffects_.end());
    // カメラ: けいれんする大蛇を近くから撮る (首を上げた頭は核よりずっと前にあるので、
    // 全部位を収めると遠い引きの画になる)。その後、破片が草原に散らばる間に
    // 引いてゆっくり回り込む
    // 画面の比率と、時間で広がる構図の範囲
    const float aspect=ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y);
    float spread=.5f+.85f*ShotEase(defeatAge_,burst,burst+1.8f);
    Bounds view{center+(defeatBounds_.min-center)*spread,center+(defeatBounds_.max-center)*spread};
    // ゆっくり回り込む向き
    float orbit=.55f*ShotEase(defeatAge_,0,DefeatResultSeconds)+.03f*defeatAge_,cs=std::cos(orbit),sn=std::sin(orbit);
    V outward{defeatOutward_.x*cs+defeatOutward_.z*sn,0,-defeatOutward_.x*sn+defeatOutward_.z*cs};
    // 範囲が収まる構図を求め、破裂前は核へ少し寄る
    auto shot=FrameShot(view,outward+V{0,.42f-.12f*ShotEase(defeatAge_,burst,burst+2.f),0},.92f,aspect);
    shot.at=Lerp(shot.at,center,defeatBurst_?0.f:.14f*ShotEase(defeatAge_,.2f,burst));
    // 固い物の手前へ寄せ、地面より上にする
    float fraction;V normal;
    for(const auto& box:solids_)if(Chrono::Sweep(center,shot.at-center,box,{.3f,.3f,.3f},fraction,normal))
        shot.at=center+(shot.at-center)*std::max(.05f,fraction-.025f);
    float floor=InkGround(shot.at);shot.at.y=std::max(shot.at.y,(floor<-500?0.f:floor)+1.5f);
    // 中心を向き、元のカメラから 0.8 秒で移る
    shot=AimShot(shot.at,center,shot.fov);
    auto pose=BlendShot(defeatReturn_,shot,ShotEase(defeatAge_,0,.8f));
    // カメラへ反映する
    ctx.camera->SetPosition(Write(pose.at));ctx.camera->SetRotation(Write(pose.rotation));
    ctx.camera->SetProjection(pose.fov,aspect,.1f,2000);ctx.camera->SetHandheld(.006f);
    p.cameraOpacity=1;
    // 演出を更新し、被写界深度と閃光の色ずれをかける
    Presentation(r,p,ctx);
    auto params=ctx.renderer->GetPostProcessParams();
    params.cinematicCamera=true;params.dofFocus=Length(center-pose.at);params.dofRange=80;params.dofStrength=.45f;params.dofBossDepth=-1000;
    float flash=defeatBurst_?std::max(0.f,1-(defeatAge_-burst)/.3f):0.f;
    params.chromaShift=flash*.8f;params.vignette=std::max(params.vignette,.25f);
    ctx.renderer->SetPostProcessParams(params);
    // 結果の時間が来たら勝ちで終える
    if(!finished_&&defeatAge_>=DefeatResultSeconds){Finish(p,true);audio->SetBGMDucked(false);}
}
// 撃破の上下の黒帯・白い閃光・タイトルを描く
void ChronoSystem::DrawBossDefeat(GameContext& ctx){
    // UI の準備と上下の黒帯
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    const float bars=ShotEase(defeatAge_,0,.5f);
    ui.Fill({0,0,1280,56*bars},{.01f,.025f,.025f,1});
    ui.Fill({0,720-56*bars,1280,56*bars},{.01f,.025f,.025f,1});
    // 破裂の後だけ: 白い閃光と字幕
    if(!defeatBurst_)return;
    float flash=std::max(0.f,1-(defeatAge_-DefeatBurstSeconds)/.35f);
    if(flash>0)ui.Fill({0,0,1280,720},{1,.95f,.85f,flash*.85f});
    float caption=ShotEase(defeatAge_,DefeatBurstSeconds+.7f,DefeatBurstSeconds+1.3f)*(1-ShotEase(defeatAge_,DefeatResultSeconds-.6f,DefeatResultSeconds));
    if(caption<=0)return;
    // 字幕をフェードさせて描く
    UI::Canvas text=ui;text.MultiplyAlpha(caption);
    const float w=text.Width("SERPENT DEFEATED",44);
    text.Brush({640-w*.5f-64,558,w+128,70},UI::WithAlpha(UI::Gold,.94f),-.025f);
    text.Center("SERPENT DEFEATED",640,568,44,UI::Ink);
    text.Bold("大蛇、砕け散る",640-text.Width("大蛇、砕け散る",22,UI::JapaneseFont)*.5f,640,22,UI::Paper,UI::JapaneseFont);
}
} // namespace Game
