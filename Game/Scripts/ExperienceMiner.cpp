#include "ExperienceMiner.h"
#include "ScriptEngine.h"
#include "Scenes/GameScene.h"
#include "ObjectTypes.h"

namespace Game {

void ExperienceMiner::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
}

void ExperienceMiner::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity)) return;
	registry.get<TransformComponent>(entity).rotate.y += 0.5f * dt;
}

void ExperienceMiner::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

REGISTER_SCRIPT(ExperienceMiner);
} // namespace Game