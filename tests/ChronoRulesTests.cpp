#include "../Game/Chrono/ChronoRules.h"
#include <cassert>
#include <iostream>
using namespace Game::Chrono;
int main(){
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
