#pragma once
#include "ChronoRules.h"
#include <vector>
#include <array>
#include <map>
#include <set>

namespace Game::Chrono {
inline float DomainDistance(Vec a,Vec b){a.y=b.y=0;return Length(a-b);}
inline float DomainCross(Vec a,Vec b){return a.x*b.z-a.z*b.x;}
enum class DomainShape {Loop,Triangle,Square,Infinity};
inline const char* DomainSkillName(DomainShape shape){
    switch(shape){case DomainShape::Triangle:return "TRIDENT";case DomainShape::Square:return "FORTRESS";
        case DomainShape::Infinity:return "TWIN STORM";default:return "HOMING DOMAIN";}
}
inline float DomainPointEdgeDistance(Vec p,Vec a,Vec b){
    Vec d=b-a;d.y=0;Vec offset=p-a;offset.y=0;
    float t=std::clamp(Dot(offset,d)/std::max(.00001f,Dot(d,d)),0.f,1.f);
    return DomainDistance(p,Lerp(a,b,t));
}
inline void DomainSimplify(const std::vector<Vec>& points,size_t first,size_t last,float tolerance,std::vector<Vec>& out){
    float best=tolerance;size_t split=first;
    for(size_t i=first+1;i<last;++i){float distance=DomainPointEdgeDistance(points[i],points[first],points[last]);
        if(distance>best){best=distance;split=i;}}
    if(split!=first){DomainSimplify(points,first,split,tolerance,out);DomainSimplify(points,split,last,tolerance,out);}
    else out.push_back(points[first]);
}
struct DomainLoop {
    std::vector<Vec> points;
    // Bounded faces of the current continuous stroke; shared edges count once.
    std::vector<std::vector<Vec>> regions;
    std::array<Vec,4> corners{};
    DomainShape shape=DomainShape::Loop;
    float length=0,area=0;
    int Enclosures()const{return regions.empty()?1:int(regions.size());}
    bool Ready()const{return points.size()>=4&&length>=12&&area>=8;}
    void Measure(){
        length=area=0;
        if(points.empty())return;
        // Use a local origin to avoid cancellation far from the arena center.
        for(size_t i=1;i<points.size();++i){length+=DomainDistance(points[i-1],points[i]);
            area+=DomainCross(points[i-1]-points.front(),points[i]-points.front());}
        area=std::abs(area)*.5f;
    }
    Vec Center()const{Vec center{};for(auto p:points)center=center+p;return points.empty()?center:center*(1.f/float(points.size()));}
    void Recognize(){
        shape=DomainShape::Loop;if(!Ready())return;
        std::vector<Vec> simplified;size_t opposite=1;float farthest=0;
        for(size_t i=1;i+1<points.size();++i){float distance=DomainDistance(points.front(),points[i]);if(distance>farthest){farthest=distance;opposite=i;}}
        float tolerance=std::max(1.f,length*.018f);
        DomainSimplify(points,0,opposite,tolerance,simplified);DomainSimplify(points,opposite,points.size()-1,tolerance,simplified);
        bool removed=true;
        while(removed&&simplified.size()>3){removed=false;
            for(size_t i=0;i<simplified.size();++i){Vec a=simplified[(i+simplified.size()-1)%simplified.size()],b=simplified[i],c=simplified[(i+1)%simplified.size()];
                if(DomainPointEdgeDistance(b,a,c)<tolerance*.65f){simplified.erase(simplified.begin()+i);removed=true;break;}}}
        if(simplified.size()!=3&&simplified.size()!=4)return;
        float turnSign=0,minEdge=length,maxEdge=0;
        for(size_t i=0;i<simplified.size();++i){Vec a=simplified[i],b=simplified[(i+1)%simplified.size()],c=simplified[(i+2)%simplified.size()];
            Vec d=b-a,e=c-b;d.y=e.y=0;float cross=DomainCross(d,e);
            if(std::abs(cross)<.01f||(turnSign!=0&&turnSign*cross<0))return;turnSign=cross;
            float turn=std::acos(std::clamp(Dot(Unit(d),Unit(e)),-1.f,1.f));
            if(turn<.6f||turn>2.65f)return;
            float edge=DomainDistance(a,b);minEdge=std::min(minEdge,edge);maxEdge=std::max(maxEdge,edge);
            // Every original sample must stay close to one recognized straight edge.
        }
        if(maxEdge>minEdge*3.5f)return;
        for(auto p:points){float distance=length;for(size_t i=0;i<simplified.size();++i)distance=std::min(distance,DomainPointEdgeDistance(p,simplified[i],simplified[(i+1)%simplified.size()]));
            if(distance>tolerance*1.2f)return;}
        shape=simplified.size()==3?DomainShape::Triangle:DomainShape::Square;
        for(size_t i=0;i<simplified.size();++i)corners[i]=simplified[i];
    }
};
inline int DomainNormalAmmo(int enclosures){return 48+16*std::clamp(enclosures-1,0,13);}
inline float DomainBulletPower(int enclosures){return 1+.45f*std::sqrt(float(std::clamp(enclosures-1,0,13)));}
inline int DomainAmmo(const DomainLoop& loop){
    float multiplier=loop.shape==DomainShape::Infinity?1.3f:loop.shape==DomainShape::Square?1.15f:loop.shape==DomainShape::Triangle?1.35f:1.f;
    return int(DomainNormalAmmo(loop.Enclosures())*multiplier);
}
inline bool DomainContains(const DomainLoop& loop,Vec p){
    if(!loop.regions.empty()){
        for(const auto& points:loop.regions){DomainLoop region;region.points=points;if(DomainContains(region,p))return true;}
        return false;
    }
    bool inside=false;
    for(size_t i=1;i<loop.points.size();++i){Vec a=loop.points[i-1],b=loop.points[i];
        if((a.z>p.z)!=(b.z>p.z)&&p.x<(b.x-a.x)*(p.z-a.z)/(b.z-a.z)+a.x)inside=!inside;}
    return inside;
}
// Build a planar graph, split crossings/collinear overlaps, then walk its bounded
// faces. Repeated edges and enclosing an existing region do not duplicate rewards.
inline std::vector<DomainLoop> DomainStrokeRegions(const std::vector<Vec>& stroke){
    constexpr float weld=.10f; // Greater than simplification error, much smaller than the liquid ribbon.
    std::vector<DomainLoop> regions;if(stroke.size()<4)return regions;
    std::vector<Vec> path;DomainSimplify(stroke,0,stroke.size()-1,.04f,path);path.push_back(stroke.back());
    struct Edge {Vec a,b;std::vector<float> cuts{0,1};};
    std::vector<Edge> edges;
    for(size_t i=1;i<path.size();++i)if(DomainDistance(path[i-1],path[i])>.02f)edges.push_back({path[i-1],path[i],{0,1}});
    for(size_t i=0;i<edges.size();++i)for(size_t j=i+1;j<edges.size();++j){
        auto& a=edges[i];auto& b=edges[j];Vec d=a.b-a.a,e=b.b-b.a;float den=DomainCross(d,e);
        if(std::abs(den)>.00001f){
            float t=DomainCross(b.a-a.a,e)/den,u=DomainCross(b.a-a.a,d)/den;
            if(t>=0&&t<=1&&u>=0&&u<=1&&std::abs(Lerp(a.a,a.b,t).y-Lerp(b.a,b.b,u).y)<1.5f){a.cuts.push_back(t);b.cuts.push_back(u);}
        }
        // Near endpoints must also join the interior of nonparallel edges.
        // Simplification can otherwise leave an accepted closure slightly off its old edge.
        {
            auto split=[&](Edge& edge,Vec p){Vec delta=edge.b-edge.a;delta.y=0;
                float t=Dot(Vec{p.x-edge.a.x,0,p.z-edge.a.z},delta)/Dot(delta,delta);
                if(t>=0&&t<=1&&DomainDistance(Lerp(edge.a,edge.b,t),p)<weld&&std::abs(Lerp(edge.a,edge.b,t).y-p.y)<1.5f)edge.cuts.push_back(t);};
            split(a,b.a);split(a,b.b);split(b,a.a);split(b,a.b);
        }
    }
    std::vector<Vec> vertices;std::map<std::pair<int,int>,std::vector<size_t>> cells;
    auto vertex=[&](Vec p){int x=int(std::floor(p.x/weld)),z=int(std::floor(p.z/weld));
        for(int dx=-1;dx<=1;++dx)for(int dz=-1;dz<=1;++dz){auto found=cells.find({x+dx,z+dz});if(found==cells.end())continue;
            for(auto n:found->second)if(DomainDistance(vertices[n],p)<weld&&std::abs(vertices[n].y-p.y)<1.5f)return n;}
        size_t n=vertices.size();vertices.push_back(p);cells[{x,z}].push_back(n);return n;};
    std::set<std::pair<size_t,size_t>> links;
    for(auto& edge:edges){std::sort(edge.cuts.begin(),edge.cuts.end());
        for(size_t i=1;i<edge.cuts.size();++i){size_t a=vertex(Lerp(edge.a,edge.b,edge.cuts[i-1])),b=vertex(Lerp(edge.a,edge.b,edge.cuts[i]));
            if(a!=b)links.insert(std::minmax(a,b));}}
    std::vector<std::vector<size_t>> neighbors(vertices.size());
    for(auto edge:links){neighbors[edge.first].push_back(edge.second);neighbors[edge.second].push_back(edge.first);}
    for(size_t i=0;i<neighbors.size();++i)std::sort(neighbors[i].begin(),neighbors[i].end(),[&](size_t a,size_t b){
        Vec da=vertices[a]-vertices[i],db=vertices[b]-vertices[i];return std::atan2(da.z,da.x)<std::atan2(db.z,db.x);});
    std::set<std::pair<size_t,size_t>> visited;
    for(auto edge:links)for(int direction=0;direction<2;++direction){
        size_t start=direction?edge.second:edge.first,next=direction?edge.first:edge.second,a=start,b=next;
        if(visited.count({a,b}))continue;DomainLoop face;bool closed=false;
        for(size_t step=0;step<=links.size()*2;++step){
            if(!visited.insert({a,b}).second)break;face.points.push_back(vertices[a]);
            const auto& adjacent=neighbors[b];auto back=std::find(adjacent.begin(),adjacent.end(),a);
            size_t index=size_t(back-adjacent.begin());size_t c=adjacent[(index+adjacent.size()-1)%adjacent.size()];a=b;b=c;
            if(a==start&&b==next){closed=true;break;}
        }
        if(!closed||face.points.size()<3)continue;face.points.push_back(face.points.front());
        float signedArea=0;for(size_t i=1;i<face.points.size();++i)signedArea+=DomainCross(face.points[i-1]-face.points.front(),face.points[i]-face.points.front());
        face.Measure();if(signedArea>0&&face.Ready())regions.push_back(std::move(face));
    }
    return regions;
}
struct DomainPath {
    static constexpr size_t MaxPoints=1536;
    size_t pointLimit=MaxPoints;
    static constexpr float ClosureRadius=1.45f; // Two visible liquid half-widths (.72m).
    std::vector<Vec> points;
    DomainLoop candidate;
    DomainLoop lastLoop;
    float lastClosure=-100;
    float length=0;
    void Clear(){points.clear();candidate={};lastLoop={};length=0;lastClosure=-100;}
    void BreakTrail(){points.clear();lastLoop={};length=0;lastClosure=-100;}
    void Recount(){length=0;for(size_t i=1;i<points.size();++i)length+=DomainDistance(points[i-1],points[i]);}
    void Damage(){
        candidate={};lastLoop={};lastClosure=-100;
        // Lose only the most recent quarter; keep the rest for a new closure.
        if(points.size()>1)points.resize(std::max(size_t(1),points.size()*3/4));
        Recount();
    }
    void Append(Vec at){
        if(points.empty()){points.push_back(at);return;}
        if(DomainDistance(points.back(),at)>8||std::abs(points.back().y-at.y)>5){Clear();points.push_back(at);return;}
        for(int samples=0;samples<16&&DomainDistance(points.back(),at)>.001f;++samples){
            float distance=DomainDistance(points.back(),at);bool sampled=distance>=1,accepted=false;
            Vec from=points.back(),to=sampled?Lerp(from,at,1/distance):at;
            // Skip the adjacent edges and reject tiny/degenerate loops.
            if(points.size()>=5)for(size_t i=points.size()-3;i>0;--i){
                Vec a=points[i-1],b=points[i],d=to-from,e=b-a;
                float den=DomainCross(d,e);Vec hit{};bool closed=false;
                if(std::abs(den)>.00001f){
                    float t=DomainCross(a-from,e)/den,u=DomainCross(a-from,d)/den;
                    closed=t>=0&&t<=1&&u>=0&&u<=1;
                    if(closed)hit=Lerp(a,b,u);
                }
                // A small closure tolerance also handles endpoints sampled one metre apart.
                if(!closed){float q=std::clamp(Dot(Vec{to.x-a.x,0,to.z-a.z},Vec{e.x,0,e.z})/std::max(.00001f,e.x*e.x+e.z*e.z),0.f,1.f);
                    hit=Lerp(a,b,q);closed=DomainDistance(hit,to)<ClosureRadius;}
                if(!closed||std::abs(hit.y-to.y)>1.5f||DomainDistance(hit,from)<.02f)continue;
                // A tolerant snap must advance toward the live tip, never pull
                // backwards onto an old junction and repeatedly resample it.
                float advance=(hit.x-from.x)*(to.x-from.x)+(hit.z-from.z)*(to.z-from.z);
                if(advance<=.0001f)continue;
                DomainLoop loop;loop.points.push_back(hit);
                loop.points.insert(loop.points.end(),points.begin()+i,points.end());
                loop.points.push_back(to);loop.points.push_back(hit);loop.Measure();
                // Face extraction validates area independently: opposite winding
                // must not cancel a real pair of enclosed regions. No distance cooldown.
                if(loop.length>=12){
                    loop.Recognize();
                    DomainShape hidden=DomainShape::Loop;
                    if(lastLoop.Ready()&&DomainDistance(lastLoop.points.front(),loop.points.front())<2.5f){
                        Vec junction=loop.points.front(),lobeA=lastLoop.Center()-junction,lobeB=loop.Center()-junction;lobeA.y=lobeB.y=0;
                        float ratio=std::min(loop.area,lastLoop.area)/std::max(loop.area,lastLoop.area);
                        if(ratio>.2f&&Dot(Unit(lobeA),Unit(lobeB))<-.2f&&!DomainContains(lastLoop,loop.Center())&&!DomainContains(loop,lastLoop.Center())){
                            hidden=DomainShape::Infinity;
                        }
                    }
                    // Snap the visual sample to its accepted closure; the player position
                    // remains untouched. This keeps tolerant closures topologically closed.
                    auto stroke=points;stroke.push_back(hit);
                    auto regions=DomainStrokeRegions(stroke);
                    if(!regions.empty()){
                        float area=0;for(const auto& region:regions)area+=region.area;
                        // Revisiting an already completed border must not snap the
                        // live stroke again or replace the last meaningful closure.
                        if(lastClosure>=0&&candidate.Ready()&&int(regions.size())==candidate.Enclosures()&&area<=candidate.area+.25f)break;
                        DomainLoop joined;joined.points=std::move(stroke);joined.length=length+DomainDistance(from,hit);
                        for(auto& region:regions){joined.area+=region.area;joined.regions.push_back(std::move(region.points));}
                        if(joined.Enclosures()==1){DomainLoop single;single.points=joined.regions.front();single.Measure();single.Recognize();joined.shape=single.shape;joined.corners=single.corners;}
                        else if(joined.Enclosures()==2&&(hidden==DomainShape::Infinity||candidate.shape==DomainShape::Infinity))joined.shape=DomainShape::Infinity;
                        candidate=std::move(joined);
                        to=hit;
                        accepted=true;
                    }
                    if(accepted){lastLoop=std::move(loop);lastClosure=length;break;}
                }
            }
            if(!sampled&&!accepted)break; // Check the live tip without densifying the entire stroke.
            points.push_back(to);length+=DomainDistance(from,to);
            if(points.size()>pointLimit){points.erase(points.begin());Recount();}
            if(!sampled)break;
        }
    }
};
// Launch vertically, fan outward, then steer toward the moving boss.
inline Vec DomainMissileVelocity(Vec position,Vec velocity,Vec target,float age,float seed,float dt){
    Vec desired;
    if(age<.5f)desired={std::cos(seed)*12,65,std::sin(seed)*12};
    else if(age<.9f)desired=Unit(target-position+Vec{std::cos(seed)*22,45,std::sin(seed)*22})*90;
    else desired=Unit(target-position)*125;
    return Lerp(velocity,desired,1-std::exp(-(age<.9f?5.f:9.f)*dt));
}
}
