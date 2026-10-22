#pragma once
#include "SlimeWeaponRules.h"
namespace Game::Chrono {
inline constexpr float HydrantAttackWater=1.1f,HydrantGuardWater=1.2f;
// Match FluidSimCS's water gravity and launch speed. Damage still comes from
// the simulated droplets; this only chooses their initial velocity.
inline constexpr float HydrantJetSpeed=72.f,HydrantGravity=20.f,HydrantAimRange=100.f;
struct HydrantAim {
    Vec target{},velocity{},impact{};
    float distance=0,flight=0;
    bool active=false,reachable=false,blocked=false,enemy=false;
};
inline Vec LifeHydrantCameraOffset(float mass,Vec forward){
    float scale=LifeScale(mass);Vec right{forward.z,0,-forward.x};
    return (Vec{0,5.7f,0}+right*3.8f-forward*9.f)*scale+Vec{0,-1.25f,0};
}
inline bool LifeHydrantLaunch(Vec origin,Vec target,Vec& velocity,float& flight){
    Vec delta=target-origin;float distance2=Dot(delta,delta);
    float a=HydrantJetSpeed*HydrantJetSpeed-HydrantGravity*delta.y;
    float discriminant=a*a-HydrantGravity*HydrantGravity*distance2;
    if(distance2<1e-6f){velocity={0,0,HydrantJetSpeed};flight=0;return true;}
    if(discriminant<0||a<=0){velocity=Unit(delta)*HydrantJetSpeed;flight=std::sqrt(distance2)/HydrantJetSpeed;return false;}
    flight=std::sqrt(2*distance2/(a+std::sqrt(discriminant)));
    velocity=(delta+Vec{0,.5f*HydrantGravity*flight*flight,0})*(1/flight);
    return true;
}
inline Vec LifeHydrantTrajectory(Vec origin,Vec velocity,float time){return origin+velocity*time+Vec{0,-.5f*HydrantGravity*time*time,0};}
inline bool LifeInsideHydrant(Vec p){
    float radial=p.x*p.x+p.z*p.z;
    bool trunk=radial<=1.15f*1.15f&&p.y>=.35f&&p.y<=4;
    bool base=radial<=1.75f*1.75f&&p.y>=0&&p.y<=.5f;
    bool cap=radial+(p.y-3.9f)*(p.y-3.9f)<=1.2f*1.2f&&p.y>=3.8f;
    bool arms=std::abs(p.x)<=2.2f&&(p.y-2.9f)*(p.y-2.9f)+p.z*p.z<=.45f*.45f;
    bool nozzle=p.x*p.x+(p.y-3.1f)*(p.y-3.1f)<=.5f*.5f&&p.z>=1&&p.z<=2.2f;
    return trunk||base||cap||arms||nozzle;
}
inline Vec LifeHydrantNozzle(const LifeBody& body){return body.at+Vec{0,-1.25f+3.1f*LifeScale(body.mass),0}+body.forward*(2.3f*LifeScale(body.mass));}
// Shared by gameplay and numerical tests. Only airborne, moving, simulated
// water exerts pressure; a puddle cannot damage an enemy or become a shield.
inline bool LifeHydrantLiveWater(float type,float contactAge,Vec velocity){
    return type>1.05f&&type<1.25f&&contactAge<.12f&&Length(velocity)>4;
}
inline bool LifeHydrantWaterTouches(Vec water,Vec velocity,Vec point,float radius,float dt){
    return LifeSegmentDistance(point,water-velocity*std::clamp(dt,0.f,.05f),water)<=radius;
}
inline float LifeWaterSegmentDistance(Vec a,Vec b,Vec c,Vec d){
    Vec u=b-a,v=d-c,w=a-c;float aa=Dot(u,u),bb=Dot(u,v),cc=Dot(v,v),dd=Dot(u,w),ee=Dot(v,w);
    if(aa<1e-6f)return LifeSegmentDistance(a,c,d);
    if(cc<1e-6f)return LifeSegmentDistance(c,a,b);
    float den=aa*cc-bb*bb,s=den>1e-6f?std::clamp((bb*ee-cc*dd)/den,0.f,1.f):0;
    float t=(bb*s+ee)/cc;
    if(t<0){t=0;s=std::clamp(-dd/aa,0.f,1.f);}else if(t>1){t=1;s=std::clamp((bb-dd)/aa,0.f,1.f);}
    return Length((a+u*s)-(c+v*t));
}
}
