// MeadowTerrain.h
// 草原のステージの地形の高さ。
#pragma once
#include <algorithm>
#include <cmath>
namespace Game::Chrono {
// 当たり判定と塗りのアリーナは x=330, z-20=360 で終わる。その範囲は全部平らにし、
// 周りの景観だけを起伏させて、支えの無い当たり判定の坂を作らない
inline float MeadowHeight(float x,float z){
    float outside=std::max(std::abs(x),std::abs(z-20))-365;
    float t=std::clamp(outside/100,0.f,1.f);t=t*t*(3-2*t);
    return -.08f+t*(8+5*std::sin(x*.013f+z*.009f)+3*std::sin(z*.022f-x*.007f));
}
}
