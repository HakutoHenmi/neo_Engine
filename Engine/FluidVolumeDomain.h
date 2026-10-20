#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Engine::FluidVolumeDomain {
// Reserve the reconstruction radius and four filter/crossfade cells.
// Radius and resolution grow together at fixed particle count.
inline float FitCell(float width,float height,float depth,uint32_t columns,uint32_t rows,float radius=1.5f,float minimum=.18f){
    return (std::max)({minimum,(width+2*radius)/float(columns-8),(depth+2*radius)/float(columns-8),(height+2*radius)/float(rows-8)});
}
inline float StableCell(float required,float previous){
    // Small breathing/turning changes must not resize the entire sampling grid
    // every frame. Grow immediately; shrink only after a meaningful reduction.
    if(previous>=required&&required>previous*.85f)return previous;
    return std::ceil((required-1e-6f)/.01f)*.01f;
}
inline float Origin(float low,float high,float cell,uint32_t size){
    // A fitted body-local volume follows continuously. Snapping its origin to
    // world voxels made large weapons change sampling phase in visible steps.
    // TemporalDensity already reprojects arbitrary previous/current origins.
    return (low+high)*.5f-float(size)*cell*.5f;
}
}
