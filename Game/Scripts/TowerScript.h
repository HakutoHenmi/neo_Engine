#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include "../../externals/entt/entt.hpp"

namespace Game {

class TowerScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override; 

private:
	float rotateSpeed_ = 2.0f; // 回転速度（1秒あたり）
};

} // namespace Game