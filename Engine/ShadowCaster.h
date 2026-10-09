#pragma once
#include <string_view>
namespace Engine {
inline bool CastsOpaqueShadow(std::string_view shader) {
    for(auto effect:{"2D","Distortion","Particle","ParticleAdditive","ParticleInstanced",
        "ProceduralSmoke","ProceduralSmokeAdditive","ProceduralSmokeInstanced",
        "SlimeBeam","EnergyBeam","EnergyCylinder","HordeEffect","DomainFill",
        "DomainGlow","LiquidTrail","Hologram","ForceField","Reflection",
        "EmissiveGlow","SlimeNoFace","SlimeNoFaceNoDepth"})
        if(shader==effect)return false;
    return true;
}
}
