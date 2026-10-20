#pragma once
#include "ChronoRules.h"
#include <array>
#include <cstdint>
#include <vector>
#include <string>
namespace Game::Chrono {
inline constexpr float LifeStartMass=100,LifeMaxMass=1200;
inline constexpr float LifeInitialGroupMass=LifeStartMass;
inline constexpr float LifeHammerMass=180,LifeSwordMass=300,LifePlaneMass=480;
inline constexpr size_t LifeBodyCount=13;
inline constexpr int LifeEnemyCount=18;
enum class LifeAction {Idle,Engulf,Hammer,Sword,Plane,Restore};
inline float LifeRequiredMass(LifeAction action){
    return action==LifeAction::Hammer?LifeHammerMass:action==LifeAction::Sword?LifeSwordMass:action==LifeAction::Plane?LifePlaneMass:0;
}
inline bool LifeCanMorph(float mass,LifeAction action){
    return (action==LifeAction::Hammer||action==LifeAction::Sword||action==LifeAction::Plane)&&mass>=LifeRequiredMass(action);
}
struct SlimeColor {
    float h=.26190476f,s=.875f,v=.8f;
    Vec ToRGB()const {
        float hue=(h-std::floor(h))*6,sv=std::clamp(s,0.f,1.f),value=std::clamp(v,0.f,1.f),f=hue-std::floor(hue);
        float p=value*(1-sv),q=value*(1-sv*f),t=value*(1-sv*(1-f));
        switch(int(hue)%6){case 0:return {value,t,p};case 1:return {q,value,p};case 2:return {p,value,t};case 3:return {p,q,value};case 4:return {t,p,value};default:return {value,p,q};}
    }
    static SlimeColor FromRGB(Vec rgb){
        rgb={std::clamp(rgb.x,0.f,1.f),std::clamp(rgb.y,0.f,1.f),std::clamp(rgb.z,0.f,1.f)};
        float hi=std::max({rgb.x,rgb.y,rgb.z}),lo=std::min({rgb.x,rgb.y,rgb.z}),d=hi-lo;SlimeColor out;out.v=hi;out.s=hi>0?d/hi:0;out.h=0;
        if(d>1e-6f){out.h=(hi==rgb.x?(rgb.y-rgb.z)/d:hi==rgb.y?2+(rgb.z-rgb.x)/d:4+(rgb.x-rgb.y)/d)/6;out.h-=std::floor(out.h);}return out;
    }
    static SlimeColor Legacy(int index){const Vec colors[]={{.4f,.8f,.1f},{.12f,.65f,.95f},{.9f,.28f,.58f},{.65f,.32f,.95f},{.95f,.64f,.12f},{.25f,.85f,.65f}};return FromRGB(colors[std::clamp(index,0,5)]);}
};
struct SlimeRecipe {
    std::array<int,4> mix{40,35,25,0};
    SlimeColor color;
    float Ratio(int i)const {return float(mix[size_t(i)])/float(std::max(1,mix[0]+mix[1]+mix[2]+mix[3]));}
    float Speed()const {return 8+20*Ratio(0)-2*Ratio(2)+6*Ratio(3);}
    float Acceleration()const {return 7+13*Ratio(0)-2*Ratio(2)+3*Ratio(1);}
    float Braking()const {return 10-6*Ratio(0)+5*Ratio(2);}
    float Gravity()const {return 30-16*Ratio(3);}
    float Bounce()const {return 8+16*Ratio(1)+7*Ratio(3);}
    float Rebound()const {return Ratio(1)>=.5f?3+4*Ratio(1):0;}
    float AbsorbDuration()const {return 1.2f-.35f*Ratio(0)-.75f*Ratio(2)+.15f*Ratio(1);}
    float AbsorbRange()const {return 4.2f*(1+.85f*Ratio(0));}
    float AbsorbDot()const {return .35f-.23f*Ratio(0);}
    float AbsorbHold()const {return 1+2*Ratio(2);}
    float StompDamage()const {return 12+42*Ratio(1);}
    float StompRadius()const {return 3.2f+4.8f*Ratio(1);}
    float DamageMultiplier()const {return 1+.35f*(Ratio(0)-.4f)+.8f*Ratio(3)-.4f*(Ratio(2)-.25f);}
    float SplitCap()const {return 20+12*(Ratio(0)-.4f)+10*Ratio(3)-8*(Ratio(2)-.25f);}
    float SplitRatio()const {return .25f+.18f*(Ratio(0)-.4f)+.12f*Ratio(3)-.1f*(Ratio(2)-.25f);}
    float ChildSpeed()const {return Speed()*(1+1.3f*Ratio(3));}
    float ChildCooldown()const {return .95f/(1+2.5f*Ratio(3));}
    float ChildDamage()const {return 6+5*Ratio(3);}
    int Style()const {int best=0;for(int i=1;i<4;++i)if(mix[size_t(i)]>mix[size_t(best)])best=i;return Ratio(best)>=.5f?best:-1;}
    const char* StyleName()const {switch(Style()){case 0:return "さらさら回収型";case 1:return "ぷるぷる踏みつけ型";case 2:return "ねばねば本体型";case 3:return "ふわふわ群れ型";default:return "バランス型";}}
    const char* Strength()const {switch(Style()){case 0:return "得意：高速移動・広い取り込み";case 1:return "得意：高いジャンプ・広く強い踏みつけ";case 2:return "得意：素早く包む・被弾時に本体を保つ";case 3:return "得意：長い滞空・子が速く何度も攻撃";default:return "得意：移動・取り込み・踏みつけを両立";}}
    const char* Weakness()const {switch(Style()){case 0:return "注意：止まりにくい・被弾で大きくちぎれる";case 1:return "注意：取り込みが遅い・着地で一度弾む";case 2:return "注意：移動と切り返しが重い・子は小さい";case 3:return "注意：本体が傷つきやすく吹き飛ばされる";default:return "材料を半分以上にすると個性が強くなる";}}
    float Spread()const {return 1+.6f*Ratio(0)-.25f*Ratio(1)-.15f*Ratio(2)+.3f*Ratio(3);}
    float Viscosity()const {return 3+105*Ratio(2)+28*Ratio(1);}
    float Damping()const {return 1.5f+7*Ratio(2)+2*Ratio(1);}
    float Cohesion()const {return 30+65*Ratio(1)+25*Ratio(2);}
    float SurfaceTension()const {return 8+45*Ratio(2)+18*Ratio(1);}
    float Slosh()const {return 1.2f+1.6f*Ratio(0)-.65f*Ratio(2)-.7f*Ratio(1);}
};
struct LifeWeaponAttack {
    float age=0,previousAge=0,buffer=0;
    bool active=false,ready=false;
};
struct LifeBody {
    Vec at{},forward{0,0,1},liquidForward{0,0,1},engulfPoint{},velocity{};
    Vec previousForward{0,0,1}; // Previous frame's weapon basis, for angular velocity.
    // World-space horizontal strain (xx-zz, xz). Blending shape, rather than
    // rotating a long body, lets the old extension relax into the new direction.
    Vec flowShape{};
    Vec fluidOffset{},coreLead{},coreVelocity{};
    float aspect=1,aspectVelocity=0,air=0,motion=0;
    float mass=0,health=0,maxHealth=0,cooldown=0,age=0,actionAge=0,pendingGrowth=0;
    uint32_t target=0xffffffffu;
    LifeAction action=LifeAction::Idle;
    uint64_t attackSerial=0;
    LifeWeaponAttack weapon;
    uint32_t particles=0;
    bool down=false,committed=false,reboundAvailable=false;
};
// Stronger growth above the starting size; fragments retain their small volume.
inline float LifeScale(float mass){float ratio=std::max(1.f,mass)/LifeStartMass;return ratio>1?std::sqrt(ratio):std::cbrt(ratio);}
inline float LifeDensity(float mass,uint32_t particles,uint32_t totalParticles){
    float scale=LifeScale(mass);return 275.f*float(particles)/(float(std::max(1u,totalParticles))*scale*scale*scale);
}
inline bool LifeCanAbsorb(float mass,float required,float health=0,float maxHealth=1){
    return mass>=required&&health<=maxHealth*.3f;
}
inline float LifeSmooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
inline float LifeDistance(Vec a,Vec b){Vec d=a-b;d.y=0;return Length(d);}
inline Vec LifeMoveVelocity(Vec velocity,Vec wish,const SlimeRecipe& recipe,bool grounded,float dt,float speedScale=1){
    float rate=Length(wish)>.05f?recipe.Acceleration():recipe.Braking();rate*=grounded?1.f:.65f+.6f*recipe.Ratio(3);
    float blend=1-std::exp(-rate*dt);velocity.x+=(wish.x*recipe.Speed()*speedScale-velocity.x)*blend;velocity.z+=(wish.z*recipe.Speed()*speedScale-velocity.z)*blend;return velocity;
}
inline void LifeTickMotion(LifeBody& b,const SlimeRecipe& recipe,Vec velocity,bool grounded,bool landed,float dt,Vec facing={}){
    b.velocity=velocity;
    Vec planar{velocity.x,0,velocity.z};float speed=Length(planar);
    if(Length(facing)<.1f)facing=speed>.25f?Unit(planar):b.forward;
    if(Length(facing)>.1f){float yaw=std::atan2(b.liquidForward.x,b.liquidForward.z),goal=std::atan2(facing.x,facing.z);
        float delta=std::remainder(goal-yaw,6.2831853f),rate=14+4*recipe.Ratio(2);
        float step=std::clamp(delta*(1-std::exp(-rate*dt)),-12*dt,12*dt);yaw+=step;b.liquidForward={std::sin(yaw),0,std::cos(yaw)};}
    float radius=(2.55f*recipe.Spread()*(1-b.air)+1.8f*b.air)*LifeScale(b.mass);
    // Only the controller/core leads. The soft envelope trails independently;
    // this never translates or rotates the simulated outer particles.
    b.fluidOffset=(b.fluidOffset-planar*dt)*std::exp(-(10+8*recipe.Ratio(2)+5*recipe.Ratio(1))*dt);
    float lag=Length(b.fluidOffset),limit=radius*.32f;
    if(lag>limit)b.fluidOffset=b.fluidOffset*(limit/lag);
    Vec previousLead=b.coreLead;
    b.coreLead=Lerp(b.coreLead,b.liquidForward*(radius*(.16f+.08f*std::min(1.f,speed/12))),1-std::exp(-18*dt));
    b.coreVelocity=velocity+(dt>1e-5f?(b.coreLead-previousLead)*(1/dt):Vec{});
    Vec strain{};
    if(speed>.25f){Vec direction=planar*(1/speed);float amount=(.08f+.02f*recipe.Ratio(0)) * std::min(1.f,speed/12) * (grounded?1.f:.25f);
        strain={amount*(direction.x*direction.x-direction.z*direction.z),0,amount*2*direction.x*direction.z};}
    b.flowShape=Lerp(b.flowShape,strain,1-std::exp(-(7+2*recipe.Ratio(0)+3*recipe.Ratio(2))*dt));
    if(landed)b.aspectVelocity-=6*recipe.Slosh();
    // Travel stretches horizontally; only impacts squash the crown vertically.
    float target=grounded?1:1+std::min(.45f,std::abs(velocity.y)*.028f)*recipe.Slosh();
    int steps=std::max(1,int(std::ceil(dt*120)));float h=dt/steps;
    for(int i=0;i<steps;++i){b.aspectVelocity+=((35+80*recipe.Ratio(1))*(target-b.aspect)-(3+7*recipe.Ratio(2)+3*recipe.Ratio(1))*b.aspectVelocity)*h;b.aspect=std::clamp(b.aspect+b.aspectVelocity*h,.48f,1.65f);}
    b.air+=((grounded?0.f:1.f)-b.air)*(1-std::exp(-14*dt));
    b.motion+=(std::min(1.f,Length(Vec{velocity.x,0,velocity.z})/18)-b.motion)*(1-std::exp(-7*dt));
}
inline Vec LifeRadii(const LifeBody& b,const SlimeRecipe& recipe){
    float spread=recipe.Spread(),s=LifeScale(b.mass),cross=1/std::sqrt(b.aspect);
    Vec base=Lerp({2.55f*spread,1.8f/(spread*spread),2.55f*spread},{1.8f,1.8f,1.8f},b.air);
    float y=std::max(.65f*s,base.y*b.aspect*s),width=std::sqrt(base.y*b.aspect*s/y);
    return {base.x*cross*s*width,y,base.z*cross*s*width};
}
inline bool LifeInMouth(Vec from,Vec forward,Vec target,float mass,float extra=0,const SlimeRecipe& recipe=SlimeRecipe{}){
    Vec delta=target-from;float height=std::abs(delta.y);delta.y=0;
    float range=recipe.AbsorbRange()*LifeScale(mass)+extra;
    return height<3*LifeScale(mass)&&Length(delta)<=range&&(Length(delta)<.5f||Dot(Unit(delta),forward)>recipe.AbsorbDot());
}
enum class SlimeTrial {None,Move,Absorb,Hit};
struct LifeCollectible {Vec at{};float required=20,reward=12;bool active=true;uint32_t visual=0xffffffffu;};
struct SlimeLife {
    std::array<LifeBody,LifeBodyCount> bodies{};
    std::vector<LifeCollectible> props;
    SlimeRecipe recipe;
    LifeBody preview;
    LifeBody previewChild;
    SlimeTrial trial=SlimeTrial::None;
    float trialAge=0,trialDistance=0,trialMessageAge=0;
    float previewImpact=0;
    std::string trialMessage;
    float labAge=0,previewJumpClock=0,previewMix=0;
    bool previewAuto=true,previewPoke=false,previewJump=false;
    int colorDrag=0;
    LifeAction form=LifeAction::Hammer;
    float age=0,messageAge=0,stompCooldown=0,contactCooldown=0,morphAge=0;
    float absorbedMass=0,tackleTime=0;
    int absorbed=0,childHits=0,stomps=0,hammerHits=0,splits=0;
    uint64_t revision=1;
    uint64_t nextAttack=1;
    bool lab=true,previousAbsorb=false,previousFire=false,previousReturn=false;
    bool previousLab=false,stomping=false,deployRequested=false;
    std::string message;
    float TotalMass()const {float n=0;for(const auto& b:bodies)n+=b.mass;return n;}
    float ReservedMass()const {float n=TotalMass();for(const auto& b:bodies)n+=b.pendingGrowth;return n;}
    int ChildLimit()const {float growth=LifeStartMass+absorbedMass;return growth>=LifePlaneMass?12:growth>=LifeHammerMass?6:2;}
    int ChildCount()const {int count=0;for(size_t i=1;i<bodies.size();++i)if(bodies[i].mass>0)++count;return count;}
};
inline void LifeInitializeBodies(SlimeLife& life,uint32_t particles){
    life.bodies={};life.absorbedMass=0;
    auto& parent=life.bodies[0];parent.mass=parent.health=parent.maxHealth=LifeStartMass;
    parent.particles=particles;
}
// Absorption by either the player or a child grows the main body. Growth is
// committed only after restoration; pending rewards still count towards the cap.
inline float LifeCompleteGrowth(SlimeLife& life,LifeBody& collector){
    float amount=std::clamp(collector.pendingGrowth,0.f,std::max(0.f,LifeMaxMass-life.TotalMass()));collector.pendingGrowth=0;
    auto& parent=life.bodies[0];parent.mass+=amount;parent.maxHealth+=amount;life.absorbedMass+=amount;
    return amount;
}
// Every accepted hit costs HP, including at the child limit. A living fragment
// takes cohesion out of the main body; there is no minimum-HP protection.
inline int LifeTakeHit(SlimeLife& life,float damage){
    auto& parent=life.bodies[0];
    if(damage<=0||parent.health<=0||parent.down)return -1;
    parent.health=std::max(0.f,parent.health-damage*life.recipe.DamageMultiplier());
    if(parent.health<=0||parent.mass<=0||parent.particles<2)return -1;
    for(size_t i=1;i<=size_t(life.ChildLimit());++i)if(life.bodies[i].mass<=0){
        float mass=std::min(life.recipe.SplitCap(),parent.mass*life.recipe.SplitRatio()),fraction=mass/parent.mass;
        auto& child=life.bodies[i];child={};child.at=parent.at;child.forward=parent.forward;child.mass=mass;
        child.maxHealth=parent.maxHealth*fraction;child.health=std::min(parent.health,child.maxHealth);
        child.particles=std::clamp(uint32_t(std::round(float(parent.particles)*fraction)),1u,parent.particles-1);
        parent.particles-=child.particles;parent.mass-=mass;parent.health-=child.health;parent.maxHealth-=child.maxHealth;
        ++life.splits;return int(i);
    }
    return -1;
}
}
