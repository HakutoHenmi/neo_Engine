// ChronoCinematic.inl
// 開幕とボス出現のムービー (ChronoSystem の一部)。カメラの構図・被写界深度・上下の黒帯とタイトルを扱う。
namespace Game {
// ムービーの更新: 始まりで元のカメラを記録し、時間に沿って構図を動かし、終わったら元へ戻す。ムービー中は true
bool ChronoSystem::UpdateCinematic(entt::registry& r,Player& p,GameContext& ctx){
    // インクのプレイヤー
    auto& ink=r.get<InkPlayer>(player_);
    // スクリプトで決めた戦闘のテストは自分のタイミングを使う。通常のプレイは演出の流れに従う
    if(r.all_of<ControlFrame>(player_)){openingPending_=false;return false;}
    // 開幕か、ボスが出現し始めたときに始める
    if(!cinematicActive_){
        if(!openingPending_&&ink.battlePhase!=BattlePhase::Emerging)return false;
        // 開幕ならプレイヤーの後ろのカメラ、ボスなら今のカメラを記録する
        cinematicBoss_=!openingPending_;openingPending_=false;cinematicAge_=0;cinematicActive_=true;
        if(!cinematicBoss_)InkCamera(r,p,ctx);
        cinematicReturn_={Read(ctx.camera->Position()),Read(ctx.camera->Rotation()),
            2*std::atan(1/DirectX::XMVectorGetY(ctx.camera->Proj().r[1]))};
        // 元の被写界深度を記録する
        auto params=ctx.renderer->GetPostProcessParams();
        cinematicDofFocus_=params.dofFocus;cinematicDofRange_=params.dofRange;cinematicDofStrength_=params.dofStrength;
        if(cinematicBoss_){
            // 完全に地上へ出た姿勢 (メッシュの拡大・回転・頭・尾を含む) の大きさを測る。
            // PoseCreature は、出現の深さを付ける前に毎回この姿勢へ戻す
            // ボスの各部位の頂点から全体の範囲を求める (メッシュごとの範囲はキャッシュする)
            PoseCreature(r);
            cinematicBossBounds_={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
            std::unordered_map<uint32_t,Bounds> meshBounds;
            for(auto e:r.view<CreaturePart,TransformComponent,MeshRendererComponent>()){
                auto handle=r.get<MeshRendererComponent>(e).modelHandle;
                if(meshBounds.find(handle)==meshBounds.end()){
                    Bounds bounds{{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
                    auto* model=ctx.renderer->GetModel(handle);if(!model||model->GetData().vertices.empty())continue;
                    for(const auto& vertex:model->GetData().vertices){
                        auto v=vertex.position;
                        bounds.min={std::min(bounds.min.x,v.x),std::min(bounds.min.y,v.y),std::min(bounds.min.z,v.z)};
                        bounds.max={std::max(bounds.max.x,v.x),std::max(bounds.max.y,v.y),std::max(bounds.max.z,v.z)};
                    }
                    meshBounds.emplace(handle,bounds);
                }
                // 部位のワールド行列で範囲の 8 つの角を変換して全体の範囲を広げる
                const auto& bounds=meshBounds.at(handle);auto matrix=r.get<TransformComponent>(e).GetTransform().ToMatrix();
                auto world=DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&matrix));
                for(float x:{bounds.min.x,bounds.max.x})for(float y:{bounds.min.y,bounds.max.y})for(float z:{bounds.min.z,bounds.max.z}){
                    DirectX::XMFLOAT3 v;DirectX::XMStoreFloat3(&v,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(x,y,z,1),world));
                    auto& b=cinematicBossBounds_;
                    b.min={std::min(b.min.x,v.x),std::min(b.min.y,v.y),std::min(b.min.z,v.z)};
                    b.max={std::max(b.max.x,v.x),std::max(b.max.y,v.y),std::max(b.max.z,v.z)};
                }
            }
        }
        // 揺れを止め、BGM を下げる
        ctx.camera->StopShake();
        Engine::Audio::GetInstance()->SetBGMDucked(true);
    }
    // 経過時間・終わる時間。ムービー中は世界と敵を止め、カーソルを隠す
    const float dt=std::clamp(ctx.dt,0.f,.05f);
    cinematicAge_+=dt;
    const float returnStart=SwarmEmergenceSeconds+BossShotHoldSeconds;
    const float duration=cinematicBoss_?returnStart+BossShotReturnSeconds:OpeningShotSeconds;
    ctx.renderer->SetRogueWorldFrozen(true);ctx.camera->SetHandheld(0);
    Engine::WindowDX::SetCursorVisible(false);p.cameraOpacity=1;
    p.worldScale=p.aimScale=0;
    if(ctx.combatFlow)ctx.combatFlow->SetChronoEnemyScale(0);
    // プレイヤーの位置と注視点
    V player=Read(r.get<TransformComponent>(player_).translate),focus=player+V{0,1,0};
    CinematicPose pose;
    // ボスの出現: 地面からせり上がり、回り込みながら全体を写して元のカメラへ戻る
    if(cinematicBoss_){
        // 進めるのは出現の時間だけ。攻撃・弾・経験値・プレイヤーは止めたまま
        ink.battleAge=std::min(cinematicAge_,SwarmEmergenceSeconds);
        PoseCreature(r);SetSwarmBossVisible(r,true,48*(1-ShotEase(ink.battleAge,0,SwarmEmergenceSeconds)));
        // プレイヤーから出現地点への向きと横の向き
        V axis=Unit(V{player.x-swarmEmergenceAt_.x,0,player.z-swarmEmergenceAt_.z});
        V side{axis.z,0,-axis.x};
        float orbit=ShotEase(cinematicAge_,.7f,SwarmEmergenceSeconds);
        float aspect=ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y);
        // 回り込みの途中で構図を合わせ直す。合わせた 2 つのカメラを補間すると途中で見切れることがあるため
        pose=FrameShot(cinematicBossBounds_,axis+side*(.36f-.62f*orbit)+V{0,.35f-.1f*orbit,0},.96f-.14f*orbit,aspect);
        pose=BlendShot(cinematicReturn_,pose,ShotEase(cinematicAge_,0,.7f));
        pose=BlendShot(pose,cinematicReturn_,ShotEase(cinematicAge_,returnStart,duration));
    // 開幕: 遠景からプレイヤーへ寄り、最後に元のカメラへ戻る
    }else{
        // 遠景から寄った画へ
        auto wide=AimShot(focus+V{26,19,-35},focus+V{0,0,15},1.02f);
        auto hero=AimShot(focus+V{8,4,-11},focus,.86f);
        pose=BlendShot(wide,hero,ShotEase(cinematicAge_,.25f,2.1f));
        pose=BlendShot(pose,cinematicReturn_,ShotEase(cinematicAge_,2.35f,duration));
    }
    // 地面と固い物の判定で、草原の端に近い出現のカメラも守る
    if(cinematicAge_<duration){
        V target=cinematicBoss_?swarmEmergenceAt_+V{0,8,0}:focus;float fraction;V normal;
        for(const auto& box:solids_)if(Chrono::Sweep(target,pose.at-target,box,{.3f,.3f,.3f},fraction,normal))
            pose.at=target+(pose.at-target)*std::max(.05f,fraction-.025f);
        pose.at.y=std::max(pose.at.y,InkGround(pose.at)+.7f);
    }
    // カメラと被写界深度を反映する
    ctx.camera->SetPosition(Write(pose.at));ctx.camera->SetRotation(Write(pose.rotation));
    ctx.camera->SetProjection(pose.fov,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),.1f,2000);
    // 被写界深度の設定 (ボスなら全体の中心にピントを合わせる)
    auto params=ctx.renderer->GetPostProcessParams();params.cinematicCamera=true;params.dofStrength=.7f;params.dofRange=cinematicBoss_?50.f:8.f;
    params.dofFocus=Length((cinematicBoss_?(cinematicBossBounds_.min+cinematicBossBounds_.max)*.5f:focus)-pose.at);
    params.dofBossDepth=cinematicBoss_?params.dofFocus:-1000;ctx.renderer->SetPostProcessParams(params);
    // 押しっぱなしの入力を消費し、再開したときに誤って撃ったり回避したりしないようにする
    ink.previousAttack=Controls::Fire();ink.previousDodge=Controls::Dodge();ink.previousLock=Controls::LockOn();prevSpace_=Controls::Jump();
    // 終わったら、ボス戦を始めて世界と音を元に戻す
    if(cinematicAge_>=duration){
        cinematicActive_=false;
        p.worldScale=p.aimScale=1;
        if(cinematicBoss_){ink.battlePhase=BattlePhase::Boss;ink.battleAge=0;}
        ink.rogue.returnDelay=.1f;
        ctx.camera->SetHandheld(.008f);ctx.renderer->SetRogueWorldFrozen(false);
        Engine::Audio::GetInstance()->SetBGMDucked(false);
        params.cinematicCamera=false;params.dofFocus=cinematicDofFocus_;params.dofRange=cinematicDofRange_;params.dofStrength=cinematicDofStrength_;
        ctx.renderer->SetPostProcessParams(params);
    }
    return true;
}
// ムービー中の上下の黒帯とタイトルを描く
void ChronoSystem::DrawCinematic(GameContext& ctx){
    // 上下の黒帯 (始めと終わりはフェードする)
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    float duration=cinematicBoss_?SwarmEmergenceSeconds+BossShotHoldSeconds+BossShotReturnSeconds:OpeningShotSeconds;
    float fade=ShotEase(cinematicAge_,0,.35f)*(1-ShotEase(cinematicAge_,duration-.55f,duration));
    ui.Fill({0,0,1280,52*fade},{.01f,.025f,.025f,1});
    ui.Fill({0,720-52*fade,1280,52*fade},{.01f,.025f,.025f,1});
    ui.MultiplyAlpha(fade);
    // タイトルと日本語の字幕
    const char* title=cinematicBoss_?"THE SERPENT AWAKENS":"SLIME SWARM";
    const float w=ui.Width(title,40);
    ui.Brush({640-w*.5f-60,560,w+120,64},UI::WithAlpha(cinematicBoss_?UI::Gold:UI::Lime,.94f),-.025f);
    ui.Center(title,640,569,40,UI::Ink);
    ui.Bold(cinematicBoss_?"巨影、目覚める":"軌跡を描け。囲って、撃ち放て。",640-ui.Width(cinematicBoss_?"巨影、目覚める":"軌跡を描け。囲って、撃ち放て。",21,UI::JapaneseFont)*.5f,634,21,UI::Paper,UI::JapaneseFont);
}
}
