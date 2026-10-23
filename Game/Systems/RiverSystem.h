// RiverSystem.h
// 川 (制御点を通るスプラインに沿った水面) のメッシュを作る。
#pragma once
#include <vector>
#include <DirectXMath.h>
#include "../../externals/entt/entt.hpp"

namespace Engine {
    class Renderer;
}

namespace Game {

// 川コンポーネント (ObjectTypes.h)
struct RiverComponent;

// 川のメッシュを作るシステム
class RiverSystem {
public:
    // 制御点を通るスプラインに沿って、幅のある水面メッシュを作る
    static void BuildRiverMesh(RiverComponent& river, Engine::Renderer* renderer, entt::registry& registry, const DirectX::XMFLOAT3& ownerPos = {0,0,0});

private:
    // スプライン上の t (0〜1) の位置を求める
    static DirectX::XMVECTOR InterpolateSpline(const std::vector<DirectX::XMFLOAT3>& points, float t);
};

} // namespace Game
