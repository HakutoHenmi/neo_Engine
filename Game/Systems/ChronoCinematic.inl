namespace Game {
bool ChronoSystem::UpdateCinematic(entt::registry& r,Player& p,GameContext& ctx){
    auto& ink=r.get<InkPlayer>(player_);
    // Scripted combat fixtures retain their timing; normal play uses the director.
    if(r.all_of<ControlFrame>(player_)){openingPending_=false;return false;}
    if(!cinematicActive_){
        if(!openingPending_&&ink.battlePhase!=BattlePhase::Emerging)return false;
        cinematicBoss_=!openingPending_;openingPending_=false;cinematicAge_=0;cinematicActive_=true;
        if(!cinematicBoss_)InkCamera(r,p,ctx);
        cinematicReturn_={Read(ctx.camera->Position()),Read(ctx.camera->Rotation()),
            2*std::atan(1/DirectX::XMVectorGetY(ctx.camera->Proj().r[1]))};
        auto params=ctx.renderer->GetPostProcessParams();
        cinematicDofFocus_=params.dofFocus;cinematicDofRange_=params.dofRange;cinematicDofStrength_=params.dofStrength;
        if(cinematicBoss_){
            // Measure the fully surfaced pose, including mesh scale, rotation, head and tail.
            // PoseCreature restores this pose before each emergence depth is applied.
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
        ctx.camera->StopShake();
        Engine::Audio::GetInstance()->SetBGMDucked(true);
    }
    const float dt=std::clamp(ctx.dt,0.f,.05f);
    cinematicAge_+=dt;
    const float returnStart=SwarmEmergenceSeconds+BossShotHoldSeconds;
    const float duration=cinematicBoss_?returnStart+BossShotReturnSeconds:OpeningShotSeconds;
    ctx.renderer->SetRogueWorldFrozen(true);ctx.camera->SetHandheld(0);
    Engine::WindowDX::SetCursorVisible(false);p.cameraOpacity=1;
    p.worldScale=p.aimScale=0;
    if(ctx.combatFlow)ctx.combatFlow->SetChronoEnemyScale(0);
    V player=Read(r.get<TransformComponent>(player_).translate),focus=player+V{0,1,0};
    CinematicPose pose;
    if(cinematicBoss_){
        // Only the reveal clock advances: attacks, projectiles, XP and the player remain frozen.
        ink.battleAge=std::min(cinematicAge_,SwarmEmergenceSeconds);
        PoseCreature(r);SetSwarmBossVisible(r,true,48*(1-ShotEase(ink.battleAge,0,SwarmEmergenceSeconds)));
        V axis=Unit(V{player.x-swarmEmergenceAt_.x,0,player.z-swarmEmergenceAt_.z});
        V side{axis.z,0,-axis.x};
        float orbit=ShotEase(cinematicAge_,.7f,SwarmEmergenceSeconds);
        float aspect=ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y);
        // Refit during the orbit; interpolating two fitted cameras can clip an intermediate view.
        pose=FrameShot(cinematicBossBounds_,axis+side*(.36f-.62f*orbit)+V{0,.35f-.1f*orbit,0},.96f-.14f*orbit,aspect);
        pose=BlendShot(cinematicReturn_,pose,ShotEase(cinematicAge_,0,.7f));
        pose=BlendShot(pose,cinematicReturn_,ShotEase(cinematicAge_,returnStart,duration));
    }else{
        auto wide=AimShot(focus+V{26,19,-35},focus+V{0,0,15},1.02f);
        auto hero=AimShot(focus+V{8,4,-11},focus,.86f);
        pose=BlendShot(wide,hero,ShotEase(cinematicAge_,.25f,2.1f));
        pose=BlendShot(pose,cinematicReturn_,ShotEase(cinematicAge_,2.35f,duration));
    }
    // Ground and solid checks also protect reveal cameras near the edge of the meadow.
    if(cinematicAge_<duration){
        V target=cinematicBoss_?swarmEmergenceAt_+V{0,8,0}:focus;float fraction;V normal;
        for(const auto& box:solids_)if(Chrono::Sweep(target,pose.at-target,box,{.3f,.3f,.3f},fraction,normal))
            pose.at=target+(pose.at-target)*std::max(.05f,fraction-.025f);
        pose.at.y=std::max(pose.at.y,InkGround(pose.at)+.7f);
    }
    ctx.camera->SetPosition(Write(pose.at));ctx.camera->SetRotation(Write(pose.rotation));
    ctx.camera->SetProjection(pose.fov,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),.1f,2000);
    auto params=ctx.renderer->GetPostProcessParams();params.cinematicCamera=true;params.dofStrength=.7f;params.dofRange=cinematicBoss_?50.f:8.f;
    params.dofFocus=Length((cinematicBoss_?(cinematicBossBounds_.min+cinematicBossBounds_.max)*.5f:focus)-pose.at);
    params.dofBossDepth=cinematicBoss_?params.dofFocus:-1000;ctx.renderer->SetPostProcessParams(params);
    // Consume held action edges so a reveal cannot accidentally fire or dodge on resume.
    ink.previousAttack=Down(VK_LBUTTON);ink.previousDodge=Down(VK_RBUTTON);ink.previousLock=Down(VK_MBUTTON);prevSpace_=Down(VK_SPACE);
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
void ChronoSystem::DrawCinematic(GameContext& ctx){
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    float duration=cinematicBoss_?SwarmEmergenceSeconds+BossShotHoldSeconds+BossShotReturnSeconds:OpeningShotSeconds;
    float fade=ShotEase(cinematicAge_,0,.35f)*(1-ShotEase(cinematicAge_,duration-.55f,duration));
    ui.Fill({0,0,1280,52*fade},{.01f,.025f,.025f,1});
    ui.Fill({0,720-52*fade,1280,52*fade},{.01f,.025f,.025f,1});
    ui.MultiplyAlpha(fade);
    ui.Fill({480,554,320,2},cinematicBoss_?UI::Gold:UI::Lime);
    ui.Center(cinematicBoss_?"THE SERPENT AWAKENS":"SLIME SWARM",640,574,38,cinematicBoss_?UI::Gold:UI::Lime);
    ui.Center(cinematicBoss_?"巨影、目覚める":"軌跡を描け。囲って、撃ち放て。",640,622,21,UI::Paper,UI::JapaneseFont);
}
}
