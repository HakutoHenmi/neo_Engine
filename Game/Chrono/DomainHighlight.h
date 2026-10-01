#pragma once
#include "DomainRules.h"
namespace Game::Chrono {
// Even-odd scan bands support concave loops and both lobes of a figure eight.
// Split at every vertex and crossing so edge order cannot change inside a band.
inline std::vector<std::array<Vec,3>> DomainHighlight(const DomainLoop& loop){
    std::vector<std::array<Vec,3>> triangles;
    if(!loop.Ready())return triangles;
    if(!loop.regions.empty()){
        for(const auto& points:loop.regions){DomainLoop region;region.points=points;region.Measure();auto part=DomainHighlight(region);triangles.insert(triangles.end(),part.begin(),part.end());}
        return triangles;
    }
    std::vector<Vec> points;
    DomainSimplify(loop.points,0,loop.points.size()-1,.08f,points);
    points.push_back(loop.points.back());
    std::vector<float> levels;for(auto p:points)levels.push_back(p.z);
    for(size_t i=1;i<points.size();++i)for(size_t j=i+2;j<points.size();++j){
        Vec a=points[i-1],d=points[i]-a,b=points[j-1],e=points[j]-b;
        float cross=DomainCross(d,e);if(std::abs(cross)<.00001f)continue;
        float t=DomainCross(b-a,e)/cross,u=DomainCross(b-a,d)/cross;
        if(t>0&&t<1&&u>0&&u<1)levels.push_back(a.z+d.z*t);
    }
    std::sort(levels.begin(),levels.end());
    levels.erase(std::unique(levels.begin(),levels.end(),[](float a,float b){return std::abs(a-b)<.0001f;}),levels.end());
    auto at=[](Vec a,Vec b,float z){return Lerp(a,b,std::clamp((z-a.z)/(b.z-a.z),0.f,1.f));};
    for(size_t band=1;band<levels.size();++band){
        float lo=levels[band-1],hi=levels[band],mid=(lo+hi)*.5f;
        std::vector<size_t> edges;
        for(size_t i=1;i<points.size();++i)if((points[i-1].z>mid)!=(points[i].z>mid))edges.push_back(i);
        std::sort(edges.begin(),edges.end(),[&](size_t a,size_t b){return at(points[a-1],points[a],mid).x<at(points[b-1],points[b],mid).x;});
        for(size_t i=1;i<edges.size();i+=2){
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
