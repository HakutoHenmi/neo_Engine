#include "WarningEffectScript.h"
#include "GameScene.h"
#include "../ObjectTypes.h"
#include "ScriptEngine.h" // ★追加
#include <cmath>
#include <string>

namespace Game {

void WarningEffectScript::Start(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();
	if (registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.get<MeshRendererComponent>(entity);
		origR_ = mr.color.x;
		origG_ = mr.color.y;
		origB_ = mr.color.z;
		origA_ = mr.color.w;
		originalColorSaved_ = true;
	}
}

void WarningEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	timer_ += dt;
	
	auto& registry = scene->GetRegistry();
	if (registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.get<MeshRendererComponent>(entity);
		
		// 高速で赤い点滅（サインカーブ）
		float pulse = std::abs(std::sin(timer_ * 40.0f)); 
		
		// 基本色と赤(1.0, 0.2, 0.2)の間を往復
		mr.color.x = origR_ + (1.0f - origR_) * pulse;
		mr.color.y = origG_ + (0.2f - origG_) * pulse;
		mr.color.z = origB_ + (0.2f - origB_) * pulse;
	}

	if (timer_ >= duration_) {
		// 時間切れで元の色に戻して、このスクリプトを自ら削除する
		if (originalColorSaved_ && registry.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry.get<MeshRendererComponent>(entity);
			mr.color = {origR_, origG_, origB_, origA_};
		}
		
		if (registry.all_of<ScriptComponent>(entity)) {
			auto& sc = registry.get<ScriptComponent>(entity);
			// 自身(WarningEffectScript)を削除
			auto it = std::remove_if(sc.scripts.begin(), sc.scripts.end(),
				[](const auto& entry) { return entry.scriptPath == "WarningEffectScript"; });
			sc.scripts.erase(it, sc.scripts.end());
		}
	}
}

void WarningEffectScript::OnDestroy(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();
	if (originalColorSaved_ && registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.get<MeshRendererComponent>(entity);
		mr.color = {origR_, origG_, origB_, origA_};
	}
}

std::string WarningEffectScript::SerializeParameters() {
	return "{ \"duration\": " + std::to_string(duration_) + " }";
}

void WarningEffectScript::DeserializeParameters(const std::string& data) {
	// 簡易パース
	size_t pos = data.find("\"duration\"");
	if (pos != std::string::npos) {
		size_t colon = data.find(":", pos);
		if (colon != std::string::npos) {
			duration_ = std::stof(data.substr(colon + 1));
		}
	}
}

REGISTER_SCRIPT(WarningEffectScript);

} // namespace Game
