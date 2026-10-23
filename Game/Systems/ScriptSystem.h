// ScriptSystem.h
// PLAY 中、ScriptComponent を持つエンティティのスクリプトを毎フレーム実行するシステム。
#pragma once
#include "../Scripts/ScriptEngine.h"
#include "../Scripts/ScriptLifecycle.h"
#include "ISystem.h"
#include "../../Engine/JobSystem.h"

namespace Game {

// GameScene の前方宣言 (ここでは中身を使わない)
class GameScene; // 前方宣言

// スクリプトのシステム
class ScriptSystem : public ISystem {
public:
	// スクリプトへ渡すシーンを設定する
	void SetScene(GameScene* scene) { scene_ = scene; }

	// 更新: 実行中にエンティティが増減しても安全なよう、一覧を複製してから順に実行する
	void Update(entt::registry& registry, GameContext& ctx) override {
		// PLAY 中でなければ実行しない
		if (!ctx.isPlaying)
			return;

		// スクリプトを動かすエンジン
		auto* scriptEngine = ScriptEngine::GetInstance();

		// スクリプトを持つエンティティの一覧を複製する
		auto view = registry.view<ScriptComponent>();
		std::vector<entt::entity> entities(view.begin(), view.end());
		if (entities.empty()) return;

		// まだ有効で、スクリプトが有効なものだけを実行する
		for (auto entity : entities) {
			if (!registry.valid(entity)) continue;
			auto& sc = registry.get<ScriptComponent>(entity);
			if (sc.enabled && !sc.scripts.empty()) {
				scriptEngine->Execute(entity, scene_, ctx.dt);
			}
		}
	}

	// PLAY 開始時: 動いているスクリプトをすべて止める
	void Reset(entt::registry& registry) override {
		StopAllScripts(registry, scene_);
	}

private:
	// スクリプトへ渡すシーン
	GameScene* scene_ = nullptr;
};

} // namespace Game
