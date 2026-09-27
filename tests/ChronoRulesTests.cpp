#include "../Game/Chrono/ChronoRules.h"
#include "../Game/Chrono/CreatureMotion.h"
#include "../Game/Chrono/InkRules.h"
#include <cassert>
#include <iostream>
#include <chrono>
using namespace Game::Chrono;
int main(){
    {
        InkSurface replay,incremental;replay.width=incremental.width=22;replay.depth=incremental.depth=28;
        std::array<float,32> outline;outline.fill(3.5f);
        auto stamp=[&](InkSurface& s,int i){s.Stamp({4.f+float(i%30)*.45f,0,4.f+float(i/30)*.6f},3.5f,&outline);};
        for(int i=0;i<300;++i)stamp(incremental,i);
        auto start=std::chrono::steady_clock::now();
        for(int tick=0;tick<12;++tick){replay.mask.fill(0);for(int i=0;i<301+tick;++i)stamp(replay,i);}
        double replayMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        start=std::chrono::steady_clock::now();for(int i=300;i<312;++i)stamp(incremental,i);
        double addMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        assert(replay.mask==incremental.mask);
        std::cout<<"PAINT CPU 12 updates on a small platform: replay "<<replayMs<<" ms, incremental "<<addMs<<" ms; identical mask\n";
    }
    {
    InkSurface floor;floor.origin={-20,0,-20};floor.width=40;floor.depth=40;
    float fraction;Vec point;
    assert(floor.Ray({0,10,0},{0,-20,0},fraction,point)&&std::abs(fraction-.5f)<.001f);
    assert(!floor.Painted(point));floor.Stamp(point,3.4f);assert(floor.Painted(point));
    assert(!floor.Painted({12,0,0})&&!floor.Painted({0,5,0}));
    auto original=floor.mask;floor.Stamp(point,3.4f);assert(original==floor.mask);
    floor.Stamp({39,0,39},3);assert(original==floor.mask); // Out-of-bounds shots never wrap.
    InkSurface ramp;ramp.v=Unit(Vec{0,18,62});ramp.width=22;ramp.depth=Length(Vec{0,18,62});
    assert(ramp.Ray({11,100,31},{0,-200,0},fraction,point)&&std::abs(point.y-9)<.001f);
    ramp.Stamp(point,3);assert(ramp.Painted(point));assert(!floor.Painted(point));
    InkSurface wall;wall.origin={21,0,39.98f};wall.u={1,0,0};wall.v={0,1,0};wall.width=22;wall.depth=18;
    assert(wall.Ray({32,9,30},{0,0,20},fraction,point));wall.Stamp(point,4.2f);assert(wall.Painted(point));
    Bounds foundation{{-80,-3.9f,-65},{80,-.1f,105}};float obstruct;Vec normal;
    assert(floor.Ray({0,1,0},{0,-2,0},fraction,point));
    assert(Sweep({0,1,0},{0,-2,0},foundation,{0,0,0},obstruct,normal));assert(fraction<obstruct);
    assert(InkMoveSpeed(true,true)==3*InkMoveSpeed(false,true));
    assert(InkMoveSpeed(true,false)==27.f);
    // Mass belongs to exactly one place. Charge is only a credit on body mass.
    for(float hz:{30.f,60.f,144.f}){
        float damageSeconds=0;for(float age=0;age<1.3f;age+=1/hz)damageSeconds+=SlimeActiveStep(age,1.2f,1/hz);
        assert(std::abs(damageSeconds-1.2f)<.0001f);
        float body=SlimeMaximumMass,trail=0;InkPlayer slime;
        for(int i=0;i<int(hz);++i)trail+=SlimeDeposit(body,SlimeMaximumMass/hz,1);
        assert(std::abs(body-SlimeMinimumMass)<.003f&&std::abs(body+trail-SlimeMaximumMass)<.003f);
        assert(SlimeDeposit(body,50,1)==0);
        slime.Collect(body,trail);trail=0;slime.limitEligible=true;
        assert(slime.Fire(body)&&slime.tier==3&&slime.limitBreak);
        assert(std::abs(body+slime.spent-SlimeMaximumMass)<.003f);
        body-=2;slime.phase=SlimePhase::Returning;
        for(int i=0;i<int(hz);++i)slime.Return(body,1/hz);
        assert(std::abs(body-(SlimeMaximumMass-2))<.01f&&!slime.Fire(body));
        float deposit=SlimeDeposit(body,20,1);slime.Collect(body,deposit);slime.Cancel();
        assert(std::abs(body-(SlimeMaximumMass-2))<.01f&&!slime.Fire(body));
    }
    assert(SlimeTier(0)==0&&SlimeTier(SlimeChargeCapacity*.1f)==1&&SlimeTier(SlimeChargeCapacity*.4f)==2&&SlimeTier(SlimeChargeCapacity*.8f)==3);
    assert(SlimeMono(-1)==0&&SlimeMono(.05f)==1&&SlimeMono(.2f)==0);
    {float reserve=SlimeMaximumMass;SlimeDeposit(reserve,27*8,1.05f);assert(reserve>SlimeMaximumMass*.7f);}
    for(float hz:{30.f,60.f,144.f})for(float direction:{-1.f,1.f}){
        float ground=9,y=ground+1.25f;
        for(int i=0;i<int(hz);++i){float travel=27/hz,nextGround=ground+direction*travel*18/62;
            assert(SlimeGrounded(y,y-30/(hz*hz),-30/hz,true,false,ground,nextGround,travel));
            y=nextGround+1.25f;ground=nextGround;}
        assert(!SlimeGrounded(y,y+.1f,12,true,true,ground,ground,0));
        assert(!SlimeGrounded(y,y-.01f,-.1f,true,false,ground,ground-10,.1f));
    }
    float stationary=100;assert(SlimeDeposit(stationary,0,1)==0&&stationary==100);
    for(Vec target:{Vec{0,0,45},Vec{20,18,35},Vec{0,-8,20}}){
        Vec velocity=InkLaunch(target);float time=std::sqrt(target.x*target.x+target.z*target.z)/std::sqrt(velocity.x*velocity.x+velocity.z*velocity.z);
        Vec impact=velocity*time+Vec{0,-.5f*InkGravity*time*time,0};
        assert(Length(impact-target)<.01f);assert(std::abs(Length(velocity)-InkShotSpeed)<.01f);
    }
    // Large rotated parts clear the full deck even when their centre is outside.
    assert(InkBossSupport({18,0,55},5)>=18);
    assert(InkBossSupport({32,0,55},10)>=18);
    assert(InkBossSupport({32,0,9},0)>=9);
    assert(InkBossSupport({-75,0,-50},2)==0);
    }
    static_assert(CreatureMorphSeconds==8.f);
    for(float step:{1.f/30,1.f/60,1.f/144})for(auto attack:{CreatureAttack::Sweep,CreatureAttack::Charge,CreatureAttack::Slam,CreatureAttack::Wing}){
        bool hit=false;for(float t=step;t<ActiveTime(attack)+step;t+=step)hit|=AttackTouches(attack,{0,1,0},t-step,t);
        assert(hit);
        for(float t=step;t<ActiveTime(attack)+step;t+=step){
            assert(!AttackTouches(attack,{40,1,40},t-step,t));
            assert(!AttackTouches(attack,{0,40,0},t-step,t));
        }
    }
    assert(AttackTouches(CreatureAttack::Slam,{0,18,0},.27f,.3f));
    assert(!AttackTouches(CreatureAttack::Sweep,{0,8,0},0,.6f));
    assert(!AttackTouches(CreatureAttack::Charge,{8,1,0},0,.6f));
    assert(!AttackTouches(CreatureAttack::Wing,{0,-8,0},0,.8f));
    // Check continuity across all departure, lattice-switch and arrival seams.
    for(float t=0;t<8;t+=.01f){auto a=EvaluateCreature(0,t/8),b=EvaluateCreature(0,(t+.0001f)/8);
        for(int i=0;i<a.count;++i)assert(Length(a.pieces[i].position-b.pieces[i].position)<.5f);}
    for(float age:{0.f,1.f,10.f,90.f})for(float form:{0.f,.25f,.5f,.75f,1.f}){
        auto pose=EvaluateCreature(age,form);assert(pose.count==224);
        for(int i=0;i<pose.count;++i){const auto& piece=pose.pieces[i];
            assert(std::isfinite(Length(piece.position))&&std::isfinite(Length(piece.rotation)));
            assert(piece.size.x>0&&piece.size.y>0&&piece.size.z>0);}
        auto again=EvaluateCreature(age,form);assert(Length(again.head-pose.head)==0);
    }
    auto snake=EvaluateCreature(0,0),bird=EvaluateCreature(0,1);
    assert(bird.head.y>145);
    auto pattern=EvaluateCreature(0,.5f);
    for(int i=0;i<pattern.count;++i){
        assert(Length(pattern.pieces[i].position-Lerp(snake.pieces[i].position,bird.pieces[i].position,.5f))>10);
        auto nearby=EvaluateCreature(0,.5001f);
        assert(Length(pattern.pieces[i].position-nearby.pieces[i].position)<3); // Fast lattice transfer, still continuous.
    }
    assert(bird.head.y>55&&snake.head.y>23);
    float wingLeft=0,wingRight=0,wingLow=1000,wingHigh=-1000;
    for(int i=104;i<218;++i){auto at=bird.pieces[i].position;
        wingLeft=std::min(wingLeft,at.x);wingRight=std::max(wingRight,at.x);
        wingLow=std::min(wingLow,at.y);wingHigh=std::max(wingHigh,at.y);}
    assert(wingRight-wingLeft>100&&wingHigh-wingLow>15);
    assert(Length(bird.pieces[107].position-bird.pieces[125].position)>1); // Overlapping layers have depth.
    assert(bird.head.y>snake.head.y+10&&bird.core.y>14);
    assert(std::abs(bird.pieces[217].position.x)>16); // Expanded right wing.
    assert(Length(snake.pieces[217].position-bird.pieces[217].position)>10);
    for(Vec start:{Vec{0,1,-17},Vec{-25,1,-25},Vec{25,1,25},Vec{105,1,-125}}){
        auto route=FeatherRoute(start,bird.core);Vec from{std::clamp(start.x,-108.f,108.f),3,std::clamp(start.z,-128.f,128.f)};
        for(Vec point:route){assert(Length(point-from)<Reach(1));from=point;}
        assert(Length(bird.core-from)<Reach(1));
    }
    Flow flow;flow.Hit(false);flow.Hit(false);
    assert(flow.remaining==0);flow.Hit(true);assert(flow.remaining>0&&flow.combo==3);
    flow.Tick(0.1f);assert(flow.scale<1&&flow.scale>=0.25f);
    for(int i=0;i<30;++i){flow.Hit(true);flow.Tick(0.05f);}
    assert(flow.remaining<=3&&flow.scale>=0.25f);
    for(int i=0;i<100;++i)flow.Tick(0.1f);
    assert(flow.combo==0&&flow.remaining==0&&flow.scale>0.99f);
    // A boss hit calls neither Hit nor Tick with enemy-scaled time.
    Flow bossOnly;for(int i=0;i<10;++i)bossOnly.Tick(0.1f);assert(bossOnly.combo==0);
    float focus=1;for(int i=0;i<60;++i)focus=AimTimeScale(focus,true,1.f/60);
    assert(focus>=.20f&&focus<.201f&&WorldTimeScale(1,focus)<.201f);
    assert(WorldTimeScale(.15f,focus)==.15f); // Never cancel a stronger existing slow.
    for(int i=0;i<60;++i)focus=AimTimeScale(focus,false,1.f/60);
    assert(focus>.99f&&WorldTimeScale(.35f,focus)==.35f);
    assert(AimTimeScale(focus,true,0)==focus);
    for(float dt:{1.f/30,1.f/60,1.f/144}){
        Vec at{},goal{};bool delayed=false;
        for(int i=0;i<120;++i){goal.x+=115*dt;at=Follow(at,goal,dt,2.2f);
            float lag=Length(goal-at);assert(lag<=2.201f);delayed=delayed||lag>.1f;}
        assert(delayed);for(int i=0;i<240;++i)at=Follow(at,goal,dt,2.2f);
        assert(Length(at-goal)<.001f);
        float remaining=24,seconds=0;while(remaining>.01f&&seconds<1){remaining-=std::min(remaining,PullSpeed(remaining)*dt);seconds+=dt;}
        assert(remaining<=.01f&&seconds<.65f);
    }
    assert(PullSpeed(20)>PullSpeed(2)&&PullSpeed(2)>PullSpeed(.2f));
    assert(Gravity(-15,false)>Gravity(15,false)&&Gravity(0,false)<Gravity(15,false));
    assert(LandingSquash(.05f,1)<.82f&&std::abs(LandingSquash(.5f,1)-1)<.001f);
    {
        InkSurface surface;surface.width=surface.depth=12;
        std::array<float,32> outline{};
        for(int k=0;k<32;++k){float a=k*6.2831853f/32-3.14159265f;
            outline[k]=1/std::sqrt(std::cos(a)*std::cos(a)/9+std::sin(a)*std::sin(a));}
        surface.Stamp({6,0,6},3,&outline);
        assert(surface.Painted({8.5f,0,6})&&!surface.Painted({6,0,8}));
        surface.mask.fill(0);surface.Stamp({6,0,6},3,&outline,.5f);
        assert(!surface.Painted({8.5f,0,6})&&surface.Painted({7,0,6}));
    }
    for(float health:{8.f,40.f,80.f,200.f,800.f}){
        InkPlayer attack;float body=health;attack.reserve=SlimeReserve(health);attack.capacity=SlimeCapacity(health);
        float deposit=SlimeDeposit(body,100000,1,attack.reserve);attack.Collect(body,deposit);
        assert(attack.Fire(body)&&attack.tier==3&&attack.Power(attack.beamCharge)>.99f);
        assert(body>=attack.reserve-.001f);attack.Return(body,1);assert(std::abs(body-health)<.001f);
    }
    {
        InkPlayer recovery;float body=1;recovery.Recover(body,3,1);assert(body==1);
        for(int i=0;i<600;++i)recovery.Recover(body,5,1.f/60);
        assert(std::abs(body-200)<.001f);body=0;recovery.Recover(body,10,1);assert(body==0);
        body=20;recovery.deployed=180;recovery.Recover(body,10,1);assert(body==20);
    }
    {
        InkPlayer reservoir;float body=SlimeMaximumMass;
        assert(reservoir.StoredRatio()==0);
        reservoir.deployed=SlimeDeposit(body,100000,1,reservoir.reserve);
        assert(std::abs(reservoir.StoredRatio()-1)<.001f);
        const float amount=reservoir.deployed*.5f;
        reservoir.deployed-=amount;reservoir.Collect(body,amount);
        assert(std::abs(reservoir.StoredRatio()-.5f)<.001f);
        const float remaining=reservoir.deployed;
        reservoir.deployed=0;reservoir.Collect(body,remaining);
        assert(reservoir.StoredRatio()==0 && reservoir.charge>0);
        assert(reservoir.Fire(body) && reservoir.StoredRatio()==0);
        reservoir.Return(body,1);assert(reservoir.StoredRatio()==0);
    }
    float f;Vec n;Bounds wall{{5,-10,-10},{5.1f,10,10}};
    assert(Sweep({0,0,0},{100,0,0},wall,{1,1,1},f,n));
    assert(std::abs(f-0.04f)<0.0001f&&n.x==-1);
    assert(!Sweep({0,12,0},{100,0,0},wall,{1,1,1},f,n));
    Bounds floor{{-10,-5,-10},{10,0,10}};
    assert(!Sweep({0,1,0},{0,2,0},floor,{1,1,1},f,n));
    assert(Sweep({0,2,0},{0,-100,0},floor,{1,1,1},f,n)&&n.y==1);
    assert(!RaySphere({0,0,0},{0,0,1},{0,0,-5},1,20,f));
    assert(RaySphere({0,0,0},{0,0,1},{0,0,5},1,20,f)&&f==4);
    assert(!RaySphere({0,0,0},{0,0,1},{4,0,5},1,20,f));
    assert(Reach(1)>=12&&Reach(200)>Reach(100));
    assert(std::abs(std::pow(BodyScale(200),3)-2)<0.0001f);
    assert(StrainCost(200,24)>StrainCost(100,12));
    float strain=101,mass=5;assert(Overload(strain,mass)&&mass==1&&strain==40);
    Stats stats;stats.counters=3;stats.seconds=120;float score=stats.Score(12);
    stats.projectiles=10000;assert(stats.Score(12)==score);
    std::cout<<"PASS: kill-gated slow, real-time expiry, minimum reach, overload recovery, swept walls/floor, manual rays, bounded score\n";
}
