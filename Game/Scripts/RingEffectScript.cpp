#include "RingEffectScript.h"
#include "GameScene.h"
#include "../ObjectTypes.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void RingEffectScript::Start(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();
	
	if (!registry.all_of<MeshRendererComponent>(entity)) {
		auto& dmr = registry.emplace<MeshRendererComponent>(entity);
		dmr.modelPath = "Resources/Models/plane.obj";
		dmr.texturePath = "Resources/Textures/ripple_normal.png";
		dmr.shaderName = "Distortion";
		dmr.color = { 1.0f, 1.0f, 1.0f, 1.0f };

		auto& context = scene->GetContext();
		if (context.renderer) {
			dmr.modelHandle = context.renderer->LoadObjMesh(dmr.modelPath);
			dmr.textureHandle = context.renderer->LoadTexture2D(dmr.texturePath);
		}
	}
	
	if (!registry.all_of<TransformComponent>(entity)) {
		auto& dtc = registry.emplace<TransformComponent>(entity);
		dtc.scale = { 0.1f, 0.1f, 0.1f };
		
		auto& context = scene->GetContext();
		if (context.camera) {
			dtc.rotate = context.camera->Rotation();
			dtc.rotate.x -= DirectX::XM_PIDIV2;
		} else {
			dtc.rotate = { 0, 0, 0 };
		}
	}
}

void RingEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	timer_ += dt;
	
	auto& registry = scene->GetRegistry();
	if (registry.all_of<TransformComponent>(entity) && registry.all_of<MeshRendererComponent>(entity)) {
		auto& tc = registry.get<TransformComponent>(entity);
		auto& mr = registry.get<MeshRendererComponent>(entity);
		
		float t = timer_ / duration_;
		if (t > 1.0f) t = 1.0f;
		
		float easeT = 1.0f - std::pow(1.0f - t, 3.0f);
		float s = startScale_ + (endScale_ - startScale_) * easeT;
		tc.scale = { s, s, s };
		mr.color.w = 1.0f - easeT;

		auto& context = scene->GetContext();
		if (isBillboard_ && context.camera) {
			tc.rotate = context.camera->Rotation();
			tc.rotate.x -= DirectX::XM_PIDIV2;
		}
		
		if (registry.all_of<HitboxComponent>(entity)) {
			auto& hb = registry.get<HitboxComponent>(entity);
			hb.size.x = s * 2.0f;
			hb.size.z = s * 2.0f;
		}
	}

	if (timer_ >= duration_) {
		if (registry.all_of<ScriptComponent>(entity)) {
			auto& sc = registry.get<ScriptComponent>(entity);
			auto it = std::remove_if(sc.scripts.begin(), sc.scripts.end(),
				[](const auto& entry) { return entry.scriptPath == "RingEffectScript"; });
			sc.scripts.erase(it, sc.scripts.end());
		}
	}
}

void RingEffectScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

std::string RingEffectScript::SerializeParameters() {
	return "{}";
}

void RingEffectScript::DeserializeParameters(const std::string& /*data*/) {
}

REGISTER_SCRIPT(RingEffectScript);

} // namespace Game
