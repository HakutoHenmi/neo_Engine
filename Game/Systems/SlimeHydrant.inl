namespace Game {
void ChronoSystem::UpdateHydrantAim(GameContext& ctx){
    hydrantAim_={};const auto& body=life_.bodies[0];
    if(body.action!=LifeAction::Hydrant||life_.lab||body.down)return;
    auto& aim=hydrantAim_;aim.active=true;
    auto inverse=DirectX::XMMatrixInverse(nullptr,ctx.camera->View());
    DirectX::XMFLOAT3 eye{},axis{};
    DirectX::XMStoreFloat3(&eye,DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(),inverse));
    DirectX::XMStoreFloat3(&axis,DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0,0,1,0),inverse));
    V camera=Read(eye),direction=Unit(Read(axis));
    // Resolve the closest visible target, never an enemy through a wall.
    auto trace=[&](V from,V delta,bool enemies,bool& enemy){
        float nearest=1,f;V normal;enemy=false;
        auto box=[&](Bounds bounds){if(Chrono::Sweep(from,delta,bounds,{},f,normal)&&f<nearest){nearest=f;enemy=false;}};
        for(const auto& solid:solids_)box(solid);
        if(!cityGates_.barricadeOpen)box({CityBarricadeCenter+V{-23,0,-6},CityBarricadeCenter+V{23,14,2}});
        if(!cityGates_.gateOpen)box({CityGateCenter+V{-32,0,-1.5f},CityGateCenter+V{32,58,1.5f}});
        if(delta.y<0){f=(.22f-from.y)/delta.y;if(f>=0&&f<nearest){nearest=f;enemy=false;}}
        if(enemies){float distance=Length(delta);V ray=Unit(delta);
            for(const auto& target:swarm_)if(SwarmAlive(target)&&RaySphere(from,ray,target.at,std::max(1.f,target.scale*1.15f),distance*nearest,f)){
                nearest=f/std::max(distance,1e-5f);enemy=true;
            }}
        return nearest;
    };
    float fraction=trace(camera,direction*HydrantAimRange,true,aim.enemy);
    aim.target=camera+direction*(HydrantAimRange*fraction);
    V origin=LifeHydrantNozzle(body);aim.distance=Length(aim.target-origin);
    aim.reachable=LifeHydrantLaunch(origin,aim.target,aim.velocity,aim.flight);
    aim.impact=LifeHydrantTrajectory(origin,aim.velocity,aim.flight);
    // A visible target may still be behind cover at the muzzle. Show the first
    // obstruction along the arc, using the same city geometry as water collisions.
    V previous=origin;
    int steps=std::max(1,int(std::ceil(aim.flight*120)));
    for(int i=1;i<=steps;++i){
        V next=LifeHydrantTrajectory(origin,aim.velocity,aim.flight*float(i)/float(steps));bool ignored;
        float hit=trace(previous,next-previous,false,ignored);
        if(hit<1){aim.impact=previous+(next-previous)*hit;aim.blocked=Length(aim.impact-aim.target)>1.5f;break;}
        previous=next;
    }
}
void ChronoSystem::UpdateHydrantWater(entt::registry& r,Player& player,GameContext& ctx,float dt,bool attack,bool guard){
    auto& body=life_.bodies[0];auto* renderer=ctx.renderer;
    bool ready=body.action==LifeAction::Hydrant&&body.weapon.ready&&!body.down&&body.health>0;
    life_.waterAttack=ready&&attack;life_.waterGuard=ready&&guard;
    if(life_.waterAttack||life_.waterGuard)renderer->EnableHydrantWaterReadback(true);
    // Enemies obstruct the simulated water, so the jet splashes off them
    // instead of passing through. Only the nearest fit the collider budget.
    std::vector<std::pair<float,Engine::Vector4>> obstacles;
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){float d=LifeDistance(enemy.at,body.at);
        if(d<HydrantAimRange*1.5f)obstacles.push_back({d,{enemy.at.x,enemy.at.y,enemy.at.z,std::max(1.f,enemy.scale*1.15f)}});}
    std::sort(obstacles.begin(),obstacles.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<Engine::Vector4> spheres;
    for(size_t i=0;i<std::min(obstacles.size(),Engine::Renderer::kFluidSphereCapacity);++i)spheres.push_back(obstacles[i].second);
    renderer->SetFluidSplashSpheres(spheres);
    V origin=LifeHydrantNozzle(body),right{body.forward.z,0,-body.forward.x};
    const Engine::Vector4 water{.16f,.72f,1,.96f};
    auto budget=[&](bool held,float& remainder,float rate){if(!held){remainder=0;return 0;}remainder+=rate*dt;int count=int(remainder);remainder-=count;return count;};
    int jet=budget(life_.waterAttack,life_.waterAttackBudget,4200);
    if(jet&&hydrantAim_.active)renderer->EmitGPUFluid(EV(origin),EV(hydrantAim_.velocity*.2f),water,jet,HydrantAttackWater,false,std::clamp(.55f*LifeScale(body.mass),.55f,1.1f));
    int curtain=budget(life_.waterGuard,life_.waterGuardBudget,2100);
    int row=life_.waterGuardRow;if(curtain)life_.waterGuardRow=(row+1)%3;
    // Seven resolved nozzles make a sheet across the front. Every droplet then
    // falls, interacts through PBF pressure/viscosity and collides with the city.
    for(int i=0;i<7&&curtain;++i){float lane=float(i-3)/3;
        int count=curtain/7+(i<curtain%7?1:0);if(!count)continue;
        V at=origin+right*(lane*2.5f*LifeScale(body.mass));
        at.y=body.at.y-1.25f+(.5f+float(row)*1.25f)*LifeScale(body.mass);
        V flow=Unit(body.forward+right*(lane*.25f)+V{0,.25f-float(row)*.2f,0})*18;
        renderer->EmitGPUFluid(EV(at),EV(flow*.2f),water,count,HydrantGuardWater);
    }
    const auto& snapshot=renderer->GetHydrantWaterSnapshot();
    life_.waterHitClock+=dt;
    if(snapshot.serial==hydrantWaterSerial_||life_.waterHitClock<.1f)return;
    hydrantWaterSerial_=snapshot.serial;float step=std::min(.15f,life_.waterHitClock);life_.waterHitClock=0;
    auto active=[](const Engine::Renderer::HydrantWaterSample& p){return LifeHydrantLiveWater(p.type,p.contactAge,{p.velocity.x,p.velocity.y,p.velocity.z});};
    for(auto& enemy:swarm_)if(SwarmAlive(enemy)){
        int hits=0,guards=0;V flow{};float radius=std::max(1.f,enemy.scale*1.15f);
        for(const auto& sample:snapshot.particles)if(active(sample)){
            V at{sample.at.x,sample.at.y,sample.at.z},velocity{sample.velocity.x,sample.velocity.y,sample.velocity.z};
            if(LifeHydrantWaterTouches(at,velocity,enemy.at,radius+.45f,step)){
                if(sample.type<1.15f)++hits;else ++guards;flow=flow+velocity;
            }}
        if(hits>=2){LifeHit(r,player,ctx,enemy.id,80*step*std::min(1.f,hits/8.f),LifeAction::Hydrant,body.at);++life_.waterHits;}
        if((hits>=2||guards>=2)&&SwarmAlive(enemy)){
            V push=flow;push.y=0;if(Length(push)>.01f){V before=enemy.at;enemy.at=CityConstrain(before,before+Unit(push)*(guards>=2?10.f:5.f)*step,std::max(1.f,enemy.scale),cityGates_);}
            enemy.stagger=std::max(enemy.stagger,.4f);if(enemy.phase==SwarmPhase::Dash||enemy.phase==SwarmPhase::Windup)ChangeSwarmPhase(enemy,SwarmPhase::Recover);
        }
    }
    // The same simulated water can break the exit after the knights are beaten.
    if(cityGateUnlocked_&&!cityGates_.gateOpen){int hits=0;
        for(const auto& sample:snapshot.particles)if(sample.type<1.15f&&active(sample)&&std::abs(sample.at.x-CityGateCenter.x)<32&&
            std::abs(sample.at.z-CityGateCenter.z)<4&&sample.at.y>.3f&&sample.at.y<58)++hits;
        if(hits>=2){cityGateHealth_-=120*step;cityGateShake_=.12f;if(cityGateHealth_<=0)BreakCityGate(r,false,false);}
    }
}
}
