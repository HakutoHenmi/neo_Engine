#include "../Game/Chrono/SlimeLifeRules.h"
#include "../Engine/FluidMorphShape.h"
#include "../Engine/SlimeEngulfShape.h"
#include <cstdio>
#include <stdexcept>
using namespace Game::Chrono;
void CheckLife(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    SlimeLife life;auto& parent=life.bodies[0];parent.mass=100;parent.health=72;parent.maxHealth=100;parent.particles=6600;
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
    LifeTakeHit(crowded,1);LifeTakeHit(crowded,1);int splits=crowded.splits;float hp=full.health;
    CheckLife(LifeTakeHit(crowded,3)==-1&&full.health==hp-3&&crowded.splits==splits,"child limit prevented damage");
    while(full.health>0)LifeTakeHit(crowded,3);
    CheckLife(full.health==0&&crowded.splits==3,"child limit prevented game over");
    uint32_t particles=0;for(const auto& b:crowded.bodies)particles+=b.particles;
    CheckLife(particles==100&&std::abs(crowded.TotalMass()-10)<.001f,"repeated damage lost particle ownership or mass");
    SlimeLife fatal;fatal.bodies[0].health=4;fatal.bodies[0].mass=100;fatal.bodies[0].particles=6600;
    CheckLife(LifeTakeHit(fatal,8)==-1&&fatal.bodies[0].health==0&&fatal.splits==0,"lethal damage spawned a zero-HP child");
    CheckLife(!LifeCanAbsorb(100,130),"oversized object absorbed");CheckLife(!LifeCanAbsorb(100,15,10,30),"healthy enemy absorbed");CheckLife(LifeCanAbsorb(100,15,9,30),"weakened enemy rejected");
    CheckLife(LifeInMouth({0,1,0},{0,0,1},{0,1,3},100),"forward target rejected");CheckLife(!LifeInMouth({0,1,0},{0,0,1},{0,1,-3},100),"rear target absorbed");
    CheckLife(std::abs(LifeScale(800)-2)<.001f,"mass scale is not cubic");
    SlimeLife next;next.bodies[0].mass=LifeStartMass;CheckLife(next.TotalMass()==100,"expedition did not reset size");
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
