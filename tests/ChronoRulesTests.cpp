#include "../Game/Chrono/ChronoRules.h"
#include "../Game/Chrono/CreatureMotion.h"
#include "../Game/Chrono/InkRules.h"
#include "../Engine/StaticLod.h"
#include "../Game/Chrono/LiquidTrail.h"
#include "../Game/Chrono/DomainHighlight.h"
#include "../Game/Chrono/SwarmRules.h"
#include <cassert>
#include <iostream>
#include <chrono>
using namespace Game::Chrono;
int main(){
    {
        Vec camera{0,0,0},hero{0,0,20};
        assert(SwarmCameraOpacity(camera,hero,{0,0,0},true)==0);
        assert(SwarmCameraOpacity(camera,hero,{0,1,10},true)<.04f);
        assert(SwarmCameraOpacity(camera,hero,{20,0,10},true)==1);
        assert(SwarmCameraOpacity(camera,hero,{0,0,40},true)==1);
        assert(SwarmCameraOpacity(camera,hero,{0,0,-20},true)==1);
        assert(SwarmCameraOpacity(camera,hero,{5,0,0},false)<SwarmCameraOpacity(camera,hero,{10,0,0},false));
        assert(SwarmCameraOpacity(camera,hero,{9,0,0},true)<SwarmCameraOpacity(camera,hero,{9,0,0},false));
        std::cout<<"PASS: near-camera enemy transparency, player sightline, wing bounds, distant/behind-player visibility\n";
    }
    {
        SwarmEnemy striker;striker.phase=SwarmPhase::Chase;
        TickSwarm(striker,{0,0,5},.05f);assert(striker.phase==SwarmPhase::Windup);
        Vec locked=striker.heading,start=striker.at;
        for(int i=0;i<10;++i)TickSwarm(striker,{20,0,-20},.05f);
        assert(striker.phase==SwarmPhase::Windup&&Length(striker.at-start)<.001f&&Length(striker.heading-locked)<.001f);
        for(int i=0;i<40&&striker.phase==SwarmPhase::Windup;++i)TickSwarm(striker,{20,0,-20},.05f);
        assert(striker.phase==SwarmPhase::Dash);TickSwarm(striker,{20,0,-20},.05f);assert(striker.at.z>start.z);
        for(int i=0;i<160&&striker.phase!=SwarmPhase::Chase;++i)TickSwarm(striker,{20,0,-20},.05f,0,false);
        assert(striker.phase==SwarmPhase::Chase);
        SwarmEnemy bomber;bomber.bomber=true;bomber.flying=true;bomber.phase=SwarmPhase::Chase;
        assert(TickSwarm(bomber,{0,0,5},.05f)==SwarmEvent::None&&bomber.phase==SwarmPhase::Windup);
        int detonations=0;for(int i=0;i<100;++i)if(TickSwarm(bomber,{0,0,5},.05f)==SwarmEvent::Detonate)++detonations;
        assert(detonations==1&&!SwarmAlive(bomber));
        Roguelite rogue;rogue.Gain(19);assert(rogue.level==1&&rogue.pending==0);rogue.Gain(32);assert(rogue.level==3&&rogue.pending==2&&rogue.xp==1);
        rogue.Open();assert(rogue.offers[0]!=rogue.offers[1]&&rogue.offers[1]!=rogue.offers[2]&&rogue.offers[0]!=rogue.offers[2]);
        for(int i=0;i<3;++i)assert(rogue.Reroll());assert(!rogue.Reroll());int upgrade=rogue.offers[0];assert(rogue.Choose(0)&&rogue.ranks[size_t(upgrade)]==1&&rogue.pending==1&&!rogue.menu);
        rogue.ranks.fill(0);rogue.ranks[0]=3;assert(rogue.Synergy(RogueTag::Rapid));rogue.ranks[17]=2;assert(rogue.Move()==.8f&&rogue.Power(4000)>1);
        for(size_t i=0;i<rogue.ranks.size();++i)rogue.ranks[i]=RogueCards[i].maxRank;rogue.Open();assert(rogue.offers[0]==-1&&!rogue.Choose(0));rogue.Close();
        assert(!SwarmBossReady(80)&&!SwarmBossReady(999)&&SwarmBossReady(1000)&&SwarmCapacity==180);
        int groundCount=0,airCount=0,bomberCount=0;
        for(uint32_t id=1;id<=100;++id){SwarmEnemy enemy;enemy.id=id;SwarmRole(enemy);
            groundCount+=!enemy.flying;airCount+=enemy.flying;bomberCount+=enemy.bomber;assert(!enemy.bomber||enemy.flying);}
        assert(groundCount==10&&airCount==90&&bomberCount==30);
        SwarmEnemy flyer;flyer.flying=true;flyer.at={0,20,12};flyer.phase=SwarmPhase::Chase;
        TickSwarm(flyer,{0,1,0},.05f);assert(flyer.phase==SwarmPhase::Windup&&flyer.heading.y<0&&flyer.at.y>10);
        Vec diveStart=flyer.at;for(int i=0;i<100&&flyer.phase!=SwarmPhase::Recover;++i)TickSwarm(flyer,{40,1,40},.05f);
        assert(flyer.at.y<diveStart.y&&flyer.at.y>=1.2f);
        float diveHeight=flyer.at.y;for(int i=0;i<10;++i)TickSwarm(flyer,{40,1,40},.05f);assert(flyer.at.y>diveHeight);
        SwarmEnemy highBomber;highBomber.flying=true;highBomber.bomber=true;highBomber.at={0,20,0};highBomber.phase=SwarmPhase::Windup;
        for(int i=0;i<60;++i)TickSwarm(highBomber,{0,1,0},.05f);assert(highBomber.at.y<4);
        for(Vec hero:std::vector<Vec>{{0,1,0},{320,1,370},{-320,1,-330},{320,1,-330},{-320,1,370}})
            for(uint32_t id=1;id<=500;++id){Vec at=SwarmSpawnPosition(hero,id);Vec delta=at-hero;delta.y=0;
                assert(Length(delta)>=84.99f&&Length(delta)<=120.01f&&at.x>=-300&&at.x<=300&&at.z>=-310&&at.z<=350);}
        SwarmEnemy waiting;waiting.id=7;waiting.flying=true;waiting.at={0,18,25};waiting.phase=SwarmPhase::Chase;
        for(int i=0;i<200;++i)TickSwarm(waiting,{0,1,0},.05f,0,false);
        assert(waiting.phase==SwarmPhase::Chase&&Length(waiting.at)>20);
        assert(SwarmWindup(waiting)>1.3f&&SwarmDashSpeed(waiting)<=32);
        assert(SwarmVariation(7)!=SwarmVariation(8)&&SwarmVariation(7,1)!=SwarmVariation(7,2));
        std::cout<<"PASS: swarm pursuit, committed telegraphed strike, recovery, one-shot delayed self-destruction, boss kill threshold\n";
    }
    {
        auto check=[](std::vector<Vec> points,float expected){
            DomainLoop loop;loop.points=std::move(points);loop.length=100;loop.area=expected;
            auto triangles=DomainHighlight(loop);float area=0;
            for(const auto& t:triangles){area+=std::abs(DomainCross(t[1]-t[0],t[2]-t[0]))*.5f;
                assert(DomainContains(loop,(t[0]+t[1]+t[2])*(1.f/3)));}
            assert(std::abs(area-expected)<.001f);
        };
        check({{0,0,0},{10,0,0},{10,0,10},{0,0,10},{0,0,0}},100);
        check({{0,0,0},{10,0,0},{10,0,4},{4,0,4},{4,0,10},{0,0,10},{0,0,0}},64);
        check({{0,0,0},{10,0,10},{0,0,10},{10,0,0},{0,0,0}},50);
        std::cout<<"PASS: enclosed-area highlight, concave boundary and crossing lobes\n";
    }
    {
        std::vector<Vec> spine;for(int i=0;i<64;++i)spine.push_back({float(i),.16f,0});auto original=spine;
        LiquidTrail liquid;liquid.Sync(spine);for(auto& s:liquid.samples){s.height=s.velocity=0;}
        liquid.samples[0].height=.03f;for(int tick=0;tick<30;++tick)liquid.Step(1.f/120);
        assert(liquid.samples[1].height>0); // Pressure transfers a deposited wave.
        for(int tick=0;tick<1200;++tick)liquid.Step(tick%2?.05f:1.f/240);
        for(size_t i=0;i<liquid.samples.size();++i){const auto& s=liquid.samples[i];Vec visual=liquid.Position(i);
            assert(LiquidTrail::DomainSame(s.anchor,original[i])&&LiquidTrail::DomainSame(spine[i],original[i]));
            assert(std::isfinite(s.velocity)&&std::isfinite(visual.x)&&std::abs(s.height)<=.0351f&&std::abs(s.side)<=.1001f);
            assert(std::abs(visual.z-original[i].z)<=.1001f);}
        auto retained=liquid.samples[20];spine.erase(spine.begin(),spine.begin()+20);liquid.Sync(spine);
        assert(liquid.samples.front().height==retained.height&&liquid.samples.front().side==retained.side);
        spine[5].z=2;liquid.Sync(spine);assert(liquid.samples[5].anchor.z==2);
        spine.clear();liquid.Sync(spine);assert(liquid.samples.empty());liquid.Step(.05f);
        std::cout<<"PASS: constrained liquid waves propagate, stable bounds, gameplay spine unchanged, partial removal preserves surviving liquid\n";
    }
    {
        struct LodVertex {struct {float x,y,z,w;} position;struct {float x,y;} texcoord;struct {float x,y,z;} normal;};
        std::vector<LodVertex> vertices;std::vector<uint32_t> indices;
        for(int z=0;z<=40;++z)for(int x=0;x<=40;++x)vertices.push_back({{float(x),std::sin(float(x)*.05f),float(z),1},{x/40.f,z/40.f},{0,1,0}});
        for(int z=0;z<40;++z)for(int x=0;x<40;++x){uint32_t a=uint32_t(z*41+x);indices.insert(indices.end(),{a,a+41,a+1,a+1,a+41,a+42});}
        std::vector<Engine::LodSubset> subsets{{0,4800,0},{4800,4800,1}};
        auto mid=Engine::ReduceStaticMesh(vertices,indices,subsets,32),far=Engine::ReduceStaticMesh(vertices,indices,subsets,20);
        assert(mid.indices.size()<indices.size()&&far.indices.size()<mid.indices.size());
        assert(far.vertices.size()<mid.vertices.size()&&mid.vertices.size()<vertices.size());
        assert(far.subsets.size()==2&&far.subsets[0].material==0&&far.subsets[1].material==1);
        for(auto id:far.indices)assert(id<far.vertices.size());
        for(size_t i=0;i<far.indices.size();i+=3)assert(far.indices[i]!=far.indices[i+1]&&far.indices[i]!=far.indices[i+2]&&far.indices[i+1]!=far.indices[i+2]);
        for(auto v:far.vertices)assert(std::isfinite(v.position.x)&&std::abs(v.normal.y-1)<.001f);
        // A closed torus with separate render vertices on every triangle catches
        // holes at normal splits and topology loss (including the central handle).
        auto topology=[](const auto& meshVertices,const auto& meshIndices){
            using Point=std::array<float,3>;std::map<Point,uint32_t> welded;std::map<std::array<uint32_t,2>,std::pair<int,int>> edges;
            std::set<std::array<uint32_t,3>> faces;
            for(size_t i=0;i<meshIndices.size();i+=3){std::array<uint32_t,3> ids{};
                for(int k=0;k<3;++k){const auto& p=meshVertices[meshIndices[i+k]].position;auto entry=welded.emplace(Point{p.x,p.y,p.z},uint32_t(welded.size()));ids[k]=entry.first->second;}
                auto sorted=ids;std::sort(sorted.begin(),sorted.end());assert(faces.insert(sorted).second);
                for(int k=0;k<3;++k){uint32_t a=ids[k],b=ids[(k+1)%3];assert(a!=b);auto& edge=edges[{(std::min)(a,b),(std::max)(a,b)}];++edge.first;edge.second+=a<b?1:-1;}}
            for(const auto& e:edges)assert(e.second.first==2&&e.second.second==0);
            return int(welded.size())-int(edges.size())+int(faces.size());
        };
        std::vector<LodVertex> ring;std::vector<uint32_t> ringIndices;
        auto ringVertex=[](int u,int v){float a=u*6.283185307f/48,b=v*6.283185307f/24;
            return LodVertex{{(3+std::cos(b))*std::cos(a),std::sin(b),(3+std::cos(b))*std::sin(a),1},{0,0},{std::cos(b)*std::cos(a),std::sin(b),std::cos(b)*std::sin(a)}};};
        for(int u=0;u<48;++u)for(int v=0;v<24;++v){LodVertex corners[]{ringVertex(u,v),ringVertex((u+1)%48,v),ringVertex(u,(v+1)%24),ringVertex((u+1)%48,(v+1)%24)};
            for(int k:{0,2,1,1,2,3}){ringIndices.push_back(uint32_t(ring.size()));auto vertex=corners[k];vertex.normal.x+=k*.001f;ring.push_back(vertex);}}
        assert(topology(ring,ringIndices)==0);
        for(int grid:{32,20}){auto reduced=Engine::ReduceStaticMesh(ring,ringIndices,{},grid);assert(reduced.indices.size()<ringIndices.size());assert(topology(reduced.vertices,reduced.indices)==0);}
        // Existing open borders are fixed; simplification cannot enlarge them.
        auto boundary=[](const auto& meshVertices,const auto& meshIndices){std::map<std::array<float,6>,int> edges;
            for(size_t i=0;i<meshIndices.size();i+=3)for(int k=0;k<3;++k){const auto& a=meshVertices[meshIndices[i+k]].position;const auto& b=meshVertices[meshIndices[i+(k+1)%3]].position;
                std::array<float,3> p{a.x,a.y,a.z},q{b.x,b.y,b.z};if(q<p)std::swap(p,q);++edges[{p[0],p[1],p[2],q[0],q[1],q[2]}];}
            std::set<std::array<float,6>> result;for(const auto& e:edges)if(e.second==1)result.insert(e.first);return result;};
        assert(boundary(vertices,indices)==boundary(mid.vertices,mid.indices));assert(boundary(vertices,indices)==boundary(far.vertices,far.indices));
        std::vector<LodVertex> cards;std::vector<uint32_t> cardIndices;
        for(int i=0;i<8;++i)for(int cross=0;cross<2;++cross){uint32_t start=uint32_t(cards.size());
            for(int j=0;j<4;++j)cards.push_back({{float(i*4+j%2),float(j/2),float(cross),1},{float(j%2),float(j/2)},{0,0,1}});
            cardIndices.insert(cardIndices.end(),{start,start+1,start+2,start+1,start+3,start+2});}
        auto thin=Engine::ReduceStaticMesh(cards,cardIndices,{},4,true);assert(thin.indices.size()==24&&thin.vertices.size()==16);
        assert(Engine::DistanceLodLevel(30,2)==0&&Engine::DistanceLodLevel(70,2)==1&&Engine::DistanceLodLevel(200,2)==2);
        assert(Engine::DistanceLodLevel(70,40)==0&&Engine::DistanceLodLevel(150,40)==1&&Engine::DistanceLodLevel(250,40)==2);
        std::cout<<"PASS: static LOD reduction, closed oriented surface and handle preserved across normal splits, open borders unchanged, material boundaries, complete grass tufts\n";
    }
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
    assert(InkBossSupport({18,0,55},5)==0);
    assert(InkBossSupport({32,0,55},10)==0);
    assert(InkBossSupport({32,0,9},0)==0);
    assert(InkBossSupport({-75,0,-50},2)==0);
    }
    static_assert(CreatureMorphSeconds==8.f);
    for(bool ink:{false,true}){
        Vec player{12,1,18};
        Vec left=CreatureCruisePoint(player,{0,0,115},1,ink),right=CreatureCruisePoint(player,{0,0,115},0,ink);
        if(ink){assert(Length(left-player)>110&&Length(right-player)>110);}
        else assert(left.x<player.x&&right.x>player.x);
        for(float hz:{30.f,60.f,144.f}){
            Vec at{},velocity{};
            for(int i=0;i<int(hz*8);++i){
                Vec delta=right-at;
                velocity=CreatureCruiseVelocity(velocity,delta,1/hz,ink?30.f:40.f);
                Vec step=velocity*(1/hz);
                at=at+(Length(step)>Length(delta)?delta:step);
            }
            assert(Length(right-at)<1.5f);
        }
    }
    for(Vec player:{Vec{0,1,0},Vec{205,1,0},Vec{-205,1,0},Vec{0,1,245},Vec{0,1,-205}}){
        Vec goal=CreatureCruisePoint(player,{0,0,115},0,true);
        assert(Length(goal-player)>145.f&&std::abs(goal.x)<=190.f&&goal.z>=-185.f&&goal.z<=225.f);
    }
    for(float step:{1.f/30,1.f/60,1.f/144})for(auto attack:{CreatureAttack::Sweep,CreatureAttack::Charge,CreatureAttack::Slam,CreatureAttack::Wing,CreatureAttack::HeadTail}){
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
    assert(AttackHead(CreatureAttack::HeadTail,1,0).z>30);
    assert(AttackHead(CreatureAttack::HeadTail,2,ActiveTime(CreatureAttack::HeadTail)).z<0);
    assert(TailTouches({0,1,0},0,TailActiveTime));
    assert(!TailTouches({0,1,12},0,TailActiveTime));
    // Check continuity across all departure, lattice-switch and arrival seams.
    for(float t=0;t<8;t+=.01f){auto a=EvaluateCreature(0,t/8),b=EvaluateCreature(0,(t+.0001f)/8);
        for(int i=0;i<a.count;++i)assert(Length(a.pieces[i].position-b.pieces[i].position)<.5f);}
    for(float age:{0.f,1.f,10.f,90.f})for(float form:{0.f,.25f,.5f,.75f,1.f}){
        auto pose=EvaluateCreature(age,form);assert(pose.count==CreaturePartCount);
        for(int i=0;i<pose.count;++i){const auto& piece=pose.pieces[i];
            assert(std::isfinite(Length(piece.position))&&std::isfinite(Length(piece.rotation)));
            assert(piece.size.x>0&&piece.size.y>0&&piece.size.z>0);}
        auto again=EvaluateCreature(age,form);assert(Length(again.head-pose.head)==0);
    }
    auto snake=EvaluateCreature(0,0),bird=EvaluateCreature(0,1);
    const auto& tailLink=snake.pieces[snake.spineIndex[25]];
    assert(std::abs(tailLink.position.y-CreatureVerticalExtent(tailLink))<4.f);
    assert(snake.head.y-tailLink.position.y>35.f);
    for(int segment=0;segment<CreatureSpineCount;++segment){
        const auto& link=snake.pieces[snake.spineIndex[segment]];
        assert(link.role==CreatureRole::Spine&&link.segment==segment);
        int armor=0;
        for(int i=0;i<snake.count;++i)if(snake.pieces[i].segment==segment&&
            snake.pieces[i].role!=CreatureRole::Spine)++armor;
        assert(armor==6);
    }
    for(int side=0;side<2;++side){
        assert(bird.pieces[bird.wingRoot[side]].role==CreatureRole::WingSpar);
        assert(bird.pieces[bird.wingTip[side]].role==CreatureRole::PrimaryFeather);
        assert(bird.pieces[bird.wingTip[side]].span==17);
    }
    assert(bird.pieces[bird.skullIndex].role==CreatureRole::Skull);
    assert(bird.head.y>145);
    auto pattern=EvaluateCreature(0,.5f);
    for(int i=0;i<pattern.count;++i){
        assert(Length(pattern.pieces[i].position-Lerp(snake.pieces[i].position,bird.pieces[i].position,.5f))>10);
        auto nearby=EvaluateCreature(0,.5001f);
        assert(Length(pattern.pieces[i].position-nearby.pieces[i].position)<3); // Fast lattice transfer, still continuous.
    }
    assert(bird.head.y>55&&snake.head.y>23);
    float wingLeft=0,wingRight=0,wingLow=1000,wingHigh=-1000;
    for(int i=0;i<bird.count;++i){if(!CreatureWing(bird.pieces[i].role))continue;auto at=bird.pieces[i].position;
        wingLeft=std::min(wingLeft,at.x);wingRight=std::max(wingRight,at.x);
        wingLow=std::min(wingLow,at.y);wingHigh=std::max(wingHigh,at.y);}
    assert(wingRight-wingLeft>100&&wingHigh-wingLow>15);
    assert(Length(bird.pieces[bird.wingRoot[0]].position-bird.pieces[bird.wingTip[0]].position)>1);
    assert(bird.head.y>snake.head.y+10&&bird.core.y>14);
    assert(std::abs(bird.pieces[bird.wingTip[1]].position.x)>16);
    assert(Length(snake.pieces[snake.wingTip[1]].position-bird.pieces[bird.wingTip[1]].position)>10);
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
    {InkPlayer bonus;float hp=40;bonus.Collect(hp,608,true);assert(hp==40&&bonus.charge==608);
        assert(bonus.Fire(hp)&&hp==40&&bonus.tier==3&&bonus.spent==0);
        bonus.Return(hp,1);assert(hp==40);
        InkPlayer graze;graze.dodgeAge=.2f;assert(graze.TryPerfectDodge(.24f));}
    {InkPlayer dodge;dodge.dodgeAge=.1f;assert(dodge.TryPerfectDodge());assert(!dodge.TryPerfectDodge());
        dodge.perfectUsed=false;dodge.dodgeAge=.121f;assert(!dodge.TryPerfectDodge());
        assert(SlimeBeamRange>600);
        assert(SlimeDodgeInvincibility>.18f&&SlimePerfectInvincibility>.65f);
        assert(SlimePerfectPulse(-1)==0&&SlimePerfectPulse(0)==0);
        assert(SlimePerfectPulse(.06f)>.99f&&SlimePerfectPulse(.2f)<1&&SlimePerfectPulse(.4f)==0);}
    {
        auto square=[](float step){DomainPath path;path.Append({0,0,0});
            for(int side=0;side<4;++side)for(float t=step;t<=20+step*.1f;t+=step){
                float d=std::min(t,20.f);Vec at=side==0?Vec{d,0,0}:side==1?Vec{20,0,d}:side==2?Vec{20-d,0,20}:Vec{0,0,20-d};path.Append(at);}
            return path;};
        auto slow=square(.25f),fast=square(1.25f);
        assert(slow.candidate.Ready()&&fast.candidate.Ready());
        assert(slow.candidate.shape==DomainShape::Square&&fast.candidate.shape==DomainShape::Square);
        auto retained=slow;for(int i=1;i<=30;++i)retained.Append({-float(i),0,0});
        assert(retained.candidate.Ready()&&retained.candidate.shape==DomainShape::Square);
        retained.BreakTrail();assert(retained.candidate.Ready()&&retained.points.empty());
        auto polygon=[](const std::vector<Vec>& vertices){DomainPath path;path.Append(vertices.front());
            for(size_t i=1;i<vertices.size();++i){float length=DomainDistance(vertices[i-1],vertices[i]);
                int steps=int(std::ceil(length*4));for(int j=1;j<=steps;++j)path.Append(Lerp(vertices[i-1],vertices[i],float(j)/steps));}
            return path;};
        auto triangle=polygon({{0,0,0},{24,0,1},{11,0,23},{0,0,0}});
        assert(triangle.candidate.Ready()&&triangle.candidate.shape==DomainShape::Triangle);
        auto skewSquare=polygon({{0,0,0},{26,0,2},{25,0,27},{-1,0,24},{0,0,0}});
        assert(skewSquare.candidate.shape==DomainShape::Square);
        DomainPath roundLoop;roundLoop.Append({20,0,0});for(int i=1;i<=400;++i){float angle=i*6.283185f/400;roundLoop.Append({20*std::cos(angle),0,20*std::sin(angle)});}
        assert(roundLoop.candidate.Ready()&&roundLoop.candidate.shape==DomainShape::Loop);
        DomainPath figureEight;figureEight.Append({0,0,0});
        for(int i=1;i<=500;++i){float angle=i*6.283185f/500;figureEight.Append({28*std::sin(angle),0,18*std::sin(angle)*std::cos(angle)});}
        assert(figureEight.candidate.Ready()&&figureEight.candidate.shape==DomainShape::Infinity&&figureEight.candidate.area>200);
        auto retraced=polygon({{0,0,0},{20,0,0},{20,0,20},{0,0,20},{0,0,0},{20,0,0},{20,0,20},{0,0,20},{0,0,0}});
        assert(retraced.candidate.shape!=DomainShape::Infinity);
        assert(DomainAmmo(triangle.candidate)>DomainNormalAmmo(triangle.candidate.Enclosures()));
        assert(std::abs(slow.candidate.area-400)<3&&std::abs(fast.candidate.area-400)<3);
        assert(slow.candidate.Enclosures()==fast.candidate.Enclosures());
        assert(DomainAmmo(slow.candidate)==DomainAmmo(fast.candidate));
        assert(DomainContains(slow.candidate,{10,60,10})&&!DomainContains(slow.candidate,{30,0,10}));
        size_t before=slow.points.size();slow.Damage();assert(!slow.candidate.Ready()&&slow.points.size()>before/2&&slow.points.size()<before);
        DomainPath line;for(int i=0;i<50;++i)line.Append({float(i),0,0});
        for(int i=49;i>=0;--i)line.Append({float(i),0,0});assert(!line.candidate.Ready());
        line.Append({200,0,200});assert(line.points.size()==1&&!line.candidate.Ready());
        DomainPath elevated;for(int i=0;i<=20;++i)elevated.Append({float(i),0,0});
        for(int i=1;i<=20;++i)elevated.Append({20,0,float(i)});
        for(int i=1;i<=20;++i)elevated.Append({20-float(i),i*.15f,20});
        for(int i=1;i<=20;++i)elevated.Append({0,3,20-float(i)});assert(!elevated.candidate.Ready());
        DomainPath bounded;for(int i=0;i<2000;++i)bounded.Append({float(i),0,0});assert(bounded.points.size()<=DomainPath::MaxPoints);
        assert(DomainNormalAmmo(1)==48&&DomainNormalAmmo(2)>DomainNormalAmmo(1));
        assert(DomainBulletPower(1)==1&&DomainBulletPower(3)>DomainBulletPower(2));
        assert(DomainNormalAmmo(10000)==256&&DomainBulletPower(10000)<3);
        assert(retraced.candidate.Enclosures()==1); // No farming by tracing the same rim.
        auto chain=polygon({{0,0,0},{-20,0,0},{-20,0,20},{0,0,20},{0,0,0},
            {20,0,0},{20,0,20},{0,0,20},{0,0,0},{20,0,0},{40,0,0},{40,0,20},{20,0,20},{20,0,0}});
        assert(chain.candidate.Ready()&&chain.candidate.Enclosures()==3&&chain.candidate.shape==DomainShape::Loop);
        assert(DomainAmmo(chain.candidate)>DomainNormalAmmo(1)&&DomainBulletPower(chain.candidate.Enclosures())>1);
        assert(DomainContains(chain.candidate,{-10,0,10})&&DomainContains(chain.candidate,{10,0,10})&&DomainContains(chain.candidate,{30,0,10}));
        assert(!DomainContains(chain.candidate,{50,0,10}));
        auto subdivided=DomainStrokeRegions({{0,0,0},{20,0,0},{20,0,20},{0,0,20},{0,0,0},{20,0,20}});
        assert(subdivided.size()==2); // A new chord splits the enclosed region.
        auto curvedJunction=DomainStrokeRegions({{0,0,0},{10,0,.03f},{20,0,0},{20,0,20},{10,0,20},{10,0,.03f}});
        assert(curvedJunction.size()==1); // Simplifying a gently curved edge must preserve its junction.
        auto shortChord=polygon({{0,0,10},{0,0,0},{6,0,0},{6,0,20},{0,0,20},{0,0,10},{6,0,10}});
        assert(shortChord.candidate.Ready()&&shortChord.candidate.Enclosures()==2);
        DomainPath liveTip;
        for(int x=0;x<=20;++x)liveTip.Append({float(x),0,0});
        for(int z=1;z<=20;++z)liveTip.Append({20,0,float(z)});
        for(int x=19;x>=0;--x)liveTip.Append({float(x),0,20});
        for(int z=19;z>=2;--z)liveTip.Append({0,0,float(z)});
        assert(!liveTip.candidate.Ready());
        liveTip.Append({0,0,1.3f}); // Visible ribbon touches; less than one sample step.
        assert(liveTip.candidate.Ready());
        for(int i=0;i<120;++i)liveTip.Append({.01f*std::sin(float(i)),0,1.3f});
        assert(liveTip.candidate.Ready()&&liveTip.points.size()<=DomainPath::MaxPoints);
        DomainPath openGap;
        for(int x=0;x<=20;++x)openGap.Append({float(x),0,0});
        for(int z=1;z<=20;++z)openGap.Append({20,0,float(z)});
        for(int x=19;x>=0;--x)openGap.Append({float(x),0,20});
        for(int z=19;z>=2;--z)openGap.Append({0,0,float(z)});
        openGap.Append({0,0,1.6f});assert(!openGap.candidate.Ready());
        auto repeated=DomainStrokeRegions({{0,0,0},{20,0,0},{20,0,20},{0,0,20},{0,0,0},{20,0,0},{20,0,20},{0,0,20},{0,0,0}});
        assert(repeated.size()==1);
        for(float step:{1.f/30,1.f/60,1.f/120}){
            Vec at{},velocity{0,55,0};bool arrived=false;
            for(float age=step;age<10;age+=step){Vec target{90+age*8,20,70};
                velocity=DomainMissileVelocity(at,velocity,target,age,1.2f,step);at=at+velocity*step;
                if(age<.45f)assert(at.y>0);
                if(age>.9f&&Length(at-target)<4.5f){arrived=true;break;}}
            assert(arrived);
        }
        std::cout<<"PASS: one-stroke enclosure accumulation, shared edges, subdivision, retracing, count-based ammo/power, hidden skills, sampling invariance, partial loss, height rejection, bounds, moving-target homing\n";
    }
    auto referencePose=EvaluateCreature(0,0);
    for(int part=0;part<referencePose.count;++part){
        Vec from{1,2,3},to{200,60,220};
        assert(Length(ReformPart(from,to,referencePose.pieces[part],part,0)-from)<.001f);
        assert(Length(ReformPart(from,to,referencePose.pieces[part],part,1)-to)<.001f);
    }
    assert(Length(ReformPart({0,0,0},{200,60,220},referencePose.pieces[referencePose.spineIndex[0]],0,.4f)-
        ReformPart({0,0,0},{200,60,220},referencePose.pieces[referencePose.spineIndex[25]],1,.4f))>5);
    for(int angle=0;angle<360;++angle){
        Vec at=SlimeFlightPoint(angle*3.14159265f/180);
        assert(std::abs(at.x)>220||at.z< -220||at.z>260);
        assert(std::abs(at.x)<290&&at.z> -280&&at.z<320);
        assert(at.y>=57&&at.y<=71);
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
