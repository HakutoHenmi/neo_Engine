#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Engine {
	class Renderer;
}

namespace Game {

class GameScene;

// スキルノード構造体
struct SkillNode {
	int id = 0;
	std::string name;
	std::string texturePath;			// スキルアイコンのテクスチャパス
	int cost = 1;						// 習得に必要なスキルポイント
	int parentId = -1;					// 親スキルID (-1 = ルートノード)
	bool unlocked = false;				// 習得済みか
	float gridX = 0.0f;				// ツリー内の列位置 (0〜)
	float gridY = 0.0f;				// ツリー内の行位置 (0=最下段)
	std::string description;			// スキルの説明文
	uint32_t textureHandle = 0;		// スキル固有のアイコン（あれば）
};

// スキルツリー管理クラス
// PhaseSystemScript などのメンバとして置くだけで使えます
class SkillTree {
public:
	void Init(Engine::Renderer* renderer);
	void LoadFromJson(const std::string& path, Engine::Renderer* renderer);
	void Update(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY);

	// 開閉切り替え
	void Toggle() { isOpen_ = !isOpen_; if(!isOpen_) pendingUnlockId_ = -1; }
	bool IsOpen() const { return isOpen_; }
	void Close() { isOpen_ = false; pendingUnlockId_ = -1; }

	// スキルポイント管理
	int GetSkillPoints() const { return skillPoints_; }
	void AddSkillPoints(int pts) { skillPoints_ += pts; }

	// スキルが習得済みかチェック
	bool IsSkillUnlocked(int skillId) const;

private:
	void DrawBackground(Engine::Renderer* renderer, float screenW, float screenH);
	void DrawNodes(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY);
	void DrawConnections(Engine::Renderer* renderer, float screenW, float screenH);
	void DrawSkillPointsText(Engine::Renderer* renderer, float screenW, float screenH);
	void DrawDescriptionPanel(Engine::Renderer* renderer, float screenW, float screenH, int hoveredNodeIndex);
	void DrawConfirmationDialog(Engine::Renderer* renderer, float screenW, float screenH);
	void HandleInput(float screenW, float screenH, float mouseX, float mouseY);
	bool TryUnlockSkill(int index);
	void ConfirmUnlock();
	void CancelUnlock();

	// 前提条件の再帰取得 (一括解放用)
	void GetPrerequisites(int index, std::vector<int>& outIndices);

	// スクリーン座標に変換
	void GetNodeScreenPos(const SkillNode& node, float screenW, float screenH, float& outX, float& outY) const;

private:
	bool isOpen_ = false;
	bool initialized_ = false;
	int skillPoints_ = 5;				// 初期スキルポイント
	int pendingUnlockId_ = -1;			// 確認中のノードID

	std::vector<SkillNode> nodes_;

	// テクスチャハンドル
	uint32_t texBg_ = 0;				// パネル背景
	uint32_t texNodeLocked_ = 0;		// 未習得ノード
	uint32_t texNodeUnlocked_ = 0;		// 習得済ノード
	uint32_t texLine_ = 0;				// 接続線用

	// レイアウト定数
	static constexpr float kPanelMargin = 100.0f;		// 画面端からのマージン
	static constexpr float kNodeSize = 64.0f;			// ノードの描画サイズ
	static constexpr float kNodeSpacingX = 100.0f;		// ノード間のX間隔
	static constexpr float kNodeSpacingY = 100.0f;		// ノード間のY間隔
	static constexpr float kLineWidth = 4.0f;			// 接続線の幅
};

} // namespace Game
