// UIComponents.h
// 画面表示のコンポーネント: UI の配置・画像・文字・ボタン、頭上の HP バー、ダメージの数字。
// ObjectTypes.h がすべての Components/*.h をまとめて読み込む。
#pragma once
#include "ComponentBase.h"

namespace Game {

// UI の配置 (スクリーン座標・大きさ・アンカー・ピボット・回転)
struct RectTransformComponent : public Component {
	DirectX::XMFLOAT2 pos = {0, 0};   // スクリーン座標
	DirectX::XMFLOAT2 size = {100, 100};
	DirectX::XMFLOAT2 anchor = {0.5f, 0.5f}; // 0.0〜1.0
	DirectX::XMFLOAT2 pivot = {0.5f, 0.5f};
	float rotation = 0.0f;
	RectTransformComponent() { type = ComponentType::RectTransform; }
};

// UI の画像。9-Slice は角を伸ばさずに拡縮する設定
struct UIImageComponent : public Component {
	uint32_t textureHandle = 0;
	std::string texturePath = "";
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	bool is9Slice = false;
	float borderTop = 10.0f;
	float borderBottom = 10.0f;
	float borderLeft = 10.0f;
	float borderRight = 10.0f;
	int layer = 0; // 描画レイヤー (大きいほど手前)
	UIImageComponent() { type = ComponentType::UIImage; }
};

// UI の文字列
struct UITextComponent : public Component {
	std::string text = "New Text";
	float fontSize = 24.0f;
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	std::string fontPath = "C:\\Windows\\Fonts\\msgothic.ttc"; // 使用するフォントファイル
	UITextComponent() { type = ComponentType::UIText; }
};

// UI のボタン。ホバー/押下状態と色、押されたときに呼ぶ名前
struct UIButtonComponent : public Component {
	bool isHovered = false;
	bool isPressed = false;
	DirectX::XMFLOAT4 normalColor = {1, 1, 1, 1};
	DirectX::XMFLOAT4 hoverColor = {0.8f, 0.8f, 0.8f, 1.0f};
	DirectX::XMFLOAT4 pressedColor = {0.6f, 0.6f, 0.6f, 1.0f};
	std::string onClickCallback = ""; // スクリプト側のメソッド名など
	
	// クリック判定の範囲を見た目とは別に調整する (中心のずれと拡大率)
	DirectX::XMFLOAT2 hitboxOffset = {0.0f, 0.0f};
	DirectX::XMFLOAT2 hitboxScale = {1.0f, 1.0f};

	UIButtonComponent() { type = ComponentType::UIButton; }
};

// ワールド空間UIコンポーネント
struct WorldSpaceUIComponent : public Component {
	bool showHealthBar = true;
	bool showDamageNumbers = true;
	DirectX::XMFLOAT3 offset = {0, 1.2f, 0};
	float barWidth = 60.0f;
	float barHeight = 6.0f;
	WorldSpaceUIComponent() { type = ComponentType::WorldSpaceUI; }
};

// ダメージ数字の表示
struct DamageNumberComponent : public Component {
	float damage = 0.0f;
	float lifetime = 1.0f;
	float maxLifetime = 1.0f;
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	DirectX::XMFLOAT3 startPos = {0,0,0};
	DamageNumberComponent() { type = static_cast<ComponentType>(999); } // 数字が浮かび上がって消えるまでの表示状態 (実行時専用)
};

} // namespace Game
