// WaveSystem.h
// 旧アクションゲームのステージの進行。敵とボスが全滅したらクリアにし、
// クリア表示・結果 (時間・体力・被弾回数・ランク・使った缶)・クリア後のメニューを描く。
#pragma once
#include "ISystem.h"
#include "../Scenes/GameScene.h"
#include "../ObjectTypes.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/WindowDX.h"
#include "../CanLoadout.h"
#include "PlayerActionSystem.h"
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include "../Scripts/GameManagerScript.h"
#pragma comment(lib, "xinput.lib")

namespace Game {

// ステージクリアの判定と結果画面のシステム (クリア後も更新を続ける)
class WaveSystem : public ISystem {
public:
	// クリア後も更新を続けるシステムとして作る
	WaveSystem() : ISystem(true) {}
	// プレイ中 / クリア
	enum class State {
		Playing,
		Clear
	};

	// 状態・クリア表示の不透明度・選択中のボタン・前フレームのパッド入力・結果の記録
	State state = State::Playing;
	float clearAlpha = 0.0f;
	int warmupFrames_ = 0; // スクリプト初期化を待つ猶予フレーム
	int selectedButton_ = 1; // 0: やり直し, 1: 装備, 2: セレクト
	// 矩形を描くための白いテクスチャ
	Engine::Renderer::TextureHandle whiteTexture_ = 0;
	// 前フレームのパッドのボタン (押した瞬間の判定用)
	bool prevA_ = false;
	bool prevB_ = false;
	bool prevLeft_ = false;
	bool prevRight_ = false;
	// 経過時間と、クリア時に記録した結果
	float elapsedTime_ = 0.0f;
	float resultTime_ = 0.0f;
	float resultHp_ = 0.0f;
	float resultMaxHp_ = 100.0f;
	int resultDamageTaken_ = 0;
	char resultRank_ = 'C';
	bool resultCaptured_ = false;
	// 使った缶 (缶の種類ごと)
	std::array<bool, 9> usedCans_{};

	// ボタンの矩形
	struct ButtonRect {
		float x;
		float y;
		float w;
		float h;
	};

	// PLAY 中の更新
	void Update(entt::registry& registry, GameContext& ctx) override;

	// PLAY 開始時: 状態と結果をすべて初期化する
	void Reset(entt::registry& /*registry*/) override;

private:
	// 現在の缶と選択中の缶を「使った缶」として記録する
	void TrackUsedCans(entt::registry& registry);

	// クリア時の時間・体力・被弾回数を記録してランクを求める
	void CaptureResult(entt::registry& registry);

	// ランク: 時間と被弾回数で減点し、残り体力で加点した点数から S/A/B/C を決める
	char CalculateRank() const {
		// 残り体力の割合
		const float hpRate = resultMaxHp_ > 0.0f ? std::clamp(resultHp_ / resultMaxHp_, 0.0f, 1.0f) : 0.0f;
		const float score = 100.0f - resultTime_ * 0.18f - static_cast<float>(resultDamageTaken_) * 10.0f + hpRate * 20.0f;
		if (score >= 95.0f) return 'S';
		if (score >= 75.0f) return 'A';
		if (score >= 55.0f) return 'B';
		return 'C';
	}

	// 秒を「分:秒.百分の一秒」の文字列にする
	std::string FormatTime(float seconds) const {
		// 百分の一秒の単位に直して分・秒・百分の一秒に分ける
		const int totalCentiseconds = static_cast<int>(seconds * 100.0f);
		const int minutes = totalCentiseconds / 6000;
		const int sec = (totalCentiseconds / 100) % 60;
		const int centis = totalCentiseconds % 100;
		char text[32];
		std::snprintf(text, sizeof(text), "%02d:%02d.%02d", minutes, sec, centis);
		return text;
	}

	// クリアメニューのボタンの矩形 (中央の装備ボタンだけ幅が広い)
	ButtonRect GetButtonRect(const GameContext& ctx, int index) const;

	// 点が矩形の中にあるか
	bool PointInRect(float x, float y, const ButtonRect& r) const {
		return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
	}

	// クリア後はプレイヤーの入力を止める
	void FreezePlayerInput(entt::registry& registry);

	// 選択を左右に動かす (端で折り返す)
	void MoveSelection(int dir) {
		// 3 つのボタンの間で選択を回す
		selectedButton_ = (selectedButton_ + dir + 3) % 3;
	}

	// 決定: 0 はやり直し、1 は装備を変えにセレクトへ (同じ設定で)、2 はセレクトへ
	void ConfirmSelection();

	// クリアメニューの操作 (キーボード・マウス・パッド)。Esc はセレクトへ
	void UpdateClearMenu(GameContext& ctx);

	// 白テクスチャで矩形を描く
	void DrawRect(Engine::Renderer* renderer, float x, float y, float w, float h, const Engine::Vector4& color, int layer);

	// 中央揃えの文字を描く
	void DrawCenteredText(Engine::Renderer* renderer, const std::string& text, float centerX, float y, float scale, const Engine::Vector4& color) {
		// 文字の幅を測って中央に置く
		const float w = renderer->MeasureTextWidth(text, scale);
		renderer->DrawString(text, centerX - w * 0.5f, y, scale, color);
	}

	// ランクごとの色
	Engine::Vector4 RankColor() const {
		// S は金、A は緑、B は水色、C は赤
		if (resultRank_ == 'S') return {1.0f, 0.95f, 0.24f, clearAlpha};
		if (resultRank_ == 'A') return {0.28f, 1.0f, 0.56f, clearAlpha};
		if (resultRank_ == 'B') return {0.34f, 0.82f, 1.0f, clearAlpha};
		return {1.0f, 0.38f, 0.42f, clearAlpha};
	}

	// 結果の項目 (見出しと値) を描く
	void DrawMetric(Engine::Renderer* renderer, const char* label, const std::string& value, float x, float y) {
		// 見出しは小さく、値はその右に大きく
		renderer->DrawString(label, x, y, 0.28f, {0.68f, 0.78f, 0.92f, clearAlpha});
		renderer->DrawString(value, x + 190.0f, y - 2.0f, 0.38f, {1.0f, 0.98f, 0.86f, clearAlpha});
	}

	// 使った缶を缶の色の札として描く
	void DrawUsedCanPill(Engine::Renderer* renderer, CanType type, float x, float y, float w);

	// 結果のパネル (クリア時間・体力・被弾回数・ランク・使った缶) を描く
	void DrawResultPanel(GameContext& ctx);

	// クリアメニューのパネルとボタンを描く
	void DrawClearMenu(GameContext& ctx);
};

} // namespace Game
