// ScriptSystem.h
// PLAY 中、ScriptComponent を持つエンティティのスクリプトを毎フレーム実行するシステム。
#pragma once
#include "ISystem.h"

namespace Game {

// GameScene の前方宣言 (ここでは中身を使わない)
class GameScene; // 前方宣言

// スクリプトのシステム
class ScriptSystem : public ISystem {
public:
	// スクリプトへ渡すシーンを設定する
	void SetScene(GameScene* scene) {
		scene_ = scene;
	}

	// 更新: 実行中にエンティティが増減しても安全なよう、一覧を複製してから順に実行する
	void Update(entt::registry& registry, GameContext& ctx) override;

	// PLAY 開始時: 動いているスクリプトをすべて止める
	void Reset(entt::registry& registry) override;

private:
	// スクリプトへ渡すシーン
	GameScene* scene_ = nullptr;
};

} // namespace Game
