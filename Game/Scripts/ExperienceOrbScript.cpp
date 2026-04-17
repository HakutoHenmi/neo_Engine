#include "ExperienceOrbScript.h"
#include "../../externals/imgui/imgui.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <cstdlib>
namespace Game {

void ExperienceOrbScript::Start(entt::entity entity, GameScene* scene) {
	(void)scene;
	(void)entity;
}

void ExperienceOrbScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity)) return;
	auto& tc = registry.get<TransformComponent>(entity);
	tc.rotate.y += 1.5f * dt;

	// プレイヤーを探す
	entt::entity playerEntity = scene->FindObjectByName("Player");
	if (playerEntity != entt::null && registry.valid(playerEntity) && registry.all_of<TransformComponent>(playerEntity)) {
		auto& playerTc = registry.get<TransformComponent>(playerEntity);
		float dx = playerTc.translate.x - tc.translate.x;
		float dy = (playerTc.translate.y + 1.0f) - tc.translate.y;
		float dz = playerTc.translate.z - tc.translate.z;
		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (distance < collectRange_) {
			// 消滅
			if (registry.all_of<HealthComponent>(entity)) {
				registry.get<HealthComponent>(entity).hp = 0.0f;
			}
			return;
		}

		if (distance < speed_ && distance > 0.0001f) {
			// 吸い寄せ
			float dirX = dx / distance;
			float dirY = dy / distance;
			float dirZ = dz / distance;
			tc.translate.x += dirX * speed_ * dt;
			tc.translate.y += dirY * speed_ * dt;
			tc.translate.z += dirZ * speed_ * dt;
			return;
		}
	}

	lifetime_ -= dt;
	if (lifetime_ <= 0.0f) {
		if (registry.all_of<HealthComponent>(entity)) {
			registry.get<HealthComponent>(entity).isDead = true;
		}
	}
}

void ExperienceOrbScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(ExperienceOrbScript);

} // namespace Game