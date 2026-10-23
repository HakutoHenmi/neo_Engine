// LiquidTrail.h
// 描いた線に沿った液体の見た目 (高さと横のずれを波として動かす)。
#pragma once
#include "ChronoRules.h"
#include <map>

namespace Game::Chrono {
// 決まったゲームの線に沿った、粘りのある浅い液体。隣との
// 圧力で波が伝わり、付着で横の動きと厚みを抑える
struct LiquidTrail {
    // 線の点ごとの液体の状態 (高さ・速さ・横のずれ)
    struct Sample {Vec anchor;float height=0,velocity=0,side=0,sideVelocity=0;};
    // 点ごとの状態
    std::vector<Sample> samples;
    // 時間
    float time=0;
    // 線の点と同期する (同じ点の状態は残す)
    void Sync(const std::vector<Vec>& points){
        if(points.empty()){samples.clear();return;}
        // 点が同じなら何もしない
        if(samples.size()==points.size()){
            bool identical=true;for(size_t i=0;i<points.size();++i)if(!DomainSame(samples[i].anchor,points[i])){identical=false;break;}
            if(identical)return;
        }
        // 前の状態を点の位置で探せるようにする
        std::map<std::array<float,3>,Sample> old;
        for(const auto& s:samples)old.emplace(std::array<float,3>{s.anchor.x,s.anchor.y,s.anchor.z},s);
        samples.clear();samples.reserve(points.size());
        // 同じ位置の状態は引き継ぎ、新しい点は初期値
        for(auto point:points){auto found=old.find({point.x,point.y,point.z});
            samples.push_back(found==old.end()?Sample{point,.012f,.18f,0,0}:found->second);}
    }
    static bool DomainSame(Vec a,Vec b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
    // 隣との差で波を伝え、元に戻る力と減衰をかけて進める (1/120 秒刻み)
    void Step(float elapsed){
        // 1/120 秒刻みの回数
        elapsed=std::clamp(elapsed,0.f,.05f);int steps=std::max(1,int(std::ceil(elapsed*120)));float dt=elapsed/steps;
        std::vector<std::array<float,2>> forces(samples.size());
        for(int tick=0;tick<steps;++tick){time=std::fmod(time+dt,3600.f);
            // 隣との差による高さと横のずれの力 (ばね・減衰・揺らぎ)
            for(size_t i=0;i<samples.size();++i){const auto& s=samples[i];const auto& left=samples[i?i-1:i];const auto& right=samples[std::min(i+1,samples.size()-1)];
                float phase=time*3+s.anchor.x*.31f+s.anchor.z*.27f;
                forces[i]={22*(left.height+right.height-2*s.height)-32*s.height-6*s.velocity+.12f*std::sin(phase),
                    14*(left.side+right.side-2*s.side)-24*s.side-7*s.sideVelocity+.8f*std::sin(phase*.7f)};}
            // 速度と位置を進め、範囲に収める
            for(size_t i=0;i<samples.size();++i){auto& s=samples[i];s.velocity+=forces[i][0]*dt;s.sideVelocity+=forces[i][1]*dt;
                s.height=std::clamp(s.height+s.velocity*dt,-.035f,.035f);s.side=std::clamp(s.side+s.sideVelocity*dt,-.10f,.10f);}
        }
    }
    // 点の見た目の位置 (横のずれと高さを足す)
    Vec Position(size_t i)const{
        // 前後の点から線の向きと横の向きを求める
        const auto& s=samples[i];Vec along=samples[std::min(i+1,samples.size()-1)].anchor-samples[i?i-1:i].anchor;
        float length=std::sqrt(along.x*along.x+along.z*along.z);Vec across=length>.001f?Vec{-along.z/length,0,along.x/length}:Vec{1,0,0};
        return s.anchor+across*s.side+Vec{0,s.height-.055f,0};
    }
};
}
