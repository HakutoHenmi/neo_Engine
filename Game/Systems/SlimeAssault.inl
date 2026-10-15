// Production loop-drawing / homing-domain combat.
namespace Game {
void ChronoSystem::UpdateInk(entt::registry& r,Player& p,GameContext& ctx){
    auto& ink=r.get<InkPlayer>(player_);auto* control=r.try_get<ControlFrame>(player_);
    const float dt=std::min(ctx.dt,.05f);diagnostic_=control!=nullptr;
    if(UpdateCinematic(r,p,ctx))return;
    if(bossDefeat_){UpdateBossDefeat(r,p,ctx);return;}
#ifndef NDEBUG
    // Debug builds: F9 skips straight to the boss-defeat sequence for checking the presentation.
    if(!control&&Down(VK_F9)&&!finished_){
        if(ink.battlePhase!=BattlePhase::Boss){ink.battlePhase=BattlePhase::Boss;ink.battleAge=SwarmEmergenceSeconds;SetSwarmBossVisible(r,true,0);PoseCreature(r);}
        StartBossDefeat(r,p,ctx);return;
    }
#endif
    if(UpdateRogueMenu(r,p,ctx))return;
    ink.domainPath.pointLimit=DomainPath::MaxPoints+size_t(384*ink.rogue.ranks[3]);
    ink.rogue.TickGameplay(dt);
    ink.dodgeAge+=dt;
    if(ink.perfectAge>=0){ink.perfectAge+=dt;if(ink.perfectAge>1.2f)ink.perfectAge=-1;}
    bool attack=control?control->attack:Controls::Fire();
    if(!control&&Down(VK_F4)&&!prevSnake_){snakeOnly_=!snakeOnly_;prevSnake_=true;Reset(r);return;}prevSnake_=!control&&Down(VK_F4);
    bool shake=!control&&Down(VK_F6);if(shake&&!prevShake_)shakeSetting_=(shakeSetting_+1)%3;prevShake_=shake;ctx.camera->SetShakeStrength(shakeSetting_*.5f);
    if(control){if(!ink.lockedOn){yaw_=control->yaw;pitch_=control->pitch;}}
    else if(ctx.input&&ink.rogue.returnDelay<=0){float padYaw,padPitch;Controls::Look(dt,padYaw,padPitch);
        yaw_+=ctx.input->GetMouseDeltaX()*.0025f+padYaw;pitch_=std::clamp(pitch_+ctx.input->GetMouseDeltaY()*.0025f+padPitch,-1.15f,1.2f);}
    bool lockPressed=control?control->lockOn:Controls::LockOn();
    if(lockPressed&&!ink.previousLock)ink.lockedOn=!ink.lockedOn;
    ink.previousLock=lockPressed;
    if(ink.lockedOn){
        V from=Read(r.get<TransformComponent>(player_).translate)+V{0,.25f,0};
        V attention=Center(r,weakpoint_);
        if(ink.battlePhase!=BattlePhase::Boss){float nearest=1e9f;
            for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&DomainDistance(enemy.at,from)<nearest){nearest=DomainDistance(enemy.at,from);attention=enemy.at;}
            if(nearest==1e9f)ink.lockedOn=false;}
        V direction=Unit(attention-from);
        float aimYaw=std::atan2(direction.x,direction.z),aimPitch=-std::asin(std::clamp(direction.y,-1.f,1.f));
        const auto& boss=r.get<CreatureBoss>(boss_);
        if(ink.battlePhase==BattlePhase::Boss&&((boss.stage==CreatureStage::Dive&&boss.sweepPhase>=2)||boss.impactAge<.4f))
            aimPitch=std::max(aimPitch,-.32f);
        yaw_+=std::remainder(aimYaw-yaw_,6.283185f)*(1-std::exp(-12*dt));
        pitch_+=(aimPitch-pitch_)*(1-std::exp(-12*dt));
    }
    Engine::WindowDX::SetCursorVisible(false);
    p.stats.seconds+=dt;p.worldScale=p.aimScale=1;p.aiming=p.automatic=false;p.target=p.preview=p.bufferedTarget=entt::null;p.buffer=0;
    if(ink.perfectAge>=0&&ink.perfectAge<.4f)p.worldScale=.15f;
    p.invincible=std::max(0.f,p.invincible-dt);p.damageAge+=dt;p.hitStop=std::max(0.f,p.hitStop-dt);ink.hitFlash=std::max(0.f,ink.hitFlash-dt);bossFlash_=std::max(0.f,bossFlash_-dt);
    ink.shotClock=std::max(0.f,ink.shotClock-dt);ink.phaseAge+=dt;if(ink.monoAge>=0)ink.monoAge+=dt;
    auto& t=r.get<TransformComponent>(player_);V pos=Read(t.translate);int surface=-1;float floor=InkGround(pos,&surface);
    ink.deployed=0;for(const auto& trail:slimeTrails_)ink.deployed+=trail.mass;
    ink.Recover(p.mass,p.damageAge,dt);
    if(ink.phase==SlimePhase::Roaming){float total=std::min(SlimeMaximumMass,p.mass+ink.deployed+ink.spent);ink.capacity=SlimeCapacity(total);ink.reserve=SlimeReserve(total);}
    bool jump=control?control->jump:Controls::Jump(),dodge=control?control->dodge:Controls::Dodge();
    bool dodged=dodge&&!ink.previousDodge&&p.cooldown<=0&&ink.phase!=SlimePhase::Firing&&ink.phase!=SlimePhase::Returning;
    V input{};if(control)input=Unit(control->move);else Controls::Move(input.x,input.z);
    V wish{input.x*std::cos(yaw_)+input.z*std::sin(yaw_),0,-input.x*std::sin(yaw_)+input.z*std::cos(yaw_)};
    ink.swimming=p.grounded&&ink.phase==SlimePhase::Roaming;
    float speed=InkMoveSpeed(ink.swimming,false)*ink.rogue.Move();
    if(ink.phase==SlimePhase::Charging)speed*=.28f;
    bool jumped=jump&&!prevSpace_&&p.grounded&&ink.phase!=SlimePhase::Firing;
    if(jumped)p.velocity.y=12;prevSpace_=jump;
    if(dodged){ink.dodgeAge=0;ink.perfectUsed=false;ink.dodgeOrigin=pos;p.action=Action::Dodge;p.timer=.18f;p.cooldown=.65f;p.invincible=std::max(p.invincible,SlimeDodgeInvincibility);p.dodgeDirection=Length(wish)>.1f?wish:Forward(yaw_,0);}ink.previousDodge=dodge;
    p.cooldown=std::max(0.f,p.cooldown-dt);
    bool isDodge=p.action==Action::Dodge;
    if(isDodge){wish=p.dodgeDirection;speed=30;p.timer-=dt;if(p.timer<=0)p.action=Action::Free;}
    else if(p.action!=Action::Free){p.timer-=dt;if(p.timer<=0)p.action=Action::Free;speed*=.4f;}
    ink.dodgeLiquid=p.action==Action::Dodge;
    p.velocity.x+=(wish.x*speed-p.velocity.x)*(1-std::exp(-18*dt));p.velocity.z+=(wish.z*speed-p.velocity.z)*(1-std::exp(-18*dt));p.velocity.y-=30*dt;
    V next=pos+p.velocity*dt;next.x=std::clamp(next.x,-328.f,328.f);next.z=std::clamp(next.z,-338.f,378.f);
    for(auto e:r.view<SlimeDissolvable>()){
        const auto& prop=r.get<SlimeDissolvable>(e);float f;V n;
        if(Chrono::Sweep(pos,next-pos,{prop.center-prop.half,prop.center+prop.half},{.6f,1.1f,.6f},f,n)){
            next=pos+(next-pos)*std::max(0.f,f-.001f);p.velocity=p.velocity-n*std::min(0.f,Dot(p.velocity,n));}
    }
    int nextSurface=-1;float nextFloor=InkGround(next,&nextSurface);
    if(nextFloor>pos.y+.1f){next.x=pos.x;next.z=pos.z;nextFloor=floor;}
    bool wasGrounded=p.grounded;
    p.grounded=SlimeGrounded(pos.y,next.y,p.velocity.y,wasGrounded,jumped,floor,nextFloor,Length(V{next.x-pos.x,0,next.z-pos.z}));
    if(p.grounded){next.y=nextFloor+1.25f;p.velocity.y=0;p.recoveryPoint=next;}
    ink.groundHeight=nextFloor;ink.groundSlope={};
    if(nextSurface>=0){V n=inkSurfaces_[nextSurface].Normal();if(n.y>.5f)ink.groundSlope={-n.x/n.y,0,-n.z/n.y};}
    // Blend local ground normals over the body's footprint for fluid presentation.
    float sx=(InkGround(next+V{2,0,0})-InkGround(next-V{2,0,0}))/4;
    float sz=(InkGround(next+V{0,0,2})-InkGround(next-V{0,0,2}))/4;
    if(std::abs(sx)<.65f)ink.groundSlope.x=sx;
    if(std::abs(sz)<.65f)ink.groundSlope.z=sz;
    // Read the latest completed GPU shape, not a fixed circle. Offsets travel
    // with the controller during the short readback latency; their deformation stays intact.
    const auto& body=Engine::Renderer::GetInstance()->GetFluidBodySnapshot();
    if(!body.offsets.empty()){ink.footprintSerial=body.serial;ink.footprintRadius=2.5f;}
    // Ground-only, fixed-distance samples also include evasive slides. Air breaks continuity.
    if(wasGrounded&&p.grounded){ink.domainPath.Append({next.x,nextFloor+.16f,next.z});}
    else if(!p.grounded)ink.domainPath.BreakTrail();
    trailLiquid_.Sync(ink.domainPath.points);trailLiquid_.Step(dt);
    candidateLiquid_.Sync(ink.domainPath.candidate.points);candidateLiquid_.Step(dt);
    captureAge_+=dt;
    if((captureScanClock_-=dt)<=0){
        captureScanClock_=.1f;candidateCapture_=0;
        if(ink.domainPath.candidate.Ready())
            for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.at.y-InkGround(enemy.at)<SwarmCaptureHeight&&DomainContains(ink.domainPath.candidate,enemy.at))++candidateCapture_;
    }
    if(attack&&!ink.previousAttack&&ink.domainPath.candidate.Ready()&&homingDomains_.size()<6){
        HomingDomain domain;domain.loop=ink.domainPath.candidate;
        domain.liquid=candidateLiquid_;
        domain.highlight=DomainHighlight(domain.loop);
        domain.bulletPower=DomainBulletPower(domain.loop.Enclosures());
        domain.total=DomainAmmo(domain.loop);domain.remaining=domain.loop.shape==DomainShape::Triangle?3:domain.total;
        ActivateRogue(r,p,ctx,domain);
        domain.reach=DomainReach(domain.loop);
        domain.enclosesBoss=DomainContains(domain.loop,Center(r,boss_));
        // Capture: everything near the ground inside the closed loop dissolves at once.
        // Flyers only come within reach while diving at the player, so the best play is to
        // bait their attacks and close a big loop around them.
        int captured=0;
        for(auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.at.y-InkGround(enemy.at)<SwarmCaptureHeight&&DomainContains(domain.loop,enemy.at)){HitSwarm(r,p,ctx,enemy.id,1e6f);++captured;}
        if(captured>0){
            captureShown_=captured;captureAge_=0;candidateCapture_=0;
            ctx.camera->StartImpactShake(.16f+.004f*float(std::min(captured,60)),std::min(.32f,.05f+.006f*float(captured)),{0,1,0},14);
        }
        ink.domainLastCount=domain.total;ink.domainLastArea=domain.loop.area;ink.domainLastLength=domain.loop.length;
        ink.domainLastEnclosures=domain.loop.Enclosures();
        ink.domainLastShape=domain.loop.shape;++ink.domainSkillUses[size_t(domain.loop.shape)];
        homingDomains_.push_back(std::move(domain));++ink.domainVolleys;++ink.shots;
        ink.domainPath.Clear();ink.domainPath.Append({next.x,nextFloor+.16f,next.z});
        ctx.camera->StartImpactShake(.10f,.08f,{0,1,0},12);
        auto* audio=Engine::Audio::GetInstance();audio->Play(slimePressureSound_,false,.25f*audio->GetMasterSEVolume(),1.1f);
    }
    ink.previousAttack=attack;
    // A damped volume-preserving pose drives forces, not mesh scale. Landing
    // excites the spring; air releases the flat supporting plane gradually.
    if(!wasGrounded&&p.grounded)ink.fluidAspectVelocity=-7.f;
    float targetAspect=p.grounded?1.f:std::clamp(1.15f+p.velocity.y*.025f,.9f,1.5f);
    ink.fluidAspectVelocity+=(70.f*(targetAspect-ink.fluidAspect)-7.f*ink.fluidAspectVelocity)*dt;
    ink.fluidAspect=std::clamp(ink.fluidAspect+ink.fluidAspectVelocity*dt,.55f,1.6f);
    ink.airBlend+=((p.grounded?0.f:1.f)-ink.airBlend)*(1-std::exp(-14*dt));
    V planar{p.velocity.x,0,p.velocity.z};float travelSpeed=Length(planar);
    if(travelSpeed>.5f){
        float heading=std::atan2(ink.fluidDirection.x,ink.fluidDirection.z);
        heading+=std::remainder(std::atan2(planar.x,planar.z)-heading,6.283185f)*(1-std::exp(-12*dt));
        ink.fluidDirection={std::sin(heading),0,std::cos(heading)};
    }
    ink.fluidMotion+=(std::min(1.f,travelSpeed/18.f)-ink.fluidMotion)*(1-std::exp(-7*dt));
    t.translate=Write(next);float bodyScale=std::cbrt(std::max(1.f,p.mass)/SlimeMaximumMass);
    float pressure=ink.phase==SlimePhase::Charging?1+.15f*(ink.Power(ink.charge)):1;
    float pulse=ink.phase==SlimePhase::Charging?1+.025f*std::sin(ink.phaseAge*(25+ink.tier*12)):1;
    t.scale={bodyScale*pressure*pulse,bodyScale*pressure/pulse,bodyScale*pressure*pulse};t.rotate.y=ink.phase==SlimePhase::Firing?ink.beamYaw:yaw_;
    InkCamera(r,p,ctx);
    UpdateSwarm(r,p,ctx);
    if(!control&&ink.battlePhase==BattlePhase::Emerging){UpdateCinematic(r,p,ctx);return;}
    if(p.hitStop<=0&&ink.battlePhase==BattlePhase::Boss)UpdateInkBoss(r,p,ctx);
    if(finished_){ink.monoAge=-1;ink.phase=SlimePhase::Roaming;Presentation(r,p,ctx);return;}
    UpdateDomains(r,p,ctx);
    UpdateRogueSkills(r,p,ctx);
    // Activation and damage can cut the path after the liquid update.
    trailLiquid_.Sync(ink.domainPath.points);candidateLiquid_.Sync(ink.domainPath.candidate.points);
    ink.deployed=0;for(const auto& trail:slimeTrails_)ink.deployed+=trail.mass;ink.tank=ink.deployed;
    inkUploadClock_+=dt;if(inkDirty_&&inkUploadClock_>=.1f){
        if(inkNeedsRebuild_){
            for(auto& s:inkSurfaces_)s.mask.fill(0);
            for(const auto& trail:slimeTrails_)inkSurfaces_[trail.surface].Stamp(trail.at,trail.radius,&trail.footprint,std::sqrt(trail.mass/std::max(trail.originalMass,.0001f)));
            inkNeedsRebuild_=false;
        }
        UploadInk();inkUploadClock_=0;
    }
    ink.onInk=surface>=0&&p.grounded&&inkSurfaces_[surface].Painted({next.x,nextFloor,next.z});
    auto& hp=r.get<HealthComponent>(player_);hp.SetHp(p.mass);hp.SetMaxHp(SlimeMaximumMass);hp.SetDead(p.mass<=0);
    Presentation(r,p,ctx);
    if(!finished_&&ink.perfectAge>=0&&ink.perfectAge<.4f){auto params=ctx.renderer->GetPostProcessParams();
        params.san=1;params.vignette=.3f;params.chromaShift=SlimePerfectPulse(ink.perfectAge);
        ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("Grayscale");}
    if(!finished_&&ink.monoAge>=0&&ink.monoAge<.2f){auto params=ctx.renderer->GetPostProcessParams();params.san=SlimeMono(ink.monoAge);params.vignette=.35f;
        ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("Grayscale");}
}

void ChronoSystem::DrawInkUI(entt::registry& r,GameContext& ctx){
    const auto& p=r.get<Player>(player_);const auto& ink=r.get<InkPlayer>(player_);
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
    if(cinematicActive_){DrawCinematic(ctx);return;}
    if(bossDefeat_&&!finished_){DrawBossDefeat(ctx);return;}
    if(ink.rogue.menu){UI::Canvas selection(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y,ink.rogue.MenuEase());
        if(ctx.useOverrideMouse)selection.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
        UI::RogueSelection(selection,ink.rogue);return;}
    if(finished_){
        ui.Result(won_,"TIME  "+TimeText(p.stats.seconds),
            "KOs  "+std::to_string(ink.swarmKills)+"  |  BEST CHAIN  "+std::to_string(ink.swarmBestCombo)+"  |  CORES  "+std::to_string(p.stats.counters)+" / 3");
        return;
    }
    const bool open=r.get<Target>(weakpoint_).active;
    bool bossBattle=ink.battlePhase==BattlePhase::Boss;
    const auto& creature=r.get<CreatureBoss>(boss_);
    std::string warning=creature.stage==CreatureStage::Dive&&creature.sweepPhase<=2?"DIVE INCOMING / LEAVE THE MARK":
        creature.stage==CreatureStage::Dive&&creature.sweepPhase==3?"RADIAL FEATHERS / FIND A GAP":
        creature.stage==CreatureStage::Snake&&creature.attack==CreatureAttack::HeadTail&&creature.sweepPhase==1?"HEAD STRIKE / DODGE SIDEWAYS":
        creature.stage==CreatureStage::Snake&&creature.attack==CreatureAttack::HeadTail&&(creature.sweepPhase==5||creature.sweepPhase==6)?"TAIL SWEEP / JUMP OR BACKSTEP":"DRAW LOOPS / CHAIN BEFORE FIRING";
    std::string objective=ink.battlePhase==BattlePhase::Horde?"CLEAR THE SWARM / CHAIN YOUR LOOPS":ink.battlePhase==BattlePhase::Emerging?"SERPENT AWAKENING / KEEP MOVING":ink.downTimer>0?"ARMOR MELTED / CORE EXPOSED":open?"CORE OPEN / CLOSE YOUR LOOP":warning;
    // Mission cluster on a translucent glass plate with a lime edge; the chain counter breaks out below it.
    ui.Rounded({22,18,428,146},{.025f,.045f,.035f,.62f},22);
    ui.RoundedRing({22,18,428,146},UI::WithAlpha(UI::Paper,.08f),22);
    ui.Rounded({22,34,5,114},bossBattle?UI::Gold:UI::Lime,3);
    ui.Label(bossBattle?"SERPENT HUNT":"SLIME SWARM",42,28,13,UI::Lime,3.2f);
    const std::string count=bossBattle?std::to_string(p.stats.counters):std::to_string(ink.swarmKills);
    ui.Bold(count,40,40,50,UI::Paper);
    ui.Text(bossBattle?" / 3 CORES":" / "+std::to_string(SwarmBossKills)+" KO",44+ui.Width(count,50),66,19,UI::Muted);
    const auto objectiveColor=open?UI::Gold:UI::Paper;
    ui.Rounded({42,108,4,18},open?UI::Gold:UI::Lime,2);ui.Bold(objective,54,106,17,objectiveColor);
    ui.Bar({42,134,380,8},bossBattle?ink.coreHealth/100:ink.battlePhase==BattlePhase::Horde?float(ink.swarmKills)/SwarmBossKills:ink.battleAge/SwarmEmergenceSeconds,bossBattle?UI::Gold:UI::Lime);
    if(bossBattle)ui.Label("ARMOR DISSOLVE  "+std::to_string(int(ink.armor))+"%",44,152,13,UI::Gold,2.4f);
    else if(ink.swarmCombo>=2&&ink.swarmComboAge>0){
        const bool hot=ink.swarmCombo>=20;const std::string chain=std::to_string(ink.swarmCombo);
        if(hot)ui.Brush({30,146,ui.Width(chain,34)+150,52},UI::WithAlpha(UI::Gold,.9f),-.03f);
        ui.Bold(chain,44,150,34,hot?UI::Ink:UI::Lime);ui.Label("CHAIN",50+ui.Width(chain,34),166,13,hot?UI::Ink:UI::Lime,2.6f);
    }
    auto banner=[&](const std::string& text,float y,float size,Engine::Vector4 color,float alpha){
        UI::Canvas b=ui;b.MultiplyAlpha(alpha);const float w=b.Width(text,size);
        b.Brush({640-w*.5f-56,y-size*.28f,w+112,size*1.55f},UI::WithAlpha(color,.94f),-.025f);
        b.Center(text,640,y,size,UI::Ink);
    };
    if(ink.battlePhase==BattlePhase::Emerging)banner("THE SERPENT RISES",40,30,UI::Gold,1);
    if(ink.perfectAge>=0)banner("PERFECT DODGE",462,28,UI::Gold,std::clamp(1.2f-ink.perfectAge,0.f,1.f));
    // Body and growth: two rings in a pool of shade, bottom-left.
    ui.Shade({18,520,270,150},.6f);
    const auto bodyColor=p.mass<=SlimeMaximumMass*.25f?UI::Gold:UI::Lime;
    ui.Ring(88,598,44,p.mass/SlimeMaximumMass,bodyColor,7);
    ui.Center(std::to_string(int(p.mass)),88,574,32,bodyColor);
    ui.LabelCenter("BODY",88,611,11,UI::Muted,2.4f);
    ui.Ring(196,604,28,float(ink.rogue.xp)/float(ink.rogue.Required()),UI::Teal,4);
    ui.Center(std::to_string(ink.rogue.level),196,588,24);
    ui.LabelCenter("LV",196,616,10,UI::Muted,2);
    bool ready=ink.domainPath.candidate.Ready();
    // The loop prompt: a glowing pill that tells what the loop will do.
    auto pill=[&](const std::string& text,Engine::Vector4 color,const char* padFire){
        const float textW=ui.SpacedWidth(text,17,1.6f)+(padFire?34.f:0.f),w=textW+56,x=640-w*.5f;
        ui.Halo(640,628,w*.62f,UI::WithAlpha(color,.14f));
        ui.Rounded({x,608,w,40},{.05f,.085f,.07f,.92f},20);ui.RoundedRing({x,608,w,40},UI::WithAlpha(color,.8f),20);
        float tx=ui.Spaced(text,x+28,618,17,1.6f,color);
        if(padFire)ui.PadGlyph(padFire,tx+6,613,28);
    };
    if(ink.regenerating||p.mass<=SlimeMaximumMass*.15f)
        pill(ink.regenerating?"REGENERATING  /  KEEP EVADING":"LOW BODY  /  EVADE TO REGENERATE",UI::Gold,nullptr);
    else if(ready){
        const int shots=DomainAmmo(ink.domainPath.candidate)+12*ink.rogue.ranks[0];
        const auto infoColor=candidateCapture_>=10?UI::Gold:UI::Lime;
        if(homingDomains_.size()>=6)pill("DOMAINS BUSY  /  KEEP EVADING",UI::Muted,nullptr);
        else{
            const std::string info="CAPTURE "+std::to_string(candidateCapture_)+"   "+std::to_string(shots)+" SHOTS   ";
            if(UI::UsingPad())pill(info+"FIRE",infoColor,"xbox_rt");
            else pill(info+"CLICK TO FIRE",infoColor,nullptr);
        }
    }
    if(captureShown_>0&&captureAge_<1.4f){
        // Bigger captures shout louder.
        const float pop=1+.3f*std::exp(-captureAge_*10),fade=std::clamp((1.4f-captureAge_)/.45f,0.f,1.f);
        const float size=(30+.5f*float(std::min(captureShown_,60)))*pop;
        banner("CAPTURE  x"+std::to_string(captureShown_),170-size*.5f,size,captureShown_>=20?UI::Gold:UI::Lime,fade);
    }
    ui.Dot(640,360,2.5f,ink.hitFlash>0?UI::Gold:UI::Paper);
    ui.Line(626,360,633,360,2,UI::WithAlpha(UI::Lime,.85f));ui.Line(647,360,654,360,2,UI::WithAlpha(UI::Lime,.85f));
    ui.Line(640,346,640,353,2,UI::WithAlpha(UI::Lime,.85f));ui.Line(640,367,640,374,2,UI::WithAlpha(UI::Lime,.85f));
    // Control hints float on a soft gradient instead of a bar.
    ui.ImageRot(UI::Art::Fade,{-200,640,1680,100},3.14159265f,UI::WithAlpha(UI::Night,.72f));
    {UI::Canvas hints=ui;hints.MultiplyAlpha(.88f);
    hints.Prompt("keyboard_w","MOVE + DRAW","xbox_stick_l","MOVE + DRAW",40,682,24,16);
    hints.Prompt("mouse_right","DODGE","xbox_button_b","DODGE",230,682,24,16);
    hints.Prompt("mouse_left","FIRE LOOPS","xbox_rt","FIRE LOOPS",360,682,24,16);
    hints.Prompt("keyboard_space","JUMP","xbox_button_a","JUMP",530,682,24,16);
    hints.Prompt("mouse_scroll",ink.lockedOn?"LOCK OFF":"LOCK ON","xbox_stick_r_press",ink.lockedOn?"LOCK OFF":"LOCK ON",640,682,24,16);
    hints.Prompt("keyboard_escape","PAUSE","xbox_button_menu","PAUSE",1150,682,24,16);}
    // North-up map: complete arena, live path and persistent selected loop.
    ui.Rounded({1074,18,186,196},{.04f,.07f,.055f,.9f},20);ui.RoundedRing({1074,18,186,196},UI::WithAlpha(UI::Paper,.12f),20);
    ui.Label("N",1240,26,11,UI::Lime,0);
    const float mapX=1101,mapY=44,mapScale=.2f;
    auto mapPoint=[&](V at){return V{mapX+(std::clamp(at.x,-330.f,330.f)+330)*mapScale,mapY+(380-std::clamp(at.z,-340.f,380.f))*mapScale,0};};
    ui.Fill({mapX,mapY,660*mapScale,720*mapScale},{.025f,.065f,.045f,1});
    for(int i=1;i<4;++i){ui.Line(mapX+i*165*mapScale,mapY,mapX+i*165*mapScale,mapY+720*mapScale,1,{.1f,.18f,.13f,1});
        ui.Line(mapX,mapY+i*180*mapScale,mapX+660*mapScale,mapY+i*180*mapScale,1,{.1f,.18f,.13f,1});}
    auto mapLine=[&](const std::vector<V>& points,Engine::Vector4 color,float width){
        if(points.empty())return;V from=mapPoint(points.front());
        for(size_t i=1;i<points.size();++i){V to=mapPoint(points[i]);if(Length(to-from)<1.2f&&i+1<points.size())continue;
            ui.Line(from.x,from.y,to.x,to.y,width,color);from=to;}
    };
    for(const auto& domain:homingDomains_)mapLine(domain.loop.points,{.22f,.46f,.27f,1},1.5f);
    mapLine(ink.domainPath.points,UI::Lime,1.5f);
    if(ready)mapLine(ink.domainPath.candidate.points,UI::Gold,2.3f);
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){V point=mapPoint(enemy.at);ui.Fill({point.x-1,point.y-1,2,2},enemy.bomber?UI::Gold:Engine::Vector4{.85f,.31f,.22f,1});}
    if(ink.battlePhase!=BattlePhase::Horde){V enemy=mapPoint(Center(r,boss_));ui.Fill({enemy.x-4,enemy.y-4,8,8},{1,.25f,.18f,1});}
    V marker=mapPoint(Read(r.get<TransformComponent>(player_).translate));
    V direction{std::sin(yaw_),-std::cos(yaw_),0},right{-direction.y,direction.x,0};
    V tip=marker+direction*6,left=marker-direction*4+right*4,rgt=marker-direction*4-right*4;
    ui.Line(tip.x,tip.y,left.x,left.y,2,UI::Paper);ui.Line(left.x,left.y,rgt.x,rgt.y,2,UI::Paper);ui.Line(rgt.x,rgt.y,tip.x,tip.y,2,UI::Paper);
    if(open||ink.lockedOn){
        V target=Center(r,weakpoint_);
        if(ink.battlePhase!=BattlePhase::Boss){V from=Read(r.get<TransformComponent>(player_).translate);float nearest=1e9f;
            for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&DomainDistance(enemy.at,from)<nearest){nearest=DomainDistance(enemy.at,from);target=enemy.at;}}
        using namespace DirectX;XMFLOAT4 clip;
        XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(target.x,target.y,target.z,1),ctx.camera->View()*ctx.camera->Proj()));
        if(clip.w>0){float x=std::clamp((clip.x/clip.w*.5f+.5f)*1280,40.f,1240.f),y=std::clamp((-clip.y/clip.w*.5f+.5f)*720,180.f,490.f);
            ui.Bold(ink.lockedOn?"LOCKED":"CORE",x-ui.Width(ink.lockedOn?"LOCKED":"CORE",22)*.5f,y-32,22,UI::Gold);
            if(ink.lockedOn){ui.Fill({x-18,y-18,8,2},UI::Gold);ui.Fill({x-18,y-18,2,10},UI::Gold);
                ui.Fill({x+10,y+16,8,2},UI::Gold);ui.Fill({x+16,y+8,2,10},UI::Gold);}}
    }
}

#include "HordeCombat.inl"
#include "HomingDomains.inl"
#include "RogueliteCombat.inl"
#include "BossDefeat.inl"
}
