#include "BulletTank.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {
void BulletTank::Start(entt::entity /*entity*/, GameScene* /*scene*/) {}

void BulletTank::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity)) return;
	registry.get<TransformComponent>(entity).rotate.y += rotationSpeed_ * dt;
}

void BulletTank::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}
REGISTER_SCRIPT(BulletTank);
} // namespace Game