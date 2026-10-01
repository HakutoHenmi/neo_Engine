#pragma once
#include "ChronoRules.h"
#include <map>

namespace Game::Chrono {
// A viscous, shallow liquid constrained to a fixed gameplay spine. Neighbor
// pressure propagates waves; adhesion bounds sideways motion and thickness.
struct LiquidTrail {
    struct Sample {Vec anchor;float height=0,velocity=0,side=0,sideVelocity=0;};
    std::vector<Sample> samples;
    float time=0;
    void Sync(const std::vector<Vec>& points){
        if(points.empty()){samples.clear();return;}
        if(samples.size()==points.size()){
            bool identical=true;for(size_t i=0;i<points.size();++i)if(!DomainSame(samples[i].anchor,points[i])){identical=false;break;}
            if(identical)return;
        }
        std::map<std::array<float,3>,Sample> old;
        for(const auto& s:samples)old.emplace(std::array<float,3>{s.anchor.x,s.anchor.y,s.anchor.z},s);
        samples.clear();samples.reserve(points.size());
        for(auto point:points){auto found=old.find({point.x,point.y,point.z});
            samples.push_back(found==old.end()?Sample{point,.012f,.18f,0,0}:found->second);}
    }
    static bool DomainSame(Vec a,Vec b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
    void Step(float elapsed){
        elapsed=std::clamp(elapsed,0.f,.05f);int steps=std::max(1,int(std::ceil(elapsed*120)));float dt=elapsed/steps;
        std::vector<std::array<float,2>> forces(samples.size());
        for(int tick=0;tick<steps;++tick){time=std::fmod(time+dt,3600.f);
            for(size_t i=0;i<samples.size();++i){const auto& s=samples[i];const auto& left=samples[i?i-1:i];const auto& right=samples[std::min(i+1,samples.size()-1)];
                float phase=time*3+s.anchor.x*.31f+s.anchor.z*.27f;
                forces[i]={22*(left.height+right.height-2*s.height)-32*s.height-6*s.velocity+.12f*std::sin(phase),
                    14*(left.side+right.side-2*s.side)-24*s.side-7*s.sideVelocity+.8f*std::sin(phase*.7f)};}
            for(size_t i=0;i<samples.size();++i){auto& s=samples[i];s.velocity+=forces[i][0]*dt;s.sideVelocity+=forces[i][1]*dt;
                s.height=std::clamp(s.height+s.velocity*dt,-.035f,.035f);s.side=std::clamp(s.side+s.sideVelocity*dt,-.10f,.10f);}
        }
    }
    Vec Position(size_t i)const{
        const auto& s=samples[i];Vec along=samples[std::min(i+1,samples.size()-1)].anchor-samples[i?i-1:i].anchor;
        float length=std::sqrt(along.x*along.x+along.z*along.z);Vec across=length>.001f?Vec{-along.z/length,0,along.x/length}:Vec{1,0,0};
        return s.anchor+across*s.side+Vec{0,s.height-.055f,0};
    }
};
}
