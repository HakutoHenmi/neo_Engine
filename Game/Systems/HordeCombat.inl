// Included inside namespace Game; bounded crowds use shared meshes and instancing.
void ChronoSystem::BuildSwarm(entt::registry& r){
    familiars_.clear();rogueDamageClock_=0;
    swarm_.clear();swarmEffects_.clear();swarmNextId_=1;swarmSpawnClock_=0;swarmSoundClock_=0;swarmAttackClock_=2.5f;swarmAttackBurst_=0;
    swarmAttackCursor_=0;swarmRestPending_=false;
    auto* renderer=Engine::Renderer::GetInstance();
    renderer->CreateShaderPipeline("HordeArmor",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/HordeArmorPS.hlsl");
    renderer->CreateShaderPipelineTransparent("HordeEffect",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/HordeEffectPS.hlsl",true,true);
    const char* paths[]={"Resources/Models/Chrono/armor.obj","Resources/Models/Chrono/beak.obj","Resources/Models/Chrono/horn.obj"};
    for(size_t i=0;i<swarmMeshes_.size();++i){auto& mesh=swarmMeshes_[i];mesh.handle=renderer->LoadObjMesh(paths[i]);
        V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& v:renderer->GetModel(mesh.handle)->GetData().vertices){
            lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
            hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};}
        mesh.center=(hi+lo)*.5f;V extent=hi-lo;mesh.inverseExtent={1/std::max(.001f,extent.x),1/std::max(.001f,extent.y),1/std::max(.001f,extent.z)};
    }
    swarmQuad_=renderer->CreateDynamicMesh({{{-1,0,-1,1},{0,0},{0,1,0}},{{1,0,-1,1},{1,0},{0,1,0}},{{1,0,1,1},{1,1},{0,1,0}},{{-1,0,1,1},{0,1},{0,1,0}}},{0,1,2,0,2,3});
    V player=Read(r.get<TransformComponent>(player_).translate);
    for(int i=0;i<72;++i){
        SwarmEnemy enemy;enemy.id=swarmNextId_++;SwarmRole(enemy);
        enemy.at=SwarmSpawnPosition(player,enemy.id);enemy.at.y=InkGround(enemy.at)+(enemy.flying?18.f+float(enemy.id%7)*2:1.f);
        enemy.heading=Unit(player-enemy.at);swarm_.push_back(enemy);}
    auto& ink=r.get<InkPlayer>(player_);ink.swarmSpawned=int(swarm_.size());ink.swarmAlive=int(swarm_.size());
    SetSwarmBossVisible(r,false);
}
void ChronoSystem::SetSwarmBossVisible(entt::registry& r,bool visible,float depth){
    for(auto e:r.view<CreaturePart,MeshRendererComponent,TransformComponent>()){
        r.get<MeshRendererComponent>(e).enabled=visible;
        if(visible&&depth>0)r.get<TransformComponent>(e).translate.y-=depth;
    }
    r.get<MeshRendererComponent>(weakpoint_).enabled=visible;
    if(visible&&depth>0)r.get<TransformComponent>(weakpoint_).translate.y-=depth;
    if(!visible||depth>0){r.get<Target>(weakpoint_).active=false;creatureSolids_.clear();}
}
void ChronoSystem::HitSwarm(entt::registry& r,Player& p,GameContext& ctx,uint32_t id,float damage,bool credit){
    auto found=std::find_if(swarm_.begin(),swarm_.end(),[&](const SwarmEnemy& enemy){return enemy.id==id&&SwarmAlive(enemy);});
    if(found==swarm_.end())return;found->health-=damage*(found->inPool?1.25f:1);found->flash=.12f;if(found->health>0)return;
    found->phase=SwarmPhase::Dead;
    if(swarmEffects_.size()<128)swarmEffects_.push_back({found->at,0,found->bomber?7.f:4.f,found->bomber?1:2});
    auto& ink=r.get<InkPlayer>(player_);
    if(credit){ink.rogue.Gain(1);++ink.swarmKills;++ink.swarmCombo;ink.swarmBestCombo=std::max(ink.swarmBestCombo,ink.swarmCombo);ink.swarmComboAge=3;
        p.mass=std::min(SlimeMaximumMass,p.mass+3.f);
        if(swarmSoundClock_<=0){auto* audio=Engine::Audio::GetInstance();audio->Play(slimeAcidSound_,false,.12f*audio->GetMasterSEVolume(),1.05f+std::min(.55f,ink.swarmCombo*.012f));swarmSoundClock_=.08f;}}
    // Destroying an armed core causes a local chain reaction, not a player-damaging blast.
    if(found->bomber){V at=found->at;
        for(auto& enemy:swarm_)if(SwarmAlive(enemy)&&Length(enemy.at-at)<7){
            enemy.health=0;enemy.phase=SwarmPhase::Dead;
            if(swarmEffects_.size()<128)swarmEffects_.push_back({enemy.at,0,3.5f,2});
            if(credit){ink.rogue.Gain(1);++ink.swarmKills;++ink.swarmCombo;ink.swarmBestCombo=std::max(ink.swarmBestCombo,ink.swarmCombo);}
        }
    }
    (void)ctx;
}
uint32_t ChronoSystem::ChooseSwarmTarget(entt::registry& r,V from,bool preferBoss){
    const auto& ink=r.get<InkPlayer>(player_);bool bossAvailable=ink.battlePhase==BattlePhase::Boss&&!r.get<Boss>(boss_).countered;
    if(!ink.hordeEnabled)return bossAvailable?0:SwarmNoTarget;
    if(preferBoss&&bossAvailable)return 0;
    V player=Read(r.get<TransformComponent>(player_).translate);float best=1e9f;uint32_t target=SwarmNoTarget;
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){
        float score=DomainDistance(player,enemy.at)+DomainDistance(from,enemy.at)*.04f+(enemy.reserved>=enemy.health?10000.f:0.f);
        if(enemy.bomber&&enemy.phase==SwarmPhase::Windup)score-=15;
        if(score<best){best=score;target=enemy.id;}
    }
    if(bossAvailable&&(target==SwarmNoTarget||best>9000))return 0;
    return target;
}
void ChronoSystem::UpdateSwarm(entt::registry& r,Player& p,GameContext& ctx){
    auto& ink=r.get<InkPlayer>(player_);float dt=std::min(ctx.dt,.05f)*p.worldScale;
    if(!ink.hordeEnabled){
        if(ink.battlePhase!=BattlePhase::Boss){ink.battlePhase=BattlePhase::Boss;PoseCreature(r);SetSwarmBossVisible(r,true);swarm_.clear();}
        return;
    }
    V player=Read(r.get<TransformComponent>(player_).translate);
    ink.battleAge+=dt;ink.swarmComboAge=std::max(0.f,ink.swarmComboAge-dt);if(ink.swarmComboAge==0)ink.swarmCombo=0;
    swarmSoundClock_=std::max(0.f,swarmSoundClock_-dt);
    for(auto& effect:swarmEffects_)effect.age+=dt;
    swarmEffects_.erase(std::remove_if(swarmEffects_.begin(),swarmEffects_.end(),[](const SwarmEffect& effect){return effect.age>.7f;}),swarmEffects_.end());
    swarm_.erase(std::remove_if(swarm_.begin(),swarm_.end(),[&](const SwarmEnemy& enemy){return !SwarmAlive(enemy)||(enemy.age>35&&DomainDistance(enemy.at,player)>170);}),swarm_.end());
    swarmSpawnClock_+=dt;float interval=ink.battlePhase==BattlePhase::Boss?.7f:1.f;
    if(swarmSpawnClock_>=interval){swarmSpawnClock_=std::fmod(swarmSpawnClock_,interval);
        for(int i=0;i<12&&swarm_.size()<SwarmCapacity;++i){uint32_t id=swarmNextId_++;
            SwarmEnemy enemy;enemy.id=id;SwarmRole(enemy);
            enemy.at=SwarmSpawnPosition(player,id);
            enemy.at.y=InkGround(enemy.at)+(enemy.flying?18.f+float(id%7)*2:1.f);enemy.heading=Unit(player-enemy.at);swarm_.push_back(enemy);++ink.swarmSpawned;}
    }
    swarmAttackClock_=std::max(0.f,swarmAttackClock_-dt);int activeAttacks=0;
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&(enemy.phase==SwarmPhase::Windup||enemy.phase==SwarmPhase::Dash))++activeAttacks;
    if(swarmRestPending_&&activeAttacks==0){swarmRestPending_=false;swarmAttackClock_=2.8f+SwarmVariation(swarmNextId_)*.8f;}
    const size_t first=swarm_.empty()?0:swarmAttackCursor_%swarm_.size();
    for(size_t step=0;step<swarm_.size();++step){size_t index=(first+step)%swarm_.size();auto& enemy=swarm_[index];
        V before=enemy.at;bool dashing=enemy.phase==SwarmPhase::Dash;auto previous=enemy.phase;
        auto event=TickSwarm(enemy,player,dt*(enemy.slow>0?.5f:enemy.inPool?.65f:1.f),InkGround(enemy.at),!swarmRestPending_&&activeAttacks<3&&swarmAttackClock_<=0);
        if(previous!=SwarmPhase::Windup&&enemy.phase==SwarmPhase::Windup){++activeAttacks;
            swarmAttackCursor_=index+1;
            swarmAttackClock_=1+SwarmVariation(enemy.id,enemy.attacks)*.4f;
            if(++swarmAttackBurst_>=4){swarmAttackBurst_=0;swarmRestPending_=true;}}
        enemy.at.x=std::clamp(enemy.at.x,-320.f,320.f);enemy.at.z=std::clamp(enemy.at.z,-330.f,370.f);
        enemy.at.y=enemy.flying?std::max(enemy.at.y,InkGround(enemy.at)+1.2f):InkGround(enemy.at)+1;
        if(event==SwarmEvent::Detonate){enemy.health=0;++ink.swarmDetonations;
            if(swarmEffects_.size()<128)swarmEffects_.push_back({enemy.at,0,8,1});
            if(Length(player-enemy.at)<7){p.damageReason="HIT - CORE DETONATION";Damage(r,p,9,ctx,enemy.at,.24f);}
        }else if(dashing&&!enemy.struck){V delta=enemy.at-before;float hit;
            if(Length(player-enemy.at)<3||RaySphere(before,Unit(delta),player,3,Length(delta),hit)){
                enemy.struck=true;++ink.swarmStrikes;p.damageReason="HIT - ARMORED SWARM";Damage(r,p,4,ctx,enemy.at,.2f);}
        }
    }
    // Local repulsion keeps dense packs readable; attacks retain their committed direction.
    for(size_t i=0;i<swarm_.size();++i)for(size_t j=i+1;j<swarm_.size();++j){auto& a=swarm_[i];auto& b=swarm_[j];
        if(!SwarmAlive(a)||!SwarmAlive(b)||a.flying!=b.flying)continue;V delta=a.at-b.at;if(!a.flying)delta.y=0;float distance=Length(delta);
        if(distance<2.7f&&distance>.01f){V push=delta*((2.7f-distance)*dt*1.8f/distance);
            if(a.phase==SwarmPhase::Chase)a.at=a.at+push;if(b.phase==SwarmPhase::Chase)b.at=b.at-push;}}
    V camera=Read(ctx.camera->Position());float opacityBlend=1-std::exp(-18*std::min(ctx.dt,.05f));
    for(auto& enemy:swarm_){float goal=SwarmCameraOpacity(camera,player,enemy.at,enemy.flying);
        enemy.cameraOpacity+=(goal-enemy.cameraOpacity)*opacityBlend;}
    ink.swarmAlive=0;ink.swarmActiveAttacks=0;for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){++ink.swarmAlive;
        if(enemy.phase==SwarmPhase::Windup||enemy.phase==SwarmPhase::Dash)++ink.swarmActiveAttacks;}
    if(ink.battlePhase==BattlePhase::Horde){
        SetSwarmBossVisible(r,false);
        if(SwarmBossReady(ink.swarmKills)){
            ink.battlePhase=BattlePhase::Emerging;ink.battleAge=0;ink.lockedOn=false;
            swarmEmergenceAt_={std::clamp(player.x+35.f,-230.f,230.f),0,std::clamp(player.z+60.f,-240.f,280.f)};
            auto& creature=r.get<CreatureBoss>(boss_);creature={};creature.stage=CreatureStage::Snake;creature.form=0;
            creature.offset=swarmEmergenceAt_-EvaluateCreature(0,0).core;creature.heading=std::atan2(swarmEmergenceAt_.x-player.x,swarmEmergenceAt_.z-player.z);
            r.get<Boss>(boss_).countered=false;ink.armor=0;ink.coreHealth=100;ink.downTimer=0;
            ctx.camera->StartImpactShake(.45f,.18f,{0,1,0},7);
            auto* audio=Engine::Audio::GetInstance();audio->Play(slimePressureSound_,false,.4f*audio->GetMasterSEVolume(),.55f);
        }
    }
    if(ink.battlePhase==BattlePhase::Emerging){
        PoseCreature(r);float progress=std::clamp(ink.battleAge/SwarmEmergenceSeconds,0.f,1.f);
        SetSwarmBossVisible(r,true,48*(1-Ease(progress)));
        if(progress>=1){ink.battlePhase=BattlePhase::Boss;ink.battleAge=0;ctx.camera->StartImpactShake(.22f,.13f,{0,1,0},11);}
    }
}
void ChronoSystem::DrawSwarm(entt::registry& r,GameContext& ctx){
    const auto& ink=r.get<InkPlayer>(player_);if(!ink.hordeEnabled||finished_)return;
    float bodyOpacity=1;
    auto piece=[&](int index,V at,V size,float yaw,Engine::Vector4 color){const auto& mesh=swarmMeshes_[size_t(index)];
        Engine::Transform t;t.scale={size.x*mesh.inverseExtent.x,size.y*mesh.inverseExtent.y,size.z*mesh.inverseExtent.z};t.rotate.y=yaw;
        V center{mesh.center.x*t.scale.x,mesh.center.y*t.scale.y,mesh.center.z*t.scale.z};float cs=std::cos(yaw),sn=std::sin(yaw);
        t.translate=EV(at-V{center.x*cs+center.z*sn,center.y,-center.x*sn+center.z*cs});
        // Store hit/arming light separately from opacity in the existing instance payload.
        color.w=2*std::round(std::clamp(color.w,0.f,1.f)*16)+bodyOpacity;
        ctx.renderer->DrawMeshInstanced(mesh.handle,white_,t,color,"HordeArmor");};
    auto floorEffect=[&](V at,float size,int kind,float progress,Engine::Vector4 color){Engine::Transform t;
        t.translate=EV(V{at.x,InkGround(at)+.18f,at.z});t.scale={size,1,size};color.w=kind*2+std::clamp(progress,0.f,.999f);
        ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,t,color,"HordeEffect");};
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){
        bodyOpacity=enemy.cameraOpacity;
        float yaw=std::atan2(enemy.heading.x,enemy.heading.z),bob=std::sin(enemy.age*9+float(enemy.id))*.13f;
        float warning=enemy.phase==SwarmPhase::Windup?1.f:0.f;Engine::Vector4 metal{.68f,.77f,.83f,enemy.flash*6+warning*.55f};
        if(enemy.slow>0)metal={.3f,.7f,1,metal.w};else if(enemy.burn>0)metal={1,.4f,.15f,metal.w};else if(enemy.corrosion>0||enemy.inPool)metal={.3f,.95f,.4f,metal.w};
        V base=enemy.at+V{0,bob,0};if(enemy.phase==SwarmPhase::Spawn)base.y-=2*(1-std::clamp(enemy.clock/.6f,0.f,1.f));
        V side{std::cos(yaw),0,-std::sin(yaw)};
        if(enemy.flying){float flap=std::sin(enemy.age*10+float(enemy.id))*.65f;
            for(float sign:{-1.f,1.f})piece(0,base+side*(sign*2.1f)+V{0,flap,0},{3.6f,.32f,2.1f},yaw,metal);}
        for(int segment=0;segment<3;++segment)piece(0,base-enemy.heading*(segment*1.15f),{2.2f-segment*.25f,1.8f-segment*.25f,1.6f},yaw,segment==1?Engine::Vector4{.09f,.14f,.2f,metal.w}:metal);
        piece(1,base+enemy.heading*1.2f,{2.1f,1.2f,2.6f},yaw,{.15f,.2f,.27f,metal.w});
        for(float sign:{-1.f,1.f}){piece(2,base+side*(sign*.85f)+V{0,.8f,0},{.4f,1.4f,.6f},yaw,metal);
            Engine::Transform eye;eye.translate=EV(base+enemy.heading*1.35f+side*(sign*.58f)+V{0,.65f,0});eye.scale={.25f,.25f,.35f};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,eye,enemy.bomber?Engine::Vector4{1,.48f,.08f,bodyOpacity}:Engine::Vector4{.2f,.85f,1,bodyOpacity},"DomainGlow");}
        if(enemy.bomber){Engine::Transform core;core.translate=EV(base+V{0,.6f,0});core.scale={.75f,.75f,.75f};
            ctx.renderer->DrawMeshInstanced(sphere_,white_,core,{1,.25f,.035f,.9f*bodyOpacity},"DomainGlow");}
        if(enemy.phase==SwarmPhase::Windup){
            if(enemy.bomber)floorEffect(enemy.at,7,0,enemy.clock/SwarmWindup(enemy),{1,.25f,.035f,1});
            else if(enemy.flying)floorEffect(enemy.attackAt,3.5f,0,enemy.clock/SwarmWindup(enemy),{1,.25f,.035f,1});
            else{V at=(enemy.at+enemy.attackAt)*.5f;Engine::Transform dashCue;
                dashCue.translate=EV(V{at.x,InkGround(at)+.18f,at.z});dashCue.rotate.y=yaw;dashCue.scale={3,1,DomainDistance(enemy.at,enemy.attackAt)*.5f+3};
                ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,dashCue,{1,.25f,.035f,10+std::clamp(enemy.clock/SwarmWindup(enemy),0.f,.999f)},"HordeEffect");}
        }
        if(enemy.phase==SwarmPhase::Spawn)floorEffect(enemy.at,3,3,enemy.clock/.6f,{.12f,.65f,1,1});
    }
    V camera=Read(ctx.camera->Position());
    for(const auto& effect:swarmEffects_){float progress=effect.age/.7f;Engine::Vector4 color=effect.kind==1?Engine::Vector4{1,.32f,.05f,1}:Engine::Vector4{.2f,.85f,1,1};
        floorEffect(effect.at,effect.size,effect.kind,progress,color);
        V normal=Unit(camera-effect.at),right=Unit(SwarmCross({0,1,0},normal)),up=SwarmCross(normal,right);Engine::Matrix4x4 world{};
        world.m[0][0]=right.x*effect.size;world.m[0][1]=right.y*effect.size;world.m[0][2]=right.z*effect.size;
        world.m[1][0]=normal.x;world.m[1][1]=normal.y;world.m[1][2]=normal.z;
        world.m[2][0]=up.x*effect.size;world.m[2][1]=up.y*effect.size;world.m[2][2]=up.z*effect.size;
        world.m[3][0]=effect.at.x;world.m[3][1]=effect.at.y+1;world.m[3][2]=effect.at.z;world.m[3][3]=1;color.w=effect.kind*2+progress;
        ctx.renderer->DrawMeshInstanced(swarmQuad_,white_,world,color,"HordeEffect");
    }
    if(ink.battlePhase==BattlePhase::Emerging)floorEffect(swarmEmergenceAt_,36,4,ink.battleAge/SwarmEmergenceSeconds,{.15f,.7f,1,1});
}

