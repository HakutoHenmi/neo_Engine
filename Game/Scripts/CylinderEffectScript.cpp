#include "CylinderEffectScript.h"
#include "GameScene.h"
#include "../ObjectTypes.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void CylinderEffectScript::Start(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();
	
	if (!registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.emplace<MeshRendererComponent>(entity);
		mr.modelPath = "Resources/Models/Cylinder/cylinder.obj";
		mr.texturePath = ""; // 適当なテクスチャ
		mr.shaderName = "EnergyCylinder";
		mr.color = { 0.2f, 0.5f, 1.0f, 1.0f }; // 青っぽいシリンダー

		auto& context = scene->GetContext();
		if (context.renderer) {
			mr.modelHandle = context.renderer->LoadObjMesh(mr.modelPath);
			// テクスチャが必要であればロード
		}
	}
	
	if (!registry.all_of<TransformComponent>(entity)) {
		auto& tc = registry.emplace<TransformComponent>(entity);
		tc.scale = { 1.0f, startScaleY_, 1.0f };
	}
}

void CylinderEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	timer_ += dt;
	auto& registry = scene->GetRegistry();
	
	if (registry.all_of<TransformComponent>(entity) && registry.all_of<MeshRendererComponent>(entity)) {
		auto& tc = registry.get<TransformComponent>(entity);
		auto& mr = registry.get<MeshRendererComponent>(entity);
		
		float t = timer_ / duration_;
		if (t > 1.0f) t = 1.0f;
		
		float easeT = 1.0f - std::pow(1.0f - t, 3.0f);
		tc.scale.y = startScaleY_ + (endScaleY_ - startScaleY_) * easeT;
		
		// フェードアウト
		mr.color.w = 1.0f - t;
	}

	if (timer_ >= duration_) {
		if (scene) {
			scene->DestroyObject(static_cast<uint32_t>(entity));
		}
	}
}

void CylinderEffectScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

std::string CylinderEffectScript::SerializeParameters() {
	return "{}";
}

void CylinderEffectScript::DeserializeParameters(const std::string& /*data*/) {
}

REGISTER_SCRIPT(CylinderEffectScript);

} // namespace Game
