#pragma once
#include <algorithm>
#include <cmath>
namespace Engine {
// Serialized counts retain their historical 60 Hz meaning. Match solver time cap.
inline int AccumulateFluidEmission(int referenceCount, float dt, float& remainder) {
    if (referenceCount<=0 || !std::isfinite(dt) || dt<=0) return 0;
    remainder+=(std::min)(referenceCount,16000)*60.0f*(std::min)(dt,1.0f/30.0f);
    int count=static_cast<int>(remainder);
    remainder-=count;
    return count;
}
}
