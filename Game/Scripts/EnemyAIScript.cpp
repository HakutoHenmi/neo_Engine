#include "EnemyAIScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <cstdlib> // rand()用
#include <iostream>
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "../../Engine/ThirdParty/nlohmann/json.hpp"

using json = nlohmann::json;

namespace Game {

void EnemyAIScript::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
	exploded_ = false; // 再プレイ時にリセット
}

void EnemyAIScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();

	// ==========================================
	// ★ 1. 死亡判定と爆発（破片生成）処理
	// ==========================================
	if (registry.all_of<HealthComponent>(entity)) {
		auto& health = registry.get<HealthComponent>(entity);
		if (health.hp <= 0.0f && !exploded_) {
			exploded_ = true;
			
			// 爆発処理 (後で新アーキテクチャのSpawnerと組み合わせるのが理想ですが、とりあえずエンティティ生成として対応)
			return; // 爆発処理を行ったら、以降の追尾処理はスキップする
		}
	}

	// ==========================================
	// ★ 2. プレイヤー追尾処理 (HPが残っている場合のみ)
	// ==========================================
	bool isAlive = true;
	if (registry.all_of<HealthComponent>(entity)) {
		isAlive = registry.get<HealthComponent>(entity).hp > 0.0f;
	}

	if (isAlive && registry.all_of<TransformComponent>(entity)) {
		auto& myTc = registry.get<TransformComponent>(entity);
		
		DirectX::XMFLOAT3 targetPos = {0, 0, 0};
		bool found = false;
		// プレイヤーを高速タグ検索で探す
		const auto& players = scene->GetEntitiesByTag("Player");
		if (!players.empty()) {
			entt::entity p = players[0];
			if (registry.valid(p) && registry.all_of<TransformComponent>(p)) {
				targetPos = registry.get<TransformComponent>(p).translate;
				found = true;
			}
		}

		if (found) {
			float dx = myTc.translate.x - targetPos.x;
			float dy = myTc.translate.y - targetPos.y;
			float dz = myTc.translate.z - targetPos.z;
			float distSq = dx * dx + dy * dy + dz * dz;

			// 視界範囲内なら移動
			if (distSq <= sightRange_ * sightRange_ && distSq > 0.001f) {
				float dist = std::sqrt(distSq);
				myTc.translate.x -= (dx / dist) * speed_ * dt;
				myTc.translate.y -= (dy / dist) * speed_ * dt;
				myTc.translate.z -= (dz / dist) * speed_ * dt;
			}
		}
	}
}

void EnemyAIScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void EnemyAIScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::SliderFloat("移動速度", &speed_, 0.0f, 20.0f, "%.1f m/s");
	ImGui::SliderFloat("索敵範囲", &sightRange_, 0.0f, 200.0f, "%.1f m");
#endif
}

std::string EnemyAIScript::SerializeParameters() {
	json j;
	j["speed"] = speed_;
	j["sightRange"] = sightRange_;
	return j.dump();
}

void EnemyAIScript::DeserializeParameters(const std::string& data) {
	if (data.empty()) return;
	try {
		json j = json::parse(data);
		if (j.contains("speed")) speed_ = j["speed"].get<float>();
		if (j.contains("sightRange")) sightRange_ = j["sightRange"].get<float>();
	} catch (...) {
	}
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(EnemyAIScript);

} // namespace Game