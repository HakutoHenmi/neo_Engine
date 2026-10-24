// SlimeHydrant.cpp
// 消火栓に変形したスライムの水 (ChronoSystem の一部)。
// 画面中央の狙いの計算と、攻撃の水流・防御の水の幕の発射、水が当たった敵や門への効果。
#include "ChronoSystemInternal.h"

namespace Game {
// 消火栓の狙い: カメラの中央の先にある最初の物 (壁・床・敵) を目標にし、放物線で届くか・途中で遮られるかを求める
void ChronoSystem::UpdateHydrantAim(GameContext& ctx){
    // 消火栓の形でなければ狙わない
    hydrantAim_={};const auto& body=life_.bodies[0];
    if(body.action!=LifeAction::Hydrant||life_.lab||body.down)return;
    auto& aim=hydrantAim_;aim.active=true;
    // カメラの位置と前方向
    auto inverse=DirectX::XMMatrixInverse(nullptr,ctx.camera->View());
    DirectX::XMFLOAT3 eye{},axis{};
    DirectX::XMStoreFloat3(&eye,DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(),inverse));
    DirectX::XMStoreFloat3(&axis,DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0,0,1,0),inverse));
    V camera=Read(eye),direction=Unit(Read(axis));
    // 見えている一番近い目標を求める。壁越しの敵は選ばない
    auto trace=[&](V from,V delta,bool enemies,bool& enemy){
        float nearest=1,f;V normal;enemy=false;
        auto box=[&](Bounds bounds){if(Chrono::Sweep(from,delta,bounds,{},f,normal)&&f<nearest){nearest=f;enemy=false;}};
        for(const auto& solid:solids_)box(solid);
        if(!cityGates_.barricadeOpen)box({CityBarricadeCenter()+V{-23,0,-6},CityBarricadeCenter()+V{23,14,2}});
        if(!cityGates_.gateOpen)box({CityGateCenter()+V{-32,0,-1.5f},CityGateCenter()+V{32,58,1.5f}});
        if(delta.y<0){f=(.22f-from.y)/delta.y;if(f>=0&&f<nearest){nearest=f;enemy=false;}}
        if(enemies){float distance=Length(delta);V ray=Unit(delta);
            for(const auto& target:swarm_)if(SwarmAlive(target)&&RaySphere(from,ray,target.at,std::max(1.f,target.scale*1.15f),distance*nearest,f)){
                nearest=f/std::max(distance,1e-5f);enemy=true;
            }}
        return nearest;
    };
    // 狙いの先と、ノズルから届く放物線
    float fraction=trace(camera,direction*HydrantAimRange,true,aim.enemy);
    aim.target=camera+direction*(HydrantAimRange*fraction);
    V origin=LifeHydrantNozzle(body);aim.distance=Length(aim.target-origin);
    aim.reachable=LifeHydrantLaunch(origin,aim.target,aim.velocity,aim.flight);
    aim.impact=LifeHydrantTrajectory(origin,aim.velocity,aim.flight);
    // 見えている目標でも、ノズルからは遮蔽物の向こうかもしれない。水の当たりと同じ街の形状を使って、
    // 放物線に沿って最初に当たる物を表示する
    V previous=origin;
    int steps=std::max(1,int(std::ceil(aim.flight*120)));
    for(int i=1;i<=steps;++i){
        V next=LifeHydrantTrajectory(origin,aim.velocity,aim.flight*float(i)/float(steps));bool ignored;
        float hit=trace(previous,next-previous,false,ignored);
        if(hit<1){aim.impact=previous+(next-previous)*hit;aim.blocked=Length(aim.impact-aim.target)>1.5f;break;}
        previous=next;
    }
}
// 消火栓の水: 左クリックで水流、右クリックで水の幕を GPU 流体として発射し、水が当たった敵を押して傷つける
void ChronoSystem::UpdateHydrantWater(entt::registry& r,Player& player,GameContext& ctx,float dt,bool attack,bool guard){
    auto& body=life_.bodies[0];auto* renderer=ctx.renderer;
    bool ready=body.action==LifeAction::Hydrant&&body.weapon.ready&&!body.down&&body.health>0;
    life_.waterAttack=ready&&attack;life_.waterGuard=ready&&guard;
    if(life_.waterAttack||life_.waterGuard)renderer->EnableHydrantWaterReadback(true);
    // 敵はシミュレーションの水をさえぎるので、水流は敵を通り抜けずに跳ね返る。
    // コライダーの上限に収まるよう、近い敵だけを使う
    std::vector<std::pair<float,Engine::Vector4>> obstacles;
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){float d=LifeDistance(enemy.at,body.at);
        if(d<HydrantAimRange*1.5f)obstacles.push_back({d,{enemy.at.x,enemy.at.y,enemy.at.z,std::max(1.f,enemy.scale*1.15f)}});}
    std::sort(obstacles.begin(),obstacles.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<Engine::Vector4> spheres;
    for(size_t i=0;i<std::min(obstacles.size(),Engine::Renderer::kFluidSphereCapacity);++i)spheres.push_back(obstacles[i].second);
    renderer->SetFluidSplashSpheres(spheres);
    // ノズルの位置・右方向・水の色、押している間の時間で発射する粒子の数を決める
    V origin=LifeHydrantNozzle(body),right{body.forward.z,0,-body.forward.x};
    const Engine::Vector4 water{.16f,.72f,1,.96f};
    auto budget=[&](bool held,float& remainder,float rate){if(!held){remainder=0;return 0;}remainder+=rate*dt;int count=int(remainder);remainder-=count;return count;};
    // 攻撃の水流 (1 秒に 4200 粒)
    int jet=budget(life_.waterAttack,life_.waterAttackBudget,4200);
    if(jet&&hydrantAim_.active)renderer->EmitGPUFluid(EV(origin),EV(hydrantAim_.velocity*.2f),water,jet,HydrantAttackWater,false,std::clamp(.55f*LifeScale(body.mass),.55f,1.1f));
    // 防御の水の幕 (1 秒に 2100 粒、3 段を順に出す)
    int curtain=budget(life_.waterGuard,life_.waterGuardBudget,2100);
    int row=life_.waterGuardRow;if(curtain)life_.waterGuardRow=(row+1)%3;
    // 7 つのノズルで前面に水の幕を作る。水滴は落ちながら PBF の圧力と粘性で互いに影響し、
    // 街と衝突する
    for(int i=0;i<7&&curtain;++i){float lane=float(i-3)/3;
        int count=curtain/7+(i<curtain%7?1:0);if(!count)continue;
        V at=origin+right*(lane*2.5f*LifeScale(body.mass));
        at.y=body.at.y-1.25f+(.5f+float(row)*1.25f)*LifeScale(body.mass);
        V flow=Unit(body.forward+right*(lane*.25f)+V{0,.25f-float(row)*.2f,0})*18;
        renderer->EmitGPUFluid(EV(at),EV(flow*.2f),water,count,HydrantGuardWater);
    }
    // GPU から読み戻した水の粒子で、敵や門への当たりを 0.1 秒ごとに調べる
    const auto& snapshot=renderer->GetHydrantWaterSnapshot();
    life_.waterHitClock+=dt;
    if(snapshot.serial==hydrantWaterSerial_||life_.waterHitClock<.1f)return;
    hydrantWaterSerial_=snapshot.serial;float step=std::min(.15f,life_.waterHitClock);life_.waterHitClock=0;
    auto active=[](const Engine::Renderer::HydrantWaterSample& p){return LifeHydrantLiveWater(p.type,p.contactAge,{p.velocity.x,p.velocity.y,p.velocity.z});};
    // 各敵: 攻撃の水が 2 粒以上当たればダメージ、水が当たれば押し流してよろけさせる
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
    // 騎士を倒した後は、同じシミュレーションの水で出口の門を壊せる
    if(cityGateUnlocked_&&!cityGates_.gateOpen){int hits=0;
        for(const auto& sample:snapshot.particles)if(sample.type<1.15f&&active(sample)&&std::abs(sample.at.x-CityGateCenter().x)<32&&
            std::abs(sample.at.z-CityGateCenter().z)<4&&sample.at.y>.3f&&sample.at.y<58)++hits;
        if(hits>=2){cityGateHealth_-=120*step;cityGateShake_=.12f;if(cityGateHealth_<=0)BreakCityGate(r,false,false);}
    }
}
}
