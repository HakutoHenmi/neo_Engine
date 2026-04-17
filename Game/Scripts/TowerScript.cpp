#include "TowerScript.h"
#include "ScriptEngine.h"
#include "ObjectTypes.h"

namespace Game {

void TowerScript::Start(entt::entity entity, GameScene* scene) {
	(void)scene;
	(void)entity;
}

void TowerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)scene;
	(void)entity;
	// Y軸回転を増やし続ける
	if (scene->GetRegistry().all_of<TransformComponent>(entity)) {
		scene->GetRegistry().get<TransformComponent>(entity).rotate.y += rotateSpeed_ * dt;
	}
}

void TowerScript::OnDestroy(entt::entity entity, GameScene* scene) {

(void)scene;
	(void)entity;
}

REGISTER_SCRIPT(TowerScript);

} // namespace Game