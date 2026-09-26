#pragma once
#include <algorithm>
#include <cmath>

// Engine-independent rules shared by gameplay and the deterministic tests.
namespace Game::Chrono {
struct Vec {
    float x = 0, y = 0, z = 0;
    Vec operator+(Vec b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec operator*(float s) const { return {x*s,y*s,z*s}; }
};
inline float Dot(Vec a, Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float Length(Vec a) { return std::sqrt(Dot(a,a)); }
inline Vec Unit(Vec a) { float n=Length(a); return n>0.00001f ? a*(1/n) : Vec{}; }
inline Vec Lerp(Vec a, Vec b, float t) { return a+(b-a)*t; }
// Exponential damping is frame-rate independent; the hard lag limit prevents
// a fast pull from overtaking the camera while still cushioning background motion.
inline Vec Follow(Vec current,Vec goal,float dt,float maxLag) {
    Vec next=Lerp(current,goal,1-std::exp(-14*dt));
    Vec lag=goal-next;float length=Length(lag);
    return length>maxLag?goal-lag*(maxLag/length):next;
}
inline float PullSpeed(float remaining) {return 30+85*std::clamp(remaining/7.0f,0.0f,1.0f);}
inline float Gravity(float vy,bool hanging) {return hanging?6.0f:std::abs(vy)<2.2f?17.0f:vy<0?38.0f:30.0f;}
inline float LandingSquash(float age,float strength) {
    return 1-0.42f*strength*std::exp(-age*5)*std::sin(std::min(age*16,3.141593f));
}
struct Bounds { Vec min, max; };
// Segment against an expanded box. Returns the earliest fraction, including
// starts inside. A finite body therefore cannot tunnel through a thin wall.
inline bool Sweep(Vec p, Vec delta, Bounds b, Vec half, float& fraction, Vec& normal) {
    b.min=b.min-half; b.max=b.max+half;
    float lo=0, hi=1; Vec hit{};
    const float origins[]={p.x,p.y,p.z}, dirs[]={delta.x,delta.y,delta.z};
    const float mins[]={b.min.x,b.min.y,b.min.z}, maxs[]={b.max.x,b.max.y,b.max.z};
    for(int i=0;i<3;++i) {
        if(std::abs(dirs[i])<1e-7f) {
            if(origins[i]<=mins[i] || origins[i]>=maxs[i]) return false;
            continue;
        }
        float a=(mins[i]-origins[i])/dirs[i], c=(maxs[i]-origins[i])/dirs[i];
        float sign=-1; if(a>c) {std::swap(a,c); sign=1;}
        if(a>=lo) {lo=a; hit={}; if(i==0)hit.x=sign; if(i==1)hit.y=sign; if(i==2)hit.z=sign;}
        hi=std::min(hi,c); if(lo>hi) return false;
    }
    if(hi<=0 || lo>1) return false;
    fraction=std::max(0.0f,lo); normal=hit; return true;
}
inline bool RaySphere(Vec origin, Vec direction, Vec center, float radius, float maxDistance, float& t) {
    Vec oc=origin-center; float b=Dot(oc,direction), c=Dot(oc,oc)-radius*radius;
    float disc=b*b-c; if(disc<0) return false;
    t=std::max(0.0f,-b-std::sqrt(disc));
    return -b+std::sqrt(disc)>=0 && t<=maxDistance;
}
struct Flow {
    int combo=0, maxCombo=0, kills=0;
    float grace=0, remaining=0, scale=1, slowSeconds=0;
    void Tick(float dt) {
        grace=std::max(0.0f,grace-dt); if(grace==0)combo=0;
        if(remaining>0)slowSeconds+=std::min(dt,remaining);
        remaining=std::max(0.0f,remaining-dt);
        float goal=remaining>0 ? std::max(0.25f,0.45f-0.04f*static_cast<float>(kills-1)) : 1.0f;
        scale+=(goal-scale)*(1-std::exp(-16*dt));
        if(remaining==0)kills=0;
    }
    void Hit(bool defeated) {
        ++combo; maxCombo=std::max(maxCombo,combo); grace=2.0f;
        if(defeated && combo>=3) {++kills; remaining=std::min(3.0f,remaining+1.6f);}
    }
    void Break() {combo=0;grace=0;}
};
inline float Reach(float mass) {return 12.0f+6.0f*std::clamp(mass/100.0f,0.0f,2.0f);}
inline float BodyScale(float mass) {return std::cbrt(std::clamp(mass,1.0f,200.0f)/100.0f);}
inline float AimTimeScale(float current, bool aiming, float dt) {
    return current+((aiming?0.20f:1.0f)-current)*(1-std::exp(-(aiming?24.0f:10.0f)*dt));
}
inline float WorldTimeScale(float comboScale,float aimScale) {return std::min(comboScale,aimScale);}
inline float StrainCost(float mass,float distance) {return (2.0f+distance*0.35f)*std::max(0.6f,mass/100.0f);}
inline bool Overload(float& instability, float& mass) {
    if(instability<100)return false;
    instability=40; mass=std::max(1.0f,mass-15); return true;
}
struct Stats {
    float seconds=0, maxMass=100, absorbed=0;
    int hitsTaken=0, collapses=0, counters=0, opportunities=0, projectiles=0;
    int airKills=0,maxAirKills=0, manual=0, automatic=0;
    // The rank deliberately has no uncapped kill/slow-time reward.
    float Score(int maxCombo) const {
        float chain=35*std::min(1.0f,static_cast<float>(maxCombo)/12);
        float counter=35*std::min(1.0f,static_cast<float>(counters)/3);
        float speed=20*std::clamp((360-seconds)/240,0.0f,1.0f);
        float control=std::max(0.0f,10-0.8f*static_cast<float>(hitsTaken)-2*static_cast<float>(collapses));
        return chain+counter+speed+control;
    }
    const char* Rank(int maxCombo) const {float s=Score(maxCombo);return s>=85?"S":s>=65?"A":s>=40?"B":"C";}
};
}
