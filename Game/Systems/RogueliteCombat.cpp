// RogueliteCombat.cpp
// 1 回の挑戦の中での成長 (ChronoSystem の一部)。強化を選んでいる間は戦闘の時間を進めない。
// ローグライトの強化メニュー: 経験値がたまったら世界を止めてカードを選ばせる (カメラは横から寄った構図へ移る)
#include "ChronoSystemInternal.h"

namespace Game {
bool ChronoSystem::UpdateRogueMenu(entt::registry& r,Player& p,GameContext& ctx){
    // インクのプレイヤーとローグライトの状態
    auto& ink=r.get<InkPlayer>(player_);auto& state=ink.rogue;
    // 無効か終了後なら世界を動かして何もしない
    if(!state.enabled||finished_){ctx.renderer->SetRogueWorldFrozen(false);return false;}
    // 強化が待っていれば、連射の途中でもすぐに割り込む
    if(state.Ready()){
        // 今の液体の見た目・カメラ・画角・被写界深度を記録して、メニューを開く
        rogueFluidAspect_=ink.fluidAspect;rogueFluidMotion_=ink.fluidMotion;
        rogueCameraPosition_=Read(ctx.camera->Position());rogueCameraRotation_=Read(ctx.camera->Rotation());
        rogueCameraFov_=2*std::atan(1/DirectX::XMVectorGetY(ctx.camera->Proj().r[1]));
        auto params=ctx.renderer->GetPostProcessParams();rogueDofFocus_=params.dofFocus;rogueDofRange_=params.dofRange;rogueDofStrength_=params.dofStrength;
        state.Open();Engine::Audio::GetInstance()->SetBGMDucked(true);
        // 以前のメニューで選ばれていたボタンではなく、カードから始める
        UI::Nav().hasFocus=false;
    }
    // メニューを開いていなければ何もしない
    if(!state.menu){ctx.renderer->SetRogueWorldFrozen(false);return false;}
    // 世界を止め、カーソルを出し、メニューの時間を進める
    ctx.renderer->SetRogueWorldFrozen(true);Engine::WindowDX::SetCursorVisible(true);
    float dt=std::clamp(ctx.dt,0.f,.05f);state.TickMenu(dt);
    // プレイヤーを呼吸のような待機の姿勢にし、戦闘に戻るときは
    // 記録した移動の姿勢へ同じ緩急で戻す
    ink.fluidAspect=state.IdleAspect(rogueFluidAspect_);
    ink.fluidMotion=rogueFluidMotion_*(1-state.MenuEase());
    // カメラはプレイヤーを横から写す構図へ移る
    p.cameraOpacity=1;
    V pos=Read(r.get<TransformComponent>(player_).translate),forward=Forward(yaw_,.08f),right{std::cos(yaw_),0,-std::sin(yaw_)};
    V camera=pos-forward*10+V{0,2.1f,0};V view=Unit(pos-right*4.1f+V{0,-.8f,0}-camera);
    float blend=state.MenuEase();V rotation{-std::asin(std::clamp(view.y,-1.f,1.f)),std::atan2(view.x,view.z),0};
    // 両端では同じ記録した姿勢を使う。ヨーは近い向きで回る
    rotation={rogueCameraRotation_.x+std::remainder(rotation.x-rogueCameraRotation_.x,6.283185f)*blend,
        rogueCameraRotation_.y+std::remainder(rotation.y-rogueCameraRotation_.y,6.283185f)*blend,rogueCameraRotation_.z*(1-blend)};
    // カメラの位置・回転・画角を補間する
    ctx.camera->SetPosition(Write(Lerp(rogueCameraPosition_,camera,blend)));ctx.camera->SetRotation(Write(rotation));
    ctx.camera->SetProjection(rogueCameraFov_+(.9f-rogueCameraFov_)*blend,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),.1f,2000);ctx.camera->SetHandheld(.008f*(1-blend));
    // 被写界深度も補間する
    auto params=ctx.renderer->GetPostProcessParams();params.cinematicCamera=true;params.dofFocus=rogueDofFocus_+(10-rogueDofFocus_)*blend;
    params.dofRange=rogueDofRange_+(8-rogueDofRange_)*blend;params.dofStrength=rogueDofStrength_+(.9f-rogueDofStrength_)*blend;ctx.renderer->SetPostProcessParams(params);
    // メニューを閉じ終えたら、入力を消費して世界を動かし、BGM を戻す
    if(!state.menu){
        params.cinematicCamera=false;ctx.renderer->SetPostProcessParams(params);
        ink.previousAttack=true;ink.previousDodge=Controls::Dodge();prevSpace_=Controls::Jump();
        if(ctx.input)ctx.input->LatchPad();
        ctx.renderer->SetRogueWorldFrozen(false);Engine::Audio::GetInstance()->SetBGMDucked(false);
        Engine::WindowDX::SetCursorVisible(false);return true;
    }
    // カードを選べるまで待つ
    if(!state.CanChoose())return true;
    // UI とマウスの準備、確認用の入力の選択
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
    auto* control=r.try_get<ControlFrame>(player_);int choice=control?control->upgradeChoice:-1;
    // 上下キーでカードを選ぶ / クリックや数字キーで決める
    if(UI::Pressed(DIK_UP)||UI::Pressed(DIK_W))state.selected=(state.selected+2)%3;
    if(UI::Pressed(DIK_DOWN)||UI::Pressed(DIK_S))state.selected=(state.selected+1)%3;
    for(int i=0;i<3;++i){if(ui.Focused(UI::RogueChoice(i)))state.selected=i;
        if(ui.Click(UI::RogueChoice(i))||UI::Pressed(BYTE(DIK_1+i)))choice=i;}
    if(UI::Pressed(DIK_RETURN))choice=state.selected;
    // 引き直し (R / Y / ボタン)
    if((control&&control->upgradeReroll)||UI::Pressed(DIK_R)||UI::PadPressed(XINPUT_GAMEPAD_Y)||(!UI::UsingPad()&&ui.Click(UI::RogueReroll))){state.Reroll();return true;}
    // 見送り (X / ボタン): 体の量を回復する
    if((control&&control->upgradeSkip)||UI::Pressed(DIK_X)||UI::PadPressed(XINPUT_GAMEPAD_X)||(!UI::UsingPad()&&ui.Click(UI::RogueSkip))){p.mass=std::min(SlimeMaximumMass,p.mass+60);state.Close();}
    else if(choice>=0)state.Choose(choice);
    // 体力の表示を量に合わせる
    r.get<HealthComponent>(player_).SetHp(p.mass);
    return true;
}
// 描いた輪の発動時に、選んだ強化を反映する (弾数・威力・隕石・持続時間・加速・周回・使い魔・共鳴)
void ChronoSystem::ActivateRogue(entt::registry& r,Player& p,GameContext&,HomingDomain& domain){
    // 選んだ強化の段階 (ranks) に応じて輪の性能を上げる
    (void)p;auto& state=r.get<InkPlayer>(player_).rogue;if(!state.enabled)return;
    // 弾数と威力
    domain.total+=12*state.ranks[0];domain.bulletPower=domain.bulletPower*state.Power(domain.loop.area)+.5f*state.ranks[2];
    // 隕石 (普通の輪だけ)
    domain.meteor=state.ranks[5]>0&&domain.loop.shape==DomainShape::Loop;
    // 残りの弾数 (隕石は 1 発、三角は 3 発)
    domain.remaining=domain.meteor?1:domain.loop.shape==DomainShape::Triangle?3:domain.total;
    // 輪の持続時間 (沼の強化ならさらに長く)
    domain.lifetime=1.2f+3*state.ranks[3];if(state.ranks[7])domain.lifetime=std::max(domain.lifetime,8.f+3*state.ranks[3]);
    // 加速・周回・共鳴の時間を付ける
    if(state.ranks[1])state.haste=2;
    if(state.ranks[12])state.orbit=8;
    // 使い魔: 輪の中心に呼ぶ (最大 6 体)
    if(state.ranks[14]){V center{};for(auto point:domain.loop.points)center=center+point;center=center*(1.f/float(domain.loop.points.size()));center.y=InkGround(center)+1;
        if(familiars_.size()>=6)familiars_.erase(familiars_.begin());
        familiars_.push_back({center,0,0,12.f+4*(state.ranks[14]-1)+6*state.ranks[16]});}
    if(state.ranks[15])state.resonance=3;
}
// 完璧な輪の強化: プレイヤーの周りに円の輪を自動で作る
void ChronoSystem::PerfectRogueLoop(entt::registry& r,Player& p,GameContext& ctx){
    // 完璧な輪の強化が無いか、輪が多すぎれば何もしない
    if(!r.get<InkPlayer>(player_).rogue.ranks[13]||homingDomains_.size()>=6)return;
    // プレイヤーの周り半径 5 の円を 32 分割して輪を作る
    HomingDomain domain;V pos=Read(r.get<TransformComponent>(player_).translate);auto& loop=domain.loop;
    for(int i=0;i<=32;++i){float angle=float(i)*6.283185f/32;V point=pos+V{std::cos(angle)*5,0,std::sin(angle)*5};point.y=InkGround(point)+.16f;loop.points.push_back(point);}
    // 面積・長さ・弾数・塗り・液体・届く範囲を設定して発動する
    loop.area=78.54f;loop.length=31.416f;domain.total=48;domain.remaining=48;domain.highlight=DomainHighlight(loop);domain.liquid.Sync(loop.points);
    domain.reach=DomainReach(loop);ActivateRogue(r,p,ctx,domain);homingDomains_.push_back(std::move(domain));
}
// 強化のスキルの更新: 使い魔の射撃と、酸の線・水たまり・周回・レーザー・状態異常による継続ダメージ
void ChronoSystem::UpdateRogueSkills(entt::registry& r,Player& p,GameContext& ctx){
    auto& ink=r.get<InkPlayer>(player_);auto& state=ink.rogue;if(!state.enabled)return;
    // 各効果の時間を減らす
    float dt=std::min(ctx.dt,.05f);state.haste=std::max(0.f,state.haste-dt);state.orbit=std::max(0.f,state.orbit-dt);state.resonance=std::max(0.f,state.resonance-dt);
    state.interceptClock-=dt;
    // プレイヤーの位置
    V player=Read(r.get<TransformComponent>(player_).translate);
    // 使い魔: 一定間隔で上へ誘導弾を撃ち、寿命が来たら消える
    for(auto& familiar:familiars_){familiar.age+=dt;familiar.clock+=dt;float interval=state.Synergy(RogueTag::Familiar)?1.f/3: .5f;
        if(familiar.clock>=interval&&domainMissiles_.size()<1024){familiar.clock=std::fmod(familiar.clock,interval);DomainMissile missile;missile.at=familiar.at+V{0,1,0};
            missile.target=ChooseSwarmTarget(r,missile.at,false);missile.power=1+.25f*state.ranks[16];missile.velocity={0,35,0};missile.seed=familiar.age;missile.tail.fill(missile.at);domainMissiles_.push_back(missile);}}
    familiars_.erase(std::remove_if(familiars_.begin(),familiars_.end(),[](const Familiar& f){return f.age>=f.lifetime;}),familiars_.end());
    // 継続ダメージは 0.25 秒ごとにまとめて与える
    rogueDamageClock_+=dt;if(rogueDamageClock_<.25f)return;float tick=rogueDamageClock_;rogueDamageClock_=0;
    // 酸の線: 描いた線から 3 以内にいるか
    auto acidLine=[&](V at){const auto& points=ink.domainPath.points;for(size_t i=1;i<points.size();++i){V a=points[i-1],delta=points[i]-a;float len2=Dot(delta,delta);float fraction=len2>.001f?std::clamp(Dot(at-a,delta)/len2,0.f,1.f):0;
            if(Length(at-(a+delta*fraction))<3)return true;}return false;};
    // 共鳴のレーザー: 使い魔同士を結ぶ線の近くにいるか
    auto laser=[&](V at){if(state.resonance<=0)return false;for(size_t i=1;i<familiars_.size();++i){V a=familiars_[i-1].at,b=familiars_[i].at,d=b-a;float length=Dot(d,d);if(length>.001f&&Length(at-(a+d*std::clamp(Dot(at-a,d)/length,0.f,1.f)))<3)return true;}return false;};
    // 沼: 発動中の輪の中にいるか
    auto pool=[&](V at){for(const auto& d:homingDomains_)if(state.ranks[7]&&DomainContains(d.loop,at)&&std::abs(at.y-InkGround(at))<8)return true;return false;};
    // 群れの敵への継続ダメージ (0.25 秒ごと)
    for(auto& enemy:swarm_)if(SwarmAlive(enemy)){
        // 状態異常の時間を減らし、沼にいるかを記録する
        enemy.slow=std::max(0.f,enemy.slow-tick);enemy.corrosion=std::max(0.f,enemy.corrosion-tick);enemy.burn=std::max(0.f,enemy.burn-tick);
        enemy.inPool=pool(enemy.at);
        // 腐食と炎上の継続ダメージ (デバフの相乗効果で 1.5 倍)
        float damage=(enemy.corrosion>0?2.f*state.ranks[8]:0)+(enemy.burn>0?3.f*state.ranks[10]:0);
        if(state.Synergy(RogueTag::Debuff))damage*=1.5f;
        if(state.ranks[6]&&acidLine(enemy.at))damage+=4*state.ranks[6];
        if(enemy.inPool)damage+=2*state.ranks[7];
        if(state.orbit>0&&Length(enemy.at-player)<8)damage+=4*state.ranks[12];
        if(laser(enemy.at))damage+=6*state.ranks[15];
        if(damage>0)HitSwarm(r,p,ctx,enemy.id,damage*tick);
    }
    // ボス戦では弱点へも継続ダメージを与える
    if(ink.battlePhase==BattlePhase::Boss){V at=Center(r,weakpoint_);float damage=0;
        if(state.ranks[6]&&acidLine(at))damage+=4*state.ranks[6];if(pool(at))damage+=2*state.ranks[7];
        if(state.orbit>0&&Length(at-player)<8)damage+=4*state.ranks[12];if(laser(at))damage+=6*state.ranks[15];
        float statusDamage=(ink.rogueBossCorrosion>0?2.f*state.ranks[8]:0)+(ink.rogueBossBurn>0?3.f*state.ranks[10]:0);damage+=statusDamage*(state.Synergy(RogueTag::Debuff)?1.5f:1.f);
        ink.rogueBossCorrosion=std::max(0.f,ink.rogueBossCorrosion-tick);ink.rogueBossBurn=std::max(0.f,ink.rogueBossBurn-tick);ink.rogueBossSlow=std::max(0.f,ink.rogueBossSlow-tick);
        ink.rogueBossPool=pool(at);if(damage>0)HitDomainMissile(r,p,ctx,at,false,damage*tick/4);
    }
}
} // namespace Game
