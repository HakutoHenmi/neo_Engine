#pragma once
#include "IScript.h"
#include <vector>

namespace Game {

class CommunicationTestScript : public IScript {
public:
    void Start(entt::entity entity, GameScene* scene) override;
    void Update(entt::entity entity, GameScene* scene, float dt) override;

private:
    float timer_ = 0.0f;
    int testCount_ = 0;
};

} // namespace Game
