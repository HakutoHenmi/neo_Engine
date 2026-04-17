// Engine/RiverSystem.h
#pragma once
#include <vector>
#include <DirectXMath.h>
#include "../../externals/entt/entt.hpp"

namespace Engine {
    class Renderer;
}

namespace Game {

struct RiverComponent;

class RiverSystem {
public:
    static void BuildRiverMesh(RiverComponent& river, Engine::Renderer* renderer, entt::registry& registry, const DirectX::XMFLOAT3& ownerPos = {0,0,0});

private:
    static DirectX::XMVECTOR InterpolateSpline(const std::vector<DirectX::XMFLOAT3>& points, float t);
};

} // namespace Game
