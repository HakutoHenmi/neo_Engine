#pragma once
#include "IScript.h"
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <memory>
#include "../../externals/entt/entt.hpp"

namespace Game {

// ★ エネミー出現パターン
enum class SpawnPattern : int {
	Point = 0,   // 単点（スポナー位置に全員出現）
	Circle = 1,  // 円形配置
	Line = 2,    // 直線配置
	Count
};

// ★ エネミースポナースクリプト
// ウェーブ数、種類、配列、全体時間(Duration)、最大出現数を管理する
class EnemySpawnerScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

	// エディターから3Dプレビューを描画するためのヘルパー
	// スポナーの位置(worldPos)を渡すと、パターンに応じた出現予定点を描画する
	void DrawSpawnPreview(const DirectX::XMFLOAT3& worldPos) const;

	// --- パラメータ (公開) ---
	int waveCount = 1;            // ウェーブ数
	int startWave = 0;            // 開始ウェーブ (0-indexed)
	float waveDelay = 2.0f;       // ウェーブ開始時の待機時間 (秒)
	int enemyType = 0;            // エネミー種類 (ID) - 後方互換のため維持
	std::string enemyScriptPath = "EnemyBehavior"; // 出現させるエネミーのスクリプト名
	std::string enemyScriptParams = "";            // 出現させる敵スクリプトのパラメータ(JSON)
	float spawnDuration = 10.0f;  // 開始～終了までの全体時間 (秒)
	SpawnPattern pattern = SpawnPattern::Point; // 配列パターン
	float patternRadius = 3.0f;   // Circle/Line の半径・長さ
	int maxCount = 5;             // 1ウェーブあたりの最大出現数

private:
	// ランタイム用
	int currentWave_ = 0;
	int spawnedThisWave_ = 0;
	float elapsedTime_ = 0.0f;
	bool isWaitingDelay_ = true;

	// エディタUI用の一時的なスクリプトインスタンス
	std::shared_ptr<IScript> editorScriptInstance_ = nullptr;
	std::string lastLoadedPath_ = "";

	// 計算されたインターバル
	float CalcInterval() const;
};

} // namespace Game
