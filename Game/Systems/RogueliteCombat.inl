// Run-local growth; the combat clocks do not advance while choosing a palette.
bool ChronoSystem::UpdateRogueMenu(entt::registry& r,Player& p,GameContext& ctx){
    auto& ink=r.get<InkPlayer>(player_);auto& state=ink.rogue;
    if(!state.enabled||finished_){ctx.renderer->SetRogueWorldFrozen(false);return false;}
    // Pending growth interrupts immediately, even during a continuous volley.
    if(state.Ready()){
        rogueFluidAspect_=ink.fluidAspect;rogueFluidMotion_=ink.fluidMotion;
        rogueCameraPosition_=Read(ctx.camera->Position());rogueCameraRotation_=Read(ctx.camera->Rotation());
        rogueCameraFov_=2*std::atan(1/DirectX::XMVectorGetY(ctx.camera->Proj().r[1]));
        auto params=ctx.renderer->GetPostProcessParams();rogueDofFocus_=params.dofFocus;rogueDofRange_=params.dofRange;rogueDofStrength_=params.dofStrength;
        state.Open();Engine::Audio::GetInstance()->SetBGMDucked(true);
    }
    if(!state.menu){ctx.renderer->SetRogueWorldFrozen(false);return false;}
    ctx.renderer->SetRogueWorldFrozen(true);Engine::WindowDX::SetCursorVisible(true);
    float dt=std::clamp(ctx.dt,0.f,.05f);state.TickMenu(dt);
    // Let the player settle into a breathing idle, then restore the captured
    // movement pose along the same eased path when combat resumes.
    ink.fluidAspect=state.IdleAspect(rogueFluidAspect_);
    ink.fluidMotion=rogueFluidMotion_*(1-state.MenuEase());
    p.cameraOpacity=1;
    V pos=Read(r.get<TransformComponent>(player_).translate),forward=Forward(yaw_,.08f),right{std::cos(yaw_),0,-std::sin(yaw_)};
    V camera=pos-forward*10+V{0,2.1f,0};V view=Unit(pos-right*4.1f+V{0,-.8f,0}-camera);
    float blend=state.MenuEase();V rotation{-std::asin(std::clamp(view.y,-1.f,1.f)),std::atan2(view.x,view.z),0};
    // The same captured gameplay pose is used at both ends; yaw takes the shortest arc.
    rotation={rogueCameraRotation_.x+std::remainder(rotation.x-rogueCameraRotation_.x,6.283185f)*blend,
        rogueCameraRotation_.y+std::remainder(rotation.y-rogueCameraRotation_.y,6.283185f)*blend,rogueCameraRotation_.z*(1-blend)};
    ctx.camera->SetPosition(Write(Lerp(rogueCameraPosition_,camera,blend)));ctx.camera->SetRotation(Write(rotation));
    ctx.camera->SetProjection(rogueCameraFov_+(.9f-rogueCameraFov_)*blend,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),.1f,2000);ctx.camera->SetHandheld(.008f*(1-blend));
    auto params=ctx.renderer->GetPostProcessParams();params.cinematicCamera=true;params.dofFocus=rogueDofFocus_+(10-rogueDofFocus_)*blend;
    params.dofRange=rogueDofRange_+(8-rogueDofRange_)*blend;params.dofStrength=rogueDofStrength_+(.9f-rogueDofStrength_)*blend;ctx.renderer->SetPostProcessParams(params);
    if(!state.menu){
        params.cinematicCamera=false;ctx.renderer->SetPostProcessParams(params);
        ink.previousAttack=true;ctx.renderer->SetRogueWorldFrozen(false);Engine::Audio::GetInstance()->SetBGMDucked(false);
        Engine::WindowDX::SetCursorVisible(false);return true;
    }
    if(!state.CanChoose())return true;
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
    auto* control=r.try_get<ControlFrame>(player_);int choice=control?control->upgradeChoice:-1;
    if(UI::Pressed(DIK_UP)||UI::Pressed(DIK_W))state.selected=(state.selected+2)%3;
    if(UI::Pressed(DIK_DOWN)||UI::Pressed(DIK_S))state.selected=(state.selected+1)%3;
    for(int i=0;i<3;++i)if(ui.Click(UI::RogueChoice(i))||UI::Pressed(BYTE(DIK_1+i)))choice=i;
    if(UI::Pressed(DIK_RETURN))choice=state.selected;
    if((control&&control->upgradeReroll)||UI::Pressed(DIK_R)||ui.Click(UI::RogueReroll)){state.Reroll();return true;}
    if((control&&control->upgradeSkip)||UI::Pressed(DIK_X)||ui.Click(UI::RogueSkip)){p.mass=std::min(SlimeMaximumMass,p.mass+60);state.Close();}
    else if(choice>=0)state.Choose(choice);
    r.get<HealthComponent>(player_).SetHp(p.mass);
    return true;
}
void ChronoSystem::ActivateRogue(entt::registry& r,Player& p,GameContext&,HomingDomain& domain){
    (void)p;auto& state=r.get<InkPlayer>(player_).rogue;if(!state.enabled)return;
    domain.total+=12*state.ranks[0];domain.bulletPower=domain.bulletPower*state.Power(domain.loop.area)+.5f*state.ranks[2];
    domain.meteor=state.ranks[5]>0&&domain.loop.shape==DomainShape::Loop;
    domain.remaining=domain.meteor?1:domain.loop.shape==DomainShape::Triangle?3:domain.total;
    domain.lifetime=1.2f+3*state.ranks[3];if(state.ranks[7])domain.lifetime=std::max(domain.lifetime,8.f+3*state.ranks[3]);
    if(state.ranks[1])state.haste=2;
    if(state.ranks[12])state.orbit=8;
    if(state.ranks[14]){V center{};for(auto point:domain.loop.points)center=center+point;center=center*(1.f/float(domain.loop.points.size()));center.y=InkGround(center)+1;
        if(familiars_.size()>=6)familiars_.erase(familiars_.begin());
        familiars_.push_back({center,0,0,12.f+4*(state.ranks[14]-1)+6*state.ranks[16]});}
    if(state.ranks[15])state.resonance=3;
}
void ChronoSystem::PerfectRogueLoop(entt::registry& r,Player& p,GameContext& ctx){
    if(!r.get<InkPlayer>(player_).rogue.ranks[13]||homingDomains_.size()>=6)return;
    HomingDomain domain;V pos=Read(r.get<TransformComponent>(player_).translate);auto& loop=domain.loop;
    for(int i=0;i<=32;++i){float angle=float(i)*6.283185f/32;V point=pos+V{std::cos(angle)*5,0,std::sin(angle)*5};point.y=InkGround(point)+.16f;loop.points.push_back(point);}
    loop.area=78.54f;loop.length=31.416f;domain.total=48;domain.remaining=48;domain.highlight=DomainHighlight(loop);domain.liquid.Sync(loop.points);
    ActivateRogue(r,p,ctx,domain);homingDomains_.push_back(std::move(domain));
}
void ChronoSystem::UpdateRogueSkills(entt::registry& r,Player& p,GameContext& ctx){
    auto& ink=r.get<InkPlayer>(player_);auto& state=ink.rogue;if(!state.enabled)return;
    float dt=std::min(ctx.dt,.05f);state.haste=std::max(0.f,state.haste-dt);state.orbit=std::max(0.f,state.orbit-dt);state.resonance=std::max(0.f,state.resonance-dt);
    state.interceptClock-=dt;
    V player=Read(r.get<TransformComponent>(player_).translate);
    for(auto& familiar:familiars_){familiar.age+=dt;familiar.clock+=dt;float interval=state.Synergy(RogueTag::Familiar)?1.f/3: .5f;
        if(familiar.clock>=interval&&domainMissiles_.size()<1024){familiar.clock=std::fmod(familiar.clock,interval);DomainMissile missile;missile.at=familiar.at+V{0,1,0};
            missile.target=ChooseSwarmTarget(r,missile.at,false);missile.power=1+.25f*state.ranks[16];missile.velocity={0,35,0};missile.seed=familiar.age;missile.tail.fill(missile.at);domainMissiles_.push_back(missile);}}
    familiars_.erase(std::remove_if(familiars_.begin(),familiars_.end(),[](const Familiar& f){return f.age>=f.lifetime;}),familiars_.end());
    rogueDamageClock_+=dt;if(rogueDamageClock_<.25f)return;float tick=rogueDamageClock_;rogueDamageClock_=0;
    auto acidLine=[&](V at){const auto& points=ink.domainPath.points;for(size_t i=1;i<points.size();++i){V a=points[i-1],delta=points[i]-a;float len2=Dot(delta,delta);float fraction=len2>.001f?std::clamp(Dot(at-a,delta)/len2,0.f,1.f):0;
            if(Length(at-(a+delta*fraction))<3)return true;}return false;};
    auto laser=[&](V at){if(state.resonance<=0)return false;for(size_t i=1;i<familiars_.size();++i){V a=familiars_[i-1].at,b=familiars_[i].at,d=b-a;float length=Dot(d,d);if(length>.001f&&Length(at-(a+d*std::clamp(Dot(at-a,d)/length,0.f,1.f)))<3)return true;}return false;};
    auto pool=[&](V at){for(const auto& d:homingDomains_)if(state.ranks[7]&&DomainContains(d.loop,at)&&std::abs(at.y-InkGround(at))<8)return true;return false;};
    for(auto& enemy:swarm_)if(SwarmAlive(enemy)){
        enemy.slow=std::max(0.f,enemy.slow-tick);enemy.corrosion=std::max(0.f,enemy.corrosion-tick);enemy.burn=std::max(0.f,enemy.burn-tick);
        enemy.inPool=pool(enemy.at);
        float damage=(enemy.corrosion>0?2.f*state.ranks[8]:0)+(enemy.burn>0?3.f*state.ranks[10]:0);
        if(state.Synergy(RogueTag::Debuff))damage*=1.5f;
        if(state.ranks[6]&&acidLine(enemy.at))damage+=4*state.ranks[6];
        if(enemy.inPool)damage+=2*state.ranks[7];
        if(state.orbit>0&&Length(enemy.at-player)<8)damage+=4*state.ranks[12];
        if(laser(enemy.at))damage+=6*state.ranks[15];
        if(damage>0)HitSwarm(r,p,ctx,enemy.id,damage*tick);
    }
    if(ink.battlePhase==BattlePhase::Boss){V at=Center(r,weakpoint_);float damage=0;
        if(state.ranks[6]&&acidLine(at))damage+=4*state.ranks[6];if(pool(at))damage+=2*state.ranks[7];
        if(state.orbit>0&&Length(at-player)<8)damage+=4*state.ranks[12];if(laser(at))damage+=6*state.ranks[15];
        float statusDamage=(ink.rogueBossCorrosion>0?2.f*state.ranks[8]:0)+(ink.rogueBossBurn>0?3.f*state.ranks[10]:0);damage+=statusDamage*(state.Synergy(RogueTag::Debuff)?1.5f:1.f);
        ink.rogueBossCorrosion=std::max(0.f,ink.rogueBossCorrosion-tick);ink.rogueBossBurn=std::max(0.f,ink.rogueBossBurn-tick);ink.rogueBossSlow=std::max(0.f,ink.rogueBossSlow-tick);
        ink.rogueBossPool=pool(at);if(damage>0)HitDomainMissile(r,p,ctx,at,false,damage*tick/4);
    }
}
