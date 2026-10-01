// Included inside namespace Game by SlimeAssault.inl.
void ChronoSystem::HitDomainMissile(entt::registry& r,Player& p,GameContext& ctx,V at,bool enclosed,float power){
    auto& ink=r.get<InkPlayer>(player_);auto& target=r.get<Target>(weakpoint_);auto& boss=r.get<Boss>(boss_);
    if(boss.countered||finished_)return;
    // Retain the existing armor/exposed-core progression and phase victories.
    float damage=power*(ink.rogueBossPool?1.25f:1.f)*(enclosed?1.4f:1.f)*(ink.hordeEnabled?4.f:1.f);
    if(!target.active){
        float needed=100-ink.armor;ink.armor+=damage;damage=std::max(0.f,damage-needed);
        if(ink.armor>=100){ink.armor=0;ink.downTimer=3.5f;target.active=true;
            auto& c=r.get<CreatureBoss>(boss_);c.sweepPhase=3;c.sweepTime=0;c.recoil=.6f;}
    }
    if(target.active&&damage>0){
        ink.coreHealth-=damage;++ink.coreHits;ink.hitFlash=.1f;
        if(ink.coreHealth<=0){
            ++p.stats.counters;ink.coreHealth=100;ink.armor=0;boss.countered=true;target.active=false;ink.downTimer=0;
            p.mass=SlimeMaximumMass;r.get<CreatureBoss>(boss_).recoil=.6f;Feedback(p,at,true);
            if(p.stats.counters>=3)Finish(p,true);
        }
    }
    if(ink.shotClock<=0){
        ctx.renderer->EmitGPUFluid(EV(at),{0,2,0},{SlimeRed,SlimeGreen,SlimeBlue,1},10,4.035f);
        auto* audio=Engine::Audio::GetInstance();audio->Play(slimeAcidSound_,false,.1f*audio->GetMasterSEVolume(),1.f);ink.shotClock=.15f;
    }
}
void ChronoSystem::UpdateDomains(entt::registry& r,Player& p,GameContext& ctx){
    auto& ink=r.get<InkPlayer>(player_);float dt=std::min(ctx.dt,.05f);
    for(auto& enemy:swarm_)enemy.reserved=0;
    for(const auto& missile:domainMissiles_)for(auto& enemy:swarm_)if(enemy.id==missile.target&&SwarmAlive(enemy)){enemy.reserved+=missile.power*4;break;}
    for(auto& domain:homingDomains_){
        domain.liquid.Step(dt);
        domain.age+=dt;domain.clock=std::min(.1f,domain.clock+dt);
        if(domain.remaining==0)domain.afterglow+=dt;
        // Rate-limit launch and bound active missiles; queued ammunition is never discarded.
        bool spear=domain.loop.shape==DomainShape::Triangle;
        float interval=domain.loop.shape==DomainShape::Square?1.f/32:domain.loop.shape==DomainShape::Infinity?1.f/190:1.f/160;
        if(ink.rogue.Synergy(RogueTag::Rapid))interval/=1.3f;
        while(domain.remaining>0&&domain.clock>=interval&&(!spear||domain.age>=.65f)&&domainMissiles_.size()<1024){
            domain.clock-=interval;
            size_t count=domain.loop.points.size()-1;
            size_t index=size_t(std::fmod(domain.launched*.618034f,1.f)*float(count));
            DomainMissile missile;missile.at=domain.loop.points[index]+V{0,.3f,0};
            missile.power=domain.bulletPower;missile.meteor=domain.meteor;if(domain.meteor)missile.power*=float(domain.total);
            missile.shape=domain.loop.shape;missile.lane=domain.launched%2==0?1:-1;
            if(spear||domain.loop.shape==DomainShape::Square)missile.at=domain.loop.corners[size_t(domain.launched)%(spear?3:4)]+V{0,1.5f,0};
            if(spear)missile.power=float(domain.total)/3*domain.bulletPower;
            missile.seed=domain.launched*2.399963f+domain.age;missile.enclosed=domain.enclosesBoss;
            missile.target=ChooseSwarmTarget(r,missile.at,domain.launched%5==0);
            for(auto& enemy:swarm_)if(enemy.id==missile.target){enemy.reserved+=missile.power*4;break;}
            missile.velocity={std::cos(missile.seed)*8,55,std::sin(missile.seed)*8};
            missile.tail.fill(missile.at);domainMissiles_.push_back(missile);
            ++domain.launched;--domain.remaining;
        }
    }
    homingDomains_.erase(std::remove_if(homingDomains_.begin(),homingDomains_.end(),[](const HomingDomain& d){return d.remaining==0&&d.afterglow>d.lifetime;}),homingDomains_.end());
    for(auto& missile:domainMissiles_){
        missile.age+=dt;V before=missile.at;
        auto& state=ink.rogue;
        if(state.ranks[11]&&state.interceptClock<=0&&missile.projectile==entt::null){V player=Read(r.get<TransformComponent>(player_).translate);
            for(auto entity:r.view<SlimeFeather,TransformComponent>())if(Length(Read(r.get<TransformComponent>(entity).translate)-player)<18){
                bool reserved=false;for(const auto& other:domainMissiles_)if(other.projectile==entity)reserved=true;
                if(!reserved){missile.projectile=entity;state.interceptClock=1.f/(4*state.ranks[11]);break;}}}
        if(missile.projectile!=entt::null){
            if(!r.valid(missile.projectile)||!r.all_of<SlimeFeather,TransformComponent>(missile.projectile))missile.projectile=entt::null;
            else{V at=Read(r.get<TransformComponent>(missile.projectile).translate);missile.velocity=Unit(at-before)*100;V delta=missile.velocity*dt;float hit;
                if(Length(at-before)<2||RaySphere(before,Unit(delta),at,2,Length(delta),hit)){r.destroy(missile.projectile);missile.age=12;if(swarmEffects_.size()<128)swarmEffects_.push_back({at,0,3,2});}
                else missile.at=before+delta;
                for(int i=7;i>0;--i)missile.tail[i]=missile.tail[i-1];missile.tail[0]=before;missile.tailCount=std::min(8,missile.tailCount+1);continue;}}
        auto mob=std::find_if(swarm_.begin(),swarm_.end(),[&](const SwarmEnemy& enemy){return enemy.id==missile.target&&SwarmAlive(enemy);});
        bool bossAvailable=ink.battlePhase==BattlePhase::Boss&&!r.get<Boss>(boss_).countered;
        if((missile.target==0&&!bossAvailable)||(missile.target!=0&&mob==swarm_.end())){
            missile.target=ChooseSwarmTarget(r,before,false);
            mob=std::find_if(swarm_.begin(),swarm_.end(),[&](const SwarmEnemy& enemy){return enemy.id==missile.target&&SwarmAlive(enemy);});
            if(mob!=swarm_.end())mob->reserved+=missile.power*4;
        }
        bool hasTarget=(missile.target==0&&bossAvailable)||mob!=swarm_.end();
        V target=missile.target==0?Center(r,weakpoint_):mob!=swarm_.end()?mob->at:before+V{0,10,0};
        V aim=target;
        if(missile.shape==DomainShape::Infinity&&missile.age>.5f&&missile.age<4){
            float radius=std::min(25.f,Length(target-before)*.25f),angle=missile.age*5*missile.lane;
            aim=target+V{std::cos(angle)*radius,12,std::sin(angle)*radius};
        }
        missile.velocity=DomainMissileVelocity(before,missile.velocity,aim,missile.age,missile.seed,dt);
        if(missile.shape==DomainShape::Triangle&&missile.age>.9f)missile.velocity=Lerp(missile.velocity,Unit(target-before)*180,1-std::exp(-10*dt));
        V delta=missile.velocity*dt;float hit;
        float radius=missile.target==0?4.5f:2.4f;
        if(hasTarget&&missile.age>.9f&&(Length(target-before)<radius||RaySphere(before,Unit(delta),target,radius,Length(delta),hit))){
            if(missile.target==0){ink.rogueBossCorrosion=ink.rogue.ranks[8]?4.f:0;ink.rogueBossBurn=ink.rogue.ranks[10]?3.f:0;ink.rogueBossSlow=ink.rogue.ranks[9]?2.f:0;HitDomainMissile(r,p,ctx,target,missile.enclosed,missile.power);}
            else{
                if(mob!=swarm_.end()){if(ink.rogue.ranks[8])mob->corrosion=4;if(ink.rogue.ranks[9])mob->slow=2;if(ink.rogue.ranks[10])mob->burn=3;}
                HitSwarm(r,p,ctx,missile.target,missile.power*4);
                float splash=missile.meteor?18.f:missile.shape==DomainShape::Triangle?10.f:3.5f;
                for(auto& enemy:swarm_)if(SwarmAlive(enemy)&&Length(enemy.at-target)<splash)HitSwarm(r,p,ctx,enemy.id,missile.power*2);
            }
            if(missile.meteor&&swarmEffects_.size()<128)swarmEffects_.push_back({target,0,18,1});
            missile.age=12;
        }else missile.at=before+delta;
        missile.tailClock+=dt;
        if(missile.tailClock>=.035f){
            missile.tailClock=std::fmod(missile.tailClock,.035f);
            for(int i=7;i>0;--i)missile.tail[i]=missile.tail[i-1];
            missile.tail[0]=before;missile.tailCount=std::min(8,missile.tailCount+1);
        }
    }
    domainMissiles_.erase(std::remove_if(domainMissiles_.begin(),domainMissiles_.end(),[](const DomainMissile& m){return m.age>=12;}),domainMissiles_.end());
    ink.domainQueued=0;for(const auto& domain:homingDomains_)ink.domainQueued+=domain.remaining;
    ink.domainFlying=int(domainMissiles_.size());
}
void ChronoSystem::DrawSlimeBeam(entt::registry& r,GameContext& ctx){
    if(finished_){ctx.renderer->SetGuidedLiquidTrail({});return;}
    const auto& ink=r.get<InkPlayer>(player_);
    auto tube=[&](V from,V to,float radius,Engine::Vector4 color){
        V delta=to-from;float length=Length(delta);if(length<.001f)return;
        V axis=delta*(1/length);Engine::Transform t;t.translate=EV(from);
        t.rotate={-std::asin(std::clamp(axis.y,-1.f,1.f)),std::atan2(axis.x,axis.z),0};
        t.scale={radius,radius,length};ctx.renderer->DrawMeshInstanced(slimeBeamMesh_,white_,t,color,"DomainGlow");
    };
    auto line=[&](const std::vector<V>& points,float radius,Engine::Vector4 color){
        for(size_t i=1;i<points.size();++i)tube(points[i-1]+V{0,.95f,0},points[i]+V{0,.95f,0},radius,color);
    };
    auto domainColor=[](DomainShape shape){return shape==DomainShape::Triangle?Engine::Vector4{1.f,.65f,.20f,1}:shape==DomainShape::Square?Engine::Vector4{.24f,.72f,1.f,1}:shape==DomainShape::Infinity?Engine::Vector4{.75f,.38f,1.f,1}:Engine::Vector4{.15f,1.f,.72f,1};};
    auto fill=[&](const std::vector<std::array<V,3>>& triangles,Engine::Vector4 color,float fade){
        color.w=.27f*fade;
        for(const auto& triangle:triangles){
            V a=triangle[0],b=triangle[1]-a,c=triangle[2]-a;
            Engine::Matrix4x4 world{};
            world.m[0][0]=b.x;world.m[0][1]=b.y;world.m[0][2]=b.z;
            world.m[1][1]=1;
            world.m[2][0]=c.x;world.m[2][1]=c.y;world.m[2][2]=c.z;
            world.m[3][0]=a.x;world.m[3][1]=a.y+.08f;world.m[3][2]=a.z;world.m[3][3]=1;
            ctx.renderer->DrawMeshInstanced(domainFillMesh_,white_,world,color,"DomainFill");
        }
    };
    std::vector<Engine::Renderer::GPUFluidParticle> particles;std::set<std::array<int,3>> occupied;
    float totalLength=ink.domainPath.length+ink.domainPath.candidate.length;for(const auto& d:homingDomains_)totalLength+=d.loop.length;
    float spacing=std::max(.4f,totalLength/15000.f);
    auto liquid=[&](const Chrono::LiquidTrail& fluid,float fade){
        for(size_t i=1;i<fluid.samples.size();++i){V from=fluid.Position(i-1),to=fluid.Position(i);float length=Length(to-from);int count=std::max(1,int(std::ceil(length/spacing)));
            for(int j=0;j<count&&particles.size()<Engine::Renderer::kTrailFluidCapacity;++j){float f=float(j)/count;V point=Lerp(from,to,f);
                std::array<int,3> key{int(std::round(point.x*2.5f)),int(std::round(point.y*2.5f)),int(std::round(point.z*2.5f))};if(!occupied.insert(key).second)continue;
                Engine::Renderer::GPUFluidParticle particle{};particle.position=EV(point+V{0,.27f,0});particle.color={SlimeRed,SlimeGreen,SlimeBlue,fade};particle.type=8;
                particle.pad2={.72f,.52f,0};particles.push_back(particle);}
        }
    };
    liquid(trailLiquid_,1);
    if(ink.domainPath.candidate.Ready()){
        liquid(candidateLiquid_,1);
        const auto& candidate=ink.domainPath.candidate;
        bool same=highlightedCandidate_.size()==candidate.points.size();
        if(same)for(size_t i=0;i<candidate.points.size();++i)if(!Chrono::LiquidTrail::DomainSame(highlightedCandidate_[i],candidate.points[i])){same=false;break;}
        if(!same){highlightedCandidate_=candidate.points;candidateHighlight_=DomainHighlight(candidate);}
        auto color=domainColor(DomainShape::Loop);fill(candidateHighlight_,color,.7f);
        color.w=.75f;line(candidate.points,.075f,color);
    }
    for(const auto& domain:homingDomains_){
        float fade=std::max(0.f,1-domain.afterglow/domain.lifetime);
        liquid(domain.liquid,fade);
        auto color=domainColor(domain.loop.shape);fill(domain.highlight,color,fade);
        color.w=fade*.9f;line(domain.loop.points,.10f,color);
        if(domain.loop.shape==DomainShape::Square||domain.loop.shape==DomainShape::Triangle){
            int count=domain.loop.shape==DomainShape::Square?4:3;
            for(int i=0;i<count;++i){V at=domain.loop.corners[i]+V{0,1.2f,0};Engine::Transform t;t.translate=EV(at);t.scale={.7f,1.2f,.7f};
                ctx.renderer->DrawMeshInstanced(sphere_,white_,t,count==4?Engine::Vector4{.3f,.75f,1.f,fade}:Engine::Vector4{1.f,.65f,.15f,fade},"DomainGlow");
                tube(at,at+V{0,3,0},.2f,{.7f,.9f,1.f,fade*.7f});}
        }
    }
    for(const auto& familiar:familiars_){Engine::Transform t;t.translate=EV(familiar.at);t.scale={1.1f,.65f,1.1f};ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{.2f,1,.7f,.85f},"DomainGlow");
        for(float sign:{-1.f,1.f}){Engine::Transform eye;eye.translate=EV(familiar.at+V{sign*.3f,.25f,-.8f});eye.scale={.12f,.2f,.12f};ctx.renderer->DrawMeshInstanced(sphere_,white_,eye,{.9f,1,.45f,1},"DomainGlow");}}
    if(ink.rogue.resonance>0)for(size_t i=1;i<familiars_.size();++i)tube(familiars_[i-1].at,familiars_[i].at,.18f,{.3f,1,.85f,.8f});
    if(ink.rogue.orbit>0){V center=Read(r.get<TransformComponent>(player_).translate);for(int i=0;i<8;++i){float a=ink.phaseAge*4+float(i)*6.283185f/8;Engine::Transform t;t.translate=EV(center+V{std::cos(a)*6,1,std::sin(a)*6});t.scale={.3f,.3f,.6f};ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{.35f,.75f,1,.75f},"DomainGlow");}}
    ctx.renderer->SetGuidedLiquidTrail(std::move(particles));
    V player=Read(r.get<TransformComponent>(player_).translate);
    for(const auto& missile:domainMissiles_){
        float opacity=std::clamp(Length(missile.at-player)/9.f,.15f,1.f);
        float size=missile.shape==DomainShape::Triangle?1.4f:1.f;
        if(missile.shape==DomainShape::Loop)size*=std::min(4.f,std::sqrt(missile.power));
        Engine::Vector4 color=missile.shape==DomainShape::Triangle?Engine::Vector4{1,.75f,.2f,opacity}:missile.shape==DomainShape::Square?Engine::Vector4{.4f,.8f,1,opacity}:missile.shape==DomainShape::Infinity?Engine::Vector4{.85f,.45f,1,opacity}:Engine::Vector4{.8f,1,.4f,opacity};
        Engine::Transform t;t.translate=EV(missile.at);t.scale={.16f*size,.16f*size,.55f*size};
        if(missile.meteor)t.scale={3.5f,3.5f,3.5f};
        if(missile.shape==DomainShape::Triangle)t.scale={.65f,.65f,3.5f};
        V axis=Unit(missile.velocity);t.rotate={-std::asin(std::clamp(axis.y,-1.f,1.f)),std::atan2(axis.x,axis.z),0};
        ctx.renderer->DrawMeshInstanced(sphere_,white_,t,color,"DomainGlow");
        V from=missile.at;
        for(int i=0;i<missile.tailCount;++i){Engine::Vector4 tint=color;tint.w*= (1-float(i)/8)*.55f;
            tube(from,missile.tail[i],(missile.shape==DomainShape::Triangle?.3f:.08f*size)*(1-float(i)/9),tint);from=missile.tail[i];}
    }
}
