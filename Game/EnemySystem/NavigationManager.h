#pragma once
#include <vector>
#include <queue>
#include <cstdint>

// 名前空間の前方宣言
namespace Game {
	class GameScene;
}

// 1マスの情報
struct FlowCell {
	uint32_t cost;	// 1:平地, 255:壁
	float bestCost; // ゴールまでの蓄積コスト
	float dirX, dirZ; // 移動すべき方向(正規化済み)
};

class NavigationManager {
public:
	/// <summary>
	/// マップ情報の初期化
	/// </summary>
	/// <param name="width">マップの幅</param>
	/// <param name="height">マップの奥行</param>
	/// <param name="cellSize">セル一つ当たりのサイズ</param>
	void Initialize(int width, int height, float cellSize, float originX, float originZ);

	// 全マスのcostを更新(壁や設備が立って地形が変わったとき用)
	void UpdateCostMap(class Game::GameScene* scene);

	// 目的地(コアの位置など)からフローフィールドを計算する
	void GenerateFlowField(float targetWorldX, float targetWorldZ);

	// ベクトル場を生成する
	void CalculateDirections();

	// 敵が自分の位置から方向を取得するための関数
	void GetDirection(float worldX, float worldZ, float& outX, float& outZ);

	// デバッグ用
	void DrawDebug(class Game::GameScene* scene);

private:
	// グリッド座標からインデックスを取得
	int GetIndex(int x, int z) { return z * width_ + x; }

private:
	std::vector<FlowCell> grid_;
	int width_, height_;
	float cellSize_;
	float originX_, originZ_; // グリッドの開始（左下）座標
};