#include "../Game/Chrono/SlimeWeaponRules.h"
#include "../Game/Chrono/SlimeHydrantRules.h"
#include "../Game/Chrono/SlimeSignalRules.h"
#include "../Game/Chrono/CityStageRules.h"
#include "../Game/Chrono/RingCityRules.h"
#include "../Game/Chrono/DevourMorphRules.h"
#include "../Game/Chrono/DevourStageRules.h"
#include "../Game/Chrono/RoomRules.h"
#include "../Game/Chrono/DevourSpillRules.h"
#include "../Game/Chrono/DevourGimmickRules.h"
#include "../Game/Chrono/SlimeTentacleRules.h"
#include "../Game/Chrono/DevourAnimation.h"
#include "../Engine/FluidMorphShape.h"
#include "../Engine/SlimeEngulfShape.h"
#include <cstdio>
#include <stdexcept>
using namespace Game::Chrono;
void CheckLife(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void TestFacingAndCamera(){
    for(int hz:{30,60,120})for(auto form:{LifeAction::Idle,LifeAction::Engulf,LifeAction::Restore,LifeAction::Sword,LifeAction::Hammer,LifeAction::Plane}){
        LifeBody body;body.action=form;body.forward={1,0,0};body.weapon.active=LifeWeaponForm(form);
        for(int frame=0;frame<hz;++frame)LifeUpdateFacing(body,{}, {0,0,1},1.f/hz);
        CheckLife(Dot(body.forward,LifeFacesCamera(form)?Vec{0,0,1}:Vec{1,0,0})>.999f,"stationary orbit changes free facing or aimed form ignores camera during attack");
        body.weapon.active=false;
        for(int frame=0;frame<hz;++frame)LifeUpdateFacing(body,{-1,0,0},{0,0,1},1.f/hz);
        CheckLife(Dot(body.forward,LifeFacesCamera(form)?Vec{0,0,1}:Vec{-1,0,0})>.999f,"movement and camera facing policies disagree");
        auto stopped=body.forward;for(int frame=0;frame<hz;++frame)LifeUpdateFacing(body,{}, {0,0,1},1.f/hz);
        CheckLife(Length(body.forward-stopped)<.002f,"releasing movement snaps to camera");
    }
    CheckLife(LifeCameraDistance(30,1.13f,16.f/9)>LifeCameraDistance(3,1.13f,16.f/9)*4,"grown subject does not pull the camera back");
    CheckLife(LifeCameraDistance(30,1.13f,.6f)>LifeCameraDistance(30,1.13f,16.f/9),"portrait viewport clips the growing subject");
    puts("PASS facing/camera: independent movement and stopped orbit, aimed hammer/plane during attacks, growth and viewport framing at 30/60/120Hz");
}
void TestGrowthHealth(){
    SlimeLife life;LifeInitializeBodies(life,6600);auto& body=life.bodies[0];
    body.health=80;body.pendingGrowth=30;
    CheckLife(LifeCompleteGrowth(life,body)==30&&body.health==110&&body.maxHealth==130,"growth does not fill new HP capacity or erases existing damage");
    CheckLife(LifeCompleteGrowth(life,body)==0&&body.health==110,"restoration grants HP twice");
    auto& child=life.bodies[1];child.mass=20;child.health=child.maxHealth=20;child.pendingGrowth=12;
    CheckLife(LifeCompleteGrowth(life,child)==12&&body.health==122&&body.maxHealth==142&&child.health==20,"child absorption does not feed main-body HP");
    body.mass=LifeMaxMass-child.mass-5;body.pendingGrowth=30;
    CheckLife(LifeCompleteGrowth(life,body)==5&&body.health==127&&body.maxHealth==147&&life.TotalMass()==LifeMaxMass,"cap-trimmed growth grants excess HP");
    CheckLife(LifeCompleteGrowth(life,body)==0&&body.health==127,"mass cap grants free healing");
    SlimeRecipe recipe;CheckLife(std::abs(recipe.Speed()-18.6f)<.001f,"default movement speed was not increased by 20 percent");
    for(int hz:{30,60,120}){Vec velocity{};float distance=0;
        for(int frame=0;frame<hz;++frame){velocity=LifeMoveVelocity(velocity,{0,0,1},recipe,true,1.f/hz);distance+=velocity.z/hz;}
        CheckLife(distance>16&&distance<19,"faster movement does not reach the controller or varies with frame rate");}
    puts("PASS growth HP: main/child, retained damage, single credit, mass cap; faster movement at 30/60/120Hz");
}
void TestLifeContactAbsorb(){
    SlimeLife life;LifeInitializeBodies(life,6600);auto& body=life.bodies[0];body.at={0,1.25f,8};Vec from{0,1.25f,0};
    body.mass=179.99f;CheckLife(!LifeCollectTouch(life,1,{0,.5f,4},15,0,1,12,from),"contact absorption unlocked too early");
    body.mass=180;body.health=80;body.maxHealth=180;LifeEnterWeapon(body,LifeAction::Hammer,{0,0,1});
    CheckLife(LifeCollectTouch(life,1,{0,.5f,4},15,0,1,12,from)&&body.mass==192&&body.health==92&&body.maxHealth==192&&body.action==LifeAction::Hammer,"fast pass misses an orb, loses growth HP or interrupts the held weapon");
    CheckLife(!LifeCollectTouch(life,2,{0,.5f,4},130,0,1,36,from),"large object bypasses the relative size gate");
    CheckLife(!LifeCollectTouch(life,3,{0,.5f,4},15,30,30,12,from),"healthy enemy was eaten by contact");
    CheckLife(!LifeCollectTouch(life,4,{0,20,4},15,0,1,12,from)&&!LifeCollectTouch(life,5,{20,.5f,4},15,0,1,12,from),"remote/high object is collected");
    CheckLife(LifeCollectTouch(life,6,{0,.5f,4},15,9,30,12,from),"weakened small enemy cannot be collected");
    body.action=LifeAction::Idle;body.at=from;CheckLife(LifeAddGrab(life,body,9,{0,.5f,3},15,0,1,12),"manual reservation setup failed");
    body.at={0,1.25f,8};CheckLife(!LifeCollectTouch(life,9,{0,.5f,3},15,0,1,12,from),"contact steals a reserved hand target");
    LifeCommitGrab(life,body,body.grabs[0]);float pending=body.pendingGrowth;
    CheckLife(LifeCollectTouch(life,10,{0,.5f,4},15,0,1,12,from)&&body.pendingGrowth==pending,"contact consumes pending hand growth");
    body.mass=LifeMaxMass-pending-5;CheckLife(!LifeCollectTouch(life,11,{0,.5f,4},15,0,1,12,from),"contact exceeds reserved global mass cap");
    SwarmEnemy rat,guard,knight;rat.kind=uint8_t(CityEnemy::Rat);guard.kind=uint8_t(CityEnemy::Guard);knight.kind=uint8_t(CityEnemy::Knight);
    auto small=CityEnemyDrop(rat),medium=CityEnemyDrop(guard),large=CityEnemyDrop(knight);
    CheckLife(small.orb&&medium.orb&&large.orb&&LifeOrbTier(small.reward)==0&&LifeOrbTier(medium.reward)==2&&small.required<medium.required&&medium.required<large.required,"enemy drop tiers are inconsistent");
    CheckLife(LifeOrbRadius(12)<LifeOrbRadius(24)&&LifeOrbRadius(24)<LifeOrbRadius(36)&&LifeOrbRadius(small.reward)<LifeOrbRadius(large.reward),"orb tiers have identical sizes");
    CheckLife(Length(LifeOrbColor(12)-LifeOrbColor(24))>.5f&&Length(LifeOrbColor(24)-LifeOrbColor(36))>.5f,"orb tier colours cannot be distinguished");
    puts("PASS contact absorption: threshold, swept movement, weak enemies, weapon continuity, reservations/cap and orb tiers");
}
void TestLifeBatchAbsorb(){
    SlimeLife life;LifeInitializeBodies(life,6600);auto& body=life.bodies[0];body.at={0,1.25f,0};
    for(uint32_t i=0;i<3;++i)CheckLife(LifeAddGrab(life,body,i,{float(i)-1,.5f,3},15,0,1,12),"front batch target rejected");
    CheckLife(body.grabs.size()==3&&life.ReservedMass()==136,"batch does not reserve all growth before commitment");
    CheckLife(!LifeAddGrab(life,body,0,{0,.5f,3},15,0,1,12),"duplicate target added twice");
    CheckLife(!LifeAddGrab(life,body,4,{0,.5f,-3},15,0,1,12)&&!LifeAddGrab(life,body,5,{0,.5f,3},130,0,1,12)&&
        !LifeAddGrab(life,body,6,{0,.5f,3},15,10,30,12),"batch bypasses cone/size/weakness gates");
    auto& child=life.bodies[1];child.mass=20;child.at=body.at;child.forward=body.forward;
    CheckLife(!LifeAddGrab(life,child,1,{0,.5f,3},15,0,1,12),"child stole a target held by the parent");
    body.at={0,1.25f,9};body.forward={1,0,0};
    CheckLife(LifeHoldGrab(body,body.grabs[0].at,life.recipe),"walking past or turning away cancels a latched hand");
    CheckLife(!LifeHoldGrab(body,{0,.5f,40},life.recipe),"escaping enemy cannot break an individual grab");
    body.grabs[1].cancelled=true;
    CheckLife(!LifeTargetReserved(life,1)&&life.ReservedMass()==144,"cancelled hand did not release its target/capacity");
    CheckLife(LifeCommitGrab(life,body,body.grabs[0])&&!LifeCommitGrab(life,body,body.grabs[0])&&
        !LifeCommitGrab(life,body,body.grabs[1]),"commit duplicated growth or collected a cancelled target");
    CheckLife(LifeCommitGrab(life,body,body.grabs[2])&&body.pendingGrowth==24&&life.absorbed==2&&life.ReservedMass()==144,"partial batch growth was lost or counted twice");
    LifeReturnToSlime(body);CheckLife(body.grabs.empty()&&!LifeTargetReserved(life,0)&&body.pendingGrowth==24,"interruption lost committed growth or retained target reservations");
    LifeCompleteGrowth(life,body);CheckLife(body.mass==124&&life.ReservedMass()==144,"restoration created/lost batch mass");
    SlimeLife capped;LifeInitializeBodies(capped,6600);capped.bodies[0].mass=LifeMaxMass-30;auto& full=capped.bodies[0];
    CheckLife(LifeAddGrab(capped,full,1,{0,0,3},15,0,1,20)&&!LifeAddGrab(capped,full,2,{0,0,3},15,0,1,20)&&
        LifeAddGrab(capped,full,3,{0,0,3},15,0,1,10)&&capped.ReservedMass()==LifeMaxMass,"batch does not enforce total reserved mass cap");
    LifeCommitGrab(capped,full,full.grabs[0]);CheckLife(capped.ReservedMass()==LifeMaxMass,"committing one hand changed reserved total");
    full.grabs[1].cancelled=true;CheckLife(capped.ReservedMass()==LifeMaxMass-10,"one cancelled hand does not release exactly its reward");
    for(int hz:{30,60,120}){LifeBody moving;moving.mass=100;moving.action=LifeAction::Engulf;Vec velocity{};float distance=0;
        for(int i=0;i<hz;++i){velocity=LifeMoveVelocity(velocity,{0,0,1},life.recipe,true,1.f/hz,LifeTravelScale(moving));distance+=velocity.z/hz;}
        CheckLife(distance>life.recipe.Speed()*.65f,"absorb movement still stalls the player");
        CheckLife(!LifeWantsAbsorb(true,moving),"held input restarts an unfinished grab");moving.action=LifeAction::Idle;
        CheckLife(LifeWantsAbsorb(true,moving)&&!LifeWantsAbsorb(false,moving),"hold/release cannot control subsequent batches");}
    LifeBody tentacle;tentacle.mass=100;tentacle.health=100;tentacle.at={0,1.25f,0};SlimeRecipe recipe;
    CheckLife(recipe.AbsorbDuration()+.12f<.55f&&recipe.AbsorbRange()>10,"tentacles are still short/slow");
    for(size_t count:{size_t(1),size_t(12),size_t(48)})for(size_t g=0;g<count;++g){
        float angle=(float(g)/float(count)-.5f)*1.2f;LifeGrab grab;grab.at={std::sin(angle)*10,.7f,std::cos(angle)*10};
        tentacle.actionAge=0;auto root=LifeTentaclePoint(tentacle,recipe,grab,g,0);
        CheckLife(Length(LifeTentaclePoint(tentacle,recipe,grab,g,1)-root)<.001f,"tentacle appears at the target before extending");
        tentacle.actionAge=.14f;CheckLife(Length(LifeTentaclePoint(tentacle,recipe,grab,g,1)-grab.at)<.01f,"tentacle does not reach its own object within 140ms");
        for(int frame=0;frame<=40;++frame){tentacle.actionAge=recipe.AbsorbDuration()*float(frame)/40;
            for(int segment=0;segment<=LifeTentacleSegments;++segment){auto point=LifeTentaclePoint(tentacle,recipe,grab,g,float(segment)/LifeTentacleSegments);
                CheckLife(std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z),"tentacle curve became nonfinite");}}
        CheckLife(Length(LifeTentaclePoint(tentacle,recipe,grab,g,1)-LifeTentaclePoint(tentacle,recipe,grab,g,0))<.001f,"tentacle does not retract into the body");
        tentacle.actionAge=.15f;grab.cancelled=true;grab.cancelAge=.05f;
        CheckLife(Length(LifeTentaclePoint(tentacle,recipe,grab,g,1)-LifeTentaclePoint(tentacle,recipe,grab,g,0))<.001f,"cancelled tentacle keeps reaching");
    }
    CheckLife(LifeTentacleRadius(100,0)<.5f&&LifeTentacleRadius(100,1)<.14f,"tentacle is still a broad hand");
    for(float mass:{100.f,600.f,LifeMaxMass})for(const auto& mix:std::array<std::array<int,4>,3>{{{100,0,0,0},{0,100,0,0},{0,0,100,0}}}){
        recipe.mix=mix;tentacle.mass=mass;tentacle.aspect=.48f;tentacle.fluidOffset={-.4f,0,.2f};LifeGrab grab;grab.at={3,.5f,10};
        auto root=LifeTentaclePoint(tentacle,recipe,grab,2,0);auto radii=LifeRadii(tentacle,recipe);Vec offset=root-tentacle.at-tentacle.fluidOffset;offset.y+=1.25f;
        float inside=offset.x*offset.x/(radii.x*radii.x)+offset.y*offset.y/(radii.y*radii.y)+offset.z*offset.z/(radii.z*radii.z);
        CheckLife(inside<.9f,"tentacle root detaches from a flattened or grown liquid body");}
    puts("PASS batch absorption: 1/12/48 fast tentacles, reservations/cap, partial cancellation, interruption, growth and held movement at 30/60/120Hz");
}
void TestLifeWeapons(){
    for(int hz:{30,60,120}){
        SlimeLife life;LifeInitializeBodies(life,6600);auto& body=life.bodies[0];body.mass=480;
        CheckLife(LifeEnterWeapon(body,LifeAction::Hammer,{0,0,1}),"unlocked hammer rejected");
        for(int frame=0;frame<hz*3;++frame){body.actionAge+=1.f/hz;LifeTickWeapon(life,body,1.f/hz,false,false,true);}
        CheckLife(body.action==LifeAction::Hammer&&!body.weapon.active,"weapon expired or attacked without input");
        int taps=0;for(int frame=0;frame<hz*4;++frame)if(LifeTickWeapon(life,body,1.f/hz,frame==0,true,true))++taps;
        CheckLife(taps>=6&&body.action==LifeAction::Hammer,"held toy hammer cannot repeat without leaving its form");
        for(int frame=0;frame<hz;++frame)LifeTickWeapon(life,body,1.f/hz,false,false,true);
        CheckLife(!body.weapon.active&&body.action==LifeAction::Hammer,"hammer release does not settle into the held form");
        CheckLife(LifeEnterWeapon(body,LifeAction::Sword,{0,0,1}),"unlocked sword rejected");
        CheckLife(LifeTickWeapon(life,body,0,true,false,true),"first slash did not begin");
        for(int cut=0;cut<3;++cut){bool queued=false,advanced=false;uint64_t serial=body.attackSerial;
            for(int frame=0;frame<hz*2;++frame){bool press=!queued&&body.weapon.age>LifeWeaponDuration(body.action)-.18f;
                queued|=press;bool started=LifeTickWeapon(life,body,1.f/hz,press,false,true);
                if(started){CheckLife(body.attackSerial>serial,"queued sword sweep reused its hit identity");advanced=true;break;}}
            CheckLife(advanced,"sword recovery dropped the buffered sweep");}
        for(int frame=0;frame<hz*2;++frame)LifeTickWeapon(life,body,1.f/hz,false,false,true);
        CheckLife(body.action==LifeAction::Sword&&!body.weapon.active,"finished combo returned to slime automatically");
        CheckLife(LifeTickWeapon(life,body,0,true,false,true),"new sword sweep rejected after recovery");
        body.pendingGrowth=12;LifeReturnToSlime(body);
        CheckLife(body.action==LifeAction::Restore&&!body.weapon.active&&body.weapon.buffer==0&&body.pendingGrowth==12,"manual return retains a damaging swing or loses growth");
        CheckLife(!LifeTickWeapon(life,body,.05f,true,true,true),"restoring body started a weapon attack");
    }
    SlimeLife life;LifeInitializeBodies(life,6600);auto& body=life.bodies[0];
    CheckLife(!LifeEnterWeapon(body,LifeAction::Sword,{0,0,1})&&body.action==LifeAction::Idle,"persistent morph bypasses size gate");
    body.mass=300;LifeEnterWeapon(body,LifeAction::Sword,{0,0,1});
    LifeTickWeapon(life,body,.05f,true,false,false);CheckLife(!body.weapon.active,"unformed fluid deals weapon attacks");
    LifeTickWeapon(life,body,.3f,false,false,false);LifeTickWeapon(life,body,0,false,false,true);
    CheckLife(!body.weapon.active,"expired formation buffer attacked without a new press");
    for(auto form:{LifeAction::Hammer,LifeAction::Sword,LifeAction::Plane}){
        body.mass=480;LifeEnterWeapon(body,form,{0,0,1});body.weapon.active=true;body.at={0,1.25f,0};
        body.weapon.previousAge=.27f;body.weapon.age=.30f;
        Vec contact=form==LifeAction::Hammer?Vec{0,4.8f,0}:form==LifeAction::Sword?Vec{0,.9f,5}:Vec{0,.8f,2};
        contact=LifeWeaponPose(body,contact,.29f)*LifeScale(body.mass);
        CheckLife(LifeWeaponTouches(body,contact,.2f,0)&&!LifeWeaponTouches(body,{100,1,100},1,0),"weapon collision does not follow the rendered motion");
        Vec point{.3f,4,2},rest=LifeWeaponPose(form,point,false,0);
        auto end=LifeWeaponPose(form,point,true,LifeWeaponDuration(form));
        CheckLife(Length(rest-end)<.001f,"attack recovery snaps instead of returning to the same weapon stance");
        for(int sample=0;sample<=120;++sample){auto p=LifeWeaponPose(form,point,true,float(sample)/120*LifeWeaponDuration(form));
            CheckLife(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z),"weapon motion became nonfinite");}
    }
    body.action=LifeAction::Sword;body.weapon.active=true;body.weapon.previousAge=.27f;body.weapon.age=.30f;
    float from,to;CheckLife(LifeWeaponHitInterval(body,0,from,to)&&!LifeWeaponHitInterval(body,1,from,to),"sword sweep has an extra combo hit");
    LifeStopWeaponAttack(body);CheckLife(!LifeWeaponHitInterval(body,1,from,to)&&body.action==LifeAction::Sword,"cancelled attack still damages or loses its form");
    body.action=LifeAction::Hammer;body.mass=600;body.age=1.1f;body.velocity={};body.forward={0,0,1};body.weapon={};
    Vec sample{1,4.8f,.6f};auto present=LifeWeaponPose(body,sample,0);body.age-=1.f/60;auto previous=LifeWeaponPose(body,sample,0);body.age+=1.f/60;
    auto velocity=LifeWeaponVelocity(body,sample,1.f/60);
    CheckLife(Length(present-previous)>.0001f&&Length(velocity-(present-previous)*(LifeScale(body.mass)*60))<.002f,"idle liquid pose and uploaded velocity disagree");
    CheckLife(Length(velocity)<2,"idle liquid deformation is too fast");
    auto headAxis=LifeWeaponPose(LifeAction::Hammer,{2.2f,4.8f,0},false,0)-LifeWeaponPose(LifeAction::Hammer,{-2.2f,4.8f,0},false,0);
    CheckLife(std::abs(headAxis.z)>4&&std::abs(headAxis.x)<.01f,"hammer head is sideways to its vertical swing plane");
    auto lift=LifeWeaponPose(LifeAction::Hammer,{0,4.8f,0},true,.10f),impact=LifeWeaponPose(LifeAction::Hammer,{0,4.8f,0},true,.37f);
    CheckLife(impact.y<lift.y-1&&impact.z>lift.z+2,"hammer does not swing down and forward");
    float low=100,high=-100;for(int i=0;i<60;++i){auto p=LifeWeaponPoseAt(LifeAction::Sword,{0,.9f,5},true,float(i)/60*.64f,0);low=std::min(low,p.y);high=std::max(high,p.y);}
    CheckLife(high-low<.0001f,"sword sweep leaves the horizontal plane");
    for(int hz:{30,60,120}){Vec facing{0,0,1};for(int i=0;i<hz;++i){auto next=LifeWeaponFacing(facing,{1,0,0},1.f/hz);
        CheckLife(std::abs(std::atan2(next.x,next.z)-std::atan2(facing.x,facing.z))<=6.f/hz+.0001f,"weapon facing snaps during travel");facing=next;}
        CheckLife(facing.x>.999f,"weapon facing never reaches travel direction");}
    body.previousForward={0,0,1};body.forward={std::sin(.03f),0,std::cos(.03f)};
    velocity=LifeWeaponVelocity(body,{2.2f,4.8f,0},1.f/60);CheckLife(std::abs(velocity.x)>1,"weapon target velocity omits heading rotation");
    puts("PASS persistent weapons: manual return, formation gating, held vertical hammer, repeated single horizontal sword sweep, shared pose/collision, smooth heading/angular velocity, 30/60/120Hz");
}
void TestCityStage(){
    CityGates closed{},open=CityGates::AllOpen();
    // Start, every spawn and the route through all four areas lie on the floor.
    CheckLife(CityWalkable(CityStart(),4,closed),"stage start is blocked");
    for(Vec at:{Vec{0,0,-1180},Vec{40,0,-1046},Vec{102,0,-1000},Vec{102,0,-900},Vec{102,0,-600},Vec{-60,0,-400},Vec{270,0,-10},Vec{102,0,380},
                Vec{102,0,450},Vec{-140,0,700},Vec{340,0,880},Vec{102,0,930},Vec{102,0,1336},Vec{-250,0,1690},Vec{450,0,980}})
        CheckLife(CityWalkable(at,3,open),"stage route is not walkable");
    // Closed exits block; opened exits let the body through. Walls block in every area.
    CheckLife(CityWalls().size()>=2,"the alley needs several walls to break");
    for(size_t i=0;i<CityWalls().size();++i){Vec c=CityWalls()[i].Center();CityGates one=closed;one.wallsOpen=1u<<i;
        CheckLife(!CityWalkable(c,3,closed)&&CityWalkable(c,3,one),"alley wall gating is wrong");}
    CheckLife(!closed.AlleyOpen()&&open.AlleyOpen(),"alley exit state is wrong");
    CheckLife(!CityWalkable(CityBarricadeCenter(),3,closed)&&CityWalkable(CityBarricadeCenter(),3,open),"barricade gating is wrong");
    CheckLife(!CityWalkable(CityGateCenter(),3,closed)&&CityWalkable(CityGateCenter(),3,open),"shutter gate gating is wrong");
    CheckLife(!CityWalkable({40,0,-1180},3,open)&&!CityWalkable({60,0,-960},3,open)&&!CityWalkable({-60,0,-200},3,open)&&!CityWalkable({20,0,400},3,open)&&
        !CityWalkable({40,0,930},3,open)&&!CityWalkable({102,0,1760},3,open),"walls do not block");
    Vec slid=CityConstrain({10,0,-1180},{40,0,-1170},4,closed);CheckLife(slid.x<18&&slid.z>-1180.01f,"walls do not slide the body");
    // Pushing straight along a wall into an inner corner rounds it instead of pinning the body.
    Vec cornered=CityConstrain({17.92f,0,-1060},{18.22f,0,-1060},4.3f,closed);CheckLife(Length(cornered-Vec{17.92f,0,-1060})>.1f,"body pinned in an inner corner");
    Vec stopped=CityConstrain({0,0,-1140},{0,0,-1110},4,closed);CheckLife(stopped.z<-1126,"the first alley wall lets the body pass");
    stopped=CityConstrain({102,0,380},{102,0,420},4,closed);CheckLife(stopped.z<410,"barricade lets the body pass");
    // The alley has no enemies; the tire and signal items lie in their own areas.
    CheckLife(CityStepSpawns(0).empty()&&CityStepInfo(0).area==CityArea::Alley,"the alley must be free of enemies");
    CheckLife(CityAreaAt(CityTireItem())==CityArea::Avenue&&CityWalkable(CityTireItem(),2,closed),"tire item is not at the avenue entrance");
    CheckLife(CityAreaAt(CitySignalItem())==CityArea::Crossing&&CityWalkable(CitySignalItem(),2,open),"signal item is off the crossing");
    for(const auto& fire:CityFires())CheckLife(CityAreaAt(fire.at)==CityArea::Fire&&CityWalkable(fire.at,fire.radius*.5f,open),"fire spot is outside the fire scene");
    // The avenue is long and wide enough for tire dashes.
    CheckLife(CityWalk()[3].x1-CityWalk()[3].x0>=200&&CityWalk()[3].z1-CityWalk()[3].z0>=1000,"avenue is too small for the tire");
    // Steps: areas in order, every enemy spawns on the floor, roles grow with the stage.
    int total=0;CityArea area=CityArea::Alley;float previousRequired=0;
    for(int step=0;step<CityStepCount;++step){auto spawns=CityStepSpawns(step);CheckLife(step==0||!spawns.empty(),"empty stage step");
        CheckLife(int(CityStepInfo(step).area)>=int(area),"stage steps go back an area");area=CityStepInfo(step).area;
        static float required=0;if(step==0||CityStepInfo(step-1).area!=area)required=0;
        for(const auto& spawn:spawns){auto enemy=CitySpawnEnemy(spawn,uint32_t(++total),step);
            CheckLife(CityWalkable(spawn.at,std::min(enemy.scale,6.f),open)&&CityAreaAt(spawn.at)==area,"enemy spawns outside its area");
            CheckLife(CityWalkable(spawn.patrol,1,open),"patrol point is off the floor");
            CheckLife(enemy.health>0&&CityEnemyRequired(enemy)>0&&CityEnemyReward(enemy)>0&&enemy.step==step,"invalid city enemy stats");
            required=std::max(required,CityEnemyRequired(enemy));}
        // Each area's largest absorb requirement exceeds every earlier area's (the empty alley needs none).
        if(step+1==CityStepCount||CityStepInfo(step+1).area!=area){CheckLife(spawns.empty()||required>previousRequired,"later areas need less size");previousRequired=std::max(previousRequired,required);}}
    CheckLife(total==CityEnemyTotal()&&CityStepInfo(CityStepCount-1).area==CityArea::Crossing,"stage totals disagree");
    CheckLife(CityAreaAt({0,0,-1180})==CityArea::Alley&&CityAreaAt({102,0,-300})==CityArea::Avenue&&CityAreaAt({102,0,600})==CityArea::Fire&&CityAreaAt(CityCrossing())==CityArea::Crossing,"area lookup is wrong");
    // Shields block frontal non-sword hits; the sword and attacks from behind get through.
    SwarmEnemy shield;shield.kind=uint8_t(CityEnemy::Shield);shield.at={0,0,0};shield.heading={0,0,-1};
    CheckLife(CityShieldBlocks(shield,{0,0,-5},false)&&!CityShieldBlocks(shield,{0,0,-5},true)&&!CityShieldBlocks(shield,{0,0,5},false),"shield facing rules are wrong");
    // A cannon shell lands on its target.
    Vec from{0,5,0},target{30,0,40},v=CityShellVelocity(from,target,1.7f),at=from;for(int i=0;i<1700;++i){v.y-=30*.001f;at=at+v*.001f;}
    CheckLife(Length(at-target)<.2f,"cannon shell misses its target");
    puts("PASS city stage: walkable 4-area route, wall sliding, alley wall/barricade/shutter gating, enemy-free alley, item and fire placement, 8 steps in area order, shields and shells");
}
void TestTireMorph(){
    SlimeLife life;LifeInitializeBodies(life,6600);auto& body=life.bodies[0];body.at={0,1.25f,0};
    life.props.push_back({{0,.5f,4},15,12,true,0xffffffffu,false,LifeMorphItem::Tire});
    CheckLife(!LifeEnterWeapon(body,LifeAction::Tire,{0,0,1})&&!body.tireUnlocked,"tire morph unlocked without absorbing a tire");
    CheckLife(LifeAddGrab(life,body,0x80000000u,life.props[0].at,15,0,1,12)&&!body.tireUnlocked,"reserving a tire unlocked its form");
    body.grabs[0].cancelled=true;CheckLife(!LifeCommitGrab(life,body,body.grabs[0])&&!body.tireUnlocked,"cancelled tire unlocks");
    LifeReturnToSlime(body);body.action=LifeAction::Idle;
    CheckLife(LifeAddGrab(life,body,0x80000000u,life.props[0].at,15,0,1,12),"cannot reserve a released tire");
    CheckLife(LifeCommitGrab(life,body,body.grabs[0])&&body.tireUnlocked,"committed tire does not unlock");
    CheckLife(!LifeCommitGrab(life,body,body.grabs[0])&&body.pendingGrowth==12,"tire absorption credited twice");
    LifeReturnToSlime(body);LifeCompleteGrowth(life,body);
    CheckLife(LifeEnterWeapon(body,LifeAction::Tire,{0,0,1})&&body.mass==112,"starting-size tire morph rejected");
    LifeTickWeapon(life,body,.1f,true,true,true);
    CheckLife(body.weapon.ready&&!body.weapon.active,"movement form queues a weapon attack");
    LifeReturnToSlime(body);CheckLife(body.tireUnlocked&&LifeEnterWeapon(body,LifeAction::Tire,{0,0,1}),"return consumes tire unlock");
    LifeTakeHit(life,2);CheckLife(body.tireUnlocked&&LifeCanMorph(body,LifeAction::Tire),"damage split loses item unlock");
    LifeInitializeBodies(life,6600);CheckLife(!life.bodies[0].tireUnlocked,"new run keeps tire unlock");
    body.mass=180;body.at={0,1.25f,8};
    CheckLife(LifeCollectTouch(life,0x80000000u,life.props[0].at,15,0,1,12,{0,1.25f,0})&&body.tireUnlocked,"contact absorption misses tire unlock");
    SlimeLife childLife;LifeInitializeBodies(childLife,6600);childLife.props=life.props;
    auto& child=childLife.bodies[1];child.mass=20;child.health=20;child.action=LifeAction::Engulf;child.grabs.push_back({0x80000000u,life.props[0].at,12});
    CheckLife(LifeCommitGrab(childLife,child,child.grabs[0])&&childLife.bodies[0].tireUnlocked,"child tire does not unlock main body");
    for(int hz:{30,60,120}){
        LifeBody tire;tire.mass=100;tire.action=LifeAction::Tire;tire.tireUnlocked=true;Vec velocity{};
        for(int i=0;i<hz*2;++i)velocity=LifeTireMoveVelocity(tire,velocity,{0,0,1},life.recipe,true,1.f/hz);
        CheckLife(velocity.z>life.recipe.Speed()*2&&std::abs(velocity.x)<.001f,"tire does not provide faster straight travel");
        auto moving=velocity;LifeUpdateFacing(tire,{1,0,0},{0,0,1},1.f/hz);
        CheckLife(tire.forward.z>.98f,"tire turns instantly");
        velocity=LifeTireMoveVelocity(tire,velocity,{},life.recipe,true,1.f/hz);
        CheckLife(velocity.z>moving.z*.9f&&velocity.z<moving.z,"tire stops instantly or fails to brake");
        for(int i=0;i<hz*4;++i)velocity=LifeTireMoveVelocity(tire,velocity,{},life.recipe,true,1.f/hz);
        CheckLife(Length(velocity)<.1f,"released wheel never stops");
    }
    CheckLife(!LifeInsideTire({0,LifeTireRadius,0})&&LifeInsideTire({0,LifeTireRadius,LifeTireMajor}),"tire has no central hole");
    for(int i=0;i<90;++i){auto point=LifeTirePose({0,LifeTireRadius,LifeTireMajor},float(i)*.13f);
        CheckLife(LifeInsideTire(point)&&point.y>=0,"rolling tire deforms or passes through floor");}
    puts("PASS tire: item gate, cancelled/manual/contact/child absorption, reusable unlock/reset, acceleration/turn/braking at 30/60/120Hz, hollow rolling shape");
}
void TestHydrantMorph(){
    for(float mass:{100.f,905.f,6000.f})for(float yaw:{0.f,1.2f,-2.5f}){
        Vec forward{std::sin(yaw),0,std::cos(yaw)},right{forward.z,0,-forward.x};
        Vec offset=LifeHydrantCameraOffset(mass,forward);float scale=LifeScale(mass);
        CheckLife(Dot(offset,right)>3.7f*scale&&offset.y>5.6f*scale-1.25f&&Dot(offset,forward)<-8.9f*scale,"hydrant camera is not above the right shoulder");
        LifeBody body;body.mass=mass;body.at={0,1.25f,0};body.forward=forward;Vec nozzle=LifeHydrantNozzle(body);
        for(float range:{4.f,20.f,55.f,90.f})for(float height:{.5f,6.f,18.f}){
            Vec target=nozzle+forward*range+right*3;target.y=height;
            Vec velocity;float flight;bool reached=LifeHydrantLaunch(nozzle,target,velocity,flight);
            if(reached){
                CheckLife(std::abs(Length(velocity)-HydrantJetSpeed)<.002f&&Length(LifeHydrantTrajectory(nozzle,velocity,flight)-target)<.002f,"camera/nozzle parallax or water drop is not corrected");
                for(int hz:{60,120,240}){Vec at=nozzle,v=velocity;float time=0;
                    while(time<flight){float dt=std::min(1.f/hz,flight-time);v.y-=HydrantGravity*dt;at=at+v*dt;time+=dt;}
                    CheckLife(Length(at-target)<.85f,"aim misses under the GPU's symplectic integration");}
            }
        }
    }
    {Vec velocity;float flight;CheckLife(!LifeHydrantLaunch({0,1,0},{0,200,200},velocity,flight)&&std::isfinite(flight)&&std::abs(Length(velocity)-HydrantJetSpeed)<.001f,"unreachable aim produces invalid water");
        CheckLife(LifeHydrantLaunch({0,10,0},{0,1,0},velocity,flight)&&Length(LifeHydrantTrajectory({0,10,0},velocity,flight)-Vec{0,1,0})<.001f,"vertical aim is singular");}
    for(bool contact:{false,true}){
        SlimeLife life;LifeInitializeBodies(life,6600);auto& body=life.bodies[0];body.at={0,1.25f,0};
        life.props.push_back({{0,.5f,3},15,24,true,0xffffffffu,false,LifeMorphItem::Hydrant});
        CheckLife(!LifeEnterWeapon(body,LifeAction::Hydrant,{0,0,1}),"hydrant bypasses item gate");
        if(contact){body.mass=180;CheckLife(LifeCollectTouch(life,0x80000000u,life.props[0].at,15,0,1,24,body.at),"hydrant contact pickup failed");}
        else{CheckLife(LifeAddGrab(life,body,0x80000000u,life.props[0].at,15,0,1,24)&&!body.hydrantUnlocked,"hydrant reservation grants ability");
            CheckLife(LifeCommitGrab(life,body,body.grabs[0]),"hydrant commit failed");LifeReturnToSlime(body);LifeCompleteGrowth(life,body);}
        CheckLife(body.hydrantUnlocked&&!body.tireUnlocked&&LifeEnterWeapon(body,LifeAction::Hydrant,{0,0,1}),"hydrant pickup unlocks wrong form");
        LifeTickWeapon(life,body,.1f,true,true,true);CheckLife(body.weapon.ready&&!body.weapon.active,"hydrant queues melee attack");
        body.actionAge=2;LifeTakeHit(life,2);LifeRetargetWeapon(body);
        CheckLife(body.action==LifeAction::Hydrant&&body.weapon.ready&&body.actionAge==2,"damage split interrupts a formed hydrant stream");
        LifeReturnToSlime(body);CheckLife(LifeEnterWeapon(body,LifeAction::Hydrant,{0,0,1}),"return consumes hydrant");
        LifeRetargetWeapon(body);CheckLife(!body.weapon.ready,"retargeting bypasses initial hydrant formation");
        LifeTakeHit(life,2);CheckLife(body.hydrantUnlocked,"split loses hydrant unlock");
        LifeInitializeBodies(life,6600);CheckLife(!body.hydrantUnlocked,"restart preserves hydrant unlock");
    }
    CheckLife(LifeInsideHydrant({0,2,0})&&LifeInsideHydrant({2,2.9f,0})&&LifeInsideHydrant({0,3.1f,2})&&!LifeInsideHydrant({2,4,2}),"hydrant body or nozzle is missing");
    CheckLife(LifeHydrantLiveWater(1.1f,0,{0,0,48})&&LifeHydrantLiveWater(1.2f,0,{0,0,18}),"airborne attack/guard water ignored");
    CheckLife(!LifeHydrantLiveWater(1,0,{0,0,48})&&!LifeHydrantLiveWater(1.2f,.5f,{0,0,18})&&!LifeHydrantLiveWater(1.2f,0,{}),"unrelated water or a puddle can attack/block");
    CheckLife(LifeHydrantWaterTouches({0,2,4},{0,0,48},{0,2,3},.3f,.03f)&&!LifeHydrantWaterTouches({0,2,4},{0,0,48},{3,2,3},.3f,.03f),"swept water hit test misses or reaches off-axis targets");
    CheckLife(LifeWaterSegmentDistance({-2,0,0},{2,0,0},{0,-2,0},{0,2,0})<1e-5f&&LifeWaterSegmentDistance({-2,0,0},{2,0,0},{0,-2,3},{0,2,3})>2.9f,"crossing water/projectile paths are not swept");
    puts("PASS hydrant: absorbed-item gate, manual/contact unlock, reusable form/reset, nozzle shape, live-water filtering and swept attack/defense");
}
// タイヤ: 押している間はその場で溜め、離すと溜めに応じて突進する。短い押しは突進しない
void TestTireCharge(){
    SlimeRecipe recipe;LifeBody body;body.mass=100;body.health=body.maxHealth=100;body.tireUnlocked=true;
    CheckLife(LifeEnterWeapon(body,LifeAction::Tire,{0,0,1}),"tire not entered");
    CheckLife(!LifeTickTireCharge(body,true,.1f)&&!body.weapon.charging,"charged before the tire was formed");
    body.weapon.ready=true;Vec velocity{0,0,20};
    for(int i=0;i<3;++i){LifeTickTireCharge(body,true,1.f/60);velocity=LifeTireChargeVelocity(body,velocity,{},recipe,true,1.f/60);}
    CheckLife(!LifeTickTireCharge(body,false,1.f/60)&&body.weapon.dash==0,"a tap dashed without enough charge");
    for(int i=0;i<120;++i){LifeTickTireCharge(body,true,1.f/60);velocity=LifeTireChargeVelocity(body,velocity,{},recipe,true,1.f/60);body.tireSpin=LifeTireSpinRate(body,1.f/60);}
    CheckLife(body.weapon.charge==1&&Length(velocity)<1&&body.tireSpin>LifeTireSpinMax*.8f,"charging does not stop in place and spin up");
    CheckLife(LifeTickTireCharge(body,false,1.f/60)&&body.weapon.dashPower==1&&body.weapon.dash>0,"release did not start a full dash");
    velocity=LifeTireChargeVelocity(body,velocity,{},recipe,true,1.f/60);
    Vec roll=LifeTireMoveVelocity(body,{},{0,0,1},recipe,true,10);
    CheckLife(velocity.z>Length(roll)*2.5f,"full dash is not much faster than rolling");
    CheckLife(!LifeTickTireCharge(body,true,1.f/60)&&!body.weapon.charging,"recharged during a dash");
    CheckLife(LifeTireDashGateDamage(1)>=CityBarricadeHealth(),"a full dash cannot break the barricade at once");
    CheckLife(LifeTireDashDamage(body,1)>=CityStats(CityEnemy::Guard).health,"a full dash cannot defeat an alley guard");
    std::puts("PASS tire charge: stops in place, tap/min charge, full dash speed, no recharge mid-dash, one-hit barricade and guard");
}
// 信号機: 赤 → 青 → 黄の順だけ、色ごとの待ちと黄の後の点検、足止め・整列・マーカー
void TestSignal(){
    SignalState s;SignalColor fired;
    CheckLife(SignalFire(s,fired)&&fired==SignalColor::Red&&s.next==SignalColor::Green&&s.wait==SignalStepWait,"red is not first");
    CheckLife(!SignalFire(s,fired),"signal fired during the step wait");
    SignalTick(s,SignalStepWait);CheckLife(SignalFire(s,fired)&&fired==SignalColor::Green,"green does not follow red");
    SignalTick(s,SignalStepWait);CheckLife(SignalFire(s,fired)&&fired==SignalColor::Yellow&&SignalResting(s)&&s.wait==SignalRestTime,"yellow does not start the long rest");
    SignalTick(s,SignalRestTime-.1f);CheckLife(!SignalFire(s,fired)&&SignalResting(s),"red repeated before the rest ended");
    SignalTick(s,.1f);CheckLife(SignalFire(s,fired)&&fired==SignalColor::Red,"cycle did not restart at red");
    std::vector<SwarmEnemy> enemies(3);for(size_t i=0;i<3;++i){enemies[i].health=enemies[i].maxHealth=100;enemies[i].scale=1;enemies[i].phase=SwarmPhase::Dash;}
    enemies[0].at={5,0,0};enemies[1].at={-10,0,8};enemies[2].at={80,0,0};
    CheckLife(SignalStopEnemies(enemies,{},30)==2&&enemies[0].signalStop==SignalStopTime&&enemies[0].phase==SwarmPhase::Chase&&enemies[2].signalStop==0,"red stops the wrong enemies or keeps their dash");
    auto line=SignalLinePositions({0,0,10},{1,0,0},3,5);CheckLife(line.size()==3&&line[0].x==-5&&line[1].x==0&&line[2].x==5&&line[1].z==10,"green line is not centred and evenly spaced");
    Vec walker;CheckLife(SignalWalkerAt({},{1,0,0},{0,0,1},10,1,1,0,SignalWalkDelay+SignalWalkTime*.5f,walker)&&std::abs(walker.x)<.01f&&walker.z<0&&walker.z>-SignalCrossNear-1.5f&&!SignalWalkerAt({},{1,0,0},{0,0,1},10,1,1,0,SignalWalkDelay+SignalWalkTime+.1f,walker),"walker does not cross the crosswalk once");
    // 横断歩道は逆の端からも敷ける。範囲と人影の届く距離は成長に合わせて広がる
    CheckLife(SignalWalkerAt({},{1,0,0},{0,0,1},10,1,-1,0,SignalWalkDelay+.01f,walker)&&walker.x>5&&SignalStripeTime(9,10)<SignalLayTime&&SignalStripeTime(1,10)>0,"crosswalk cannot be laid from the other side");
    CheckLife(SignalStopRadius(LifeMaxMass)>SignalStopRadius(LifeStartMass)*3&&SignalMarkReach(900)>SignalMarkReach(100)*2&&SignalWalkerReach(3.4f,SignalReach(900))>SignalWalkerReach(3.4f,1)+5,"signal ranges do not grow with the body");
    SwarmEnemy marked;marked.health=1;marked.signalMark=SignalMarkTime;marked.signalMarkAt=marked.at;
    CheckLife(SignalTickMark(marked,1)==0&&SignalTickMark(marked,SignalMarkTime)==1&&marked.signalMark==0,"timed marker did not burst once");
    marked.signalMark=SignalMarkTime;marked.at={SignalMarkMove+.1f,0,0};CheckLife(SignalTickMark(marked,.01f)==2,"moving with a marker did not burst");
    CheckLife(SignalMarkDamage(600,true)>SignalMarkDamage(600,false)*2,"moving burst is not much stronger");
    std::puts("PASS signal: fixed red/green/yellow order, step waits and long rest, stop radius, centred line, crossing walkers, timed/moving markers");
}
// 吸収モード: 覆える大きさ・成長・段階と時間・飛びかかりの目標と形
void TestDevour(){
    CheckLife(DevourComboPitch(0)==1&&DevourComboPitch(1)==1&&DevourComboPitch(2)>1,"combo notes do not start at the base note and rise");
    for(int combo=1;combo<200;++combo)CheckLife(DevourComboPitch(combo)>=1&&DevourComboPitch(combo)<=2.0001f,"combo note left the range the audio engine can play");
    for(int combo=1;combo<6;++combo)CheckLife(DevourComboPitch(combo+1)>DevourComboPitch(combo),"first combo notes do not climb the scale");
    // 開始の体はゴミ袋を覆えるが、車は覆えない。車は成長すれば覆える
    CheckLife(DevourCanCover(LifeStartMass,1.7f)&&!DevourCanCover(LifeStartMass,13),"start body cover limit is wrong");
    CheckLife(DevourCanCover(DevourMassToCover(13)+1,13)&&!DevourCanCover(DevourMassToCover(13)*.9f,13),"mass needed to cover disagrees with the cover rule");
    // 大きい物ほど増える。覆える一番大きな物でも、体が一度に倍にはならない
    CheckLife(DevourReward(13)>DevourReward(1.7f)*50,"growth does not scale with object size");
    for(float mass:{100.f,4000.f,120000.f}){float largest=DevourBodyRadius(mass)*DevourCoverRatio;CheckLife(DevourReward(largest)<mass*.08f&&DevourReward(largest)>mass*.02f,"one bite grows too much or too little");}
    // 町: 門の大きさが輪の段階に合い、大通りと広場には建物が無く、輪の壁の内側に門がある
    {const auto& zones=RingZones();const float gates[]={500};
        for(size_t z=1;z<zones.size();++z)CheckLife(std::abs(DevourMassToCover(zones[z].gateRadius)/gates[z-1]-1)<.02f,"ring gate size does not match its mass");
        auto lots=RingLots();auto plazas=RingPlazas();int doors=0;
        for(const auto& lot:lots){if(lot.gate){++doors;CheckLife(std::abs(RingLotRadius(lot)-zones[size_t(lot.gate)].gateRadius)<.01f,"gate radius is wrong");continue;}
            CheckLife(lot.foot.x0>0||lot.foot.x1<0,"building on the north-south avenue");CheckLife(lot.foot.z0>0||lot.foot.z1<0,"building on the east-west avenue");
            for(const auto& plaza:plazas)CheckLife(LifeDistance(lot.foot.Center(),plaza.at)>plaza.radius*.9f,"building in a plaza");}
        CheckLife(doors==4*(RingStyleCount-1)&&plazas.size()==8,"ring gates or plazas are missing");
        CheckLife(lots.size()<1000,"too many buildings");}
    // ステージ: 開始の量で開始の大きさ、目標は大きくなる順、最初の目標でクリア、最後の目標で S ランク
    for(const auto& stage:DevourStages()){CheckLife(std::abs(DevourSizeCm(stage,LifeStartMass)-stage.start)<.001f&&DevourRankAt(stage,LifeStartMass)==0&&DevourRankName(stage,0)=="D","stage start is wrong");
        int count=int(stage.goals.size());CheckLife(DevourRankAt(stage,DevourGoalMass(stage,stage.goals[0]))==1&&DevourRankAt(stage,DevourGoalMass(stage,stage.goals.back()))==count&&DevourRankName(stage,count)=="S"&&DevourRankName(stage,count-1)=="A","stage ranks are wrong");
        CheckLife(stage.time>0&&stage.itemTime>0,"stage times are wrong");}
    CheckLife(DevourStages()[0].time==180&&DevourStages()[0].start==5&&DevourStages()[0].goals[0]==15&&DevourStages()[0].goals[1]==20&&DevourStages()[0].gates[0]==8&&DevourSizeText(15)=="15.0 cm"&&DevourSizeText(12600)=="126.00 m","first stage rules are wrong");
    // 成長と剥がれは上限と開始の大きさを守る
    LifeBody body;body.mass=body.health=body.maxHealth=DevourMaxMass-10;CheckLife(DevourGrow(body,50)==10&&body.mass==DevourMaxMass,"growth exceeds the devour cap");
    // 飲み込んだ量は少しずつ体へ移り、フレームの速さによらず同じ時間で全部移る
    {float pending=0;CheckLife(DevourSwallow(DevourMaxMass-10,pending,6)==6&&DevourSwallow(DevourMaxMass-10,pending,6)==4&&pending==10,"swallowing ignores the devour cap");
        for(int hz:{30,60,120}){LifeBody eater;eater.mass=eater.health=eater.maxHealth=1000;float left=0;DevourSwallow(eater.mass,left,160);
            for(int frame=0;frame<hz/10;++frame)DevourDigest(eater,left,1.f/float(hz));
            CheckLife(eater.mass>1000+160*.35f&&eater.mass<1000+160*.65f,"digesting is instant or depends on the frame rate");
            for(int frame=0;frame<hz;++frame)DevourDigest(eater,left,1.f/float(hz));
            CheckLife(left==0&&std::abs(eater.mass-1160)<.01f&&eater.health==eater.mass,"digesting loses mass or never ends");}}
    // タイヤの速さは大きさの倍率に合わせて上がる
    {SlimeRecipe recipe;LifeBody tire;tire.mass=5000;CheckLife(std::abs(LifeTireDashSpeed(tire,recipe,1,3)-3*LifeTireDashSpeed(tire,recipe,1))<.01f,"tire speed ignores the devour scale");}
    // 砕けるのは覆えない物のうち 1.5 倍までで、必要な量で砕ける。欠片は覆える大きさで、量は元より少ない
    for(float mass:{200.f,20000.f}){float r=DevourBodyRadius(mass)*DevourCoverRatio;CheckLife(!DevourCanShatter(mass,r*.9f)&&DevourCanShatter(mass,r*1.4f)&&!DevourCanShatter(mass,r*1.6f),"shatter size is wrong");
        CheckLife(DevourCanShatter(DevourMassToShatter(r*1.4f)*1.01f,r*1.4f)&&!DevourCanShatter(DevourMassToShatter(r*1.4f)*.9f,r*1.4f),"mass to shatter is wrong");
        CheckLife(DevourCanCover(mass,r*1.5f*.35f)&&DevourShardShare<1,"shards cannot be eaten or are worth too much");}
    // 掃除機: 本体・ホース・柄・吸い口がつながった形で、上の弧の下は空いている
    CheckLife(DevourInsideVacuum({DevourVacuumBodyX,1.9f,-2})&&DevourInsideVacuum({DevourVacuumBodyX+2.2f,1.1f,-2.4f})&&DevourInsideVacuum({DevourVacuumMouthX+2.5f,.5f,6})&&!DevourInsideVacuum({0,1,2.5f})&&!DevourInsideVacuum({DevourVacuumMouthX+2.5f,3,6}),"vacuum shape is wrong");
    // バネ: 上下の板の間に隙間の広いコイルがある (真ん中は空洞)
    {int solid=0;for(int i=0;i<200;++i)solid+=DevourInsideSpring({DevourSpringCoil,DevourSpringPlate+.01f+(DevourSpringHeight-2*DevourSpringPlate)*float(i)/200,0});
        CheckLife(DevourInsideSpring({0,.15f,0})&&DevourInsideSpring({0,DevourSpringHeight-.15f,0})&&!DevourInsideSpring({0,DevourSpringHeight*.5f,0})&&solid>40&&solid<130&&DevourSpringPitch>2*DevourSpringTube+1,"spring coil is not readable");}
    // 掃除機は前の小さい物だけ届き、後ろと大きい物は届かない
    CheckLife(DevourVacuumReaches({},{0,0,1},{0,0,20},1000,3)&&!DevourVacuumReaches({},{0,0,1},{0,0,-20},1000,3)&&!DevourVacuumReaches({},{0,0,1},{0,0,20},1000,9)&&!DevourVacuumReaches({},{0,0,1},{0,0,60},1000,3),"vacuum reach is wrong");
    // バネは溜めるほど遠く高く、どの大きさでも体より高く跳ぶ
    for(float mass:{500.f,450000.f})CheckLife(DevourSpringRange(mass,1)>DevourSpringRange(mass,.5f)&&DevourSpringRise(mass,DevourSpringMinPower)>DevourBodyRadius(mass)*5&&DevourLeapRange(mass)<DevourSpringRange(mass,DevourSpringMinPower),"spring reach is wrong");
    // 部屋の高さ: 坂はつながっていて登れる傾きで、床は 0
    // 部屋: ベッドと机は下を通れて上にも乗れる。こたつの布団と座布団はなだらかで、そのまま登れる
    CheckLife(RoomGround({0,0,-100})==0&&RoomGround({-180,1,-240})==0&&RoomGround({-180,50,-240})==56&&RoomGround({235,1,-264})==0&&RoomGround({235,80,-264})>86,"room heights are wrong");
    CheckLife(RoomGround({-120,0,176})==42&&RoomGround({-238,0,100})==0&&RoomGround({-200,0,100})>0&&RoomGround({-200,0,100})<42&&RoomTop({-180,0,-240})==56,"kotatsu quilt is not a hill");
    for(const auto& s:RoomSurfaces){if(s.hill>0&&s.base==0)CheckLife(s.h0/s.hill<.6f,"room hill is too steep");else if(s.h0!=s.h1)CheckLife(std::abs(s.h1-s.h0)/(s.foot.x1-s.foot.x0)<.6f,"room ramp is too steep");}
    // ベッドの下の収納ケースは開始の大きさでは跳べず、ケースの目標の大きさで跳べる。階段はその大きさで 1 段ずつ登れる
    {const auto& room=DevourStages()[0];CheckLife(DevourStepHeight(LifeStartMass)<RoomCaseHeight&&DevourStepHeight(DevourGoalMass(room,room.gates[0]))>RoomCaseHeight&&RoomStairRise<RoomCaseHeight,"step hop does not gate the bed");
        CheckLife(RoomGround({-200,0,-186})==RoomCaseHeight&&RoomTop({-180,0,-170})==RoomBedTop,"bed stairs or cases are wrong");}
    // 体に対して小さい物は価値が下がり、大きい物は満額。基準が 0 なら下げない
    CheckLife(DevourFoodValue(1000,5,.35f)==1&&DevourFoodValue(1000,7,.35f)>1.5f&&DevourFoodValue(1000,1,.35f)<.3f&&DevourFoodValue(1000,.01f,.35f)==.1f&&DevourFoodValue(1000,.01f,0)==1,"food value is wrong");
    // 限界ギリギリの物は大きく育つが重く、飲めない物へ速くぶつかると縮む。ゆっくりなら何も失わない
    CheckLife(DevourHeavyTime(1000,3)==0&&DevourHeavyTime(1000,7.5f)>1&&DevourBumpImpact(1,10)==0&&DevourBumpImpact(10,10)==1,"heavy or bump rule is wrong");
    CheckLife(DevourBumpLoss(LifeStartMass,1)==0&&std::abs(DevourBumpLoss(1000,1)-100)<.01f&&DevourBumpPops(1)==5&&DevourBumpStun(1)>DevourBumpStun(0),"bump loss is wrong");
    // 大きい物ほど大きく口を開ける
    CheckLife(DevourMouthScale(1000,7)>DevourMouthScale(1000,2)&&DevourMouthScale(1000,7)<1.4f,"mouth scale is wrong");
    // 大きい物ほど時間をかけて飲み込み、後から飲んだ物に埋もれるほど中心へ沈む
    CheckLife(DevourBiteTime(1000,1)<DevourBiteTime(1000,7)&&DevourBiteTime(1000,7)<.6f&&DevourMealDepth(0)>.9f&&DevourMealDepth(.3f)<DevourMealDepth(.1f)&&DevourMealDepth(5)>=.3f,"bite time or meal depth is wrong");
    // 飲み込みの重さは数か増えた割合で上がり、つぶれは重いほど大きく、揺れて収まる
    CheckLife(DevourGulpWeight(1,1,1000)==.2f&&DevourGulpWeight(5,1,1000)==1&&DevourGulpWeight(1,160,1000)==1,"gulp weight ignores the count or the share");
    CheckLife(DevourSquash(.07f,1)>DevourSquash(.07f,.2f)&&DevourSquash(.07f,1)<.4f&&DevourSquash(0,1)==0&&DevourSquash(.3f,1)<0&&DevourSquash(.7f,1)==0,"squash does not hit, rebound and settle");
    CheckLife(DevourShed(LifeStartMass,20)==0&&DevourShed(1000,10)>10&&1000-DevourShed(1000,5000)==LifeStartMass,"shedding shrinks below the start size");
    // 目標: 正面の覆える物を選び、後ろは無視し、覆えない物しか無ければ弾かれる目標にする
    std::vector<LifeCollectible> props(3);
    props[0].at={0,1.2f,7};props[0].radius=1.5f;props[1].at={0,1.2f,-5};props[1].radius=1;props[2].at={2,1.2f,5};props[2].radius=20;
    auto pick=DevourChooseTarget({0,1.25f,0},{0,0,1},LifeStartMass,props);CheckLife(pick.index==0&&pick.fits,"front coverable object is not chosen");
    props[0].active=false;pick=DevourChooseTarget({0,1.25f,0},{0,0,1},LifeStartMass,props);CheckLife(pick.index==2&&!pick.fits,"oversized object is not the bounce target");
    props[2].active=false;pick=DevourChooseTarget({0,1.25f,0},{0,0,1},LifeStartMass,props);CheckLife(pick.index<0&&pick.at.z>0,"empty pounce does not leap forward");
    // 着地した所の下だけを覆う。塀のような長い物は余裕の分まで届く
    CheckLife(DevourUnderCover({0,1.25f,0},{3,1.2f,0},LifeStartMass,1)&&!DevourUnderCover({0,1.25f,0},{9,1.2f,0},LifeStartMass,1)&&DevourUnderCover({0,1.25f,0},{9,1.2f,0},LifeStartMass,1,6),"cover reach is wrong");
    // 吸収のアニメーション: 塊とキー
    {const auto anim=DevourDefaultAnimation();
        CheckLife(anim.keys.size()>=3&&anim.keys.front().time==0&&std::abs(anim.keys.back().time-DevourPounceTime)<1e-4f,"devour animation does not span the pounce");
        for(const auto& key:anim.keys)CheckLife(key.blobs.size()==anim.names.size(),"devour key has the wrong blob count");
        // キーの上ではそのキーの値、キーの間ではなめらかに変わる (急に飛ばない)
        auto at0=DevourSampleBlobs(anim,anim.keys[2].time);CheckLife(Length(at0[0].at-anim.keys[2].blobs[0].at)<1e-4f,"sampling a key does not return the key");
        for(float t=0;t<DevourPounceTime;t+=.01f){auto a=DevourSampleBlobs(anim,t),b=DevourSampleBlobs(anim,t+.001f);
            for(size_t i=0;i<a.size();++i)CheckLife(Length(a[i].at-b[i].at)<.05f&&Length(a[i].size-b[i].size)<.05f,"devour animation jumps between keys");}
        // 粒子の割り当て: 全部の点がどれかの塊の中に入り、体の量は始めと終わりで普段の体と同じ
        std::vector<Vec> rest;for(uint32_t i=1;rest.size()<600;++i){Vec p{(Engine::FluidShape::Halton(i,2)*2-1)*2.55f,Engine::FluidShape::Halton(i,3)*1.8f,(Engine::FluidShape::Halton(i,5)*2-1)*2.55f};
            if(p.x*p.x/6.5025f+p.y*p.y/3.24f+p.z*p.z/6.5025f<=1)rest.push_back(p);}
        auto rank=DevourRanks(rest);std::vector<Vec> pose;
        // 全部の点がどれかの塊の中に入る (アメーバは仮足・波・傾きの分の箱の中)
        // (後ろの粒子は少し前の時間の形へ寄るので、今と少し前の両方の塊で確かめる)
        for(float t:{0.f,DevourWindup+DevourLeap*.5f,DevourLandAt,DevourCoverEnd}){DevourPose(anim,t,rest,rank,pose);auto blobs=DevourSampleBlobs(anim,t);
            auto late=DevourSampleBlobs(anim,std::max(0.f,t-DevourFollowLag));
            for(size_t i=0;i<late.size();++i)if(late[i].kind==DevourBlobKind::Gulp&&blobs[i].kind==DevourBlobKind::Gulp)blobs.push_back(late[i]);
            for(const auto& p:pose){bool inside=p.y<=.031f;
                // 大口は今と少し前の箱を合わせた範囲 (2 つの間の位置もとる)
                for(size_t i=0;i<late.size();++i)if(late[i].kind==DevourBlobKind::Gulp&&blobs[i].kind==DevourBlobKind::Gulp){
                    auto box=[](const DevourBlob& b,Vec& lo,Vec& hi){float g=(1+DevourGulpFlare)*(1+b.wave)*(1+.8f*b.wave);
                        lo={b.at.x-b.size.x*(g+b.twist),b.at.y-b.size.y*g,b.at.z-b.size.z};hi={b.at.x+b.size.x*(g+b.twist),b.at.y+b.size.y*g,b.at.z+b.size.z*1.35f};};
                    Vec lo0,hi0,lo1,hi1;box(blobs[i],lo0,hi0);box(late[i],lo1,hi1);
                    inside|=p.x>=std::min(lo0.x,lo1.x)-.01f&&p.x<=std::max(hi0.x,hi1.x)+.01f&&p.y>=std::min(lo0.y,lo1.y)-.01f&&p.y<=std::max(hi0.y,hi1.y)+.01f&&p.z>=std::min(lo0.z,lo1.z)-.01f&&p.z<=std::max(hi0.z,hi1.z)+.01f;}
                for(const auto& b:blobs){Vec d=p-b.at;
                    if(b.kind==DevourBlobKind::Lump){d={d.x/b.size.x,d.y/b.size.y,d.z/b.size.z};inside|=Length(d)<=1.01f;}
                    else if(b.kind==DevourBlobKind::Tail)inside|=std::abs(d.x)<=b.size.x+2*b.wave+.01f&&std::abs(d.y)<=b.size.y+.01f&&std::abs(d.z)<=b.size.z+.01f;
                    else if(b.kind==DevourBlobKind::Gulp)inside|=std::abs(d.x)<=b.size.x*((1+DevourGulpFlare)*(1+b.wave)*(1+.8f*b.wave)+b.twist)+.01f&&std::abs(d.y)<=b.size.y*(1+DevourGulpFlare)*(1+b.wave)*(1+.8f*b.wave)+.01f&&d.z>=-b.size.z-.01f&&d.z<=b.size.z*1.35f+.01f;
                    else if(b.kind==DevourBlobKind::Amoeba){float r=(1+1.25f*b.reach)*(1+b.wave)+.01f;
                        inside|=std::abs(d.x)<=b.size.x*r&&std::abs(d.y)<=b.size.y+.01f&&std::abs(d.z)<=b.size.z*(r+.5f*b.curl);}
                    else inside|=std::abs(d.x)<=b.size.x*(1+.5f*b.reach+b.wave*2)+.01f&&std::abs(d.y)<=b.size.y*(1+.5f*b.reach+b.wave*2)+.01f&&d.z>=-b.size.z-b.wave-.01f&&d.z<=b.size.z*1.25f+b.reach+b.wave+.01f;}
                CheckLife(inside,"devour particle is outside every blob");}}
        // 体はちぎれない: どの時間でも、各点のいちばん近い点までの距離が普段の体の 2.5 倍を超えない (離れた粒が無い)
        {auto gap=[&](const std::vector<Vec>& points){float worst=0;
                for(size_t i=0;i<points.size();++i){float nearest=1e9f;for(size_t j=0;j<points.size();++j)if(i!=j)nearest=std::min(nearest,Length(points[i]-points[j]));worst=std::max(worst,nearest);}
                return worst;};
            DevourPose(anim,0,rest,rank,pose);float base=gap(pose);
            for(float t=0;t<=DevourPounceTime;t+=DevourPounceTime/12){DevourPose(anim,t,rest,rank,pose);CheckLife(gap(pose)<base*2.5f,"devour body tears apart");}}
        // 口: 飛び込んだ時は体と同じくらい大きく開いて中が空き (前の方の点は軸から離れる)、後ろは尾のように細い。
        // 包み終えると唇が巻き込んで口が小さくなる
        {auto mouth=[&](float t,float& hole,float& rim,float& tail){DevourPose(anim,t,rest,rank,pose);auto blobs=DevourSampleBlobs(anim,t);const auto& b=blobs[0];
                hole=1e9f;rim=0;tail=0;
                for(const auto& p:pose){Vec d=p-b.at;float u=(d.z/b.size.z+1)*.5f,r=std::sqrt(d.x*d.x/(b.size.x*b.size.x)+d.y*d.y/(b.size.y*b.size.y));
                    if(u>.85f&&p.y>.05f){hole=std::min(hole,r);rim=std::max(rim,r);}if(u<.15f)tail=std::max(tail,std::abs(d.x)/b.size.x);}};
            float hole,rim,tail;mouth(DevourLandAt,hole,rim,tail);
            CheckLife(anim.keys.size()>2&&DevourSampleBlobs(anim,DevourLandAt)[0].kind==DevourBlobKind::Gulp,"default animation has no gaping mouth");
            CheckLife(hole>.35f,"mouth is not open at landing");CheckLife(rim>.9f&&tail<rim*.55f,"body does not flow back from a wide mouth");
            float closedHole,closedRim,closedTail;mouth(DevourCoverEnd,closedHole,closedRim,closedTail);(void)closedHole;(void)closedTail;
            CheckLife(closedRim<rim*.8f,"mouth lips do not curl in to close");
            // 口の周りだけ形を保ち、胴体は普段の液体のように柔らかい。溜めと満足の終わりは全身が柔らかい
            CheckLife(DevourStiffness(DevourLandAt,1)>3*DevourStiffness(DevourLandAt,0)&&DevourStiffness(DevourLandAt,0)<=DevourBodyStiffness+.01f,"devour mouth is not firmer than its liquid body");
            {std::vector<float> wall;DevourPose(anim,DevourLandAt,rest,rank,pose,0,&wall);size_t firm=0,softTail=0,tailCount=0;
                for(size_t i=0;i<rest.size();++i){firm+=wall[i]>.9f;if(rank[i]<.08f){++tailCount;softTail+=wall[i]<.2f;}}
                CheckLife(firm>rest.size()/3&&softTail==tailCount,"open mouth walls are not firm while the tail stays liquid");
                DevourPose(anim,0,rest,rank,pose,0,&wall);float most=0;for(float w:wall)most=std::max(most,w);
                DevourPose(anim,DevourPounceTime,rest,rank,pose,0,&wall);for(float w:wall)most=std::max(most,w);
                CheckLife(most<.01f,"devour body is stiff outside the gaping mouth");}
            // 普段の液体から形へは、今の粒子の位置からゆっくり寄せる (飛びかかりの最初に形へはまらない)
            CheckLife(DevourFormBlend(0)<1e-4f&&DevourFormBlend(DevourWindup)<.4f&&DevourFormBlend(DevourLandAt)>.999f,"devour pose snaps the liquid into shape");
            // 口の周りの壁には、どの向きにも同じくらいの粒子がある (左右や上が薄くなって穴が開かない)
            {DevourPose(anim,DevourLandAt,rest,rank,pose);auto blobs=DevourSampleBlobs(anim,DevourLandAt);const auto& b=blobs[0];std::array<int,6> sector{};
                for(const auto& p:pose){Vec d=p-b.at;if(d.z<.75f*b.size.z||d.y<0)continue;
                    float angle=std::atan2(d.y/b.size.y,d.x/b.size.x);++sector[size_t(std::clamp(int(angle/3.1415927f*6),0,5))];}
                int least=*std::min_element(sector.begin(),sector.end()),most=*std::max_element(sector.begin(),sector.end());
                CheckLife(least>0&&float(least)>.6f*float(most),"mouth wall particles bunch up and leave thin holes");}
            // 口を開けている間も、後ろ側は底を地面に着けている (浮いた管にならない)
            {DevourPose(anim,DevourLandAt,rest,rank,pose);float low=1e9f;for(size_t i=0;i<rest.size();++i)if(rank[i]<.3f)low=std::min(low,pose[i].y);
                CheckLife(low<.12f,"rear of the gaping body floats above the ground");}
            CheckLife(std::abs(DevourGulp(0,.5f,0).volume-1)<.02f,"closed mouth is not an ellipsoid volume");}
        // 最後は普段の体に戻る (全部の粒子が最初の塊へ)
        {auto blobs=DevourSampleBlobs(anim,DevourPounceTime);auto start=DevourBlobStarts(blobs);size_t inBody=0;for(size_t i=0;i<rest.size();++i)inBody+=DevourBlobOf(rank[i],start)==0;
            CheckLife(inBody>=rest.size()-2,"body does not swallow everything at the end");}
        CheckLife(std::abs(DevourVolume(anim.keys.front().blobs)/DevourBodyVolume-1)<.05f&&std::abs(DevourVolume(anim.keys.back().blobs)/DevourBodyVolume-1)<.05f,"devour animation does not start and end at the body volume");
        // 後ろの点は前の塊へ行かない (塊の並び順が後ろ → 前)
        DevourPose(anim,DevourLandAt,rest,rank,pose);size_t back=0,front=0;for(size_t i=0;i<rest.size();++i){if(rank[i]<.2f&&pose[i].z<0)++back;if(rank[i]>.97f&&pose[i].z>.8f)++front;}
        CheckLife(back>0&&front>0,"devour particles are not assigned back to front");
        // キーを足す・塊を足して消す
        auto edit=anim;int key=DevourEnsureKey(edit,.2f);CheckLife(edit.keys.size()==anim.keys.size()+1&&std::abs(edit.keys[size_t(key)].time-.2f)<1e-5f&&DevourEnsureKey(edit,.2f)==key,"adding a key failed");
        const size_t blobCount=edit.names.size();DevourAddBlob(edit,"しっぽ",{});CheckLife(edit.names.size()==blobCount+1&&edit.keys[0].blobs.size()==blobCount+1,"adding a blob failed");
        DevourRemoveBlob(edit,0);CheckLife(edit.names.size()==blobCount&&edit.names.back()=="しっぽ"&&edit.keys.back().blobs.size()==blobCount,"removing a blob failed");}
    CheckLife(DevourLeapProgress(DevourWindup)==0&&DevourLeapProgress(DevourCoverEnd)>.999f&&DevourLeapProgress(DevourLandAt)>.8f&&DevourLeapProgress(DevourWindup+DevourLeap*.5f)>.45f,"leap does not burst out and stop on the target");
    CheckLife(DevourPounceTime<.85f,"pounce is too slow to chain");
    puts("PASS devour: cover/sink sizes, growth per bite, tiers and time bonus, caps, pounce targets, cover reach and shape phases");
}
// 部屋のギミック: 形の体積・だるま落としの狙い・ボウリングのピン・道のたどり方・家具の上の面
void TestGimmick(){
    // チュートリアルの場所: 開始の位置はベッドの下、収納ケースの上・外の床・ベッドの上は外
    CheckLife(RoomUnderBed({-200,1,-245})&&!RoomUnderBed({-200,9.6f,-186})&&!RoomUnderBed({-66,9.6f,-240})&&!RoomUnderBed({-200,1,-150})&&!RoomUnderBed({-200,58,-245}),"tutorial area is the floor under the bed");
    // 体の中の物を集める芯は、散らした先も形の中にある (物が形からはみ出して形を崩さない)
    for(int shape=GimmickBallShape;shape<=GimmickHeliShape;++shape){const auto core=GimmickShapeCore(shape);const float d=core.thin*GimmickMealSpread;
        for(Vec dir:{Vec{1,0,0},Vec{-1,0,0},Vec{0,1,0},Vec{0,-1,0},Vec{0,0,1},Vec{0,0,-1}})CheckLife(GimmickInside(shape,core.center+dir*d),"gimmick meal core stays inside the shape");}
    // 形は普段の体と同じくらいの体積 (粒子の数は決まっているので、大きすぎると表面が途切れ、小さすぎると詰まる)
    const float body=4.18879f*2.55f*1.8f*2.55f;
    for(int shape=GimmickBallShape;shape<=GimmickHeliShape;++shape){const auto box=GimmickShapeBounds(shape);int inside=0;const int n=40;
        for(int i=0;i<n;++i)for(int j=0;j<n;++j)for(int k=0;k<n;++k){Vec p{(-1+2*(i+.5f)/n)*box.x,(j+.5f)/n*box.y,(-1+2*(k+.5f)/n)*box.z};if(GimmickInside(shape,p))++inside;}
        const float volume=float(inside)/float(n*n*n)*8*box.x*box.y*box.z*.5f;
        CheckLife(volume>body*.45f&&volume<body*1.4f,"gimmick shape volume is far from the body volume");}
    // ヘリコプター: 回転翼の先は軸の周りを回り (高さは変わらない)、胴は回らない。高い所から落ち始めたときだけなる
    {GimmickPose pose;pose.spin=1.5708f;Vec tip=GimmickPosePoint(GimmickHeliShape,{0,GimmickRotorY,GimmickRotorReach*.9f},pose),hull=GimmickPosePoint(GimmickHeliShape,{0,2,1},pose);
        CheckLife(std::abs(tip.y-GimmickRotorY)<1e-3f&&std::abs(tip.x)>GimmickRotorReach*.85f&&Length(hull-Vec{0,2,1})<1e-3f,"helicopter rotor does not spin about its mast");
        // こぼれた液体: 蜂蜜とのりは机の高さをゆっくり (数秒) 落ち、ジュースは 1 秒もかからない。粘性は前後の粒の速さへ寄せる
        CheckLife(SpillFallTime(SpillParamsOf(SpillKind::Honey),86.4f)>7&&SpillFallTime(SpillParamsOf(SpillKind::Glue),86.4f)>SpillFallTime(SpillParamsOf(SpillKind::Honey),86.4f)&&SpillFallTime(SpillParamsOf(SpillKind::Juice),86.4f)<1.6f,"spill viscosity does not change the fall");
        CheckLife(std::abs(SpillViscous(SpillParamsOf(SpillKind::Honey),0,5,1)-5)<.01f&&SpillViscous(SpillParamsOf(SpillKind::Juice),0,5,.1f)<.3f,"spill viscosity does not pull drops together");
        CheckLife(GimmickHeliStarts(30,1,-5,false)&&!GimmickHeliStarts(5,1,-5,false)&&!GimmickHeliStarts(30,1,-5,true)&&!GimmickHeliStarts(30,1,2,false),"helicopter starts at the wrong time");}
    // 形の姿勢: 転がっても大きさは変わらず、ドミノは前へ倒れる
    CheckLife(std::abs(Length(GimmickPosePoint(GimmickBallShape,{0,2*GimmickBallRadius,0},{3.1415927f})-Vec{0,GimmickBallRadius,0})-GimmickBallRadius)<.01f,"rolling ball changes size");
    {GimmickPose pose;pose.tip=1.5708f;Vec top=GimmickPosePoint(GimmickDominoShape,{0,GimmickDominoHeight,0},pose);CheckLife(top.z>GimmickDominoHeight*.9f&&top.y<2,"domino does not fall forward");}
    // 狙いは上下に往復し、一番下は胴の最初の輪、一番上は頭
    CheckLife(GimmickAim(0)==0&&std::abs(GimmickAim(GimmickAimPeriod*.5f)-1)<1e-4f&&GimmickAim(GimmickAimPeriod*.25f)>.45f,"aim does not sweep up and down");
    CheckLife(GimmickAimTarget(0,5)==0&&GimmickAimTarget(1,5)==5&&GimmickAimTarget(1,1)==1&&GimmickAimTarget(.2f,0)==0,"aim targets the wrong piece");
    // ボウリング: 真ん中を通せばストライク、端を通すと少ししか倒れない
    {std::vector<Vec> pins;const float spacing=9.6f,pinR=3.1f,ball=5;for(int row=0;row<4;++row)for(int k=0;k<=row;++k)pins.push_back({float(row)*spacing,0,(float(k)-float(row)*.5f)*7.68f});
        auto count=[&](float lateral){int n=0;for(bool d:GimmickPinsDown(pins,lateral,ball,pinR,spacing))n+=d?1:0;return n;};
        CheckLife(count(0)==10,"center hit is not a strike");CheckLife(count(13)<5,"edge hit knocks too many pins");CheckLife(count(30)==0,"a miss knocks pins");}
    // 道: 真上へ登る区間でも、水平の向きは前後の区間から取る
    {std::vector<Vec> path{{0,0,0},{0,0,10},{0,20,10},{0,20,20}};CheckLife(std::abs(GimmickPathLength(path)-40)<1e-4f,"path length");
        Vec d;Vec mid=GimmickPathPoint(path,20,&d);CheckLife(std::abs(mid.y-10)<1e-4f&&d.y>.99f,"path point on the wall");
        Vec h=GimmickPathHeading(path,20);CheckLife(std::abs(h.z-1)<1e-4f&&std::abs(h.y)<1e-4f,"heading on the wall is not horizontal");
        CheckLife(std::abs(GimmickPathPoint(path,99).y-20)<1e-4f,"path end");}
    // 本棚とおもちゃ箱の上に乗れる (下にいる間は床)
    CheckLife(std::abs(RoomGround({282,RoomShelfTop+1,-60})-RoomShelfTop)<.01f&&RoomGround({282,1,-60})<.01f,"bookshelf top");
    CheckLife(std::abs(RoomGround({-232,RoomToyboxTop+1,-20})-RoomToyboxTop)<.01f&&RoomGround({-232,1,-20})<.01f,"toybox top");
    CheckLife(GimmickSameColor(.92f,.28f,.24f,.92f,.28f,.24f)&&!GimmickSameColor(.92f,.28f,.24f,.3f,.55f,.92f),"paint color match");
    puts("PASS gimmick: shape volumes, poses, daruma aim, bowling pins, paths over furniture, furniture tops, paint colors");
}
int main(){try{
    TestGimmick();
    TestDevour();
    TestSignal();
    TestTireCharge();
    TestHydrantMorph();
    TestTireMorph();
    TestFacingAndCamera();
    TestGrowthHealth();
    TestCityStage();
    TestLifeContactAbsorb();
    TestLifeBatchAbsorb();
    TestLifeWeapons();
    SlimeLife life;life.absorbedMass=LifeHammerMass-LifeStartMass;auto& parent=life.bodies[0];parent.mass=100;parent.health=72;parent.maxHealth=100;parent.particles=6600;
    CheckLife(LifeTakeHit(life,0)==-1&&life.splits==0&&parent.health==72,"non-damage triggered a split");
    parent.action=LifeAction::Engulf;parent.pendingGrowth=8;
    CheckLife(LifeTakeHit(life,8)==1,"damage during engulf did not split");
    const auto& child=life.bodies[1];
    CheckLife(life.TotalMass()==100&&life.ReservedMass()==108&&std::abs(parent.health+child.health-64)<.001f,"damage split created mass or health, or lost committed growth");
    CheckLife(parent.health==44&&child.health==20,"split did not consume main-body HP");
    CheckLife(parent.particles+child.particles==6600&&child.particles==1320,"split did not preserve particle ownership");
    CheckLife(LifeTakeHit(life,8)==2&&LifeTakeHit(life,8)==3&&parent.health==0,"low-HP split cannot end the expedition");
    CheckLife(life.bodies[3].health>0&&LifeTakeHit(life,8)==-1,"living children prevented main-body death");
    SlimeLife crowded;auto& full=crowded.bodies[0];full.mass=10;full.health=full.maxHealth=40;full.particles=100;
    CheckLife(LifeTakeHit(crowded,1)==1&&full.mass<10,"small body was protected from splitting");
    for(int i=1;i<crowded.ChildLimit();++i)LifeTakeHit(crowded,1);int splits=crowded.splits;float hp=full.health;
    CheckLife(LifeTakeHit(crowded,3)==-1&&full.health==hp-3&&crowded.splits==splits,"child limit prevented damage");
    while(full.health>0)LifeTakeHit(crowded,3);
    CheckLife(full.health==0&&crowded.splits==crowded.ChildLimit(),"child limit prevented game over");
    uint32_t particles=0;for(const auto& b:crowded.bodies)particles+=b.particles;
    CheckLife(particles==100&&std::abs(crowded.TotalMass()-10)<.001f,"repeated damage lost particle ownership or mass");
    SlimeLife fatal;fatal.bodies[0].health=4;fatal.bodies[0].mass=100;fatal.bodies[0].particles=6600;
    CheckLife(LifeTakeHit(fatal,8)==-1&&fatal.bodies[0].health==0&&fatal.splits==0,"lethal damage spawned a zero-HP child");
    CheckLife(!LifeCanAbsorb(100,130),"oversized object absorbed");CheckLife(!LifeCanAbsorb(100,15,10,30),"healthy enemy absorbed");CheckLife(LifeCanAbsorb(100,15,9,30),"weakened enemy rejected");
    CheckLife(LifeInMouth({0,1,0},{0,0,1},{0,1,3},100),"forward target rejected");CheckLife(!LifeInMouth({0,1,0},{0,0,1},{0,1,-3},100),"rear target absorbed");
    CheckLife(std::abs(LifeScale(800)-std::sqrt(8.f))<.001f&&LifeScale(1200)>3.4f&&std::abs(LifeScale(12.5f)-.5f)<.001f,"growth is too small or fragments enlarged");
    for(float mass:{20.f,100.f,180.f,480.f,1200.f}){float size=LifeScale(mass);
        CheckLife(std::abs(LifeDensity(mass,6600,6600)*size*size*size-275)<.001f,"growth density does not preserve fluid particle volume");}
    SlimeLife next;next.bodies[0].mass=LifeStartMass;CheckLife(next.TotalMass()==100,"expedition did not reset size");
    // Absorption grows the body without splitting. Only actual damage adds children.
    SlimeLife growth;LifeInitializeBodies(growth,6600);
    CheckLife(growth.bodies[0].mass==100&&growth.bodies[0].particles==6600&&growth.ChildCount()==0&&growth.ChildLimit()==2,"expedition must start without a split");
    CheckLife(growth.TotalMass()==LifeInitialGroupMass,"initial group mass is not explicit");
    CheckLife(!LifeCanMorph(179.99f,LifeAction::Hammer)&&LifeCanMorph(180,LifeAction::Hammer),"hammer size gate boundary");
    CheckLife(!LifeCanMorph(299.99f,LifeAction::Sword)&&LifeCanMorph(300,LifeAction::Sword),"sword size gate boundary");
    CheckLife(!LifeCanMorph(479.99f,LifeAction::Plane)&&LifeCanMorph(480,LifeAction::Plane),"plane size gate boundary");
    CheckLife(!LifeCanMorph(1200,LifeAction::Idle)&&!LifeCanMorph(1200,LifeAction::Restore),"non-weapons accepted as morph attacks");
    float startSize=LifeScale(growth.bodies[0].mass),expectedMass=growth.TotalMass();bool mid=false,late=false;
    for(int i=0;i<30;++i){auto& collector=growth.bodies[0];collector.pendingGrowth=30;
        CheckLife(std::abs(growth.ReservedMass()-(expectedMass+30))<.01f,"pending growth no longer reserves capacity");
        LifeCompleteGrowth(growth,collector);expectedMass+=30;
        CheckLife(std::abs(growth.TotalMass()-expectedMass)<.01f&&growth.ReservedMass()==growth.TotalMass(),"growth creates or loses mass");
        uint32_t total=0;for(const auto& body:growth.bodies)total+=body.particles;CheckLife(total==6600,"growth loses fluid particles");
        CheckLife(growth.ChildCount()==0&&growth.splits==0,"absorption generated a child without damage");
        if(growth.absorbedMass>=80){mid=true;CheckLife(growth.ChildLimit()>=6,"mid-game child capacity not unlocked");}
        if(growth.absorbedMass>=380){late=true;CheckLife(growth.ChildLimit()==12,"late-game child capacity not unlocked");}}
    CheckLife(mid&&late&&LifeScale(growth.bodies[0].mass)>startSize*3&&LifeCanMorph(growth.bodies[0].mass,LifeAction::Plane),"absorption cannot reach a large late-game body and all weapons");
    CheckLife(LifeTakeHit(growth,1)==1&&growth.ChildCount()==1,"damage after growth did not split");
    float hpBefore=growth.bodies[0].health;growth.bodies[1].pendingGrowth=12;LifeCompleteGrowth(growth,growth.bodies[1]);
    CheckLife(growth.bodies[0].health==hpBefore+12&&growth.bodies[1].pendingGrowth==0&&growth.ChildCount()==1,"child collection lost growth HP or creates another child");
    SlimeLife army;LifeInitializeBodies(army,6600);army.absorbedMass=900;army.bodies[0].mass=army.bodies[0].health=army.bodies[0].maxHealth=1000;
    for(int childIndex=1;childIndex<=12;++childIndex)CheckLife(LifeTakeHit(army,1)==childIndex,"late-game damage did not fill the unlocked child slots");
    uint32_t grownParticles=0;for(const auto& body:army.bodies)grownParticles+=body.particles;
    CheckLife(army.ChildCount()==12&&army.splits==12&&grownParticles==6600,"damage army lost particles or split without a hit");
    growth.bodies[0].pendingGrowth=LifeMaxMass*2;LifeCompleteGrowth(growth,growth.bodies[0]);
    CheckLife(growth.TotalMass()==LifeMaxMass&&growth.ReservedMass()==LifeMaxMass,"growth exceeds the group mass cap");
    SlimeLife restart;LifeInitializeBodies(restart,6600);CheckLife(restart.absorbedMass==0&&restart.ChildCount()==0&&restart.ChildLimit()==2&&!LifeCanMorph(restart.bodies[0].mass,LifeAction::Hammer),"restart keeps growth/army/weapons");
    puts("PASS progression: absorption size, exact weapon gates, 2/6/12 children, child-fed growth, mass/particle conservation and restart");
    for(int r=0;r<=4;++r)for(int g=0;g<=4;++g)for(int b=0;b<=4;++b){Vec rgb{r/4.f,g/4.f,b/4.f};auto back=SlimeColor::FromRGB(rgb).ToRGB();CheckLife(Length(back-rgb)<.0001f,"HSV picker does not cover free RGB colours");}
    CheckLife(Length(SlimeColor::Legacy(2).ToRGB()-Vec{.9f,.28f,.58f})<.0001f,"legacy colour migration changed the saved tint");
    SlimeColor black{.72f,.8f,0};CheckLife(Length(black.ToRGB())==0&&black.h==.72f,"black picker loses hue");
    SlimeRecipe watery,glue,bouncy;watery.mix={80,10,10,0};glue.mix={10,10,80,0};bouncy.mix={10,80,10,0};
    CheckLife(watery.Spread()>glue.Spread()+.4f&&glue.Viscosity()>watery.Viscosity()*3,"mixture has no visible spread/viscosity difference");
    CheckLife(bouncy.Bounce()>watery.Bounce()+7&&bouncy.Cohesion()>watery.Cohesion()+35,"activator does not change elasticity");
    SlimeRecipe waterOnly,glueOnly,boraxOnly,foamOnly;waterOnly.mix={100,0,0,0};glueOnly.mix={0,0,100,0};boraxOnly.mix={0,100,0,0};foamOnly.mix={0,0,0,100};
    auto runDistance=[](const SlimeRecipe& recipe){Vec v{};float distance=0;for(int i=0;i<120;++i){v=LifeMoveVelocity(v,{1,0,0},recipe,true,1.f/120);distance+=v.x/120;}return distance;};
    CheckLife(runDistance(waterOnly)>runDistance(glueOnly)*3,"water/glue travel cannot be distinguished in actual acceleration");
    Vec coastWater{waterOnly.Speed(),0,0},coastGlue{glueOnly.Speed(),0,0};for(int i=0;i<36;++i){coastWater=LifeMoveVelocity(coastWater,{},waterOnly,true,1.f/120);coastGlue=LifeMoveVelocity(coastGlue,{},glueOnly,true,1.f/120);}
    CheckLife(coastWater.x>coastGlue.x*10&&coastWater.x>5,"watery stopping has no observable momentum tradeoff");
    CheckLife(LifeInMouth({},{0,0,1},{0,0,12.5f},100,0,waterOnly)&&!LifeInMouth({},{0,0,1},{0,0,12.5f},100,0,glueOnly),"mixture does not change real absorb reach");
    CheckLife(LifeInMouth({},{0,0,1},{4,0,1},100,0,waterOnly)&&!LifeInMouth({},{0,0,1},{4,0,1},100,0,glueOnly),"water does not widen the actual mouth");
    CheckLife(glueOnly.AbsorbDuration()<waterOnly.AbsorbDuration()*.6f&&glueOnly.AbsorbHold()>waterOnly.AbsorbHold()*2,"glue does not improve wrapping speed/hold");
    auto flight=[](const SlimeRecipe& recipe){float y=0,peak=0,v=recipe.Bounce(),time=0;do{v-=recipe.Gravity()/240;y+=v/240;peak=std::max(peak,y);time+=1.f/240;}while(y>0&&time<6);return Vec{peak,time,0};};
    CheckLife(flight(boraxOnly).x>flight(waterOnly).x*6&&boraxOnly.StompDamage()>waterOnly.StompDamage()*4&&boraxOnly.StompRadius()>waterOnly.StompRadius()*2,"activator does not create a stomp playstyle");
    CheckLife(flight(foamOnly).y>flight(waterOnly).y*3&&foamOnly.ChildDamage()/foamOnly.ChildCooldown()>waterOnly.ChildDamage()/waterOnly.ChildCooldown()*5,"foam has no airtime/child combat identity");
    float retainedWater=0,retainedGlue=0;
    for(auto recipe:{waterOnly,glueOnly,boraxOnly,foamOnly}){SlimeLife hit;hit.recipe=recipe;auto& main=hit.bodies[0];main.mass=main.health=main.maxHealth=100;main.particles=6600;
        int spawned=LifeTakeHit(hit,8);CheckLife(spawned==1&&main.health>0,"mixture hit failed to split a healthy body");auto& fragment=hit.bodies[1];
        CheckLife(std::abs(main.health+fragment.health-(100-8*recipe.DamageMultiplier()))<.001f&&std::abs(main.mass+fragment.mass-100)<.001f&&main.particles+fragment.particles==6600,"mixture split creates HP, mass or particles");
        if(recipe.Style()==0)retainedWater=main.health;if(recipe.Style()==2)retainedGlue=main.health;
        while(main.health>0)LifeTakeHit(hit,8);CheckLife(main.health==0,"mixture protects main HP at child limit");}
    CheckLife(retainedGlue>retainedWater+20,"glue versus water does not visibly change retained body HP");
    LifeBody soft;soft.mass=100;LifeTickMotion(soft,watery,{0,12,0},false,false,.05f);
    CheckLife(soft.air>.4f&&soft.aspect>1,"jump has no round/stretch response");
    float minAspect=2;for(int i=0;i<40;++i){LifeTickMotion(soft,watery,{},true,i==0,1.f/120);minAspect=std::min(minAspect,soft.aspect);}
    CheckLife(minAspect<.8f,"landing has no squash");
    for(int i=0;i<600;++i)LifeTickMotion(soft,watery,{},true,false,1.f/120);
    CheckLife(std::abs(soft.aspect-1)<.02f&&soft.air<.01f,"landing oscillation does not settle");
    auto r0=LifeRadii(soft,watery),r1=LifeRadii(soft,glue);
    CheckLife(r0.x>r1.x&&r0.y<r1.y&&std::abs(r0.x*r0.y*r0.z-r1.x*r1.y*r1.z)<.001f,"mixture changes volume rather than spread");
    SlimeRecipe pureWater;pureWater.mix={100,0,0,0};LifeBody flat;flat.mass=100;flat.aspect=.48f;auto flatRadii=LifeRadii(flat,pureWater);
    CheckLife(flatRadii.y>=.65f&&std::abs(flatRadii.x*flatRadii.y*flatRadii.z-2.55f*2.55f*1.8f)<.001f,"thin mixture collapses below the surface resolution or changes volume");
    for(int hz:{30,60,120}){LifeBody turning;turning.mass=100;float previous=0;
        for(int i=0;i<hz;++i){LifeTickMotion(turning,pureWater,{15,0,0},true,false,1.f/hz);float yaw=std::atan2(turning.liquidForward.x,turning.liquidForward.z);
            CheckLife(yaw>=previous-.0001f&&yaw-previous<=12.f/hz+.0001f,"core turn snaps or takes the long rotation");previous=yaw;}
        CheckLife(previous>1.5f&&turning.flowShape.x>.095f,"liquid shape does not follow actual travel");
        CheckLife(std::abs(turning.aspect-1)<.001f,"forward motion flattens the slime without an impact");
        CheckLife(turning.coreLead.x>.8f&&turning.fluidOffset.x<-.5f,"core does not lead the following envelope");
        float oldStrain=turning.flowShape.x;bool relaxed=false;
        for(int i=0;i<hz;++i){LifeTickMotion(turning,pureWater,{0,0,15},true,false,1.f/hz);
            CheckLife(turning.flowShape.x<=oldStrain+.0001f&&std::abs(turning.flowShape.z)<.0001f,"right-angle turn swings or hooks the old extension sideways");
            relaxed|=std::abs(turning.flowShape.x)<.04f;oldStrain=turning.flowShape.x;}
        CheckLife(relaxed&&turning.flowShape.x<-.095f,"old extension does not relax and reform in the new direction");
        auto beforeReverse=turning.flowShape;LifeTickMotion(turning,pureWater,{0,0,-15},true,false,1.f/hz);
        CheckLife(Length(turning.flowShape-beforeReverse)<.001f,"reversing travel rotates the symmetric body unnecessarily");
        for(int i=0;i<hz;++i)LifeTickMotion(turning,pureWater,{},true,false,1.f/hz);
        CheckLife(Length(turning.flowShape)<.001f,"stopped slime keeps its long moving shape");
        CheckLife(Length(turning.fluidOffset)<.001f,"outer envelope does not catch the stopped core");
        auto lead=turning.coreLead;
        LifeTickMotion(turning,pureWater,{},true,false,.1f,{1,0,0});
        CheckLife(turning.coreLead.x>lead.x+.2f&&Length(turning.fluidOffset)<.001f,"stationary look-turn moves the entire body instead of the core");
        LifeTickMotion(turning,pureWater,{10,0,10},true,false,.1f);
        CheckLife(turning.flowShape.z>.045f&&std::abs(turning.flowShape.x)<.001f,"diagonal travel does not reshape the body along the velocity");}
    LifeBody jumpingCore;jumpingCore.mass=100;
    for(int i=0;i<120;++i)LifeTickMotion(jumpingCore,pureWater,{20,0,0},false,false,1.f/120);
    CheckLife(Length(jumpingCore.coreLead-jumpingCore.fluidOffset)<1.2f,"liquid core leaves the smaller airborne body");
    auto handRest=Engine::FluidShape::Sample(nullptr,6600);auto hands=Engine::FluidShape::SampleEngulf(handRest);
    float openWidth=0,high=0,low=100,closedTip=0,openTip=0,pullZ=0,wrapZ=0;int fingers=0,palms=0,left=0,right=0;
    for(const auto& sample:hands){using namespace Engine::FluidShape;auto open=EngulfPose(sample,.3f,{0,.5f,6}),wrap=EngulfPose(sample,.75f,{0,.5f,6}),pull=EngulfPose(sample,1,{0,.5f,6});
        auto start=EngulfPose(sample,0,{0,.5f,6});CheckLife(start.x==sample.rest.x&&start.y==sample.rest.y&&start.z==sample.rest.z,"hands snap away from the starting body");
        openWidth=std::max(openWidth,std::abs(open.x));high=std::max(high,open.y);low=std::min(low,open.y);
        if(sample.part==EngulfPart::Finger&&sample.u>.8f){openTip+=std::abs(open.x);closedTip+=std::abs(wrap.x);++fingers;}
        if(sample.part==EngulfPart::Palm){wrapZ+=wrap.z;pullZ+=pull.z;++palms;if(sample.side<0)++left;else ++right;}
        for(int frame=0;frame<=100;++frame){auto p=EngulfPose(sample,float(frame)/100,{0,.5f,6});CheckLife(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&p.y>=0,"hand targets became invalid");}}
    CheckLife(hands.size()==6600&&left>400&&right>400&&fingers>100,"hands lose particle count or a palm/finger set");
    CheckLife(openWidth>4&&high-low>3,"engulf hands are still a flat horizontal stretch");
    CheckLife(closedTip<openTip*.35f&&wrapZ/palms>5.5f&&pullZ/palms<2,"fingers do not close over the target and pull it back");
    for(const char* shape:{"hammer","sword","plane"}){Engine::FluidShape::Mesh mesh(std::string("Resources/Models/SlimeLife/")+shape+".obj");
        auto points=Engine::FluidShape::Sample(&mesh,1024),same=Engine::FluidShape::Sample(&mesh,1024);CheckLife(points.size()==1024,"shape point count mismatch");
        for(size_t i=0;i<points.size();++i){auto p=points[i];CheckLife(std::isfinite(p.y),"nonfinite point");CheckLife(p.x==same[i].x&&p.z==same[i].z,"shape sampling is not deterministic");
            CheckLife(mesh.Contains({p.x/mesh.scale,p.y/mesh.scale,p.z/mesh.scale}),"point outside closed mesh");}
    }
    puts("PASS slime life: damage-only splits, HP depletion at child limit/low mass, conservation, absorption, stage reset, closed-mesh sampling");return 0;
}catch(const std::exception& error){fprintf(stderr,"FAIL %s\n",error.what());return 2;}}
