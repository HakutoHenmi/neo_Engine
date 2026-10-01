#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace Engine {
// Collapse only interior manifold edges. Shared positions move together across
// render-vertex splits; open borders, UV seams and material seams remain fixed.
template<class Vertex> void ReduceConnectedSurface(const std::vector<Vertex>& input,
    const std::vector<uint32_t>& indices,const std::vector<LodSubset>& subsets,int grid,
    ReducedStaticMesh<Vertex>& out){
    using Point=std::array<float,3>;
    auto subtract=[](Point a,Point b){return Point{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
    auto dot=[](Point a,Point b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    auto cross=[](Point a,Point b){return Point{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    struct Node {Point p;Vertex attribute;std::set<uint32_t> faces;bool fixed=false;float radius=0;};
    struct Face {std::array<uint32_t,3> nodes,source;Point normal;size_t subset;bool alive=true;};
    std::vector<Node> nodes;std::vector<Face> faces;std::vector<uint32_t> welded(input.size());
    std::map<Point,uint32_t> positions;
    Point lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
    for(size_t i=0;i<input.size();++i){const auto& v=input[i];Point p{v.position.x,v.position.y,v.position.z};
        auto insertion=positions.emplace(p,uint32_t(nodes.size()));welded[i]=insertion.first->second;
        if(insertion.second)nodes.push_back({p,v,{},false,0});
        else{auto& n=nodes[welded[i]];if(std::abs(n.attribute.texcoord.x-v.texcoord.x)>1e-5f||std::abs(n.attribute.texcoord.y-v.texcoord.y)>1e-5f)n.fixed=true;}
        for(int axis=0;axis<3;++axis){lo[axis]=(std::min)(lo[axis],p[axis]);hi[axis]=(std::max)(hi[axis],p[axis]);}
    }
    std::map<std::array<uint32_t,2>,std::vector<uint32_t>> edges;
    for(size_t s=0;s<subsets.size();++s){const auto& sub=subsets[s];
        if(size_t(sub.first)+sub.count>indices.size()||sub.count%3)continue;
        for(uint32_t i=0;i<sub.count;i+=3){std::array<uint32_t,3> source{indices[sub.first+i],indices[sub.first+i+1],indices[sub.first+i+2]};
            if(source[0]>=input.size()||source[1]>=input.size()||source[2]>=input.size())continue;
            std::array<uint32_t,3> ids{welded[source[0]],welded[source[1]],welded[source[2]]};
            Point normal=cross(subtract(nodes[ids[1]].p,nodes[ids[0]].p),subtract(nodes[ids[2]].p,nodes[ids[0]].p));
            uint32_t id=uint32_t(faces.size());faces.push_back({ids,source,normal,s,true});
            for(auto n:ids)nodes[n].faces.insert(id);
            for(int e=0;e<3;++e){std::array<uint32_t,2> edge{ids[e],ids[(e+1)%3]};std::sort(edge.begin(),edge.end());edges[edge].push_back(id);}
        }
    }
    for(const auto& entry:edges){const auto& adjacent=entry.second;bool protect=adjacent.size()!=2;
        if(!protect){const auto& a=faces[adjacent[0]];const auto& b=faces[adjacent[1]];
            protect=subsets[a.subset].material!=subsets[b.subset].material||dot(a.normal,b.normal)<.5f*std::sqrt(dot(a.normal,a.normal)*dot(b.normal,b.normal));}
        if(protect)for(auto n:entry.first)nodes[n].fixed=true;
    }
    const float extent=(std::max)({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2],.00001f});
    const float maxRadius=extent/float(grid)*.6f;
    size_t remaining=faces.size(),target=size_t(double(remaining)*(grid>=32?.65:.40));
    auto neighbors=[&](uint32_t id){std::set<uint32_t> result;for(auto f:nodes[id].faces)for(auto n:faces[f].nodes)if(n!=id)result.insert(n);return result;};
    // Rebuild the short-edge order between passes; adjacency is updated after
    // every collapse, so later candidates are checked against the current mesh.
    for(int pass=0;pass<12&&remaining>target;++pass){
        struct Candidate {uint32_t a,b;float length;};std::vector<Candidate> candidates;std::set<std::array<uint32_t,2>> seen;
        for(const auto& f:faces)if(f.alive)for(int e=0;e<3;++e){uint32_t a=f.nodes[e],b=f.nodes[(e+1)%3];if(a>b)std::swap(a,b);
            if(a==b||nodes[a].fixed||nodes[b].fixed||!seen.insert({a,b}).second)continue;
            auto delta=subtract(nodes[a].p,nodes[b].p);float length=dot(delta,delta);if(length<=maxRadius*maxRadius*4)candidates.push_back({a,b,length});}
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.length==b.length?std::array<uint32_t,2>{a.a,a.b}<std::array<uint32_t,2>{b.a,b.b}:a.length<b.length;});
        size_t before=remaining;
        for(const auto& edge:candidates){if(remaining<=target)break;auto& a=nodes[edge.a];auto& b=nodes[edge.b];if(a.faces.empty()||b.faces.empty())continue;
            std::vector<uint32_t> shared;std::set_intersection(a.faces.begin(),a.faces.end(),b.faces.begin(),b.faces.end(),std::back_inserter(shared));
            if(shared.size()!=2)continue;
            auto an=neighbors(edge.a),bn=neighbors(edge.b);std::vector<uint32_t> common;
            std::set_intersection(an.begin(),an.end(),bn.begin(),bn.end(),std::back_inserter(common));if(common.size()!=2)continue;
            Point position{(a.p[0]+b.p[0])*.5f,(a.p[1]+b.p[1])*.5f,(a.p[2]+b.p[2])*.5f};
            float radius=(std::max)(a.radius+std::sqrt(dot(subtract(a.p,position),subtract(a.p,position))),b.radius+std::sqrt(dot(subtract(b.p,position),subtract(b.p,position))));
            if(radius>maxRadius)continue;
            std::set<uint32_t> affected=a.faces;affected.insert(b.faces.begin(),b.faces.end());
            std::set<std::array<uint32_t,3>> localFaces;bool valid=true;
            for(auto f:affected){if(f==shared[0]||f==shared[1])continue;const auto& face=faces[f];auto ids=face.nodes;Point points[3];
                for(int k=0;k<3;++k){if(ids[k]==edge.b)ids[k]=edge.a;points[k]=ids[k]==edge.a?position:nodes[ids[k]].p;}
                auto canonical=ids;std::sort(canonical.begin(),canonical.end());if(!localFaces.insert(canonical).second){valid=false;break;}
                auto normal=cross(subtract(points[1],points[0]),subtract(points[2],points[0]));
                Point current=cross(subtract(nodes[face.nodes[1]].p,nodes[face.nodes[0]].p),subtract(nodes[face.nodes[2]].p,nodes[face.nodes[0]].p));
                if(dot(normal,normal)<1e-20f||dot(normal,current)<=0||dot(normal,face.normal)<.5f*std::sqrt(dot(normal,normal)*dot(face.normal,face.normal))){valid=false;break;}
            }
            if(!valid)continue;
            // The link condition plus duplicate-face and orientation checks
            // preserve topology; only the two faces on this edge disappear.
            for(auto f:shared){faces[f].alive=false;for(auto n:faces[f].nodes)nodes[n].faces.erase(f);--remaining;}
            auto moved=b.faces;for(auto f:moved){for(auto& n:faces[f].nodes)if(n==edge.b)n=edge.a;a.faces.insert(f);}b.faces.clear();
            a.p=position;a.radius=radius;
            a.attribute.texcoord.x=(a.attribute.texcoord.x+b.attribute.texcoord.x)*.5f;a.attribute.texcoord.y=(a.attribute.texcoord.y+b.attribute.texcoord.y)*.5f;
        }
        if(remaining==before)break;
    }
    // Preserve original per-corner normals (including hard shading splits),
    // while using one geometric position for every copy of a shared vertex.
    std::map<std::array<float,8>,uint32_t> renderVertices;
    for(size_t s=0;s<subsets.size();++s){uint32_t first=uint32_t(out.indices.size());renderVertices.clear();
        for(const auto& f:faces)if(f.alive&&f.subset==s)for(int k=0;k<3;++k){const auto& n=nodes[f.nodes[k]];auto v=input[f.source[k]];
            v.position.x=n.p[0];v.position.y=n.p[1];v.position.z=n.p[2];if(!n.fixed)v.texcoord=n.attribute.texcoord;
            std::array<float,8> key{v.position.x,v.position.y,v.position.z,v.texcoord.x,v.texcoord.y,v.normal.x,v.normal.y,v.normal.z};
            auto inserted=renderVertices.emplace(key,uint32_t(out.vertices.size()));if(inserted.second)out.vertices.push_back(v);out.indices.push_back(inserted.first->second);}
        uint32_t count=uint32_t(out.indices.size())-first;if(count)out.subsets.push_back({first,count,subsets[s].material});
    }
}
}
