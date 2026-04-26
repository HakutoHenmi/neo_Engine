#pragma once
#include "BaseScript.h"
#include <DirectXMath.h>

namespace Game {

class BossTestScript : public BaseScript {
public:
    void Start(entt::entity entity, GameScene* scene) override;
    void Update(entt::entity entity, GameScene* scene, float dt) override;
    void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
    DirectX::XMFLOAT3 originalScale_ = {1.0f, 1.0f, 1.0f};
    entt::entity tailEntity_ = entt::null;
    BossState prevBossState_ = BossState::Idle; // ★追加: 状態変化検出用
};

} // namespace Game
