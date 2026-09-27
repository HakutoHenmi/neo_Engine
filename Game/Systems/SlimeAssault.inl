// Production contact-trail / recall / pressure-beam loop.
namespace Game {
void ChronoSystem::UpdateInk(entt::registry& r,Player& p,GameContext& ctx){
    auto& ink=r.get<InkPlayer>(player_);auto* control=r.try_get<ControlFrame>(player_);
    const float dt=std::min(ctx.dt,.05f);diagnostic_=control!=nullptr;
    bool attack=control?control->attack:Down(VK_LBUTTON),slide=control?control->aim:Down(VK_RBUTTON);
    if(!control&&Down(VK_F4)&&!prevSnake_){snakeOnly_=!snakeOnly_;prevSnake_=true;Reset(r);return;}prevSnake_=!control&&Down(VK_F4);
    bool shake=!control&&Down(VK_F6);if(shake&&!prevShake_)shakeSetting_=(shakeSetting_+1)%3;prevShake_=shake;ctx.camera->SetShakeStrength(shakeSetting_*.5f);
    if(control){yaw_=control->yaw;pitch_=control->pitch;}
    else if(ctx.input){yaw_+=ctx.input->GetMouseDeltaX()*.0025f;pitch_=std::clamp(pitch_+ctx.input->GetMouseDeltaY()*.0025f,-1.15f,1.2f);}
    Engine::WindowDX::SetCursorVisible(false);
    p.stats.seconds+=dt;p.worldScale=p.aimScale=1;p.aiming=p.automatic=false;p.target=p.preview=p.bufferedTarget=entt::null;p.buffer=0;
    p.invincible=std::max(0.f,p.invincible-dt);p.damageAge+=dt;p.hitStop=std::max(0.f,p.hitStop-dt);ink.hitFlash=std::max(0.f,ink.hitFlash-dt);
    ink.shotClock=std::max(0.f,ink.shotClock-dt);ink.phaseAge+=dt;if(ink.monoAge>=0)ink.monoAge+=dt;
    auto& t=r.get<TransformComponent>(player_);V pos=Read(t.translate);int surface=-1;float floor=InkGround(pos,&surface);
    ink.deployed=0;for(const auto& trail:slimeTrails_)ink.deployed+=trail.mass;
    ink.Recover(p.mass,p.damageAge,dt);
    if(ink.phase==SlimePhase::Roaming){float total=p.mass+ink.deployed+ink.spent;ink.capacity=SlimeCapacity(total);ink.reserve=SlimeReserve(total);}
    bool jump=control?control->jump:Down(VK_SPACE),dodge=control?control->dodge:Down(VK_SHIFT);
    bool dodged=dodge&&!prevShift_&&p.cooldown<=0&&ink.phase!=SlimePhase::Firing&&ink.phase!=SlimePhase::Returning;
    if(attack&&!ink.previousAttack&&ink.phase==SlimePhase::Roaming&&p.action!=Action::Dodge){
        ink.phase=SlimePhase::Charging;ink.phaseAge=0;ink.charge=0;
        ink.limitEligible=p.mass<=ink.reserve*2&&ink.deployed>=ink.capacity*.75f;
    }
    if(dodged&&ink.phase==SlimePhase::Charging)ink.Cancel();
    if(ink.phase==SlimePhase::Charging){
        float budget=ink.capacity*dt/.9f;
        for(auto& trail:slimeTrails_){if(budget<=0)break;float amount=std::min(trail.mass,budget);
            if(amount<=0)continue;trail.mass-=amount;budget-=amount;ink.Collect(p.mass,amount);inkDirty_=true;inkNeedsRebuild_=true;
            if(sparks_.size()<180)sparks_.push_back({trail.at,pos,0});
        }
        slimeTrails_.erase(std::remove_if(slimeTrails_.begin(),slimeTrails_.end(),[](const SlimeTrail& a){return a.mass<=.0001f;}),slimeTrails_.end());
        int tier=SlimeTier(ink.charge,ink.capacity);if(tier>ink.tier){auto* audio=Engine::Audio::GetInstance();audio->Play(slimeRecallSound_,false,.32f*audio->GetMasterSEVolume(),.8f+tier*.25f);}ink.tier=tier;
        if(!attack&&ink.previousAttack&&ink.Fire(p.mass)){
            ink.beamYaw=yaw_;ink.beamPitch=pitch_;ctx.camera->StartImpactShake(.12f+ink.tier*.04f,.08f+ink.tier*.08f,{0,0,1},12);
            auto* audio=Engine::Audio::GetInstance();audio->Play(slimePressureSound_,false,(.16f+ink.tier*.08f)*audio->GetMasterSEVolume(),ink.tier==3?1.f:ink.tier==2?1.08f:1.16f);
        }
    }
    ink.previousAttack=attack;
    V input=control?control->move:V{float(Down('D'))-float(Down('A')),0,float(Down('W'))-float(Down('S'))};input=Unit(input);
    V wish{input.x*std::cos(yaw_)+input.z*std::sin(yaw_),0,-input.x*std::sin(yaw_)+input.z*std::cos(yaw_)};
    ink.swimming=slide&&p.grounded&&ink.phase==SlimePhase::Roaming;
    float speed=InkMoveSpeed(ink.swimming,false);
    if(ink.phase==SlimePhase::Charging)speed*=.28f;
    bool jumped=jump&&!prevSpace_&&p.grounded&&ink.phase!=SlimePhase::Firing;
    if(jumped)p.velocity.y=12;prevSpace_=jump;
    if(dodged){p.action=Action::Dodge;p.timer=.18f;p.cooldown=.65f;p.invincible=.16f;p.dodgeDirection=Length(wish)>.1f?wish:Forward(yaw_,0);}prevShift_=dodge;
    p.cooldown=std::max(0.f,p.cooldown-dt);
    bool isDodge=p.action==Action::Dodge;
    if(isDodge){wish=p.dodgeDirection;speed=30;p.timer-=dt;if(p.timer<=0)p.action=Action::Free;}
    else if(p.action!=Action::Free){p.timer-=dt;if(p.timer<=0)p.action=Action::Free;speed*=.4f;}
    if(ink.phase==SlimePhase::Firing){
        float turn=(ink.tier==3?.7f:ink.tier==2?1.3f:2.8f)*dt;
        ink.beamYaw+=std::clamp(std::remainder(yaw_-ink.beamYaw,6.283185f),-turn,turn);
        ink.beamPitch+=std::clamp(pitch_-ink.beamPitch,-turn,turn);
        V back=Forward(ink.beamYaw,0)*(-2.f-(ink.Power(ink.beamCharge))*7.125f);wish=wish*1.4f+back;speed=1;
    }
    p.velocity.x+=(wish.x*speed-p.velocity.x)*(1-std::exp(-18*dt));p.velocity.z+=(wish.z*speed-p.velocity.z)*(1-std::exp(-18*dt));p.velocity.y-=30*dt;
    V next=pos+p.velocity*dt;next.x=std::clamp(next.x,-138.f,138.f);next.z=std::clamp(next.z,-128.f,168.f);
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
    // Blend slope changes over a body-width footprint at ramp/landing seams.
    // Real cliffs retain their local normal instead of tilting the whole body.
    float sx=(InkGround(next+V{2,0,0})-InkGround(next-V{2,0,0}))/4;
    float sz=(InkGround(next+V{0,0,2})-InkGround(next-V{0,0,2}))/4;
    if(std::abs(sx)<.65f)ink.groundSlope.x=sx;
    if(std::abs(sz)<.65f)ink.groundSlope.z=sz;
    // Read the latest completed GPU shape, not a fixed circle. Offsets travel
    // with the controller during the short readback latency; their deformation stays intact.
    const auto& body=Engine::Renderer::GetInstance()->GetFluidBodySnapshot();
    float distance=Length(V{next.x-pos.x,0,next.z-pos.z});
    if(ink.phase==SlimePhase::Roaming&&distance>.0001f&&wasGrounded&&p.grounded&&!body.offsets.empty()){
        struct ContactShape {int surface;V at;std::array<float,32> outline{};float radius=0;};
        std::vector<ContactShape> contacts;
        for(int k=0;k<int(inkSurfaces_.size());++k){const auto& s=inkSurfaces_[k];V normal=s.Normal();
            V center=next+V{0,.18f,0};float gap=Dot(center-s.origin,normal);V at=center-normal*gap;float u,v;
            if(std::abs(gap)>7||!s.UV(at,u,v))continue;
            ContactShape contact{};contact.surface=k;contact.at=at;
            for(auto offset:body.offsets){V point=center+V{offset.x,offset.y,offset.z};
                float planeGap=Dot(point-s.origin,normal);
                if(std::abs(planeGap)>.55f)continue;
                V delta=point-at;float x=Dot(delta,s.u),y=Dot(delta,s.v),radius=std::sqrt(x*x+y*y)+.22f;
                if(radius>7)continue;
                float angle=std::atan2(y,x);int bin=int((angle+3.14159265f)*32/6.2831853f)%32;
                // Reconstruction kernels overlap adjacent angular sectors.
                for(int n=-1;n<=1;++n)contact.outline[(bin+n+32)%32]=std::max(contact.outline[(bin+n+32)%32],radius);
                contact.radius=std::max(contact.radius,radius);
            }
            if(contact.radius>.15f)contacts.push_back(contact);
        }
        if(!contacts.empty()){
            ink.footprintRadius=contacts.front().radius;ink.footprintSerial=body.serial;
            int steps=std::max(1,int(std::ceil(distance/.45f)));
            for(int j=1;j<=steps;++j){
                float amount=SlimeDeposit(p.mass,distance/steps,(isDodge?1.3f:ink.swimming?1.05f:.65f)*(ink.capacity/SlimeChargeCapacity),ink.reserve);if(amount<=0)break;
                for(const auto& c:contacts){const auto& s=inkSurfaces_[c.surface];V normal=s.Normal();
                    V delta=Lerp(pos,next,float(j)/steps)-next;delta=delta-normal*Dot(delta,normal);V at=c.at+delta;
                    float share=amount/float(contacts.size());bool merged=false;
                    for(auto it=slimeTrails_.rbegin();it!=slimeTrails_.rend();++it)if(it->surface==c.surface&&Length(it->at-at)<.35f){
                        it->mass+=share;it->originalMass+=share;it->radius=std::max(it->radius,c.radius);
                        for(int n=0;n<32;++n)it->footprint[n]=std::max(it->footprint[n],c.outline[n]);merged=true;break;
                    }
                    if(!merged)slimeTrails_.push_back({at,c.surface,share,c.radius,c.outline,share});
                    // Deposits only add coverage. Rasterize this footprint once,
                    // instead of replaying every old deposit on every upload.
                    inkSurfaces_[c.surface].Stamp(at,c.radius,&c.outline);
                }
            }
            inkDirty_=true;
        }
    }
    // A damped volume-preserving pose drives forces, not mesh scale. Landing
    // excites the spring; air releases the flat supporting plane gradually.
    if(!wasGrounded&&p.grounded)ink.fluidAspectVelocity=-7.f;
    float targetAspect=p.grounded?1.f:std::clamp(1.15f+p.velocity.y*.025f,.9f,1.5f);
    ink.fluidAspectVelocity+=(70.f*(targetAspect-ink.fluidAspect)-7.f*ink.fluidAspectVelocity)*dt;
    ink.fluidAspect=std::clamp(ink.fluidAspect+ink.fluidAspectVelocity*dt,.55f,1.6f);
    ink.airBlend+=((p.grounded?0.f:1.f)-ink.airBlend)*(1-std::exp(-14*dt));
    V planar{p.velocity.x,0,p.velocity.z};float travelSpeed=Length(planar);
    if(travelSpeed>.5f)ink.fluidDirection=Unit(Lerp(ink.fluidDirection,Unit(planar),1-std::exp(-9*dt)));
    ink.fluidMotion+=(std::min(1.f,travelSpeed/18.f)-ink.fluidMotion)*(1-std::exp(-7*dt));
    t.translate=Write(next);float bodyScale=std::cbrt(std::max(1.f,p.mass)/SlimeMaximumMass);
    float pressure=ink.phase==SlimePhase::Charging?1+.15f*(ink.Power(ink.charge)):1;
    float pulse=ink.phase==SlimePhase::Charging?1+.025f*std::sin(ink.phaseAge*(25+ink.tier*12)):1;
    t.scale={bodyScale*pressure*pulse,bodyScale*pressure/pulse,bodyScale*pressure*pulse};t.rotate.y=ink.phase==SlimePhase::Firing?ink.beamYaw:yaw_;
    InkCamera(r,p,ctx);
    if(p.hitStop<=0)UpdateInkBoss(r,p,ctx);
    if(finished_){ink.monoAge=-1;ink.phase=SlimePhase::Roaming;Presentation(r,p,ctx);return;}
    if(ink.phase==SlimePhase::Charging)ink.charge=std::min(ink.charge,std::max(0.f,p.mass-ink.reserve));
    if(ink.phase==SlimePhase::Firing){
        V direction=Forward(ink.beamYaw,ink.beamPitch);float range=24+(ink.Power(ink.beamCharge))*71.25f;
        ink.beamStart=next+V{0,.25f,0}+direction*.8f;float length=range;
        // Sweep the beam cross-section. The underside can brush the supporting floor.
        V half{ink.beamRadius,std::min(.35f,ink.beamRadius),ink.beamRadius};
        for(const auto& box:solids_){float f;V n;if(Chrono::Sweep(ink.beamStart,direction*range,box,half,f,n))length=std::min(length,range*f);}
        for(const auto& s:inkSurfaces_){float f;V at;if(s.Ray(ink.beamStart,direction*range,f,at))length=std::min(length,range*f);}
        entt::entity dissolvedProp=entt::null;
        for(auto e:r.view<SlimeDissolvable>()){
            const auto& prop=r.get<SlimeDissolvable>(e);float f;V n;
            if(Chrono::Sweep(ink.beamStart,direction*range,{prop.center-prop.half,prop.center+prop.half},half,f,n)&&range*f<length){length=range*f;dissolvedProp=e;}
        }
        float hit;bool core=RaySphere(ink.beamStart,direction,Center(r,weakpoint_),3.6f+ink.beamRadius,length,hit);
        bool armorHit=false;
        if(core){length=std::min(length,hit);dissolvedProp=entt::null;}
        else for(const auto& box:creatureSolids_){float f;V n;if(Chrono::Sweep(ink.beamStart,direction*length,box,half,f,n)){length*=f;armorHit=true;dissolvedProp=entt::null;}}
        ink.beamLength=length;ink.beamEnd=ink.beamStart+direction*length;ink.beamContact=core||armorHit||length<range-.01f;
        ink.damageClock+=SlimeActiveStep(ink.phaseAge,ink.beamDuration,dt);float q=ink.Power(ink.beamCharge);
        while(ink.damageClock>=.05f-.000001f){ink.damageClock-=.05f;
            if(dissolvedProp!=entt::null&&r.valid(dissolvedProp)){
                auto& prop=r.get<SlimeDissolvable>(dissolvedProp);prop.integrity-=.05f*(18+q*q*135);
                r.get<MeshRendererComponent>(dissolvedProp).color={.2f,.8f,.08f,1};
                if(prop.integrity<=0){r.destroy(dissolvedProp);dissolvedProp=entt::null;}
            }
            if(core||armorHit){
                auto& target=r.get<Target>(weakpoint_);auto& boss=r.get<Boss>(boss_);
                if(!boss.countered&&((ink.limitBreak&&!ink.downUsed)||(!target.active&&(ink.armor+=.05f*(20+q*q*80))>=100))){
                    ink.downUsed=true;ink.armor=0;ink.downTimer=3.5f;target.active=true;
                    auto& c=r.get<CreatureBoss>(boss_);c.sweepPhase=3;c.sweepTime=0;c.recoil=.6f;
                }
                if(core&&target.active&&!boss.countered){ink.coreHealth-=.05f*(18+q*q*135);++ink.coreHits;ink.hitFlash=.1f;
                    if(ink.coreHealth<=0){++p.stats.counters;ink.coreHealth=100;ink.armor=0;boss.countered=true;target.active=false;ink.downTimer=0;
                        // A phase victory rebuilds lost body mass. Existing trail
                        // and in-flight liquid count toward the cap, never duplicate them.
                        p.mass+=std::max(0.f,SlimeMaximumMass-p.mass-ink.deployed-ink.spent);
                        r.get<CreatureBoss>(boss_).recoil=.6f;Feedback(p,ink.beamEnd,true);
                        if(p.stats.counters>=3){Finish(p,true);break;}}
                }
            }
        }
        // Existing enemy projectile entities can be dissolved without touching boss bodies or warning volumes.
        std::vector<entt::entity> dissolveProjectiles;
        for(auto e:r.view<HitboxComponent,TransformComponent>()){
            const auto& hb=r.get<HitboxComponent>(e);if(!hb.isProjectile||hb.tag!=TagType::Enemy)continue;
            float impact;V at=Read(r.get<TransformComponent>(e).translate);
            if(RaySphere(ink.beamStart,direction,at,ink.beamRadius+.4f,length,impact))dissolveProjectiles.push_back(e);
        }
        for(auto e:dissolveProjectiles){V at=Read(r.get<TransformComponent>(e).translate);
            ctx.renderer->EmitGPUFluid(EV(at),{0,1,0},{SlimeRed,SlimeGreen,SlimeBlue,1},16,4.025f);r.destroy(e);}
        ink.effectClock+=dt;if(ink.effectClock>=.09f){ink.effectClock=0;
            if(ink.beamContact)ctx.renderer->EmitGPUFluid(EV(ink.beamEnd-direction*.4f),{0,1.8f,0},{SlimeRed,SlimeGreen,SlimeBlue,1},12+ink.tier*8,4.035f);
            if((core||armorHit)&&ink.shotClock<=0){auto* audio=Engine::Audio::GetInstance();audio->Play(slimeAcidSound_,false,.08f*audio->GetMasterSEVolume(),1.f);ink.shotClock=.22f;}
        }
        if(ink.phaseAge>=ink.beamDuration){ink.phase=SlimePhase::Returning;ink.phaseAge=0;}
    }else if(ink.phase==SlimePhase::Returning){
        if(sparks_.size()<180)sparks_.push_back({ink.beamEnd,next,0});ink.Return(p.mass,dt);
    }
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
    auto& hp=r.get<HealthComponent>(player_);hp.hp=p.mass;hp.maxHp=SlimeMaximumMass;hp.isDead=p.mass<=0;
    Presentation(r,p,ctx);
    if(!finished_&&ink.monoAge>=0&&ink.monoAge<.2f){auto params=ctx.renderer->GetPostProcessParams();params.san=SlimeMono(ink.monoAge);params.vignette=.35f;
        ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("Grayscale");}
}

void ChronoSystem::DrawInkUI(entt::registry& r,GameContext& ctx){
    const auto& p=r.get<Player>(player_);const auto& ink=r.get<InkPlayer>(player_);
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
    if(finished_){
        ui.Result(won_,"TIME  "+TimeText(p.stats.seconds),
            "CORES  "+std::to_string(p.stats.counters)+" / 3     |     HITS TAKEN  "+std::to_string(p.stats.hitsTaken));
        return;
    }
    const bool open=r.get<Target>(weakpoint_).active;
    // Keep the three status panels compact, anchored to the screen edges.
    ui.Panel({24,24,448,93});
    ui.Text("SLIME ASSAULT",40,35,21,UI::Lime);
    ui.Text("CORES  "+std::to_string(p.stats.counters)+" / 3",323,36,19);
    ui.Text(ink.downTimer>0?"ARMOR MELTED / CORE EXPOSED":open?"CORE OPEN / RECALL, AIM, RELEASE":"MOVE / SLIDE TO STORE YOUR MASS",40,66,18,open?UI::Gold:UI::Paper);
    ui.Bar({40,97,416,8},1-ink.coreHealth/100,UI::Gold);
    ui.Panel({1050,24,206,74});
    ui.Text("ARMOR DISSOLVE",1066,35,16,UI::Muted);
    ui.Text(std::to_string(int(ink.armor))+"%",1066,58,21,UI::Lime);
    ui.Panel({1080,123,176,42});
    ui.Prompt("keyboard_escape","PAUSE",1090,127);
    ui.Panel({414,574,452,86});
    ui.Text("BODY  "+std::to_string(int(p.mass)),430,585,19);
    ui.Text("TRAIL  "+std::to_string(int(ink.deployed)),577,585,19,UI::Lime);
    ui.Text("RETURN  "+std::to_string(int(ink.spent)),726,585,19,UI::Muted);
    float q=ink.StoredRatio();
    ui.Bar({430,615,420,10},q);

    std::string state=ink.phase==SlimePhase::Charging?"RECALL / COMPRESS":ink.phase==SlimePhase::Firing?"PRESSURE RELEASE":ink.phase==SlimePhase::Returning?"MASS RETURNING":"MOVE / SLIDE";
    if(ink.tier>0)state+="   /   LEVEL "+std::to_string(ink.tier);
    ui.Text("STORED  "+std::to_string(int(q*100))+"%",430,636,17,UI::Lime);
    ui.Text(state,592,636,17);
    if((ink.limitBreak&&ink.phase==SlimePhase::Firing)||ink.regenerating||p.mass<=SlimeMaximumMass*.15f)
        ui.Panel({370,495,540,49});
    if(ink.limitBreak&&ink.phase==SlimePhase::Firing)ui.Center("LIMIT BREAK",640,500,36,UI::Lime);
    else if(ink.regenerating)ui.Center("REGENERATING / KEEP EVADING",640,513,23,UI::Lime);
    else if(p.mass<=SlimeMaximumMass*.15f)ui.Center(ink.deployed>.1f?"LOW BODY / HOLD LMB TO RECALL":"LOW BODY / EVADE TO REGENERATE",640,513,23,UI::Gold);
    ui.Fill({631,359,18,2},ink.hitFlash>0?UI::Gold:UI::Lime);ui.Fill({639,351,2,18},UI::Lime);
    ui.Panel({24,674,1232,40});
    ui.Prompt("keyboard_w","WASD MOVE",44,678);
    ui.Prompt("mouse_right","SLIDE",240,678);
    ui.Prompt("keyboard_shift","DODGE / CANCEL",401,678);
    ui.Prompt("mouse_left","HOLD: RECALL / RELEASE: BEAM",652,678);
    ui.Prompt("keyboard_space","JUMP",1090,678);
    if(open){
        V target=Center(r,weakpoint_);using namespace DirectX;XMFLOAT4 clip;
        XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(target.x,target.y,target.z,1),ctx.camera->View()*ctx.camera->Proj()));
        if(clip.w>0){float x=std::clamp((clip.x/clip.w*.5f+.5f)*1280,40.f,1240.f),y=std::clamp((-clip.y/clip.w*.5f+.5f)*720,180.f,490.f);
            ui.Center("CORE",x,y-30,24,UI::Gold);}
    }
}
void ChronoSystem::DrawSlimeBeam(entt::registry& r,GameContext& ctx){
    const auto& ink=r.get<InkPlayer>(player_);if(finished_)return;
    auto sphere=[&](V at,float size,Engine::Vector4 color){Engine::Transform t;t.translate=EV(at);t.scale={size,size,size};ctx.renderer->DrawMesh(sphere_,white_,t,color,"SlimeBeam",0,false);};
    for(const auto& s:sparks_){float f=std::clamp(s.age/.35f,0.f,1.f);V at=Lerp(s.from,s.to,f*f);sphere(at,.12f+(1-f)*.12f,{SlimeRed,SlimeGreen,SlimeBlue,1});}
    bool firing=ink.phase==SlimePhase::Firing,charging=ink.phase==SlimePhase::Charging;
    if(!firing&&!charging)return;
    V dir=Forward(firing?ink.beamYaw:yaw_,firing?ink.beamPitch:pitch_);
    V right{std::cos(firing?ink.beamYaw:yaw_),0,-std::sin(firing?ink.beamYaw:yaw_)};
    V up=Unit(V{dir.y*right.z-dir.z*right.y,dir.z*right.x-dir.x*right.z,dir.x*right.y-dir.y*right.x});
    V start=firing?ink.beamStart:Read(r.get<TransformComponent>(player_).translate)+dir*1.5f;
    float yaw=firing?ink.beamYaw:yaw_,pitch=firing?ink.beamPitch:pitch_;
    auto mesh=[&](uint32_t handle,V at,V scale,Engine::Vector4 color){Engine::Transform t;t.translate=EV(at);t.scale=EV(scale);t.rotate={pitch,yaw,0};ctx.renderer->DrawMesh(handle,white_,t,color,"SlimeBeam",0,false);};
    float age=ink.phaseAge;
    if(charging){if(ink.tier>=2){float size=.7f+ink.Power(ink.charge)*1.5f;mesh(slimeRingMesh_,start,{size,size,size},{SlimeRed,SlimeGreen,SlimeBlue,.7f});}return;}
    float radius=ink.beamRadius,length=ink.beamLength;
    if(length<.01f)return;
    float envelope=std::min(1.f,(ink.beamDuration-age)/.12f);radius*=std::max(.15f,envelope);
    mesh(slimeBeamMesh_,start,{radius,radius,length},{SlimeRed,SlimeGreen,SlimeBlue,.6f});
    mesh(slimeBeamMesh_,start,{radius*.48f,radius*.48f,length},{.65f+SlimeRed*.35f,.65f+SlimeGreen*.35f,.65f+SlimeBlue*.35f,.95f});
    // A closed, tapered pressure core stays readable when looking along the beam.
    mesh(sphere_,start+dir*(length*.5f),{radius*.58f,radius*.58f,length*.5f},{.72f,.98f,.48f,.96f});
    if(ink.tier>=2){float ringRadius=radius*(ink.tier==3?2.6f:1.7f)*(1+.15f*std::sin(std::min(age,.2f)*15));
        mesh(slimeRingMesh_,start+dir*.3f,{ringRadius,ringRadius,ringRadius},{SlimeRed,SlimeGreen,SlimeBlue,.85f*std::max(0.f,1-age/.4f)});
        float collar=radius*1.3f;
        mesh(slimeRingMesh_,start+dir*.45f,{collar,collar,collar},{SlimeRed,SlimeGreen,SlimeBlue,.7f});
        int rings=ink.tier==3?(ink.limitBreak?7:5):2;
        for(int i=0;i<rings;++i){float f=std::fmod(age*1.3f+float(i)/rings,1.f);float size=radius*(1.1f+.35f*f);
            mesh(slimeRingMesh_,start+dir*(f*length),{size,size,size},{SlimeRed,SlimeGreen,SlimeBlue,(1-f)*.6f});}
        // Two continuous liquid helices around the central column.
        for(int strand=0;strand<(ink.tier==3?2:1);++strand)for(int i=0;i<72;++i){float f=float(i)/72,g=float(i+1)/72;
            float a=f*20-age*15+strand*3.14159f,b=g*20-age*15+strand*3.14159f;
            V from=start+dir*(length*f)+(right*std::cos(a)+up*std::sin(a))*(radius*1.1f);
            V to=start+dir*(length*g)+(right*std::cos(b)+up*std::sin(b))*(radius*1.1f),axis=Unit(to-from);
            Engine::Transform tube;tube.translate=EV(from);tube.rotate={-std::asin(std::clamp(axis.y,-1.f,1.f)),std::atan2(axis.x,axis.z),0};
            tube.scale={radius*.1f,radius*.1f,Length(to-from)*1.02f};
            ctx.renderer->DrawMesh(slimeBeamMesh_,white_,tube,{SlimeRed,SlimeGreen,SlimeBlue,.8f},"SlimeBeam",0,false);}
    }
    if(ink.beamContact){sphere(ink.beamEnd,radius*.9f,{.7f,.95f,.5f,.8f});
        for(int i=0;i<7;++i){float f=std::fmod(age*1.7f+i*.17f,1.f),a=i*2.39996f;
            V at=ink.beamEnd+(right*std::cos(a)+up*std::sin(a))*(radius*.6f+f)+V{0,f*4,0};
            sphere(at,(.25f+f*.8f)*std::max(.5f,radius),{.65f,.8f,.58f,(1-f)*.22f});}
        for(int i=0;i<12;++i){float a=i*2.39996f+age*3;V radial=right*std::cos(a)+up*std::sin(a);float f=std::fmod(age*3+i*.13f,1.f);
            sphere(ink.beamEnd+radial*(radius+f*3)-dir*f,radius*.13f*(1-f)+.05f,{SlimeRed,SlimeGreen,SlimeBlue,1-f});}
    }
}
}
