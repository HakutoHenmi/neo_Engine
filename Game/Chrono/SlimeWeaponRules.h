#pragma once
#include "SlimeLifeRules.h"

namespace Game::Chrono {
inline bool LifeWeaponForm(LifeAction form){return LifeRequiredMass(form)>0;}
inline void LifeStopWeaponAttack(LifeBody& body){bool ready=body.weapon.ready;body.weapon={};body.weapon.ready=ready;}
inline void LifeReturnToSlime(LifeBody& body){
    body.action=LifeAction::Restore;body.actionAge=0;body.target=0xffffffffu;body.weapon={};
}
inline bool LifeEnterWeapon(LifeBody& body,LifeAction form,Vec facing){
    if(!LifeCanMorph(body.mass,form)||body.down||body.health<=0)return false;
    body.action=form;body.actionAge=0;body.forward=body.previousForward=facing;body.committed=false;body.weapon={};return true;
}
inline Vec LifeWeaponFacing(Vec facing,Vec desired,float dt){
    float yaw=std::atan2(facing.x,facing.z),delta=std::remainder(std::atan2(desired.x,desired.z)-yaw,6.2831853f);
    yaw+=std::clamp(delta*(1-std::exp(-14*dt)),-6*dt,6*dt);
    return {std::sin(yaw),0,std::cos(yaw)};
}
inline float LifeWeaponDuration(LifeAction form){
    return form==LifeAction::Hammer?.58f:form==LifeAction::Sword?.64f:.95f;
}
// Morph formation and attack time are independent. A buffered press lasts only
// through the recovery window; holding the hammer repeats without extra presses.
inline bool LifeTickWeapon(SlimeLife& life,LifeBody& body,float dt,bool pressed,bool held,bool formed){
    if(!LifeWeaponForm(body.action))return false;
    auto& attack=body.weapon;attack.ready|=formed;
    attack.buffer=pressed?.24f:std::max(0.f,attack.buffer-dt);
    attack.previousAge=attack.age;
    if(attack.active){
        attack.age+=dt;
        if(attack.age>=LifeWeaponDuration(body.action))attack.active=false;
    }
    if(!attack.active&&attack.ready&&(attack.buffer>0||(body.action==LifeAction::Hammer&&held))){
        attack.active=true;attack.age=attack.previousAge=0;attack.buffer=0;
        body.attackSerial=++life.nextAttack;return true;
    }
    return false;
}
inline float LifeWeaponSwing(float age,float duration,float start,float end,float rest){
    float u=age/duration;
    return u<.18f?rest+(start-rest)*LifeSmooth(u/.18f):u<.65f?start+(end-start)*LifeSmooth((u-.18f)/.47f):end+(rest-end)*LifeSmooth((u-.65f)/.35f);
}
// Shared by GPU targets and collision capsules, in normalized mesh coordinates.
inline Vec LifeWeaponPoseAt(LifeAction form,Vec point,bool attacking,float age,float fluidAge){
    // One slow, coherent shear/breathing motion, not independent vertex noise.
    // Reciprocal cross-section scales preserve volume and keep the silhouette.
    float breath=1+.022f*std::sin(fluidAge*1.5f),cross=1+.035f*std::sin(fluidAge*1.8f-point.y*.65f);
    point.x*=cross/std::sqrt(breath);point.z/=cross*std::sqrt(breath);point.y*=breath;
    point.z+=.075f*std::sin(fluidAge*1.8f-point.y*.65f)*LifeSmooth(point.y/4);
    float duration=LifeWeaponDuration(form);
    if(form==LifeAction::Hammer){
        // The striking axis belongs in the forward/up swing plane. The source
        // mesh's head runs left/right, so turn the complete hammer by 90 degrees.
        float x=point.x;point.x=-point.z;point.z=x;
        float angle=attacking?LifeWeaponSwing(age,duration,-.55f,1.05f,-.18f):-.18f;
        float squash=attacking?1-.24f*std::exp(-std::pow((age-duration*.65f)/.045f,2.f)):1;
        if(point.y>3.5f){point.y=4.7f+(point.y-4.7f)*squash;point.x/=std::sqrt(squash);point.z/=std::sqrt(squash);}
        float y=point.y-.35f,c=std::cos(angle),s=std::sin(angle);
        return {point.x,.35f+y*c-point.z*s,y*s+point.z*c};
    }
    if(form==LifeAction::Sword){
        float yaw=attacking?LifeWeaponSwing(age,duration,-1.1f,1.15f,-.15f):-.15f;
        float pitch=0; // One horizontal sweep; no diagonal or spinning combo.
        float y=.6f+(point.y-.6f)*std::cos(pitch)-point.z*std::sin(pitch),z=(point.y-.6f)*std::sin(pitch)+point.z*std::cos(pitch);
        return {point.x*std::cos(yaw)+z*std::sin(yaw),y,-point.x*std::sin(yaw)+z*std::cos(yaw)};
    }
    return point;
}
inline Vec LifeWeaponPose(LifeAction form,Vec point,bool attacking,float age){return LifeWeaponPoseAt(form,point,attacking,age,0);}
inline Vec LifeWeaponPose(const LifeBody& body,Vec point,float age){return LifeWeaponPoseAt(body.action,point,body.weapon.active,age,body.age-(body.weapon.age-age));}
inline Vec LifeWeaponVelocity(const LifeBody& body,Vec point,float dt){
    if(dt<=1e-5f)return body.velocity;
    auto world=[](Vec p,Vec f){return Vec{f.z*p.x+f.x*p.z,p.y,-f.x*p.x+f.z*p.z};};
    Vec current=LifeWeaponPoseAt(body.action,point,body.weapon.active,body.weapon.age,body.age);
    Vec previous=LifeWeaponPoseAt(body.action,point,body.weapon.active,body.weapon.previousAge,body.age-dt);
    return body.velocity+(world(current,body.forward)-world(previous,body.previousForward))*(LifeScale(body.mass)/dt);
}
inline bool LifeWeaponHitInterval(const LifeBody& body,int pulse,float& from,float& to){
    if(!body.weapon.active||pulse!=0)return false;
    float begin=0,end=0;
    if(body.action==LifeAction::Hammer){if(pulse)return false;begin=.19f;end=.40f;}
    else if(body.action==LifeAction::Sword){begin=.14f;end=.43f;}
    else if(body.action==LifeAction::Plane){begin=.18f;end=.72f;}
    else return false;
    from=std::max(begin,body.weapon.previousAge);to=std::min(end,body.weapon.age);return to>=from;
}
inline float LifeSegmentDistance(Vec point,Vec a,Vec b){
    Vec line=b-a;float t=std::clamp(Dot(point-a,line)/std::max(1e-6f,Dot(line,line)),0.f,1.f);return Length(point-(a+line*t));
}
inline bool LifeWeaponTouches(const LifeBody& body,Vec target,float targetRadius,int pulse){
    float from,to;if(!LifeWeaponHitInterval(body,pulse,from,to))return false;
    Vec a,b;float radius;
    if(body.action==LifeAction::Hammer){a={-2.2f,4.8f,0};b={2.2f,4.8f,0};radius=1.2f;}
    else if(body.action==LifeAction::Sword){a={0,.9f,.5f};b={0,.9f,7.8f};radius=.7f;}
    else {a={0,.8f,-2};b={0,.8f,3.6f};radius=1.5f;}
    float scale=LifeScale(body.mass);int samples=std::max(1,int(std::ceil((to-from)*180)));
    for(int i=0;i<=samples;++i){float age=from+(to-from)*float(i)/samples;
        Vec origin=body.at-body.velocity*(body.weapon.age-age);origin.y-=1.25f;
        auto world=[&](Vec point){point=LifeWeaponPose(body,point,age)*scale;return origin+Vec{body.forward.z*point.x+body.forward.x*point.z,point.y,-body.forward.x*point.x+body.forward.z*point.z};};
        if(LifeSegmentDistance(target,world(a),world(b))<=radius*scale+targetRadius)return true;
    }
    return false;
}
inline float LifeWeaponDamage(const LifeBody& body){
    return (body.action==LifeAction::Hammer?24.f:body.action==LifeAction::Sword?16.f:20.f)*LifeScale(body.mass);
}
}
