// Included by ChronoSystem.cpp: the new game's complete simulation path.
namespace Game {
void ChronoSystem::BuildInk(entt::registry& r){
    r.emplace_or_replace<InkPlayer>(player_);auto& p=r.get<Player>(player_);p.mass=SlimeMaximumMass;p.automatic=false;
    // The opening shot runs before UpdateInk; rendering must already see the full body mass.
    auto& health=r.get<HealthComponent>(player_);health.SetHp(p.mass);health.SetMaxHp(SlimeMaximumMass);health.SetDead(false);
    r.get<InkPlayer>(player_).rogue.random=uint32_t(GetTickCount64())|1u;
    Engine::Renderer::GetInstance()->SetRogueWorldFrozen(false);
    Engine::Renderer::GetInstance()->SetDrawFluidDebugArrows(false);
    Engine::Renderer::GetInstance()->SetFluidVolumeDebugMode(0);
    std::vector<entt::entity> old;
    for(auto e:r.view<NameComponent>())if((r.get<NameComponent>(e).name.rfind("Chrono ",0)==0&&e!=weakpoint_&&!r.any_of<CreaturePart,SerpentRelay>(e))||r.all_of<Anchor>(e)||r.all_of<Feather>(e))old.push_back(e);
    for(auto e:old)r.destroy(e);
    auto solid=[&](const std::string& name,V center,V size){
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono "+name+" collision");
        r.emplace<TransformComponent>(e).translate=Write(center);
        r.emplace<BoxColliderComponent>(e).size=Write(size);
        r.emplace<TagComponent>(e).tag=TagType::Wall;r.emplace<Solid>(e);
    };
    solid("Meadow foundation",{0,-4.02f,20},{660,8,720});
    // Nine paint tiles preserve trail detail as the playable field grows.
    auto add=[&](V origin,V u,V v,float width,float depth){InkSurface surface;surface.origin=origin;surface.u=u;surface.v=v;surface.width=width;surface.depth=depth;inkSurfaces_.push_back(surface);};
    for(float z:{-340.f,-100.f,140.f})for(float x:{-330.f,-110.f,110.f})add({x,0,z},{1,0,0},{0,0,1},220,240);
    auto* render=Engine::Renderer::GetInstance();
    render->CreateShaderPipeline("EnvironmentSurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/EnvironmentSurfacePS.hlsl");
    render->CreateShaderPipeline("MeadowGrass",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/MeadowGrassPS.hlsl");
    render->CreateShaderPipeline("MeadowGround",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/MeadowGroundPS.hlsl");
    const auto groundTex=render->LoadTexture2D("Resources/Textures/PolyHaven/leafy_grass_diff_4k.jpg");
    const auto rockFaceTex=render->LoadTexture2D("Resources/Textures/PolyHaven/boulder_01_diff_2k.jpg");
    const auto boulderTex=render->LoadTexture2D("Resources/Textures/PolyHaven/namaqualand_boulder_02_diff_2k.jpg");
    const auto stoneTex=render->LoadTexture2D("Resources/Textures/PolyHaven/rock_09_diff_2k.jpg");
    const auto grassTex=render->LoadTexture2D("Resources/Textures/PolyHaven/grass_medium_02_rgba_2k.png");
    std::map<uint32_t,std::vector<uint32_t>> pbr;
    auto maps=[&](uint32_t albedo,const std::string& prefix,const std::array<std::string,3>& suffix){
        for(const auto& s:suffix)pbr[albedo].push_back(render->LoadTexture2D("Resources/Textures/PBR/"+prefix+s+".jpg",false));
    };
    auto freshMaps=[&](uint32_t albedo,const char* id){for(const char* map:{"nor_dx","Rough","AO"})pbr[albedo].push_back(render->LoadTexture2D(std::string("Resources/Textures/PolyHaven/")+id+"_"+map+"_2k.jpg",false));};
    freshMaps(groundTex,"leafy_grass");freshMaps(rockFaceTex,"boulder_01");freshMaps(boulderTex,"namaqualand_boulder_02");freshMaps(grassTex,"grass_medium_02");
    maps(stoneTex,"rock_09_",{"nor_dx","Rough","AO"});
    auto scenery=[&](const std::string& name,const std::string& path,auto texture,V at,V size,const char* shader="EnvironmentSurface"){
        auto e=Mesh(r,name,path,at,size);auto& mr=r.get<MeshRendererComponent>(e);
        mr.textureHandle=texture;mr.shaderName=shader;
        if(pbr.count(texture))mr.extraTextureHandles=pbr[texture];
        r.remove<CameraOccluder>(e);return e;
    };
    auto floor=scenery("Meadow ground","Resources/Models/Environment/meadow-floor.obj",groundTex,{0,0,0},{1,1,1},"MeadowGround");
    std::vector<Engine::VertexData> terrainVertices;std::vector<uint32_t> terrainIndices;
    constexpr int grid=110;terrainVertices.reserve((grid+1)*(grid+1));
    for(int iz=0;iz<=grid;++iz)for(int ix=0;ix<=grid;++ix){float x=-550.f+ix*10.f,z=-560.f+iz*(1160.f/grid);
        float sx=(MeadowHeight(x+.5f,z)-MeadowHeight(x-.5f,z)),sz=(MeadowHeight(x,z+.5f)-MeadowHeight(x,z-.5f));V normal=Unit(V{-sx,1,-sz});
        Engine::VertexData vertex{};vertex.position={x,MeadowHeight(x,z),z,1};vertex.texcoord={x/4.f,z/4.f};vertex.normal=Write(normal);terrainVertices.push_back(vertex);
        if(ix<grid&&iz<grid){uint32_t a=iz*(grid+1)+ix,b=a+1,c=a+grid+1,d=c+1;terrainIndices.insert(terrainIndices.end(),{a,c,b,b,c,d});}
    }
    r.get<MeshRendererComponent>(floor).modelHandle=render->CreateDynamicMesh(terrainVertices,terrainIndices);
    // Grid vertices are already in world units; the old flat mesh normalization
    // would otherwise multiply terrain elevations by 1000.
    auto& terrainTransform=r.get<TransformComponent>(floor);terrainTransform.scale={1,1,1};terrainTransform.translate={0,0,0};terrainTransform.rotate={0,0,0};
    r.get<MeshRendererComponent>(floor).color={.76f,1.f,.78f,1};
    // Preserve each scanned model's proportions. The target width determines a
    // uniform scale, and its lowest vertex is placed on the flat arena floor.
    auto natural=[&](const std::string& name,const std::string& path,auto texture,V at,float width,float yaw,const char* shader="EnvironmentSurface"){
        auto e=scenery(name,path,texture,{0,0,0},{1,1,1},shader);
        auto* model=render->GetModel(r.get<MeshRendererComponent>(e).modelHandle);
        if(!model||model->GetData().vertices.empty())return e;
        // Foliage LOD keeps complete connected leaves and their authored UVs.
        bool foliage=std::string(shader)=="MeadowGrass";
        render->PrepareDistanceLods(r.get<MeshRendererComponent>(e).modelHandle,foliage,foliage);
        V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& v:model->GetData().vertices){
            lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
            hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};
        }
        float scale=width/std::max({hi.x-lo.x,hi.z-lo.z,.001f});
        V mid=(lo+hi)*.5f;float cs=std::cos(yaw),sn=std::sin(yaw);
        auto& t=r.get<TransformComponent>(e);t.scale=Write({scale,scale,scale});t.rotate=Write({0,yaw,0});
        t.translate=Write({at.x-(mid.x*cs+mid.z*sn)*scale,MeadowHeight(at.x,at.z)+at.y-lo.y*scale,at.z-(-mid.x*sn+mid.z*cs)*scale});
        return e;
    };
    auto noise=[](int seed){float n=std::sin(float(seed)*12.9898f)*43758.5453f;return n-std::floor(n);};
    const std::string rockFaceMesh="Resources/Models/PolyHaven/boulder_01_2k.fbx";
    const std::string boulderMesh="Resources/Models/PolyHaven/namaqualand_boulder_02_2k.fbx";
    const std::string stoneMesh="Resources/Models/PolyHaven/rock_09_1k.fbx";
    const std::string grassMesh="Resources/Models/PolyHaven/grass_medium_02_2k.fbx";
    for(int side=0;side<4;++side)for(int i=0;i<5;++i){
        float along=-300.f+i*150.f+(noise(side*31+i)*2-1)*22.f;
        V at=side<2?V{along,-.6f,side==0?-380.f:420.f}:V{side==2?-380.f:380.f,-.6f,along+20};
        natural("Weathered boulder",rockFaceMesh,rockFaceTex,at,20.f+noise(side*71+i)*16.f,noise(side*101+i)*6.28318f);
    }
    for(int side=0;side<4;++side)for(int i=0;i<3;++i){
        float along=-290.f+i*290.f+(noise(side*37+i)*2-1)*17.f;
        V at=side<2?V{along,-2,side==0?-352.f:392.f}:V{side==2?-348.f:348.f,-2,along+20};
        natural("Sandstone boulder",boulderMesh,boulderTex,at,12.f+noise(side*83+i)*13.f,noise(side*113+i)*6.28318f);
    }
    for(int i=0;i<8;++i){
        V at{-315.f+noise(i*3+1)*630.f,-.08f,-325.f+noise(i*3+2)*690.f};
        natural("Scanned field stone",stoneMesh,stoneTex,at,2.f+noise(i*3+3)*3.f,noise(i*5+4)*6.28318f);
    }
    auto pebble=[&](V at,float width,float yaw){
        auto e=natural("Scanned pebble",rockFaceMesh,rockFaceTex,at,width,yaw);
        auto& mr=r.get<MeshRendererComponent>(e);
        // A fixed detailed reduction is sufficient for small stones and keeps
        // their raster/ray geometry identical as the camera moves.
        mr.modelHandle=render->GetDistanceLodMeshes(mr.modelHandle)[2];
        if(!wcsstr(GetCommandLineW(),L"--scenery-baseline"))for(int level=0;level<2;++level){
            render->PrepareDistanceLods(mr.modelHandle);mr.modelHandle=render->GetDistanceLodMeshes(mr.modelHandle)[2];}
    };
    for(int i=0;i<155;++i){
        V at{-315.f+noise(i*3+23)*630.f,-.08f,-325.f+noise(i*3+24)*690.f};
        pebble(at,.45f+noise(i*3+25)*1.5f,noise(i*5+26)*6.28318f);
    }
    for(int i=0;i<48;++i){
        V at{-78.f+noise(i*3+801)*156.f,-.08f,-105.f+noise(i*3+802)*180.f};
        pebble(at,.55f+noise(i*3+803)*1.25f,noise(i*5+804)*6.28318f);
    }
    for(int z=0;z<21;++z)for(int x=0;x<22;++x){
        int index=z*22+x;V at{-320.f+x*30.f+(noise(index*3+1)*2-1)*9.f,-.015f,
            -335.f+z*34.f+(noise(index*3+2)*2-1)*9.f};
        natural("Scanned meadow tuft",grassMesh,grassTex,at,2.8f+noise(index*7+3)*2.f,noise(index*7+4)*6.28318f,"MeadowGrass");
    }
    for(int i=0;i<110;++i){
        V at{-78.f+noise(i*3+251)*156.f,-.015f,-90.f+noise(i*3+252)*180.f};
        if(std::abs(at.x)<6&&std::abs(at.z)<12)continue;
        natural("Near meadow tuft",grassMesh,grassTex,at,1.4f+noise(i*3+253)*1.8f,noise(i*5+254)*6.28318f,"MeadowGrass");
    }
    // Foreground accents frame the opening view without covering the player.
    for(V at:{V{8,-.005f,-5},V{-9,-.005f,-2},V{14,-.005f,5},V{-16,-.005f,9}})
        natural("Foreground meadow tuft",grassMesh,grassTex,at,4.5f,noise(int(at.x*19+at.z*13))*6.28318f,"MeadowGrass");
    natural("Foreground weathered stone",rockFaceMesh,rockFaceTex,{-19,-.12f,18},4.8f,1.1f);
    natural("Foreground sandstone",boulderMesh,boulderTex,{25,-.1f,32},5.5f,2.3f);
    render->CreateShaderPipeline("InkSurface",L"Resources/shaders/ObjVS.hlsl",L"Resources/shaders/InkSurfacePS.hlsl");
    for(int i=0;i<static_cast<int>(inkSurfaces_.size());++i)inkMeshes_.push_back(render->LoadObjMesh("Resources/Models/Ink/surface"+std::to_string(i)+".obj"));
    slimeTrails_.clear();homingDomains_.clear();domainMissiles_.clear();inkNeedsRebuild_=false;inkUploadClock_=0;inkDirty_=true;UploadInk();
    slimeBeamMesh_=render->LoadObjMesh("Resources/Models/Ink/slime-beam.obj");
    slimeRingMesh_=render->LoadObjMesh("Resources/Models/Ink/slime-ring.obj");
    render->CreateShaderPipelineTransparent("SlimeBeam",L"Resources/shaders/ObjVS.hlsl",L"Resources/shaders/SlimeBeamPS.hlsl",false,true);
    render->CreateShaderPipelineTransparent("DomainGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainGlowPS.hlsl",true,true);
    render->CreateShaderPipelineTransparent("DomainFill",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainFillPS.hlsl",true,true);
    domainFillMesh_=render->CreateDynamicMesh({{{0,0,0,1},{0,0},{0,1,0}},{{1,0,0,1},{1,0},{0,1,0}},{{0,0,1,1},{0,1},{0,1,0}}},{0,1,2});
    highlightedCandidate_.clear();candidateHighlight_.clear();
    trailLiquid_={};candidateLiquid_={};
    render->SetGuidedLiquidTrail({});
    if(slimePressureSound_==0xffffffff){auto* audio=Engine::Audio::GetInstance();
        slimePressureSound_=audio->Load("Resources/Sound/slime-pressure.wav");slimeRecallSound_=audio->Load("Resources/Sound/slime-recall.wav");slimeAcidSound_=audio->Load("Resources/Sound/slime-acid.wav");}
    BuildSwarm(r);
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
    float domainTarget=(!homingDomains_.empty()||!domainMissiles_.empty())?1.f:0.f;
    // Widen the view during the aerial volley without stopping movement or aim input.
    ink.cameraDomainView=approach(ink.cameraDomainView,domainTarget,domainTarget>0?.65f:.9f);
    float domainView=Ease(ink.cameraDomainView);
    const auto& boss=r.get<CreatureBoss>(boss_);
    float arrival=boss.stage==CreatureStage::Dive&&boss.sweepPhase==2?6.f*AttackEase(boss.sweepTime/.65f):0.f;
    float aftershock=boss.impactAge<.45f?5.f*(1-boss.impactAge/.45f):0.f;
    float pullback=((charging||firing)?3*power:0.f)+domainView*14+std::max(arrival,aftershock);
    ink.cameraPullback+=(pullback-ink.cameraPullback)*(1-std::exp(-10*ctx.dt));
    V focus=cameraFollow_+V{0,2.f,0};
    V wanted=focus-forward*(17.f+ink.cameraPullback+heavy*5)+right*(1.5f+shot*4.5f+heavy*8)+V{0,shot*1.2f+heavy*3+domainView*6,0};
    float f;V n;
    for(const auto& box:solids_)if(Chrono::Sweep(focus,wanted-focus,box,{.3f,.3f,.3f},f,n))wanted=focus+(wanted-focus)*std::max(.05f,f-.025f);
    for(const auto& surface:inkSurfaces_){V hit;if(surface.Ray(focus,wanted-focus,f,hit))wanted=focus+(wanted-focus)*std::max(.05f,f-.06f);}
    wanted.y=std::max(wanted.y,.7f);
    V beamForward=firing?Forward(ink.beamYaw,ink.beamPitch):forward;
    ink.cameraBeamDirection=Unit(Lerp(ink.cameraBeamDirection,beamForward,1-std::exp(-14*ctx.dt)));
    V lookAt=focus+ink.cameraBeamDirection*(45-heavy*20)+V{0,domainView*20,0};
    V view=Unit(Lerp(forward,Unit(lookAt-wanted),std::max(shot,domainView*.8f)));
    ctx.camera->SetPosition(Write(wanted));
    ctx.camera->SetRotation(-std::asin(std::clamp(view.y,-1.f,1.f)),std::atan2(view.x,view.z),0);
    float goal=(ink.swimming?1.22f:1.13f)+heavy*.06f+domainView*.12f;fov_+=(goal-fov_)*(1-std::exp(-6*ctx.dt));
    // A quick seven-degree kick, returning fully to the regular camera within 0.4 seconds.
    ctx.camera->SetProjection(fov_+.12f*SlimePerfectPulse(ink.perfectAge),ctx.viewportSize.x/std::max(1.f,ctx.viewportSize.y),.1f,2000);
    ctx.camera->SetHandheld(.008f);
    p.cameraOpacity=std::clamp((Length(wanted-pos)-2.5f)/5.f,.2f,1.f);
    // The boss can reach through the player on impact. Fade only the pieces
    // between the camera and the player so the hit and floor cue remain visible.
    for(auto e:r.view<CreaturePart,CameraOccluder>()){
        auto& o=r.get<CameraOccluder>(e);float fraction;V normal;
        bool occludes=Chrono::Sweep(wanted,focus-wanted,
            {o.center-o.size*.48f,o.center+o.size*.48f},{.4f,.4f,.4f},fraction,normal);
        bool closeToCamera=Length(o.center-wanted)<Length(o.size)*.42f+2.f;
        o.opacity+=(((occludes||closeToCamera)?.03f:1.f)-o.opacity)*(1-std::exp(-20.f*ctx.dt));
    }
}
void ChronoSystem::UpdateInkBoss(entt::registry& r,Player& p,GameContext& context){
    GameContext ctx=context;const auto& state=r.get<InkPlayer>(player_);ctx.dt*=state.rogueBossSlow>0?.5f:state.rogueBossPool?.65f:1.f;
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
    c.age+=dt;c.timer+=dt;c.recoil=std::max(0.f,c.recoil-dt);c.impactAge+=ctx.dt;
    auto transition=[&](CreatureStage stage){c.stage=stage;c.timer=0;c.sweepPhase=0;c.sweepTime=0;};
    auto move=[&](V goal,float speed){V delta=goal-c.offset;
        c.moveVelocity=Lerp(c.moveVelocity,Unit(delta)*std::min(speed,Length(delta)*3.f),1-std::exp(-5.5f*dt));
        c.offset=c.offset+c.moveVelocity*dt;};
    V base=EvaluateCreature(c.age,c.form).core;
    if(c.stage==CreatureStage::Assemble){
        if(c.timer<=dt*1.5f){V at=Center(r,boss_)-V{0,0,20};c.flightAngle=std::atan2(at.x,at.z);c.volleys=0;}
        c.form=std::min(1.f,c.timer/CreatureMorphSeconds);
        if(c.timer>=CreatureMorphSeconds){c.offset=c.morphDestination-EvaluateCreature(c.age,1).core;c.heading=c.morphHeading;
            transition(CreatureStage::Volley);b.countered=false;}
    }else if(c.stage==CreatureStage::Volley){
        c.form=1;
        // A fast, damped arc keeps the wings in motion without a slow chase loop.
        float bearing=std::atan2(player.x,player.z-20);
        c.flightAngle+=dt*(.48f+std::clamp(std::remainder(bearing-c.flightAngle,6.283185f),-1.f,1.f)*.18f);
        move(SlimeFlightPoint(c.flightAngle)-base,180);
        if(c.timer>=4.5f&&Length(SlimeFlightPoint(c.flightAngle)-Center(r,boss_))<45){
            c.moveVelocity={};
            if(c.volleys%2==0){transition(CreatureStage::Opening);c.sweepPhase=1;c.volleyClock=0;b.emitted=0;}
            else{c.diveStart=Center(r,boss_);c.sweepCenter={std::clamp(player.x,-300.f,300.f),0,std::clamp(player.z,-310.f,350.f)};
                c.sweepCenter.y=InkGround(c.sweepCenter);transition(CreatureStage::Dive);c.sweepPhase=1;c.sweepHit=false;}
        }
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
    }else if(c.stage==CreatureStage::Dive){
        c.form=1;c.sweepTime+=dt;
        if(c.sweepPhase==1&&c.sweepTime>=1.15f){c.sweepPhase=2;c.sweepTime=0;}
        if(c.sweepPhase==2){
            V impact=c.sweepCenter+V{0,23,0};
            V center=Lerp(c.diveStart,impact,AttackEase(c.sweepTime/.65f));
            c.offset=center-base;
            if(c.sweepTime>=.65f){
                V delta=player-c.sweepCenter;delta.y=0;
                if(Length(delta)<11.f&&player.y<c.sweepCenter.y+7){p.damageReason="HIT - BIRD DIVE";Damage(r,p,26,ctx,c.sweepCenter,.16f);}
                BossImpact(r,ctx,c.sweepCenter,{0,1,0},1.2f);
                for(int i=0;i<18;++i){
                    float angle=(i+.25f*(i%2))*6.283185f/18.f;
                    V direction{std::cos(angle),0,std::sin(angle)};
                    V start=c.sweepCenter+direction*5.f+V{0,2.5f,0};
                    auto e=Mesh(r,"Dive radial feather","Resources/Models/Chrono/feather.obj",start,{1.1f,.45f,3.6f});
                    r.emplace<SlimeFeather>(e).velocity=direction*58.f;
                    r.get<TransformComponent>(e).rotate={0,std::atan2(direction.x,direction.z),0};
                    r.get<MeshRendererComponent>(e).color={1.7f,.45f,.12f,1};
                }
                c.sweepPhase=3;c.sweepTime=0;
            }
        }else if(c.sweepPhase==3&&(c.sweepTime>=2.2f||b.countered)){
            ++c.volleys;transition(CreatureStage::Descend);
        }
    }else{
        c.form=1-std::min(1.f,c.timer/CreatureMorphSeconds);
        if(c.timer>=CreatureMorphSeconds){c.offset=c.morphDestination-EvaluateCreature(c.age,0).core;c.heading=c.morphHeading;
            transition(CreatureStage::Snake);c.form=0;}
    }
    V toward=player-Center(r,boss_);
    float heading=std::atan2(-toward.x,-toward.z);
    c.heading+=std::remainder(heading-c.heading,6.283185f)*(1-std::exp(-2*dt));
    r.get<Target>(weakpoint_).active=!b.countered&&
        ((c.stage==CreatureStage::Opening||c.stage==CreatureStage::Dive)&&c.sweepPhase==3);
    PoseCreature(r);
}
void ChronoSystem::DrawInk(entt::registry& r,GameContext& ctx){
    DrawSlimeBeam(r,ctx);
    Engine::Transform identity;
    for(auto mesh:inkMeshes_)ctx.renderer->DrawMesh(mesh,inkTexture_,identity,{1,1,1,1},"InkSurface",.4f,false);
    DrawSwarm(r,ctx);
    if(r.get<InkPlayer>(player_).battlePhase!=BattlePhase::Boss)return;
    const auto& c=r.get<CreatureBoss>(boss_);
    if(c.stage==CreatureStage::Snake){
        for(auto e:r.view<CreaturePart,TransformComponent>()){
            const auto& part=r.get<CreaturePart>(e);
            // One flattened oval every three vertebrae reads as a continuous
            // contact shadow without adding geometry to the animal.
            if(part.role!=CreatureRole::Spine||part.segment<6||part.segment%3!=0)continue;
            const auto& t=r.get<TransformComponent>(e);
            Engine::Transform shadow;shadow.translate={t.translate.x,InkGround(Read(t.translate))+.075f,t.translate.z};
            float width=11.f-5.f*float(part.segment)/25.f;
            shadow.scale={width*2.f,.09f,18.f};
            ctx.renderer->DrawMesh(sphere_,white_,shadow,{.035f,.09f,.025f,1},"Default",0,false);
        }
    }
    DrawBossImpact(ctx,c);
    for(const auto& d:droplets_)if(d.dust){
        Engine::Transform t;t.translate=EV(d.position);
        float size=.8f*(1-d.age/.45f)+.035f;t.scale={size,size,size};
        ctx.renderer->DrawMesh(sphere_,white_,t,{.68f,.56f,.35f,1},"Default",.3f,false);
    }
    if(c.stage==CreatureStage::Snake&&(c.sweepPhase==1||c.sweepPhase==2||c.sweepPhase==5||c.sweepPhase==6)){auto color=c.sweepPhase==1||c.sweepPhase==5?Engine::Vector4{1,.7f,.05f,1}:Engine::Vector4{2,.12f,.02f,1};
        float width=c.sweepPhase>=5?22.f:c.attack==CreatureAttack::Charge||c.attack==CreatureAttack::HeadTail?5.f:c.attack==CreatureAttack::Slam?8.f:16.f;
        float depth=c.sweepPhase>=5?7.f:c.attack==CreatureAttack::Charge||c.attack==CreatureAttack::HeadTail?24.f:c.attack==CreatureAttack::Slam?8.f:7.f;
        V center=c.sweepCenter;center.y=InkGround(center)+.12f;
        for(float x:{-width,width})ctx.renderer->DrawLine3D(EV(center+V{x,0,-depth}),EV(center+V{x,0,depth}),color);
        for(float z:{-depth,depth})ctx.renderer->DrawLine3D(EV(center+V{-width,0,z}),EV(center+V{width,0,z}),color);
    }
    if(c.stage==CreatureStage::Dive&&(c.sweepPhase==1||c.sweepPhase==2)){
        V center=c.sweepCenter+V{0,.15f,0};
        Engine::Vector4 color=c.sweepPhase==1?Engine::Vector4{1,.7f,.05f,1}:Engine::Vector4{2,.12f,.02f,1};
        for(int i=0;i<32;++i){float a=i*6.283185f/32.f,b=(i+1)*6.283185f/32.f;
            ctx.renderer->DrawLine3D(EV(center+V{std::cos(a)*11,0,std::sin(a)*11}),
                EV(center+V{std::cos(b)*11,0,std::sin(b)*11}),color);}
    }
}

}
