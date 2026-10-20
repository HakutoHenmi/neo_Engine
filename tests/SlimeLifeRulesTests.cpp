#include "../Game/Chrono/SlimeWeaponRules.h"
#include "../Engine/FluidMorphShape.h"
#include "../Engine/SlimeEngulfShape.h"
#include <cstdio>
#include <stdexcept>
using namespace Game::Chrono;
void CheckLife(bool value,const char* message){if(!value)throw std::runtime_error(message);}
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
int main(){try{
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
    CheckLife(growth.bodies[0].health==hpBefore&&growth.bodies[1].pendingGrowth==0&&growth.ChildCount()==1,"child collection heals or creates another child");
    SlimeLife army;LifeInitializeBodies(army,6600);army.absorbedMass=900;army.bodies[0].mass=army.bodies[0].health=army.bodies[0].maxHealth=1000;
    for(int childIndex=1;childIndex<=12;++childIndex)CheckLife(LifeTakeHit(army,1)==childIndex,"late-game damage did not fill the unlocked child slots");
    uint32_t grownParticles=0;for(const auto& body:army.bodies)grownParticles+=body.particles;
    CheckLife(army.ChildCount()==12&&army.splits==12&&grownParticles==6600,"damage army lost particles or split without a hit");
    growth.bodies[0].pendingGrowth=2000;LifeCompleteGrowth(growth,growth.bodies[0]);
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
    CheckLife(LifeInMouth({},{0,0,1},{0,0,6.5f},100,0,waterOnly)&&!LifeInMouth({},{0,0,1},{0,0,6.5f},100,0,glueOnly),"mixture does not change real absorb reach");
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
