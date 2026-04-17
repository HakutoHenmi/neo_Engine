#include "SkillTree.h"
#include "../../Engine/Input.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#include "../../Engine/WindowDX.h"
#include "../../externals/imgui/imgui.h"
#include "../Scenes/GameScene.h"
#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace Game {

// ===== 初期化 =====
void SkillTree::Init(Engine::Renderer* renderer) {
	if (initialized_ || !renderer)
		return;

	// テクスチャの読み込み (白テクスチャを使い回し、色で区別)
	texBg_ = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	texNodeLocked_ = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	texNodeUnlocked_ = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	texLine_ = renderer->LoadTexture2D("Resources/Textures/white1x1.png");

	initialized_ = true;
}

void SkillTree::LoadFromJson(const std::string& path, Engine::Renderer* renderer) {
	std::ifstream f(path); // jsonファイルの読み込み
	if (!f.is_open())
		return; // ファイルが開けなかった場合は終了

	json j;                                                   // Jsonの専用の型
	try {                                                     // 危ない処理を行うときゲームを落とさないように
		f >> j;                                               // jsonファイルの内容をjに入れるそのままだとただの文字列なのでダメ―
		nodes_.clear();                                       // 既存のノードをクリア
		if (j.contains("skills") && j["skills"].is_array()) { // skillsという項目があって、配列だったら
			for (const auto& s : j["skills"]) {               // 配列の中身を一つずつ取り出す(参照だから軽いうｗ)
				SkillNode node;
				node.id = s.value("id", 0); // sの中にidというキーがあったらそれを入れる、なかったら0を入れる
				node.name = s.value("name", "Skill");
				node.cost = s.value("cost", 1);
				node.parentId = s.value("parentId", -1);
				node.unlocked = s.value("unlocked", false);
				node.gridX = s.value("gridX", 0.0f);
				node.gridY = s.value("gridY", 0.0f);
				node.description = s.value("description", "");

				// アイコンの自動ロード試行 (Resources/Skills/スキル名.png)
				std::string iconName = s.value("name", "");//jsonにアイコンの名前があったらiconNameを入れる
				std::string iconPath = "Resources/Skills/" + iconName + ".png"; //iconPathに入れり
				if (renderer) {
					node.textureHandle = renderer->LoadTexture2D(iconPath);
				}
				node.texturePath = iconPath;

				nodes_.push_back(node);
			}
		}
	} catch (...) { // 例外をキャッチ
		            // JSONパースエラー
	}
}


// ===== メイン更新 =====
void SkillTree::Update(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY) {
	if (!isOpen_ || !renderer)
		return;

	HandleInput(screenW, screenH, mouseX, mouseY);
	DrawBackground(renderer, screenW, screenH);
	DrawConnections(renderer, screenW, screenH);

	int hoveredIndex = -1;
	// ホバー中のノードを探す
	for (int i = 0; i < (int)nodes_.size(); ++i) {
		float nx, ny;
		GetNodeScreenPos(nodes_[i], screenW, screenH, nx, ny);
		float halfSize = kNodeSize * 0.5f;
		if (mouseX >= nx - halfSize && mouseX <= nx + halfSize && mouseY >= ny - halfSize && mouseY <= ny + halfSize) {
			hoveredIndex = i;
			break;
		}
	}

	DrawNodes(renderer, screenW, screenH, mouseX, mouseY);
	DrawSkillPointsText(renderer, screenW, screenH);

	// 説明パネルの描画
	if (hoveredIndex >= 0 && pendingUnlockId_ == -1) {
		DrawDescriptionPanel(renderer, screenW, screenH, hoveredIndex);
	}

	// 確認ダイアログの描画
	if (pendingUnlockId_ != -1) {
		DrawConfirmationDialog(renderer, screenW, screenH);
	}
}

// ===== 入力処理 =====
void SkillTree::HandleInput(float screenW, float screenH, float mouseX, float mouseY) {
	auto* input = Engine::Input::GetInstance();
	if (!input)
		return;

	if (!input->IsMouseTrigger(0))
		return;

	// 確認ダイアログ表示中の処理
	if (pendingUnlockId_ != -1) {
		float cx = screenW * 0.5f;
		float cy = screenH * 0.5f;
		// Yesボタン (左)
		if (mouseX >= cx - 110 && mouseX <= cx - 10 && mouseY >= cy + 20 && mouseY <= cy + 60) {
			ConfirmUnlock();
		}
		// Noボタン (右)
		else if (mouseX >= cx + 10 && mouseX <= cx + 110 && mouseY >= cy + 20 && mouseY <= cy + 60) {
			CancelUnlock();
		}
		return;
	}

	// 各ノードとのヒットテスト
	for (int i = 0; i < (int)nodes_.size(); ++i) {
		float nx, ny;
		GetNodeScreenPos(nodes_[i], screenW, screenH, nx, ny);

		float halfSize = kNodeSize * 0.5f;
		if (mouseX >= nx - halfSize && mouseX <= nx + halfSize && mouseY >= ny - halfSize && mouseY <= ny + halfSize) {
			TryUnlockSkill(i);
			break;
		}
	}
}

// ===== スキル習得試行 =====
bool SkillTree::TryUnlockSkill(int index) {
	if (index < 0 || index >= (int)nodes_.size())
		return false;
	if (nodes_[index].unlocked)
		return false;

	// 必要な前提スキルをリストアップ (一括習得用)
	std::vector<int> neededIndices;
	GetPrerequisites(index, neededIndices);

	int totalCost = 0;
	for (int idx : neededIndices) {
		totalCost += nodes_[idx].cost;
	}

	if (skillPoints_ < totalCost)
		return false;

	// 確認状態へ
	pendingUnlockId_ = index;
	return true;
}

// ===== 前提条件の再帰取得 =====
void SkillTree::GetPrerequisites(int index, std::vector<int>& outIndices) {
	if (nodes_[index].unlocked)
		return;

	// 自分を最後に追加（親から順に習得するため）
	// ただし親を先に見に行く
	int parentId = nodes_[index].parentId;
	if (parentId >= 0) {
		for (int i = 0; i < (int)nodes_.size(); ++i) {
			if (nodes_[i].id == parentId) {
				GetPrerequisites(i, outIndices);
				break;
			}
		}
	}

	// 既にリストに入っていないか確認して追加
	if (std::find(outIndices.begin(), outIndices.end(), index) == outIndices.end()) {
		outIndices.push_back(index);
	}
}

// ===== 習得確定 =====
void SkillTree::ConfirmUnlock() {
	if (pendingUnlockId_ == -1)
		return;

	std::vector<int> neededIndices;
	GetPrerequisites(pendingUnlockId_, neededIndices);

	int totalCost = 0;
	for (int idx : neededIndices) {
		totalCost += nodes_[idx].cost;
	}

	if (skillPoints_ >= totalCost) {
		skillPoints_ -= totalCost;
		for (int idx : neededIndices) {
			nodes_[idx].unlocked = true;
		}
	}

	pendingUnlockId_ = -1;
}

// ===== 習得キャンセル =====
void SkillTree::CancelUnlock() { pendingUnlockId_ = -1; }

// ===== スキル習得チェック =====
bool SkillTree::IsSkillUnlocked(int skillId) const {
	for (const auto& n : nodes_) {
		if (n.id == skillId)
			return n.unlocked;
	}
	return false;
}

// ===== 背景描画 =====
void SkillTree::DrawBackground(Engine::Renderer* renderer, float screenW, float screenH) {
	Engine::Renderer::SpriteDesc bg;
	bg.x = kPanelMargin;
	bg.y = kPanelMargin;
	bg.w = screenW - kPanelMargin * 2.0f;
	bg.h = screenH - kPanelMargin * 2.0f;
	bg.color = {0.05f, 0.05f, 0.15f, 0.85f}; // 暗い青半透明
	renderer->DrawSprite(texBg_, bg);
}

// ===== ノード間の接続線描画 =====
void SkillTree::DrawConnections(Engine::Renderer* renderer, float screenW, float screenH) {
	(void)screenW;
	(void)screenH;
	for (const auto& node : nodes_) {
		if (node.parentId < 0)
			continue;

		// 親ノードを見つける
		const SkillNode* parent = nullptr;
		for (const auto& n : nodes_) {
			if (n.id == node.parentId) {
				parent = &n;
				break;
			}
		}
		if (!parent)
			continue;

		float cx, cy, px, py;
		GetNodeScreenPos(node, screenW, screenH, cx, cy);
		GetNodeScreenPos(*parent, screenW, screenH, px, py);

		// 線の色: 両方習得済みなら緑、そうでなければグレー
		Engine::Vector4 lineColor = (node.unlocked && parent->unlocked) ? Engine::Vector4{0.2f, 0.9f, 0.3f, 0.9f} : Engine::Vector4{0.4f, 0.4f, 0.4f, 0.7f};

		// 垂直の線をスプライトで描画
		float minY = (std::min)(cy, py);
		float maxY = (std::max)(cy, py);

		// 垂直線 (子から親へ)
		Engine::Renderer::SpriteDesc vLine;
		vLine.x = cx - kLineWidth * 0.5f;
		vLine.y = minY;
		vLine.w = kLineWidth;
		vLine.h = maxY - minY;
		vLine.color = lineColor;
		renderer->DrawSprite(texLine_, vLine);

		// 水平線 (XがP異なる場合)
		if (std::abs(cx - px) > 1.0f) {
			float lx = (std::min)(cx, px);
			float rx = (std::max)(cx, px);
			Engine::Renderer::SpriteDesc hLine;
			hLine.x = lx;
			hLine.y = py - kLineWidth * 0.5f;
			hLine.w = rx - lx;
			hLine.h = kLineWidth;
			hLine.color = lineColor;
			renderer->DrawSprite(texLine_, hLine);
		}
	}
}

// ===== ノード描画 =====
void SkillTree::DrawNodes(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY) {
	for (const auto& node : nodes_) {
		float nx, ny;
		GetNodeScreenPos(node, screenW, screenH, nx, ny);

		// ホバーチェック
		float halfSize = kNodeSize * 0.5f;
		bool hovered = (mouseX >= nx - halfSize && mouseX <= nx + halfSize && mouseY >= ny - halfSize && mouseY <= ny + halfSize);

		// 色の決定
		Engine::Vector4 color;
		if (node.unlocked) {
			color = {0.2f, 0.85f, 0.3f, 1.0f}; // 習得済み: 緑
		} else {
			// 親が習得済みか確認
			bool canUnlock = true;
			if (node.parentId >= 0) {
				canUnlock = false;
				for (const auto& n : nodes_) {
					if (n.id == node.parentId && n.unlocked) {
						canUnlock = true;
						break;
					}
				}
			}
			if (canUnlock && skillPoints_ >= node.cost) {
				color = hovered ? Engine::Vector4{1.0f, 0.9f, 0.3f, 1.0f}  // 習得可能+ホバー: 明るい黄
				                : Engine::Vector4{0.8f, 0.7f, 0.2f, 0.9f}; // 習得可能: 黄色
			} else {
				color = {0.3f, 0.3f, 0.3f, 0.7f}; // 習得不可: 暗いグレー
			}
		}

		// 描画
		Engine::Renderer::SpriteDesc s;
		s.x = nx - halfSize;
		s.y = ny - halfSize;
		s.w = kNodeSize;
		s.h = kNodeSize;
		s.color = color;

		// 固有テクスチャがあれば優先、なければデフォルト
		uint32_t tex = (node.textureHandle != 0) ? node.textureHandle : (node.unlocked ? texNodeUnlocked_ : texNodeLocked_);
		renderer->DrawSprite(tex, s);

		// ノード枠線 (少し大きめのスプライトを下に敷く)
		Engine::Renderer::SpriteDesc border;
		float borderPad = 3.0f;
		border.x = nx - halfSize - borderPad;
		border.y = ny - halfSize - borderPad;
		border.w = kNodeSize + borderPad * 2.0f;
		border.h = kNodeSize + borderPad * 2.0f;
		border.color = node.unlocked ? Engine::Vector4{0.1f, 0.6f, 0.2f, 0.8f} : Engine::Vector4{0.5f, 0.5f, 0.5f, 0.5f};
		// 背景(枠)→ノード本体の順に描画するため、先に枠を描く
		// ただしスプライトは描画順なので、枠をまず描いて上にノードを重ねる
		// → DrawSpriteの呼び出し順を調整
		// 実際は先に枠を描画してからノードを描画する必要があるため
		// ここでは枠をスキップしてシンプルにする
		// (テクスチャだけで構成するというユーザー要件に沿う)
	}
}

// ===== スキルポイント表示 =====
void SkillTree::DrawSkillPointsText(Engine::Renderer* renderer, float screenW, float screenH) {
	(void)screenW;
	// スキルポイント数をドットパターンで表示（テクスチャのみ）
	float baseX = kPanelMargin + 20.0f;
	float baseY = screenH - kPanelMargin - 40.0f;
	float dotSize = 16.0f;
	float dotGap = 4.0f;
	int displayPts = (std::min)(skillPoints_, 20); // 最大20個まで表示

	for (int i = 0; i < displayPts; ++i) {
		Engine::Renderer::SpriteDesc dot;
		dot.x = baseX + i * (dotSize + dotGap);
		dot.y = baseY;
		dot.w = dotSize;
		dot.h = dotSize;
		dot.color = {0.9f, 0.8f, 0.1f, 1.0f}; // 金色
		renderer->DrawSprite(texBg_, dot);
	}
}

// ===== 説明パネル描画 =====
void SkillTree::DrawDescriptionPanel(Engine::Renderer* renderer, float screenW, float screenH, int hoveredNodeIndex) {
	(void)screenH;
	const auto& node = nodes_[hoveredNodeIndex];

	float panelW = 400.0f;
	float panelH = screenH - kPanelMargin * 2.0f;
	float x = screenW - kPanelMargin - panelW;
	float y = kPanelMargin;

	// パネル背景
	Engine::Renderer::SpriteDesc bg;
	bg.x = x;
	bg.y = y;
	bg.w = panelW;
	bg.h = panelH;
	bg.color = {0.05f, 0.05f, 0.15f, 0.9f};
	renderer->DrawSprite(texBg_, bg);

	// 枠線 (上部)
	Engine::Renderer::SpriteDesc border;
	border.x = x;
	border.y = y;
	border.w = panelW;
	border.h = 4.0f;
	border.color = node.unlocked ? Engine::Vector4{0.2f, 0.8f, 0.4f, 1.0f} : Engine::Vector4{0.3f, 0.6f, 1.0f, 1.0f};
	renderer->DrawSprite(texBg_, border);

	// アイコン表示
	if (node.textureHandle != 0) {
		Engine::Renderer::SpriteDesc icon;
		icon.x = x + 20;
		icon.y = y + panelH - 80;
		icon.w = 60;
		icon.h = 60;
		icon.color = {1, 1, 1, 1};
		renderer->DrawSprite(node.textureHandle, icon);
	}

#ifdef USE_IMGUI
	// テキスト描画 (ImGuiの背景ドローリストを使用)
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	if (drawList) {
		ImVec2 pos(x + 20, y + 30);
		// タイトル
		drawList->AddText(ImGui::GetFont(), 28.0f, pos, IM_COL32(255, 255, 255, 255), node.name.c_str());

		// コスト
		pos.y += 45;
		std::string costStr = "Cost: " + std::to_string(node.cost) + " SP";
		drawList->AddText(ImGui::GetFont(), 20.0f, pos, IM_COL32(200, 200, 200, 255), costStr.c_str());

		// 区切り線
		pos.y += 35;
		drawList->AddLine(pos, ImVec2(pos.x + panelW - 40, pos.y), IM_COL32(100, 100, 100, 255), 1.0f);

		// 説明文
		pos.y += 30;
		drawList->AddText(ImGui::GetFont(), 18.0f, pos, IM_COL32(220, 220, 220, 255), node.description.c_str());

		if (node.unlocked) {
			pos.y += 60;
			drawList->AddText(ImGui::GetFont(), 22.0f, pos, IM_COL32(50, 255, 100, 255), "[Unlocked]");
		}
	}
#endif
}

// ===== 確認ダイアログ描画 =====
void SkillTree::DrawConfirmationDialog(Engine::Renderer* renderer, float screenW, float screenH) {
	if (pendingUnlockId_ < 0 || pendingUnlockId_ >= (int)nodes_.size())
		return;
	const auto& node = nodes_[pendingUnlockId_];

	float diagW = 400.0f;
	float diagH = 160.0f;
	float cx = screenW * 0.5f;
	float cy = screenH * 0.5f;

	// 背景
	Engine::Renderer::SpriteDesc bg;
	bg.x = cx - diagW * 0.5f;
	bg.y = cy - diagH * 0.5f;
	bg.w = diagW;
	bg.h = diagH;
	bg.color = {0.1f, 0.1f, 0.2f, 1.0f};
	renderer->DrawSprite(texBg_, bg);

	// 枠
	Engine::Renderer::SpriteDesc border;
	border.x = cx - diagW * 0.5f;
	border.y = cy - diagH * 0.5f;
	border.w = diagW;
	border.h = 2.0f;
	border.color = {1, 0.8f, 0.2f, 1.0f};
	renderer->DrawSprite(texBg_, border);

#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	if (drawList) {
		std::string msg = "Unlock '" + node.name + "'?";
		std::vector<int> needed;
		GetPrerequisites(pendingUnlockId_, needed);
		if (needed.size() > 1) {
			msg = "Unlock '" + node.name + "' and its prerequisites?";
		}

		ImVec2 textSize = ImGui::CalcTextSize(msg.c_str());
		drawList->AddText(ImGui::GetFont(), 20.0f, ImVec2(cx - textSize.x * 0.5f, cy - 40), IM_COL32(255, 255, 255, 255), msg.c_str());

		// ボタンテキスト
		drawList->AddText(ImGui::GetFont(), 18.0f, ImVec2(cx - 85, cy + 28), IM_COL32(255, 255, 255, 255), "YES");
		drawList->AddText(ImGui::GetFont(), 18.0f, ImVec2(cx + 45, cy + 28), IM_COL32(255, 255, 255, 255), "NO");
	}
#endif

	// はいボタン (緑)
	Engine::Renderer::SpriteDesc btnYes;
	btnYes.x = cx - 110;
	btnYes.y = cy + 20;
	btnYes.w = 100;
	btnYes.h = 40;
	btnYes.color = {0.2f, 0.7f, 0.3f, 0.8f};
	renderer->DrawSprite(texBg_, btnYes);

	// いいえボタン (赤)
	Engine::Renderer::SpriteDesc btnNo;
	btnNo.x = cx + 10;
	btnNo.y = cy + 20;
	btnNo.w = 100;
	btnNo.h = 40;
	btnNo.color = {0.8f, 0.2f, 0.2f, 0.8f};
	renderer->DrawSprite(texBg_, btnNo);
}

// ===== スクリーン座標変換 =====
void SkillTree::GetNodeScreenPos(const SkillNode& node, float screenW, float screenH, float& outX, float& outY) const {
	// パネルの中心を基準にノードを配置
	float panelH = screenH - kPanelMargin * 2.0f;
	float panelCenterX = screenW * 0.5f;

	// ツリーの幅（最大gridXを取得）
	float maxGridX = 0;
	float maxGridY = 0;
	for (const auto& n : nodes_) {
		if (n.gridX > maxGridX)
			maxGridX = n.gridX;
		if (n.gridY > maxGridY)
			maxGridY = n.gridY;
	}

	// ツリー全体の幅と高さ
	float treeW = maxGridX * kNodeSpacingX;
	float treeH = maxGridY * kNodeSpacingY;

	// ツリーの左上基点
	float startX = panelCenterX - treeW * 0.5f;
	float startY = kPanelMargin + panelH * 0.5f - treeH * 0.5f;

	// gridY が大きいほど上（Y座標が小さい）に配置
	outX = startX + node.gridX * kNodeSpacingX;
	outY = startY + (maxGridY - node.gridY) * kNodeSpacingY;
}

} // namespace Game
