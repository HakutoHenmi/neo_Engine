#pragma once
#include <string_view>
namespace Engine {
inline bool CastsOpaqueShadow(std::string_view shader) {
	// 部屋の天井と照明器具は光を遮らない
	if (shader == "LowpolyNoShadow") return false;
    for(auto effect:{"2D","Distortion","Particle","ParticleAdditive","ParticleInstanced",
        "ProceduralSmoke","ProceduralSmokeAdditive","ProceduralSmokeInstanced",
        "SlimeBeam","EnergyBeam","EnergyCylinder","HordeEffect","DomainFill",
        "DomainGlow","LiquidTrail","Hologram","ForceField","Reflection","Glass",
        "EmissiveGlow","SlimeNoFace","SlimeNoFaceNoDepth"})
        if(shader==effect)return false;
    return true;
}
}
