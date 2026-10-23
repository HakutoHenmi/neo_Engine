// CylinderEffectScript.cpp
// 光の柱のエフェクトの実装。
#include "CylinderEffectScript.h"
#include "GameScene.h"
#include "../ObjectTypes.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

// 開始: メッシュとトランスフォームが無ければ用意する (青い円柱・専用シェーダー)
void CylinderEffectScript::Start(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetComponents();
	
	if (!registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.emplace<MeshRendererComponent>(entity);
		mr.modelPath = "Resources/Models/Cylinder/cylinder.obj";
		mr.texturePath = ""; // テクスチャは使わない
		mr.shaderName = "EnergyCylinder";
		mr.color = { 0.2f, 0.5f, 1.0f, 1.0f }; // 青い円柱

		auto* renderer = scene->GetRenderer();
		if (renderer) {
			mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
			// テクスチャが必要ならここで読み込む
		}
	}
	
	if (!registry.all_of<TransformComponent>(entity)) {
		auto& tc = registry.emplace<TransformComponent>(entity);
		tc.scale = { 1.0f, startScaleY_, 1.0f };
	}
}

// 更新: 減速しながら縦に伸ばし、だんだん透明にする。時間が来たら削除する
void CylinderEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	timer_ += dt;
	auto& registry = scene->GetComponents();
	
	if (registry.all_of<TransformComponent>(entity) && registry.all_of<MeshRendererComponent>(entity)) {
		auto& tc = registry.get<TransformComponent>(entity);
		auto& mr = registry.get<MeshRendererComponent>(entity);
		
		float t = timer_ / duration_;
		if (t > 1.0f) t = 1.0f;
		
		// 最初は速く、だんだん遅くなる (EaseOutCubic)
		float easeT = 1.0f - std::pow(1.0f - t, 3.0f);
		tc.scale.y = startScaleY_ + (endScaleY_ - startScaleY_) * easeT;
		
		// だんだん透明にする
		mr.color.w = 1.0f - t;
	}

	if (timer_ >= duration_) {
		if (scene) {
			scene->DestroyObject(static_cast<uint32_t>(entity));
		}
	}
}

// 破棄時の処理は無い / パラメータは保存しない
void CylinderEffectScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

std::string CylinderEffectScript::SerializeParameters() {
	return "{}";
}

void CylinderEffectScript::DeserializeParameters(const std::string& /*data*/) {
}

// ScriptEngine へ自動登録する
REGISTER_SCRIPT(CylinderEffectScript);

} // namespace Game
