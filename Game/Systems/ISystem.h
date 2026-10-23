// ISystem.h
// ゲームのシステム (ECS の処理単位) の基底クラスと、システムへ渡す共有データ。
#pragma once
#include "../ObjectTypes.h"
#include "../../Engine/Camera.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/EventSystem.h" // スクリプト間のイベント通知
#include <vector>
#include <algorithm>
#include "../../externals/entt/entt.hpp"

namespace Game {

// 戦闘の流れ (コンボと敵のスロー)。コンボの時間は実時間で進み、遅くなるのは敵の AI だけ
class CombatFlowState {
private:
	int combo = 0;
	float remaining = 0.0f;
	float enemyScale = 1.0f;
public:
    // 現在のコンボ数・コンボの残り時間・敵の時間の速さ
    int Combo() const { return combo; }
    float Remaining() const { return remaining; }
    float EnemyScale() const { return enemyScale; }
    void SetChronoEnemyScale(float scale) { enemyScale = scale; }

	// リセット / 時間を進める (コンボが続くほど敵の時間を遅くする) / 命中の記録 (倒すと +2)
	void Reset() { combo = 0; remaining = 0.0f; enemyScale = 1.0f; }
	// コンボが切れるまでの時間を減らし、切れたらコンボを 0 にする
	void Tick(float dt) {
		if (remaining > 0.0f) {
			remaining = (std::max)(0.0f, remaining - dt);
			if (remaining == 0.0f) combo = 0;
		}
		// コンボ数に応じた敵の時間の速さへなめらかに近づける
		const float target = combo >= 4 ? 0.45f : combo >= 3 ? 0.58f : combo >= 2 ? 0.75f : 1.0f;
		enemyScale += (target - enemyScale) * (std::min)(1.0f, dt * 9.0f);
	}
	void RegisterHit(bool defeated) {
		// コンボを増やし (倒したら 2)、残り時間を 1.8 秒にする
		combo = (std::min)(12, combo + 1 + (defeated ? 1 : 0));
		remaining = 1.8f;
	}
};

// 各Systemに渡す共有コンテキスト
// 経過時間・戦闘の流れ・カメラ・描画
struct GameContext {
	// 1 フレームの経過時間
	float dt = 0.0f;
	CombatFlowState* combatFlow = nullptr;
	Engine::Camera* camera = nullptr;
	Engine::Renderer* renderer = nullptr;
	class GameScene* scene = nullptr; // ゲームシーン
	// 入力の管理
	Engine::Input* input = nullptr;
	Engine::EventSystem* eventSystem = nullptr; // スクリプト間のイベント通知
	// PLAY 中か
	bool isPlaying = false;
	bool isSandbagMode = false; // サンドバッグモード (敵が反撃しない練習用)
	entt::registry* pendingSpawns = nullptr; // 生成を後から反映するためのレジストリ (現在は未使用)

	// エディタの Game ビューに表示しているときの座標補正 (マウス座標の上書き・ビューの位置と大きさ)
	bool useOverrideMouse = false;
	float overrideMouseX = 0.0f;
	float overrideMouseY = 0.0f;
	DirectX::XMFLOAT2 viewportOffset = { 0, 0 };
	DirectX::XMFLOAT2 viewportSize = { 0, 0 };
};

// System基底インターフェース
// システムの基底クラス
class ISystem {
public:
	// updateAfterStageClear が true なら、ステージクリア後も更新を続ける
	explicit ISystem(bool updateAfterStageClear = false) : updateAfterStageClear_(updateAfterStageClear) {}
	bool UpdatesAfterStageClear() const { return updateAfterStageClear_; }
	virtual ~ISystem() = default;
	// 毎フレームの更新 (必須)
	virtual void Update(entt::registry& registry, GameContext& ctx) = 0;
	virtual void Draw(entt::registry& /*registry*/, GameContext& /*ctx*/) {} // 描画処理用
	virtual void DrawUI(entt::registry& /*registry*/, GameContext& /*ctx*/) {} // ImGui等のUI処理用
	virtual void Reset(entt::registry& /*registry*/) {} // Play開始時のリセット
	// シーンを空にするときの後片付け (既定はリセットと同じ)
	virtual void Clear(entt::registry& registry) { Reset(registry); }
private:
	// ステージクリア後も更新するか
	bool updateAfterStageClear_;
};

} // namespace Game
