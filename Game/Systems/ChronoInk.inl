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
    const auto& ink=r.get<InkPlayer>(player_);
    float pullback=ink.phase==SlimePhase::Charging?3*ink.Power(ink.charge):ink.phase==SlimePhase::Firing?3.f:0.f;
    V focus=cameraFollow_+V{0,2.f,0};V wanted=focus-forward*(17.f+pullback)+right*1.5f;
    float f;V n;
    for(const auto& box:solids_)if(Chrono::Sweep(focus,wanted-focus,box,{.3f,.3f,.3f},f,n))wanted=focus+(wanted-focus)*std::max(.05f,f-.025f);
    for(const auto& surface:inkSurfaces_){V hit;if(surface.Ray(focus,wanted-focus,f,hit))wanted=focus+(wanted-focus)*std::max(.05f,f-.06f);}
    wanted.y=std::max(wanted.y,.7f);
    ctx.camera->SetPosition(Write(wanted));ctx.camera->SetRotation(pitch_,yaw_,0);
    float goal=r.get<InkPlayer>(player_).swimming?1.22f:1.13f;fov_+=(goal-fov_)*(1-std::exp(-6*ctx.dt));
    ctx.camera->SetProjection(fov_,ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),.1f,2000);
    ctx.camera->SetHandheld(.008f);p.cameraOpacity=1;
}
void ChronoSystem::UpdateInkBoss(entt::registry& r,Player& p,GameContext& ctx){
    auto& c=r.get<CreatureBoss>(boss_);auto& b=r.get<Boss>(boss_);
    auto& ink=r.get<InkPlayer>(player_);
    if(ink.downTimer>0){ink.downTimer=std::max(0.f,ink.downTimer-ctx.dt);c.recoil=.35f;
        r.get<Target>(weakpoint_).active=!b.countered;PoseCreature(r);return;}
    if(c.stage==CreatureStage::Snake){
        UpdateCreature(r,p,ctx);return;
    }
    c.age+=ctx.dt;c.timer+=ctx.dt;c.recoil=std::max(0.f,c.recoil-ctx.dt);
    auto transition=[&](CreatureStage stage){c.stage=stage;c.timer=0;c.sweepTime=0;c.sweepPhase=0;};
    V player=Read(r.get<TransformComponent>(player_).translate);
    if(c.stage==CreatureStage::Assemble){c.form=std::min(1.f,c.timer/CreatureMorphSeconds);
        if(c.timer>=CreatureMorphSeconds){transition(CreatureStage::Volley);b.countered=false;c.sweepCenter=player;}}
    else if(c.stage==CreatureStage::Volley){
        c.form=1;
        // Bird brings its chest within firing range, above the two reachable gun decks.
        V desired=c.sweepCenter+V{0,23,28};
        // Keep the readable central attack lane; shift that lane toward a
        // distant player only when the expanded outskirts require it.
        desired.x=std::clamp(desired.x,std::min(-8.f,c.sweepCenter.x+72.f),std::max(8.f,c.sweepCenter.x-72.f));
        desired.z=std::clamp(desired.z,std::min(-15.f,c.sweepCenter.z+60.f),std::max(65.f,c.sweepCenter.z-60.f));
        V base=EvaluateCreature(c.age,1).core;
        c.offset=Lerp(c.offset,desired-base,1-std::exp(-3*ctx.dt));
        if(c.timer>=2.5f){transition(CreatureStage::Opening);c.attack=CreatureAttack::Wing;c.sweepPhase=1;c.sweepCenter=player;c.sweepHit=false;}}
    else if(c.stage==CreatureStage::Opening){
        c.sweepTime+=ctx.dt;
        if(c.sweepPhase==1&&c.sweepTime>=1.2f){c.sweepPhase=2;c.sweepTime=0;}
        else if(c.sweepPhase==2){if(!c.sweepHit&&p.invincible<=0&&AttackTouches(CreatureAttack::Wing,player-c.sweepCenter,std::max(0.f,c.sweepTime-ctx.dt),c.sweepTime)){
                c.sweepHit=true;p.damageReason="WING STRIKE";Damage(r,p,22,ctx,c.sweepCenter);}
            if(c.sweepTime>=ActiveTime(CreatureAttack::Wing)){c.sweepPhase=3;c.sweepTime=0;}}
        if(b.countered){transition(CreatureStage::Descend);}
        else if(c.sweepPhase==3&&c.sweepTime>5.5f){transition(CreatureStage::Volley);c.sweepCenter=player;}
    }else{c.form=1-std::min(1.f,c.timer/CreatureMorphSeconds);
        // Return to ground smoothly, not a height snap at the next snake warning.
        c.offset.y+=(0-c.offset.y)*(1-std::exp(-ctx.dt*2));
        if(c.timer>=CreatureMorphSeconds){transition(CreatureStage::Snake);c.form=0;c.offset.y=0;}}
    r.get<Target>(weakpoint_).active=!b.countered&&c.stage==CreatureStage::Opening&&c.sweepPhase==3;
    PoseCreature(r);
}
void ChronoSystem::DrawInk(entt::registry& r,GameContext& ctx){
    DrawSlimeBeam(r,ctx);
    Engine::Transform identity;
    for(auto mesh:inkMeshes_)ctx.renderer->DrawMesh(mesh,inkTexture_,identity,{1,1,1,1},"InkSurface",.4f,false);
    const auto& c=r.get<CreatureBoss>(boss_);
    if(c.sweepPhase==1||c.sweepPhase==2){auto color=c.sweepPhase==1?Engine::Vector4{1,.7f,.05f,1}:Engine::Vector4{2,.12f,.02f,1};
        float width=c.attack==CreatureAttack::Charge?4.f:c.attack==CreatureAttack::Slam?8.f:16.f;
        float depth=c.attack==CreatureAttack::Charge?22.f:c.attack==CreatureAttack::Slam?8.f:7.f;
        V center=c.sweepCenter;center.y=InkGround(center)+.12f;
        for(float x:{-width,width})ctx.renderer->DrawLine3D(EV(center+V{x,0,-depth}),EV(center+V{x,0,depth}),color);
        for(float z:{-depth,depth})ctx.renderer->DrawLine3D(EV(center+V{-width,0,z}),EV(center+V{width,0,z}),color);
    }
}

}
