// Included by ChronoSystem.cpp: the new game's complete simulation path.
namespace Game {
void ChronoSystem::BuildInk(entt::registry& r){
    r.emplace_or_replace<InkPlayer>(player_);auto& p=r.get<Player>(player_);p.mass=SlimeMaximumMass;p.automatic=false;
    Engine::Renderer::GetInstance()->SetDrawFluidDebugArrows(false);
    Engine::Renderer::GetInstance()->SetFluidVolumeDebugMode(0);
    std::vector<entt::entity> old;
    for(auto e:r.view<NameComponent>())if((r.get<NameComponent>(e).name.find("Chrono Creature ")==0&&!r.all_of<CreaturePart>(e))||r.all_of<Anchor>(e)||r.all_of<Feather>(e))old.push_back(e);
    for(auto e:old)r.destroy(e);
    Block(r,"Ink foundation",{0,-4.02f,20},{280,8,300});
    for(float x:{-53.f,53.f}){
        auto obstacle=Mesh(r,"Acid soluble barrier",grass,{x,2,24},{4,4,4});
        auto& acid=r.emplace<SlimeDissolvable>(obstacle);acid.center={x,2,24};
        r.get<MeshRendererComponent>(obstacle).color={.45f,.65f,.22f,1};
    }
    // Painted top surfaces use a shared atlas; the ramps are continuous collision planes.
    auto add=[&](V origin,V u,V v,float width,float depth){InkSurface surface;surface.origin=origin;surface.u=u;surface.v=v;surface.width=width;surface.depth=depth;inkSurfaces_.push_back(surface);};
    add({-140,0,-130},{1,0,0},{0,0,1},280,300);
    for(float x:{-43.f,21.f}){
        add({x,0,-22},{1,0,0},Unit(V{0,18,62}),22,Length(V{0,18,62}));
        add({x,18,40},{1,0,0},{0,0,1},22,28);
        Block(r,"Ink tower",{x+11,8.99f,54},{22,17.98f,28});
    }
    for(float x:{-43.f,21.f}){
        add({x-.02f,0,68},{0,0,-1},{0,1,0},28,18);
        add({x+22.02f,0,40},{0,0,1},{0,1,0},28,18);
        add({x,0,39.98f},{1,0,0},{0,1,0},22,18);
        add({x+22,0,68.02f},{-1,0,0},{0,1,0},22,18);
    }
    // Purely decorative curbs; play bounds are clamped to the painted arena.
    // A continuous low rim makes the playable boundary legible. Scenery beyond
    // it has no collision, paint surfaces or per-particle physics cost.
    for(float x:{-142.f,142.f})Block(r,"Basin rim",{x,1,20},{4,2,304});
    for(float z:{-132.f,172.f})Block(r,"Basin rim",{0,1,z},{280,2,4});
    const std::string nature="Resources/Models/Quaternius/Nature/FBX/";
    auto scenery=[&](const std::string& name,const std::string& path,V at,V size){
        auto e=Mesh(r,name,path,at,size);r.remove<CameraOccluder>(e);return e;
    };
    scenery("Valley backdrop ground",grass,{0,-15,20},{900,20,900});
    for(int side=0;side<4;++side)for(int i=0;i<7;++i){
        float along=-270.f+i*90.f,height=70.f+float((i*3+side)%5)*17.f;
        V at=side<2?V{along,height*.5f-5,side==0?-310.f:350.f}:V{side==2?-320.f:320.f,height*.5f-5,along+20};
        scenery("Distant mountain",nature+"RockPlatforms_Large.fbx",at,{115,height,105});
    }
    for(int side=0;side<4;++side)for(int i=0;i<8;++i){
        float along=-119.f+i*34.f;
        V at=side<2?V{along,0,side==0?-158.f:199.f}:V{side==2?-168.f:168.f,0,along+20};
        float height=13.f+(i%3)*4.f;
        scenery("Woodland tree",nature+"Tree.fbx",at+V{0,height*.5f,0},{height*.65f,height,height*.65f});
        if(i%2==0)scenery("Basin rocks",nature+"Rock_1.fbx",at+V{13,3,7},{12,7,10});
    }
    scenery("Citadel island",grass,{0,0,228},{90,14,55});
    for(float x:{-28.f,0.f,28.f})scenery("Citadel tower","Resources/Models/Quaternius/Level and Mechanics/FBX/Tower.fbx",
        {x,x==0?28.f:23.f,228},{19,x==0?42.f:32.f,19});
    auto* render=Engine::Renderer::GetInstance();
    render->CreateShaderPipeline("InkSurface",L"Resources/shaders/ObjVS.hlsl",L"Resources/shaders/InkSurfacePS.hlsl");
    for(int i=0;i<static_cast<int>(inkSurfaces_.size());++i)inkMeshes_.push_back(render->LoadObjMesh("Resources/Models/Ink/surface"+std::to_string(i)+".obj"));
    slimeTrails_.clear();inkNeedsRebuild_=false;inkUploadClock_=0;inkDirty_=true;UploadInk();
    slimeBeamMesh_=render->LoadObjMesh("Resources/Models/Ink/slime-beam.obj");
    slimeRingMesh_=render->LoadObjMesh("Resources/Models/Ink/slime-ring.obj");
    render->CreateShaderPipelineTransparent("SlimeBeam",L"Resources/shaders/ObjVS.hlsl",L"Resources/shaders/SlimeBeamPS.hlsl",false,true);
    if(slimePressureSound_==0xffffffff){auto* audio=Engine::Audio::GetInstance();
        slimePressureSound_=audio->Load("Resources/Sound/slime-pressure.wav");slimeRecallSound_=audio->Load("Resources/Sound/slime-recall.wav");slimeAcidSound_=audio->Load("Resources/Sound/slime-acid.wav");}
}
void ChronoSystem::UploadInk(){
    constexpr int tile=InkSurface::Resolution,size=tile*4;std::vector<uint8_t> pixels(size*size*4,0);
    for(int k=0;k<static_cast<int>(inkSurfaces_.size());++k)for(int y=0;y<tile;++y)for(int x=0;x<tile;++x){
        int index=(((k/4)*tile+y)*size+(k%4)*tile+x)*4;
        pixels[index]=inkSurfaces_[k].mask[y*tile+x];pixels[index+3]=255;}
    inkTexture_=Engine::Renderer::GetInstance()->UpdatePaintTexture("boss-arena",size,size,pixels.data());inkDirty_=false;
}
float ChronoSystem::InkGround(V at,int* surface)const{
    float height=-1000;int found=-1;
    for(int i=0;i<static_cast<int>(inkSurfaces_.size());++i){float f;V hit;
        if(inkSurfaces_[i].Ray({at.x,200,at.z},{0,-400,0},f,hit)&&hit.y>height){height=hit.y;found=i;}}
    if(surface)*surface=found;return height;
}
void ChronoSystem::InkCamera(entt::registry& r,Player& p,GameContext& ctx){
    V pos=Read(r.get<TransformComponent>(player_).translate);
    if(!cameraReady_){cameraFollow_=pos;cameraReady_=true;}
    cameraFollow_=Follow(cameraFollow_,pos,ctx.dt,1.2f);
    V forward=Forward(yaw_,pitch_),right{std::cos(yaw_),0,-std::sin(yaw_)};
    auto& ink=r.get<InkPlayer>(player_);
    bool firing=ink.phase==SlimePhase::Firing;
    bool charging=ink.phase==SlimePhase::Charging;
    float power=ink.Power(charging?ink.charge:ink.beamCharge);
    // Smoothstep eases both ends of the shoulder move; aim input remains independent.
    auto approach=[&](float value,float target,float seconds){float step=std::min(ctx.dt,.05f)/seconds;return value+std::clamp(target-value,-step,step);};
    float shoulderTarget=firing?1.f:charging?std::clamp(.4f+power/.35f*.6f,0.f,1.f):0.f;
    float heavyTarget=(charging||firing)?std::clamp((power-.35f)/.4f,0.f,1.f):0.f;
    ink.beamView=approach(ink.beamView,shoulderTarget,(charging||firing)?.22f:.36f);
    ink.heavyView=approach(ink.heavyView,heavyTarget,(charging||firing)?.28f:.42f);
    float shot=Ease(ink.beamView),heavy=Ease(ink.heavyView);
    float pullback=(charging||firing)?3*power:0.f;
    ink.cameraPullback+=(pullback-ink.cameraPullback)*(1-std::exp(-10*ctx.dt));
    V focus=cameraFollow_+V{0,2.f,0};
    V wanted=focus-forward*(17.f+ink.cameraPullback+heavy*5)+right*(1.5f+shot*4.5f+heavy*8)+V{0,shot*1.2f+heavy*3,0};
    float f;V n;
    for(const auto& box:solids_)if(Chrono::Sweep(focus,wanted-focus,box,{.3f,.3f,.3f},f,n))wanted=focus+(wanted-focus)*std::max(.05f,f-.025f);
    for(const auto& surface:inkSurfaces_){V hit;if(surface.Ray(focus,wanted-focus,f,hit))wanted=focus+(wanted-focus)*std::max(.05f,f-.06f);}
    wanted.y=std::max(wanted.y,.7f);
    V beamForward=firing?Forward(ink.beamYaw,ink.beamPitch):forward;
    ink.cameraBeamDirection=Unit(Lerp(ink.cameraBeamDirection,beamForward,1-std::exp(-14*ctx.dt)));
    V lookAt=focus+ink.cameraBeamDirection*(45-heavy*20);
    V view=Unit(Lerp(forward,Unit(lookAt-wanted),shot));
    ctx.camera->SetPosition(Write(wanted));
    ctx.camera->SetRotation(-std::asin(std::clamp(view.y,-1.f,1.f)),std::atan2(view.x,view.z),0);
    float goal=(ink.swimming?1.22f:1.13f)+heavy*.06f;fov_+=(goal-fov_)*(1-std::exp(-6*ctx.dt));
    // A quick seven-degree kick, returning fully to the regular camera within 0.4 seconds.
    ctx.camera->SetProjection(fov_+.12f*SlimePerfectPulse(ink.perfectAge),ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),.1f,2000);
    ctx.camera->SetHandheld(.008f);p.cameraOpacity=1;
}
void ChronoSystem::UpdateInkBoss(entt::registry& r,Player& p,GameContext& ctx){
    auto& c=r.get<CreatureBoss>(boss_);auto& b=r.get<Boss>(boss_);
    auto& ink=r.get<InkPlayer>(player_);const float dt=std::min(ctx.dt,.05f)*p.worldScale;
    V player=Read(r.get<TransformComponent>(player_).translate);
    std::vector<entt::entity> expired;
    for(auto e:r.view<SlimeFeather,TransformComponent>()){
        auto& feather=r.get<SlimeFeather>(e);auto& transform=r.get<TransformComponent>(e);
        V before=Read(transform.translate),delta=feather.velocity*dt;float hit;
        bool blocked=false;float travel=Length(delta);
        for(const auto& surface:inkSurfaces_){V at;float fraction;if(surface.Ray(before,delta,fraction,at)){blocked=true;travel=std::min(travel,Length(delta)*fraction);}}
        for(const auto& box:solids_){V normal;float fraction;if(Chrono::Sweep(before,delta,box,{.3f,.3f,.3f},fraction,normal)){blocked=true;travel=std::min(travel,Length(delta)*fraction);}}
        bool contact=RaySphere(before,Unit(delta),player,2.f,travel,hit);
        // Generous graze region only awards a dodge; it never enlarges the damage hitbox.
        if(ink.dodgeAge<=.24f&&!ink.perfectUsed)
            contact|=RaySphere(before,Unit(delta),ink.dodgeOrigin,4.f,travel,hit)||RaySphere(before,Unit(delta),player,4.f,travel,hit);
        feather.life-=dt;
        if(contact){p.damageReason="HIT - FEATHER VOLLEY";Damage(r,p,14,ctx,before,.24f);}
        if(contact||blocked||feather.life<=0)expired.push_back(e);
        else transform.translate=Write(before+delta);
    }
    for(auto e:expired)r.destroy(e);
    if(finished_)return;
    if(ink.downTimer>0){ink.downTimer=std::max(0.f,ink.downTimer-dt);c.recoil=.35f;
        r.get<Target>(weakpoint_).active=!b.countered;PoseCreature(r);return;}
    if(c.stage==CreatureStage::Snake){UpdateCreature(r,p,ctx);return;}
    c.age+=dt;c.timer+=dt;c.recoil=std::max(0.f,c.recoil-dt);
    auto transition=[&](CreatureStage stage){c.stage=stage;c.timer=0;c.sweepPhase=0;c.sweepTime=0;};
    auto move=[&](V goal,float speed){V delta=goal-c.offset;
        c.moveVelocity=Lerp(c.moveVelocity,Unit(delta)*std::min(speed,Length(delta)*2.f),1-std::exp(-3*dt));
        c.offset=c.offset+c.moveVelocity*dt;};
    V base=EvaluateCreature(c.age,c.form).core;
    if(c.stage==CreatureStage::Assemble){
        if(c.timer<=dt*1.5f){V at=Center(r,boss_)-V{0,0,20};c.flightAngle=std::atan2(at.x,at.z);c.volleys=0;}
        c.form=std::min(1.f,c.timer/CreatureMorphSeconds);
        if(c.timer>=CreatureMorphSeconds){c.offset=c.morphDestination-EvaluateCreature(c.age,1).core;c.heading=c.morphHeading;
            transition(CreatureStage::Volley);b.countered=false;}
    }else if(c.stage==CreatureStage::Volley){
        c.form=1;
        // Circle on the player's side of the rim, continually correcting the orbit toward them.
        float bearing=std::atan2(player.x,player.z-20);
        c.flightAngle+=dt*(.10f+std::clamp(std::remainder(bearing-c.flightAngle,6.283185f),-.8f,.8f)*.10f);
        move(SlimeFlightPoint(c.flightAngle)-base,60);
        if(c.timer>=4.5f&&Length(SlimeFlightPoint(c.flightAngle)-Center(r,boss_))<35){
            transition(CreatureStage::Opening);c.sweepPhase=1;c.volleyClock=0;b.emitted=0;c.moveVelocity={};}
    }else if(c.stage==CreatureStage::Opening){
        c.sweepTime+=dt;
        // Twenty nine-feather fans sweep across the player, with gaps between lanes.
        if(c.sweepPhase==1&&c.sweepTime>=1.15f){c.sweepPhase=2;c.sweepTime=0;c.volleyClock=.14f;}
        if(c.sweepPhase==2){
            c.volleyClock+=dt;
            while(c.volleyClock>=.14f&&b.emitted<180){
                c.volleyClock-=.14f;int wave=b.emitted/9;float side=wave%2==0?-1.f:1.f;
                V right{std::cos(c.heading),0,-std::sin(c.heading)};
                V start=Center(r,boss_)+right*(side*15)+V{0,5,0};
                V aim=player+p.velocity*.2f;V axis=Unit(aim-start);
                float yaw=std::atan2(axis.x,axis.z),pitch=-std::asin(axis.y);
                for(int lane=-4;lane<=4;++lane){
                V direction=Forward(yaw+lane*.055f+std::sin(wave*.55f)*.12f,pitch+(wave%3-1)*.012f);
                auto e=Mesh(r,"Bird volley feather","Resources/Models/Chrono/feather.obj",start,{1.2f,.55f,4.5f});
                r.emplace<SlimeFeather>(e).velocity=direction*62;
                r.get<TransformComponent>(e).rotate={-std::asin(direction.y),std::atan2(direction.x,direction.z),0};
                r.get<MeshRendererComponent>(e).color={1.6f,.65f,.15f,1};++b.emitted;
                }
            }
            if(b.emitted>=180){c.sweepPhase=3;c.sweepTime=0;++c.volleys;}
        }
        if(b.countered||(c.sweepPhase==3&&c.sweepTime>=2.f)){
            transition(b.countered||c.volleys>=2?CreatureStage::Descend:CreatureStage::Volley);}
    }else{
        c.form=1-std::min(1.f,c.timer/CreatureMorphSeconds);
        if(c.timer>=CreatureMorphSeconds){c.offset=c.morphDestination-EvaluateCreature(c.age,0).core;c.heading=c.morphHeading;
            transition(CreatureStage::Snake);c.form=0;}
    }
    V toward=player-Center(r,boss_);
    float heading=std::atan2(-toward.x,-toward.z);
    c.heading+=std::remainder(heading-c.heading,6.283185f)*(1-std::exp(-2*dt));
    r.get<Target>(weakpoint_).active=!b.countered&&c.stage==CreatureStage::Opening&&c.sweepPhase==3;
    PoseCreature(r);
}
void ChronoSystem::DrawInk(entt::registry& r,GameContext& ctx){
    DrawSlimeBeam(r,ctx);
    Engine::Transform identity;
    for(auto mesh:inkMeshes_)ctx.renderer->DrawMesh(mesh,inkTexture_,identity,{1,1,1,1},"InkSurface",.4f,false);
    const auto& c=r.get<CreatureBoss>(boss_);
    if(c.stage==CreatureStage::Snake&&(c.sweepPhase==1||c.sweepPhase==2)){auto color=c.sweepPhase==1?Engine::Vector4{1,.7f,.05f,1}:Engine::Vector4{2,.12f,.02f,1};
        float width=c.attack==CreatureAttack::Charge?4.f:c.attack==CreatureAttack::Slam?8.f:16.f;
        float depth=c.attack==CreatureAttack::Charge?22.f:c.attack==CreatureAttack::Slam?8.f:7.f;
        V center=c.sweepCenter;center.y=InkGround(center)+.12f;
        for(float x:{-width,width})ctx.renderer->DrawLine3D(EV(center+V{x,0,-depth}),EV(center+V{x,0,depth}),color);
        for(float z:{-depth,depth})ctx.renderer->DrawLine3D(EV(center+V{-width,0,z}),EV(center+V{width,0,z}),color);
    }
}

}
