#pragma once
#include <algorithm>
#include <cmath>
namespace Game::Chrono {
// The collision/paint arena ends at x=330, z-20=360. Keep that whole area flat;
// roll the surrounding landscape instead of introducing unsupported collision slopes.
inline float MeadowHeight(float x,float z){
    float outside=std::max(std::abs(x),std::abs(z-20))-365;
    float t=std::clamp(outside/100,0.f,1.f);t=t*t*(3-2*t);
    return -.08f+t*(8+5*std::sin(x*.013f+z*.009f)+3*std::sin(z*.022f-x*.007f));
}
}
