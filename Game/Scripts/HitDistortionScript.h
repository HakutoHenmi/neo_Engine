#pragma once
#include "IScript.h"
#include "ObjectTypes.h"
#include <DirectXMath.h>

namespace Game {

class HitDistortionScript : public IScript {
public:
    void Start(entt::entity entity, GameScene* scene) override;
    void Update(entt::entity entity, GameScene* scene, float dt) override;
    void OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) override {}

private:
    float timer_ = 0.0f;
    float duration_ = 0.4f;     // 高速化 (2.5 -> 0.4)
    float startScale_ = 0.1f;
    float endScale_ = 6.0f;     // 範囲縮小 (12.0 -> 6.0)
    float initialAlpha_ = 1.5f; 
};

} // namespace Game
