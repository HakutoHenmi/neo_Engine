#include "../Game/Chrono/SlimeWeaponRules.h"
#include "../Game/Chrono/SlimeHydrantRules.h"
#include "../Game/Chrono/SlimeSignalRules.h"
#include "../Game/Chrono/CityStageRules.h"
#include "../Game/Chrono/SlimeTentacleRules.h"
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
    CityGates closed{},open{true,true};
    // Start, every spawn and the route through all three areas lie on the floor.
    CheckLife(CityWalkable(CityStart(),4,closed),"stage start is blocked");
    for(Vec at:{Vec{0,0,-640},Vec{40,0,-562},Vec{78,0,-500},Vec{78,0,-300},Vec{-100,0,-265},Vec{250,0,-265},Vec{78,0,-50},Vec{78,0,340},Vec{-250,0,650},Vec{400,0,0}})
        CheckLife(CityWalkable(at,3,open),"stage route is not walkable");
    // Closed gates block; opened gates let the body through. Walls block in every area.
    CheckLife(!CityWalkable(CityBarricadeCenter(),3,closed)&&CityWalkable(CityBarricadeCenter(),3,open),"barricade gating is wrong");
    CheckLife(!CityWalkable(CityGateCenter(),3,closed)&&CityWalkable(CityGateCenter(),3,open),"shutter gate gating is wrong");
    CheckLife(!CityWalkable({0,0,-480},3,open)&&!CityWalkable({-50,0,-650},3,open)&&!CityWalkable({0,0,-56},3,open)&&!CityWalkable({78,0,760},3,open),"walls do not block");
    Vec slid=CityConstrain({10,0,-660},{40,0,-650},4,closed);CheckLife(slid.x<22&&slid.z>-660.01f,"walls do not slide the body");
    // Pushing straight along a wall into an inner corner rounds it instead of pinning the body.
    Vec cornered=CityConstrain({21.92f,0,-580},{22.22f,0,-580},4.3f,closed);CheckLife(Length(cornered-Vec{21.92f,0,-580})>.1f,"body pinned in an inner corner");
    Vec stopped=CityConstrain({78,0,-500},{78,0,-470},4,closed);CheckLife(stopped.z<-486,"barricade lets the body pass");
    // Steps: areas in order, every enemy spawns on the floor, roles grow with the stage.
    int total=0;CityArea area=CityArea::Alley;float previousRequired=0;
    for(int step=0;step<CityStepCount;++step){auto spawns=CityStepSpawns(step);CheckLife(!spawns.empty(),"empty stage step");
        CheckLife(int(CityStepInfo(step).area)>=int(area),"stage steps go back an area");area=CityStepInfo(step).area;
        static float required=0;if(step==0||CityStepInfo(step-1).area!=area)required=0;
        for(const auto& spawn:spawns){auto enemy=CitySpawnEnemy(spawn,uint32_t(++total),step);
            CheckLife(CityWalkable(spawn.at,std::min(enemy.scale,6.f),open)&&CityAreaAt(spawn.at)==area,"enemy spawns outside its area");
            CheckLife(CityWalkable(spawn.patrol,1,open),"patrol point is off the floor");
            CheckLife(enemy.health>0&&CityEnemyRequired(enemy)>0&&CityEnemyReward(enemy)>0&&enemy.step==step,"invalid city enemy stats");
            required=std::max(required,CityEnemyRequired(enemy));}
        // Each area's largest absorb requirement exceeds every earlier area's.
        if(step+1==CityStepCount||CityStepInfo(step+1).area!=area){CheckLife(required>previousRequired,"later areas need less size");previousRequired=required;}}
    CheckLife(total==CityEnemyTotal()&&CityStepInfo(CityStepCount-1).area==CityArea::Plaza,"stage totals disagree");
    CheckLife(CityAreaAt({0,0,-640})==CityArea::Alley&&CityAreaAt({78,0,-300})==CityArea::Street&&CityAreaAt(CityCrossing())==CityArea::Plaza,"area lookup is wrong");
    // Shields block frontal non-sword hits; the sword and attacks from behind get through.
    SwarmEnemy shield;shield.kind=uint8_t(CityEnemy::Shield);shield.at={0,0,0};shield.heading={0,0,-1};
    CheckLife(CityShieldBlocks(shield,{0,0,-5},false)&&!CityShieldBlocks(shield,{0,0,-5},true)&&!CityShieldBlocks(shield,{0,0,5},false),"shield facing rules are wrong");
    // A cannon shell lands on its target.
    Vec from{0,5,0},target{30,0,40},v=CityShellVelocity(from,target,1.7f),at=from;for(int i=0;i<1700;++i){v.y-=30*.001f;at=at+v*.001f;}
    CheckLife(Length(at-target)<.2f,"cannon shell misses its target");
    puts("PASS city stage: walkable route, wall sliding, barricade/shutter gate gating, 8 steps in area order, spawn placement, shields and shells");
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
int main(){try{
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
