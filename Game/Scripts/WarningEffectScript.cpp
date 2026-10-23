// WarningEffectScript.cpp
// 攻撃の予兆の点滅の実装。
#include "WarningEffectScript.h"
#include "GameScene.h"
#include "../ObjectTypes.h"
#include "ScriptEngine.h" // REGISTER_SCRIPT を使うため
#include <cmath>
#include <string>

namespace Game {

// 開始: 元の色を覚える
void WarningEffectScript::Start(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetComponents();
	if (registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.get<MeshRendererComponent>(entity);
		origR_ = mr.color.x;
		origG_ = mr.color.y;
		origB_ = mr.color.z;
		origA_ = mr.color.w;
		originalColorSaved_ = true;
	}
}

// 更新: 元の色と赤の間を高速で往復させ、時間が来たら元に戻して終了する
void WarningEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (isFinished_) return;
	timer_ += dt;
	
	auto& registry = scene->GetComponents();
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
		// 時間切れで元の色に戻して終了する
		if (originalColorSaved_ && registry.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry.get<MeshRendererComponent>(entity);
			mr.color = {origR_, origG_, origB_, origA_};
			originalColorSaved_ = false; // 二度戻さないようにする
		}
		isFinished_ = true;
	}
}

// 破棄時: 色が戻っていなければ戻す
void WarningEffectScript::OnDestroy(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetComponents();
	if (originalColorSaved_ && registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.get<MeshRendererComponent>(entity);
		mr.color = {origR_, origG_, origB_, origA_};
	}
}

// 表示時間を保存する / 読み込む
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

// ScriptEngine へ自動登録する
REGISTER_SCRIPT(WarningEffectScript);

} // namespace Game
