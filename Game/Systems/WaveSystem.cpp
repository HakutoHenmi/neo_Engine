// WaveSystem.cpp
// WaveSystem の関数の実装 (宣言は WaveSystem.h)。
#include "../Scenes/GameScene.h"
#include "../Scripts/ScriptEngine.h"
#include "EnemyAISystem.h"
#include "WaveSystem.h"

namespace Game {

void WaveSystem::Update(entt::registry& registry, GameContext& ctx) {
	// PLAY 中でなければ何もしない
	if (!ctx.isPlaying) return;

	// ScriptSystem は WaveSystem の後に実行されるため、
	// 最初の数フレームはスクリプトの初期化を待つ
	if (warmupFrames_ < 3) {
		warmupFrames_++;
		return;
	}

	// GameManagerScript が無ければ、作ってアタッチする (予備の処理)
	if (!GameManagerScript::GetInstance()) {
		entt::entity gmEntity = ctx.scene->CreateEntity("GameManager");
		auto& sc = registry.emplace<ScriptComponent>(gmEntity);
		sc.scripts.push_back({"GameManagerScript", "", nullptr, false});
		ScriptEngine::GetInstance()->Execute(gmEntity, ctx.scene, 0.0f); // Start を呼ぶために一度実行する
	}

	// プレイヤーの生存確認
	// 入力を持つプレイヤーの体力を調べる
	bool playerAlive = false;
	auto pView = registry.view<PlayerInputComponent, HealthComponent>();
	if (pView.begin() != pView.end()) {
		auto& hc = pView.get<HealthComponent>(*pView.begin());
		if (!hc.IsDead()) playerAlive = true;
	}

	// プレイヤーが倒れていれば進めない
	if (!playerAlive) return;
	// プレイ中は経過時間と使った缶を記録する
	if (state == State::Playing) {
		elapsedTime_ += ctx.dt;
		TrackUsedCans(registry);
	}

	// 敵の数を数える
	int enemyCount = 0;
	// 雑魚敵: 体力があって生きていれば数える
	auto eView = registry.view<EnemyAIComponent>();
	for (auto entity : eView) {
		if (registry.all_of<HealthComponent>(entity)) {
			if (!registry.get<HealthComponent>(entity).IsDead()) enemyCount++;
		} else {
			enemyCount++; // 初期化待ち
		}
	}
	// ボスも同じように数える
	auto bView = registry.view<BossActionComponent>();
	for (auto entity : bView) {
		if (registry.all_of<HealthComponent>(entity)) {
			if (!registry.get<HealthComponent>(entity).IsDead()) enemyCount++;
		} else {
			enemyCount++; // 初期化待ち
		}
	}


	// プレイ中に敵が 0 になったらクリアにして結果を記録する。クリア後はメニューを動かして描く
	if (state == State::Playing) {
		if (enemyCount == 0) {
			state = State::Clear;
			CaptureResult(registry);
		}
	} else if (state == State::Clear) {
		// クリア後はカーソルを出し、プレイヤーの入力を止める
		Engine::WindowDX::SetCursorVisible(true);
		FreezePlayerInput(registry);

		// 徐々にフェードイン
		clearAlpha += ctx.dt;
		if (clearAlpha > 1.0f) clearAlpha = 1.0f;
		UpdateClearMenu(ctx);

		// STAGE CLEAR テキストの描画
		// 白テクスチャを一度だけ読み込む
		if (ctx.renderer) {
			if (whiteTexture_ == 0) {
				whiteTexture_ = ctx.renderer->LoadTexture2D("Resources/Textures/white1x1.png");
			}
			// クリアの文字・大きさ・色は GameManagerScript の設定を使う (無ければ既定)
			auto* gm = GameManagerScript::GetInstance();
			std::string msg = gm ? gm->clearText : "STAGE CLEAR!";
			float scale = (std::min)(gm ? gm->clearTextScale : 3.0f, 2.15f);
			float cColor[4] = {1.0f, 0.8f, 0.2f, 1.0f};
			if (gm) { cColor[0]=gm->clearColor[0]; cColor[1]=gm->clearColor[1]; cColor[2]=gm->clearColor[2]; cColor[3]=gm->clearColor[3]; }

			// 画面の中央・上寄りに置く
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

void WaveSystem::Reset(entt::registry& /*registry*/) {
	// 状態・表示・選択・パッド・結果・使った缶をすべて初期化する
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

void WaveSystem::TrackUsedCans(entt::registry& registry) {
	// プレイヤーの行動の今の缶
	auto actionView = registry.view<PlayerActionComponent>();
	for (auto entity : actionView) {
		const auto& action = actionView.get<PlayerActionComponent>(entity);
		const int index = static_cast<int>(action.currentCan);
		if (index > static_cast<int>(CanType::None) && index < static_cast<int>(usedCans_.size())) {
			usedCans_[index] = true;
		}
	}

	// プレイヤーの入力の選んでいる缶
	auto inputView = registry.view<PlayerInputComponent>();
	for (auto entity : inputView) {
		const auto& input = inputView.get<PlayerInputComponent>(entity);
		const int index = static_cast<int>(input.selectedCan);
		if (index > static_cast<int>(CanType::None) && index < static_cast<int>(usedCans_.size())) {
			usedCans_[index] = true;
		}
	}
}

void WaveSystem::CaptureResult(entt::registry& registry) {
	// 一度だけ記録する
	if (resultCaptured_) return;
	resultCaptured_ = true;
	resultTime_ = elapsedTime_;

	// プレイヤーの体力・最大体力・被弾回数
	auto playerView = registry.view<PlayerInputComponent, HealthComponent>();
	if (playerView.begin() != playerView.end()) {
		const auto& health = playerView.get<HealthComponent>(*playerView.begin());
		resultHp_ = health.Hp();
		resultMaxHp_ = health.MaxHp() > 0.0f ? health.MaxHp() : 100.0f;
		resultDamageTaken_ = health.damageTakenCount;
	}
	resultRank_ = CalculateRank();
}

WaveSystem::ButtonRect WaveSystem::GetButtonRect(const GameContext& ctx, int index) const {
	// ビューの大きさ (無ければ内部解像度)
	const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
	const float viewH = ctx.viewportSize.y > 0.0f ? ctx.viewportSize.y : static_cast<float>(Engine::WindowDX::kH);
	const float w = index == 1 ? 300.0f : 230.0f;
	const float h = 68.0f;
	const float gap = 24.0f;
	// 3 つのボタンを並べた全体の幅で中央にそろえる
	const float totalW = 230.0f + gap + 300.0f + gap + 230.0f;
	const float startX = viewW * 0.5f - totalW * 0.5f;
	const float y = viewH * 0.68f + 68.0f;
	if (index == 0) return {startX, y, w, h};
	if (index == 1) return {startX + 230.0f + gap, y, w, h};
	return {startX + 230.0f + gap + 300.0f + gap, y, w, h};
}

void WaveSystem::FreezePlayerInput(entt::registry& registry) {
	// プレイヤーの移動・ジャンプ・攻撃・カメラ・メニューを止める
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

void WaveSystem::ConfirmSelection() {
	// やり直し: 同じ設定でゲームを開き直す
	if (selectedButton_ == 0) {
		Engine::SceneManager::GetInstance()->RequestChange("Game", Engine::SceneManager::GetInstance()->CurrentParameters());
	// 装備: 同じ設定でセレクトへ
	} else if (selectedButton_ == 1) {
		Engine::SceneManager::GetInstance()->RequestChange("Select", Engine::SceneManager::GetInstance()->CurrentParameters());
	// セレクト: 設定を引き継がずにセレクトへ
	} else {
		Engine::SceneManager::GetInstance()->RequestChange("Select");
	}
}

void WaveSystem::UpdateClearMenu(GameContext& ctx) {
	// 入力が無ければ何もしない
	auto* input = ctx.input;
	if (!input) return;

	if (input->Trigger(0xCB) || input->Trigger(0x1E)) MoveSelection(-1); // 左 / A
	if (input->Trigger(0xCD) || input->Trigger(0x20)) MoveSelection(1);  // 右 / D
	if (input->Trigger(0x1C) || input->Trigger(0x39)) ConfirmSelection(); // Enter か Space で決定
	// Esc はセレクトへ
	if (input->Trigger(0x01)) {
		selectedButton_ = 2;
		ConfirmSelection();
		return;
	}

	// マウスの位置を Game ビューの座標に直してボタンとの当たりを調べる
	float mx = 0.0f;
	float my = 0.0f;
	input->GetMousePos(mx, my);
	if (ctx.viewportSize.x > 0.0f && ctx.viewportSize.y > 0.0f) {
		mx = (mx - ctx.viewportOffset.x) * static_cast<float>(Engine::WindowDX::kW) / ctx.viewportSize.x;
		my = (my - ctx.viewportOffset.y) * static_cast<float>(Engine::WindowDX::kH) / ctx.viewportSize.y;
	}
	// マウスが乗っているボタンを選び、クリックで決定
	for (int i = 0; i < 3; ++i) {
		if (PointInRect(mx, my, GetButtonRect(ctx, i))) {
			selectedButton_ = i;
			if (input->IsMouseTrigger(0)) {
				ConfirmSelection();
				return;
			}
		}
	}

	// パッド: A で決定、B でセレクトへ、十字キーの左右で選択
	XINPUT_STATE statePad = {};
	// 接続されているパッドの状態を読む
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

void WaveSystem::DrawRect(Engine::Renderer* renderer, float x, float y, float w, float h, const Engine::Vector4& color, int layer) {
	// 白テクスチャが無ければ描かない
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

void WaveSystem::DrawUsedCanPill(Engine::Renderer* renderer, CanType type, float x, float y, float w) {
	// 缶の種類ごとの色
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
	// 色の枠・暗い地・色の帯・缶の名前
	DrawRect(renderer, x - 3.0f, y - 3.0f, w + 6.0f, 34.0f, color, 288);
	DrawRect(renderer, x, y, w, 28.0f, {0.07f, 0.09f, 0.14f, 0.96f * clearAlpha}, 289);
	DrawRect(renderer, x, y, 8.0f, 28.0f, color, 290);
	DrawCenteredText(renderer, CanLoadout::Name(type), x + w * 0.5f, y + 6.0f, 0.28f, {0.92f, 0.96f, 1.0f, clearAlpha});
}

void WaveSystem::DrawResultPanel(GameContext& ctx) {
	auto* renderer = ctx.renderer;
	if (!renderer || whiteTexture_ == 0) return;

	// ビューの大きさとパネルの位置
	const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
	const float viewH = ctx.viewportSize.y > 0.0f ? ctx.viewportSize.y : static_cast<float>(Engine::WindowDX::kH);
	const float panelW = 880.0f;
	const float panelH = 218.0f;
	const float panelX = viewW * 0.5f - panelW * 0.5f;
	const float panelY = viewH * 0.42f;

	// パネルの枠・地・上の帯・見出し
	DrawRect(renderer, panelX - 4.0f, panelY - 4.0f, panelW + 8.0f, panelH + 8.0f, {0.18f, 0.78f, 0.98f, 0.86f * clearAlpha}, 280);
	DrawRect(renderer, panelX, panelY, panelW, panelH, {0.05f, 0.07f, 0.12f, 0.90f * clearAlpha}, 281);
	DrawRect(renderer, panelX, panelY, panelW, 8.0f, {1.0f, 0.33f, 0.62f, clearAlpha}, 282);
	DrawCenteredText(renderer, "CLEAR RESULT", viewW * 0.5f, panelY + 18.0f, 0.40f, {0.86f, 0.94f, 1.0f, clearAlpha});

	// 体力を「今 / 最大」の文字にする
	char hpText[48];
	std::snprintf(hpText, sizeof(hpText), "%.0f / %.0f", resultHp_, resultMaxHp_);
	// クリア時間・体力・被弾回数
	DrawMetric(renderer, "CLEAR TIME", FormatTime(resultTime_), panelX + 44.0f, panelY + 62.0f);
	DrawMetric(renderer, "PLAYER HP", hpText, panelX + 44.0f, panelY + 104.0f);
	DrawMetric(renderer, "DAMAGE TAKEN", std::to_string(resultDamageTaken_), panelX + 44.0f, panelY + 146.0f);

	// 右側に大きくランク
	DrawCenteredText(renderer, "RANK", panelX + 700.0f, panelY + 54.0f, 0.34f, {0.68f, 0.78f, 0.92f, clearAlpha});
	std::string rankText(1, resultRank_);
	DrawCenteredText(renderer, rankText, panelX + 700.0f, panelY + 82.0f, 1.35f, RankColor());

	// 使った缶を札で並べる (1 つも無ければ「なし」の札)
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

void WaveSystem::DrawClearMenu(GameContext& ctx) {
	auto* renderer = ctx.renderer;
	if (!renderer || whiteTexture_ == 0) return;

	// クリアメニューのパネルの位置
	const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
	const float viewH = ctx.viewportSize.y > 0.0f ? ctx.viewportSize.y : static_cast<float>(Engine::WindowDX::kH);
	const float panelW = 880.0f;
	const float panelH = 184.0f;
	const float panelX = viewW * 0.5f - panelW * 0.5f;
	const float panelY = viewH * 0.68f;

	// パネルの枠・地・上の帯・見出し
	DrawRect(renderer, panelX - 4.0f, panelY - 4.0f, panelW + 8.0f, panelH + 8.0f, {0.18f, 0.78f, 0.98f, 0.86f * clearAlpha}, 280);
	DrawRect(renderer, panelX, panelY, panelW, panelH, {0.05f, 0.07f, 0.12f, 0.88f * clearAlpha}, 281);
	DrawRect(renderer, panelX, panelY, panelW, 8.0f, {1.0f, 0.33f, 0.62f, clearAlpha}, 282);
	DrawCenteredText(renderer, "CLEAR MENU", viewW * 0.5f, panelY + 18.0f, 0.38f, {0.86f, 0.94f, 1.0f, clearAlpha});

	// ボタンの名前と説明
	const char* labels[3] = {"RETRY", "EQUIPMENT", "SELECT"};
	const char* hints[3] = {"もう一度", "缶を変える", "セレクトへ"};
	// 各ボタン: 選択中は金の枠、装備のボタンは緑の地
	for (int i = 0; i < 3; ++i) {
		const ButtonRect r = GetButtonRect(ctx, i);
		const bool selected = selectedButton_ == i;
		const Engine::Vector4 edge = selected ? Engine::Vector4{1.0f, 0.95f, 0.24f, clearAlpha} : Engine::Vector4{0.24f, 0.36f, 0.54f, clearAlpha};
		Engine::Vector4 fill = {0.16f, 0.20f, 0.30f, 0.95f * clearAlpha};
		if (i == 1) fill = {0.12f, 0.48f, 0.26f, 0.95f * clearAlpha};

		// 枠・地・名前・説明を描く
		DrawRect(renderer, r.x - 5.0f, r.y - 5.0f, r.w + 10.0f, r.h + 10.0f, edge, 283);
		DrawRect(renderer, r.x, r.y, r.w, r.h, fill, 284);
		DrawCenteredText(renderer, labels[i], r.x + r.w * 0.5f, r.y + 12.0f, i == 1 ? 0.46f : 0.50f, {1.0f, 0.98f, 0.88f, clearAlpha});
		DrawCenteredText(renderer, hints[i], r.x + r.w * 0.5f, r.y + 43.0f, 0.22f, {0.86f, 0.94f, 1.0f, clearAlpha});
	}
	// 操作の案内
	DrawCenteredText(renderer, "Mouse: click   Keyboard/Pad: move and confirm", viewW * 0.5f, panelY + 154.0f, 0.24f, {0.72f, 0.80f, 0.92f, clearAlpha});
}

} // namespace Game
