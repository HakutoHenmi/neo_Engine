#include "HitEffectScript.h"
#include "GameScene.h"
#include "../ObjectTypes.h"
#include "ScriptEngine.h"

namespace Game {

void HitEffectScript::Start(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();
	
	if (!registry.all_of<ParticleEmitterComponent>(entity)) {
		auto& pe = registry.emplace<ParticleEmitterComponent>(entity);
		pe.emitter.params.name = "HitEffect";
		pe.emitter.params.position = {0, 0, 0};
		pe.emitter.params.emitRate = 0; // 継続放出なし
		pe.emitter.params.burstCount = 15; // バーストで放出
		
		// 色とサイズ
		pe.emitter.params.startColor = {1.0f, 0.8f, 0.2f, 1.0f}; // オレンジ
		pe.emitter.params.endColor = {1.0f, 0.2f, 0.0f, 0.0f}; // 赤く消える
		pe.emitter.params.startSize = {0.3f, 0.3f, 0.3f};
		pe.emitter.params.startSizeVariance = {0.1f, 0.1f, 0.1f};
		
		// 速度と形状
		pe.emitter.params.startVelocity = {0, 3.0f, 0};
		pe.emitter.params.velocityVariance = {5.0f, 5.0f, 5.0f};
		pe.emitter.params.shape = Engine::EmissionShape::Sphere;
		pe.emitter.params.shapeRadius = 0.2f;
		pe.emitter.params.damping = 2.0f;
		
		// 寿命
		pe.emitter.params.lifeTime = 0.4f;
		pe.emitter.params.lifeTimeVariance = 0.2f;

		// Planeの回転を使用（ビルボード無効化）
		pe.emitter.params.useBillboard = false;
		pe.emitter.params.angularVelocity = {10.0f, 10.0f, 10.0f};
		pe.emitter.params.angularVelocityVariance = {5.0f, 5.0f, 5.0f};
	}
}

void HitEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	timer_ += dt;
	if (timer_ >= duration_) {
		auto& registry = scene->GetRegistry();
		if (registry.all_of<ScriptComponent>(entity)) {
			auto& sc = registry.get<ScriptComponent>(entity);
			auto it = std::remove_if(sc.scripts.begin(), sc.scripts.end(),
				[](const auto& entry) { return entry.scriptPath == "HitEffectScript"; });
			sc.scripts.erase(it, sc.scripts.end());
		}
	}
}

void HitEffectScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

std::string HitEffectScript::SerializeParameters() {
	return "{}";
}

void HitEffectScript::DeserializeParameters(const std::string& /*data*/) {
}

REGISTER_SCRIPT(HitEffectScript);

} // namespace Game
