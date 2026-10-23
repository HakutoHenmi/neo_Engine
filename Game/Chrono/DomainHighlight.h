// DomainHighlight.h
// 描いた輪の内側を塗るための三角形を作る。
#pragma once
#include "DomainRules.h"
namespace Game::Chrono {
// 偶奇の走査の帯で、へこんだ輪や 8 の字の両方の輪にも対応する。
// すべての頂点と交点で分けるので、帯の中で辺の並びが変わらない
inline std::vector<std::array<Vec,3>> DomainHighlight(const DomainLoop& loop){
    // 結果の三角形の一覧
    std::vector<std::array<Vec,3>> triangles;
    if(!loop.Ready())return triangles;
    // 囲んだ領域が複数あれば、それぞれを塗る
    if(!loop.regions.empty()){
        for(const auto& points:loop.regions){DomainLoop region;region.points=points;region.Measure();auto part=DomainHighlight(region);triangles.insert(triangles.end(),part.begin(),part.end());}
        return triangles;
    }
    // 点を単純化する
    std::vector<Vec> points;
    DomainSimplify(loop.points,0,loop.points.size()-1,.08f,points);
    points.push_back(loop.points.back());
    // 頂点と、辺同士の交点の高さで帯に分ける
    std::vector<float> levels;for(auto p:points)levels.push_back(p.z);
    for(size_t i=1;i<points.size();++i)for(size_t j=i+2;j<points.size();++j){
        // 2 本の辺が交わる位置の高さも加える
        Vec a=points[i-1],d=points[i]-a,b=points[j-1],e=points[j]-b;
        float cross=DomainCross(d,e);if(std::abs(cross)<.00001f)continue;
        float t=DomainCross(b-a,e)/cross,u=DomainCross(b-a,d)/cross;
        if(t>0&&t<1&&u>0&&u<1)levels.push_back(a.z+d.z*t);
    }
    // 高さを並べて重複を消す
    std::sort(levels.begin(),levels.end());
    levels.erase(std::unique(levels.begin(),levels.end(),[](float a,float b){return std::abs(a-b)<.0001f;}),levels.end());
    // 2 点の間で指定の高さの位置
    auto at=[](Vec a,Vec b,float z){return Lerp(a,b,std::clamp((z-a.z)/(b.z-a.z),0.f,1.f));};
    // 各帯で、中央の高さを横切る辺を左から並べ、組ごとに台形 (三角形 2 枚) を作る
    for(size_t band=1;band<levels.size();++band){
        float lo=levels[band-1],hi=levels[band],mid=(lo+hi)*.5f;
        // 帯の中央の高さを横切る辺
        std::vector<size_t> edges;
        for(size_t i=1;i<points.size();++i)if((points[i-1].z>mid)!=(points[i].z>mid))edges.push_back(i);
        // 横切る位置の x の順に並べる
        std::sort(edges.begin(),edges.end(),[&](size_t a,size_t b){return at(points[a-1],points[a],mid).x<at(points[b-1],points[b],mid).x;});
        for(size_t i=1;i<edges.size();i+=2){
            // 左右の辺の組で、帯の下と上の 4 点から三角形 2 枚を作る
            size_t l=edges[i-1],r=edges[i];
            Vec a=at(points[l-1],points[l],lo),b=at(points[r-1],points[r],lo);
            Vec c=at(points[r-1],points[r],hi),d=at(points[l-1],points[l],hi);
            if(std::abs(DomainCross(b-a,c-a))>.00001f)triangles.push_back({a,b,c});
            if(std::abs(DomainCross(c-a,d-a))>.00001f)triangles.push_back({a,c,d});
        }
    }
    return triangles;
}
}
