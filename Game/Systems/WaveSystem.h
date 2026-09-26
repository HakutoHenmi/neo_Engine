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

class WaveSystem : public ISystem {
public:
	enum class State {
		Playing,
		Clear
	};

	State state = State::Playing;
	float clearAlpha = 0.0f;
	int warmupFrames_ = 0; // スクリプト初期化を待つ猶予フレーム
	int selectedButton_ = 1; // 0: Retry, 1: Equipment, 2: Select
	Engine::Renderer::TextureHandle whiteTexture_ = 0;
	bool prevA_ = false;
	bool prevB_ = false;
	bool prevLeft_ = false;
	bool prevRight_ = false;
	float elapsedTime_ = 0.0f;
	float resultTime_ = 0.0f;
	float resultHp_ = 0.0f;
	float resultMaxHp_ = 100.0f;
	int resultDamageTaken_ = 0;
	char resultRank_ = 'C';
	bool resultCaptured_ = false;
	std::array<bool, 9> usedCans_{};

	struct ButtonRect {
		float x;
		float y;
		float w;
		float h;
	};

	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// ScriptSystemがWaveSystemの後に実行されるため、
		// 最初の数フレームはスクリプトの初期化を待つ
		if (warmupFrames_ < 3) {
			warmupFrames_++;
			return;
		}

		// スクリプトインスタンスがなければ動的に生成してアタッチする（フォールバック）
		if (!GameManagerScript::GetInstance()) {
			entt::entity gmEntity = ctx.scene->CreateEntity("GameManager");
			auto& sc = registry.emplace<ScriptComponent>(gmEntity);
			sc.scripts.push_back({"GameManagerScript", "", nullptr, false});
			ScriptEngine::GetInstance()->Execute(gmEntity, ctx.scene, 0.0f); // Startを呼ぶため
		}

		// プレイヤーの生存確認
		bool playerAlive = false;
		auto pView = registry.view<PlayerInputComponent, HealthComponent>();
		if (pView.begin() != pView.end()) {
			auto& hc = pView.get<HealthComponent>(*pView.begin());
			if (!hc.isDead) playerAlive = true;
		}

		if (!playerAlive) return;
		if (state == State::Playing) {
			elapsedTime_ += ctx.dt;
			TrackUsedCans(registry);
		}

		// 敵の数を数える
		int enemyCount = 0;
		auto eView = registry.view<EnemyAIComponent>();
		for (auto entity : eView) {
			if (registry.all_of<HealthComponent>(entity)) {
				if (!registry.get<HealthComponent>(entity).isDead) enemyCount++;
			} else {
				enemyCount++; // Initialization pending
			}
		}
		auto bView = registry.view<BossActionComponent>();
		for (auto entity : bView) {
			if (registry.all_of<HealthComponent>(entity)) {
				if (!registry.get<HealthComponent>(entity).isDead) enemyCount++;
			} else {
				enemyCount++; // Initialization pending
			}
		}


		if (state == State::Playing) {
			if (enemyCount == 0) {
				state = State::Clear;
				CaptureResult(registry);
			}
		} else if (state == State::Clear) {
			Engine::WindowDX::SetCursorVisible(true);
			FreezePlayerInput(registry);

			// 徐々にフェードイン
			clearAlpha += ctx.dt;
			if (clearAlpha > 1.0f) clearAlpha = 1.0f;
			UpdateClearMenu(ctx);

			// STAGE CLEAR テキストの描画
			if (ctx.renderer) {
				if (whiteTexture_ == 0) {
					whiteTexture_ = ctx.renderer->LoadTexture2D("Resources/Textures/white1x1.png");
				}
				auto* gm = GameManagerScript::GetInstance();
				std::string msg = gm ? gm->clearText : "STAGE CLEAR!";
				float scale = (std::min)(gm ? gm->clearTextScale : 3.0f, 2.15f);
				float cColor[4] = {1.0f, 0.8f, 0.2f, 1.0f};
				if (gm) { cColor[0]=gm->clearColor[0]; cColor[1]=gm->clearColor[1]; cColor[2]=gm->clearColor[2]; cColor[3]=gm->clearColor[3]; }

				float centerX = ctx.viewportOffset.x + ctx.viewportSize.x * 0.5f;
				float centerY = ctx.viewportOffset.y + ctx.viewportSize.y * 0.26f;

				float width = ctx.renderer->MeasureTextWidth(msg, scale);
				float sx = centerX - width * 0.5f;

				// シャドウとメイン
				ctx.renderer->DrawString(msg, sx + 5.0f, centerY + 5.0f, scale, {0.0f, 0.0f, 0.0f, clearAlpha});
				ctx.renderer->DrawString(msg, sx, centerY, scale, {cColor[0], cColor[1], cColor[2], clearAlpha});
				DrawResultPanel(ctx);
				DrawClearMenu(ctx);
			}
		}
	}

	void Reset(entt::registry& /*registry*/) override {
		state = State::Playing;
		clearAlpha = 0.0f;
		warmupFrames_ = 0;
		selectedButton_ = 1;
		prevA_ = false;
		prevB_ = false;
		prevLeft_ = false;
		prevRight_ = false;
		elapsedTime_ = 0.0f;
		resultTime_ = 0.0f;
		resultHp_ = 0.0f;
		resultMaxHp_ = 100.0f;
		resultDamageTaken_ = 0;
		resultRank_ = 'C';
		resultCaptured_ = false;
		usedCans_.fill(false);
	}

private:
	void TrackUsedCans(entt::registry& registry) {
		auto actionView = registry.view<PlayerActionComponent>();
		for (auto entity : actionView) {
			const auto& action = actionView.get<PlayerActionComponent>(entity);
			const int index = static_cast<int>(action.currentCan);
			if (index > static_cast<int>(CanType::None) && index < static_cast<int>(usedCans_.size())) {
				usedCans_[index] = true;
			}
		}

		auto inputView = registry.view<PlayerInputComponent>();
		for (auto entity : inputView) {
			const auto& input = inputView.get<PlayerInputComponent>(entity);
			const int index = static_cast<int>(input.selectedCan);
			if (index > static_cast<int>(CanType::None) && index < static_cast<int>(usedCans_.size())) {
				usedCans_[index] = true;
			}
		}
	}

	void CaptureResult(entt::registry& registry) {
		if (resultCaptured_) return;
		resultCaptured_ = true;
		resultTime_ = elapsedTime_;

		auto playerView = registry.view<PlayerInputComponent, HealthComponent>();
		if (playerView.begin() != playerView.end()) {
			const auto& health = playerView.get<HealthComponent>(*playerView.begin());
			resultHp_ = health.hp;
			resultMaxHp_ = health.maxHp > 0.0f ? health.maxHp : 100.0f;
			resultDamageTaken_ = health.damageTakenCount;
		}
		resultRank_ = CalculateRank();
	}

	char CalculateRank() const {
		const float hpRate = resultMaxHp_ > 0.0f ? std::clamp(resultHp_ / resultMaxHp_, 0.0f, 1.0f) : 0.0f;
		const float score = 100.0f - resultTime_ * 0.18f - static_cast<float>(resultDamageTaken_) * 10.0f + hpRate * 20.0f;
		if (score >= 95.0f) return 'S';
		if (score >= 75.0f) return 'A';
		if (score >= 55.0f) return 'B';
		return 'C';
	}

	std::string FormatTime(float seconds) const {
		const int totalCentiseconds = static_cast<int>(seconds * 100.0f);
		const int minutes = totalCentiseconds / 6000;
		const int sec = (totalCentiseconds / 100) % 60;
		const int centis = totalCentiseconds % 100;
		char text[32];
		std::snprintf(text, sizeof(text), "%02d:%02d.%02d", minutes, sec, centis);
		return text;
	}

	ButtonRect GetButtonRect(const GameContext& ctx, int index) const {
		const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
		const float viewH = ctx.viewportSize.y > 0.0f ? ctx.viewportSize.y : static_cast<float>(Engine::WindowDX::kH);
		const float w = index == 1 ? 300.0f : 230.0f;
		const float h = 68.0f;
		const float gap = 24.0f;
		const float totalW = 230.0f + gap + 300.0f + gap + 230.0f;
		const float startX = viewW * 0.5f - totalW * 0.5f;
		const float y = viewH * 0.68f + 68.0f;
		if (index == 0) return {startX, y, w, h};
		if (index == 1) return {startX + 230.0f + gap, y, w, h};
		return {startX + 230.0f + gap + 300.0f + gap, y, w, h};
	}

	bool PointInRect(float x, float y, const ButtonRect& r) const {
		return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
	}

	void FreezePlayerInput(entt::registry& registry) {
		auto playerView = registry.view<PlayerInputComponent>();
		for (auto entity : playerView) {
			auto& pi = playerView.get<PlayerInputComponent>(entity);
			pi.moveDir = {0.0f, 0.0f};
			pi.jumpRequested = false;
			pi.attackRequested = false;
			pi.cameraYaw = 0.0f;
			pi.cameraPitch = 0.0f;
			pi.isRadialMenuOpen = false;
		}
	}

	void MoveSelection(int dir) {
		selectedButton_ = (selectedButton_ + dir + 3) % 3;
	}

	void ConfirmSelection() {
		if (selectedButton_ == 0) {
			Engine::SceneManager::GetInstance()->RequestChange("Game", Engine::SceneManager::GetInstance()->CurrentParameters());
		} else if (selectedButton_ == 1) {
			Engine::SceneManager::GetInstance()->RequestChange("Equipment", Engine::SceneManager::GetInstance()->CurrentParameters());
		} else {
			Engine::SceneManager::GetInstance()->RequestChange("Select");
		}
	}

	void UpdateClearMenu(GameContext& ctx) {
		auto* input = ctx.input;
		if (!input) return;

		if (input->Trigger(0xCB) || input->Trigger(0x1E)) MoveSelection(-1); // Left / A
		if (input->Trigger(0xCD) || input->Trigger(0x20)) MoveSelection(1);  // Right / D
		if (input->Trigger(0x1C) || input->Trigger(0x39)) ConfirmSelection(); // Enter / Space
		if (input->Trigger(0x01)) {
			selectedButton_ = 2;
			ConfirmSelection();
			return;
		}

		float mx = 0.0f;
		float my = 0.0f;
		input->GetMousePos(mx, my);
		if (ctx.viewportSize.x > 0.0f && ctx.viewportSize.y > 0.0f) {
			mx = (mx - ctx.viewportOffset.x) * static_cast<float>(Engine::WindowDX::kW) / ctx.viewportSize.x;
			my = (my - ctx.viewportOffset.y) * static_cast<float>(Engine::WindowDX::kH) / ctx.viewportSize.y;
		}
		for (int i = 0; i < 3; ++i) {
			if (PointInRect(mx, my, GetButtonRect(ctx, i))) {
				selectedButton_ = i;
				if (input->IsMouseTrigger(0)) {
					ConfirmSelection();
					return;
				}
			}
		}

		XINPUT_STATE statePad = {};
		if (XInputGetState(0, &statePad) == ERROR_SUCCESS) {
			const bool currA = (statePad.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
			if (currA && !prevA_) ConfirmSelection();
			prevA_ = currA;

			const bool currB = (statePad.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
			if (currB && !prevB_) {
				selectedButton_ = 2;
				ConfirmSelection();
			}
			prevB_ = currB;

			const bool currLeft = (statePad.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
			if (currLeft && !prevLeft_) MoveSelection(-1);
			prevLeft_ = currLeft;

			const bool currRight = (statePad.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
			if (currRight && !prevRight_) MoveSelection(1);
			prevRight_ = currRight;
		}
	}

	void DrawRect(Engine::Renderer* renderer, float x, float y, float w, float h, const Engine::Vector4& color, int layer) {
		if (!renderer || whiteTexture_ == 0) return;
		Engine::Renderer::SpriteDesc desc;
		desc.x = x;
		desc.y = y;
		desc.w = w;
		desc.h = h;
		desc.color = color;
		desc.layer = layer;
		renderer->DrawSprite(whiteTexture_, desc);
	}

	void DrawCenteredText(Engine::Renderer* renderer, const std::string& text, float centerX, float y, float scale, const Engine::Vector4& color) {
		const float w = renderer->MeasureTextWidth(text, scale);
		renderer->DrawString(text, centerX - w * 0.5f, y, scale, color);
	}

	Engine::Vector4 RankColor() const {
		if (resultRank_ == 'S') return {1.0f, 0.95f, 0.24f, clearAlpha};
		if (resultRank_ == 'A') return {0.28f, 1.0f, 0.56f, clearAlpha};
		if (resultRank_ == 'B') return {0.34f, 0.82f, 1.0f, clearAlpha};
		return {1.0f, 0.38f, 0.42f, clearAlpha};
	}

	void DrawMetric(Engine::Renderer* renderer, const char* label, const std::string& value, float x, float y) {
		renderer->DrawString(label, x, y, 0.28f, {0.68f, 0.78f, 0.92f, clearAlpha});
		renderer->DrawString(value, x + 190.0f, y - 2.0f, 0.38f, {1.0f, 0.98f, 0.86f, clearAlpha});
	}

	void DrawUsedCanPill(Engine::Renderer* renderer, CanType type, float x, float y, float w) {
		Engine::Vector4 color = {0.42f, 0.48f, 0.58f, clearAlpha};
		switch (type) {
		case CanType::Fire: color = {0.95f, 0.22f, 0.12f, clearAlpha}; break;
		case CanType::Water: color = {0.18f, 0.55f, 1.0f, clearAlpha}; break;
		case CanType::Thunder: color = {1.0f, 0.84f, 0.16f, clearAlpha}; break;
		case CanType::Soda: color = {0.08f, 0.82f, 0.66f, clearAlpha}; break;
		case CanType::Ice: color = {0.38f, 0.88f, 1.0f, clearAlpha}; break;
		case CanType::Magnet: color = {0.78f, 0.32f, 1.0f, clearAlpha}; break;
		case CanType::Acid: color = {0.55f, 1.0f, 0.16f, clearAlpha}; break;
		case CanType::Bubble: color = {1.0f, 0.40f, 0.78f, clearAlpha}; break;
		default: break;
		}
		DrawRect(renderer, x - 3.0f, y - 3.0f, w + 6.0f, 34.0f, color, 288);
		DrawRect(renderer, x, y, w, 28.0f, {0.07f, 0.09f, 0.14f, 0.96f * clearAlpha}, 289);
		DrawRect(renderer, x, y, 8.0f, 28.0f, color, 290);
		DrawCenteredText(renderer, CanLoadout::Name(type), x + w * 0.5f, y + 6.0f, 0.28f, {0.92f, 0.96f, 1.0f, clearAlpha});
	}

	void DrawResultPanel(GameContext& ctx) {
		auto* renderer = ctx.renderer;
		if (!renderer || whiteTexture_ == 0) return;

		const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
		const float viewH = ctx.viewportSize.y > 0.0f ? ctx.viewportSize.y : static_cast<float>(Engine::WindowDX::kH);
		const float panelW = 880.0f;
		const float panelH = 218.0f;
		const float panelX = viewW * 0.5f - panelW * 0.5f;
		const float panelY = viewH * 0.42f;

		DrawRect(renderer, panelX - 4.0f, panelY - 4.0f, panelW + 8.0f, panelH + 8.0f, {0.18f, 0.78f, 0.98f, 0.86f * clearAlpha}, 280);
		DrawRect(renderer, panelX, panelY, panelW, panelH, {0.05f, 0.07f, 0.12f, 0.90f * clearAlpha}, 281);
		DrawRect(renderer, panelX, panelY, panelW, 8.0f, {1.0f, 0.33f, 0.62f, clearAlpha}, 282);
		DrawCenteredText(renderer, "CLEAR RESULT", viewW * 0.5f, panelY + 18.0f, 0.40f, {0.86f, 0.94f, 1.0f, clearAlpha});

		char hpText[48];
		std::snprintf(hpText, sizeof(hpText), "%.0f / %.0f", resultHp_, resultMaxHp_);
		DrawMetric(renderer, "CLEAR TIME", FormatTime(resultTime_), panelX + 44.0f, panelY + 62.0f);
		DrawMetric(renderer, "PLAYER HP", hpText, panelX + 44.0f, panelY + 104.0f);
		DrawMetric(renderer, "DAMAGE TAKEN", std::to_string(resultDamageTaken_), panelX + 44.0f, panelY + 146.0f);

		DrawCenteredText(renderer, "RANK", panelX + 700.0f, panelY + 54.0f, 0.34f, {0.68f, 0.78f, 0.92f, clearAlpha});
		std::string rankText(1, resultRank_);
		DrawCenteredText(renderer, rankText, panelX + 700.0f, panelY + 82.0f, 1.35f, RankColor());

		renderer->DrawString("USED CANS", panelX + 44.0f, panelY + 184.0f, 0.28f, {0.68f, 0.78f, 0.92f, clearAlpha});
		float pillX = panelX + 210.0f;
		bool hasUsedCan = false;
		for (int i = static_cast<int>(CanType::Fire); i <= static_cast<int>(CanType::Bubble); ++i) {
			if (!usedCans_[i]) continue;
			hasUsedCan = true;
			DrawUsedCanPill(renderer, static_cast<CanType>(i), pillX, panelY + 180.0f, 124.0f);
			pillX += 138.0f;
		}
		if (!hasUsedCan) {
			DrawUsedCanPill(renderer, CanType::None, pillX, panelY + 180.0f, 124.0f);
		}
	}

	void DrawClearMenu(GameContext& ctx) {
		auto* renderer = ctx.renderer;
		if (!renderer || whiteTexture_ == 0) return;

		const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
		const float viewH = ctx.viewportSize.y > 0.0f ? ctx.viewportSize.y : static_cast<float>(Engine::WindowDX::kH);
		const float panelW = 880.0f;
		const float panelH = 184.0f;
		const float panelX = viewW * 0.5f - panelW * 0.5f;
		const float panelY = viewH * 0.68f;

		DrawRect(renderer, panelX - 4.0f, panelY - 4.0f, panelW + 8.0f, panelH + 8.0f, {0.18f, 0.78f, 0.98f, 0.86f * clearAlpha}, 280);
		DrawRect(renderer, panelX, panelY, panelW, panelH, {0.05f, 0.07f, 0.12f, 0.88f * clearAlpha}, 281);
		DrawRect(renderer, panelX, panelY, panelW, 8.0f, {1.0f, 0.33f, 0.62f, clearAlpha}, 282);
		DrawCenteredText(renderer, "CLEAR MENU", viewW * 0.5f, panelY + 18.0f, 0.38f, {0.86f, 0.94f, 1.0f, clearAlpha});

		const char* labels[3] = {"RETRY", "EQUIPMENT", "SELECT"};
		const char* hints[3] = {"もう一度", "缶を変える", "セレクトへ"};
		for (int i = 0; i < 3; ++i) {
			const ButtonRect r = GetButtonRect(ctx, i);
			const bool selected = selectedButton_ == i;
			const Engine::Vector4 edge = selected ? Engine::Vector4{1.0f, 0.95f, 0.24f, clearAlpha} : Engine::Vector4{0.24f, 0.36f, 0.54f, clearAlpha};
			Engine::Vector4 fill = {0.16f, 0.20f, 0.30f, 0.95f * clearAlpha};
			if (i == 1) fill = {0.12f, 0.48f, 0.26f, 0.95f * clearAlpha};

			DrawRect(renderer, r.x - 5.0f, r.y - 5.0f, r.w + 10.0f, r.h + 10.0f, edge, 283);
			DrawRect(renderer, r.x, r.y, r.w, r.h, fill, 284);
			DrawCenteredText(renderer, labels[i], r.x + r.w * 0.5f, r.y + 12.0f, i == 1 ? 0.46f : 0.50f, {1.0f, 0.98f, 0.88f, clearAlpha});
			DrawCenteredText(renderer, hints[i], r.x + r.w * 0.5f, r.y + 43.0f, 0.22f, {0.86f, 0.94f, 1.0f, clearAlpha});
		}
		DrawCenteredText(renderer, "Mouse: click   Keyboard/Pad: move and confirm", viewW * 0.5f, panelY + 154.0f, 0.24f, {0.72f, 0.80f, 0.92f, clearAlpha});
	}
};

} // namespace Game
