#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace Engine {
struct LodSubset {uint32_t first=0,count=0;int material=-1;};
template<class Vertex> struct ReducedStaticMesh {std::vector<Vertex> vertices;std::vector<uint32_t> indices;std::vector<LodSubset> subsets;};
}
#include "TopologyLod.h"
namespace Engine {
// Solid surfaces use topology-preserving edge collapses; grass is thinned only
// in whole crossed tufts. Neither path arbitrarily deletes surviving faces.
template<class Vertex> ReducedStaticMesh<Vertex> ReduceStaticMesh(const std::vector<Vertex>& vertices,const std::vector<uint32_t>& indices,std::vector<LodSubset> subsets,int grid,bool grassCards=false){
    ReducedStaticMesh<Vertex> out;if(vertices.empty()||indices.size()<3||grid<1)return out;
    if(subsets.empty())subsets.push_back({0,uint32_t(indices.size()),-1});
    if(!grassCards){ReduceConnectedSurface(vertices,indices,subsets,grid,out);return out;}
    for(const auto& sub:subsets){
        if(size_t(sub.first)+sub.count>indices.size()||sub.count%3!=0)continue;
        uint32_t start=uint32_t(out.indices.size());
        if(grassCards){
            // Twelve indices are one complete crossed grass tuft in the generated patch.
            std::map<uint32_t,uint32_t> remap;uint32_t stride=uint32_t(grid);
            for(uint32_t i=0;i<sub.count;++i){if((i/12)%stride!=0)continue;
                uint32_t source=indices[sub.first+i];if(source>=vertices.size())continue;
                auto it=remap.find(source);if(it==remap.end()){uint32_t id=uint32_t(out.vertices.size());out.vertices.push_back(vertices[source]);it=remap.emplace(source,id).first;}
                out.indices.push_back(it->second);}
        }
        uint32_t count=uint32_t(out.indices.size())-start;if(count)out.subsets.push_back({start,count,sub.material});
    }
    // Compact unreferenced clusters so the GPU buffer also contains fewer vertices.
    std::vector<uint32_t> compact(out.vertices.size(),UINT32_MAX);std::vector<Vertex> used;
    for(auto& id:out.indices){if(compact[id]==UINT32_MAX){compact[id]=uint32_t(used.size());used.push_back(out.vertices[id]);}id=compact[id];}
    out.vertices=std::move(used);
    for(auto& v:out.vertices){float length=std::sqrt(v.normal.x*v.normal.x+v.normal.y*v.normal.y+v.normal.z*v.normal.z);if(length>.00001f){v.normal.x/=length;v.normal.y/=length;v.normal.z/=length;}}
    return out;
}
inline int DistanceLodLevel(float distance,float radius){return distance>(std::max)(110.f,radius*6)?2:distance>(std::max)(45.f,radius*3)?1:0;}

// Authored curved leaves are connected strips, not twelve-index generated
// crossed cards. Keep or remove whole connected leaves, preserving every UV,
// normal and triangle on the surviving strips.
template<class Vertex> ReducedStaticMesh<Vertex> ReduceLeafCards(const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,std::vector<LodSubset> subsets,uint32_t stride){
    ReducedStaticMesh<Vertex> out;if(vertices.empty()||indices.size()<3||stride<1)return out;
    for(auto id:indices)if(id>=vertices.size())return out;
    if(subsets.empty())subsets.push_back({0,uint32_t(indices.size()),-1});
    std::vector<uint32_t> parent(vertices.size());for(uint32_t i=0;i<parent.size();++i)parent[i]=i;
    auto root=[&](uint32_t id){while(parent[id]!=id){parent[id]=parent[parent[id]];id=parent[id];}return id;};
    std::map<std::array<float,5>,uint32_t> welded;
    for(uint32_t i=0;i<vertices.size();++i){const auto& v=vertices[i];
        auto inserted=welded.emplace(std::array<float,5>{v.position.x,v.position.y,v.position.z,v.texcoord.x,v.texcoord.y},i);
        if(!inserted.second)parent[i]=inserted.first->second;}
    for(size_t i=0;i+2<indices.size();i+=3){uint32_t a=root(indices[i]);for(int k=1;k<3;++k){uint32_t b=root(indices[i+k]);if(a!=b){parent[b]=a;}}}
    std::vector<uint32_t> remap(vertices.size(),UINT32_MAX);
    for(const auto& sub:subsets){if(size_t(sub.first)+sub.count>indices.size()||sub.count%3)continue;
        uint32_t start=uint32_t(out.indices.size());std::map<uint32_t,bool> kept;uint32_t ordinal=0;
        for(uint32_t i=0;i<sub.count;i+=3){uint32_t component=root(indices[sub.first+i]);auto found=kept.find(component);
            if(found==kept.end()){found=kept.emplace(component,ordinal%stride==0).first;++ordinal;}
            if(!found->second)continue;
            for(int k=0;k<3;++k){uint32_t id=indices[sub.first+i+k];if(remap[id]==UINT32_MAX){remap[id]=uint32_t(out.vertices.size());out.vertices.push_back(vertices[id]);}out.indices.push_back(remap[id]);}
        }
        uint32_t count=uint32_t(out.indices.size())-start;if(count)out.subsets.push_back({start,count,sub.material});
    }
    return out;
}
}
