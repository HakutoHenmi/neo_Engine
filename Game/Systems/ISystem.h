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

// 各Systemに渡す共有コンテキスト
// 経過時間・カメラ・描画
struct GameContext {
	// 1 フレームの経過時間
	float dt = 0.0f;
	Engine::Camera* camera = nullptr;
	Engine::Renderer* renderer = nullptr;
	class GameScene* scene = nullptr; // ゲームシーン
	// 入力の管理
	Engine::Input* input = nullptr;
	Engine::EventSystem* eventSystem = nullptr; // スクリプト間のイベント通知
	// PLAY 中か
	bool isPlaying = false;
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
