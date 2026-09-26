#pragma once
#include "../ObjectTypes.h"
#include "../../Engine/Camera.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/EventSystem.h" // ★追加: イベントシステム
#include <vector>
#include <algorithm>
#include "../../externals/entt/entt.hpp"

namespace Game {

// The combo clock uses real time; only hostile AI consumes the reduced time scale.
struct CombatFlowState {
	int combo = 0;
	float remaining = 0.0f;
	float enemyScale = 1.0f;

	void Reset() { combo = 0; remaining = 0.0f; enemyScale = 1.0f; }
	void Tick(float dt) {
		if (remaining > 0.0f) {
			remaining = (std::max)(0.0f, remaining - dt);
			if (remaining == 0.0f) combo = 0;
		}
		const float target = combo >= 4 ? 0.45f : combo >= 3 ? 0.58f : combo >= 2 ? 0.75f : 1.0f;
		enemyScale += (target - enemyScale) * (std::min)(1.0f, dt * 9.0f);
	}
	void RegisterHit(bool defeated) {
		combo = (std::min)(12, combo + 1 + (defeated ? 1 : 0));
		remaining = 1.8f;
	}
};

// 各Systemに渡す共有コンテキスト
struct GameContext {
	float dt = 0.0f;
	CombatFlowState* combatFlow = nullptr;
	Engine::Camera* camera = nullptr;
	Engine::Renderer* renderer = nullptr;
	class GameScene* scene = nullptr; // ★追加
	Engine::Input* input = nullptr;
	Engine::EventSystem* eventSystem = nullptr; // ★追加: スクリプト間通信用
	bool isPlaying = false;
	bool isSandbagMode = false; // ★追加: サンドバッグモード
	entt::registry* pendingSpawns = nullptr; // SpawnObject等の遅延追加用

	// ★追加: 座標系補正用 (エディターGameビュー等での相対座標)
	bool useOverrideMouse = false;
	float overrideMouseX = 0.0f;
	float overrideMouseY = 0.0f;
	DirectX::XMFLOAT2 viewportOffset = { 0, 0 };
	DirectX::XMFLOAT2 viewportSize = { 0, 0 };
};

// System基底インターフェース
class ISystem {
public:
	virtual ~ISystem() = default;
	virtual void Update(entt::registry& registry, GameContext& ctx) = 0;
	virtual void Draw(entt::registry& /*registry*/, GameContext& /*ctx*/) {} // 描画処理用
	virtual void DrawUI(entt::registry& /*registry*/, GameContext& /*ctx*/) {} // ImGui等のUI処理用
	virtual void Reset(entt::registry& /*registry*/) {} // Play開始時のリセット
};

} // namespace Game
