#include "ResultScene.h"
#include "../../Engine/SceneManager.h"
#include "../Systems/UISystem.h"
#include "../../Engine/Input.h"
#include <chrono>

namespace Game {

void ResultScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();

	camera_.Initialize();
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);
	camera_.SetPosition(0, 0, -10);

	// システムのセットアップ
	systems_.clear();
	systems_.push_back(std::make_unique<UISystem>());

	CreateUI(params);
}

void ResultScene::CreateUI(const Engine::SceneParameters& params) {
	registry_.clear();

	// 背景
	auto bg = registry_.create();
	registry_.emplace<NameComponent>(bg, "Background");
	auto& rectBg = registry_.emplace<RectTransformComponent>(bg);
	rectBg.size = {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH};
	rectBg.anchor = {0.5f, 0.5f};
	rectBg.pivot = {0.5f, 0.5f};
	auto& imgBg = registry_.emplace<UIImageComponent>(bg);
	imgBg.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
	imgBg.color = params.isWin ? DirectX::XMFLOAT4{0.1f, 0.2f, 0.1f, 0.9f} : DirectX::XMFLOAT4{0.2f, 0.1f, 0.1f, 0.9f};

	// 勝ち負け判定テキスト
	auto resultText = registry_.create();
	registry_.emplace<NameComponent>(resultText, "ResultText");
	auto& rectResult = registry_.emplace<RectTransformComponent>(resultText);
	rectResult.pos = {0, -200};
	rectResult.size = {800, 150};
	rectResult.anchor = {0.5f, 0.5f};
	rectResult.pivot = {0.5f, 0.5f};
	auto& textRes = registry_.emplace<UITextComponent>(resultText);
	textRes.text = params.isWin ? "MISSION CLEAR" : "MISSION FAILED";
	textRes.fontSize = 96.0f;
	textRes.color = params.isWin ? DirectX::XMFLOAT4{0.2f, 1.0f, 0.2f, 1.0f} : DirectX::XMFLOAT4{1.0f, 0.2f, 0.2f, 1.0f};

	// 統計情報 (クリアタイム / スコア)
	auto statsText = registry_.create();
	registry_.emplace<NameComponent>(statsText, "StatsText");
	auto& rectStats = registry_.emplace<RectTransformComponent>(statsText);
	rectStats.pos = {0, 0};
	rectStats.size = {600, 80};
	rectStats.anchor = {0.5f, 0.5f};
	rectStats.pivot = {0.5f, 0.5f};
	auto& textStats = registry_.emplace<UITextComponent>(statsText);
	textStats.text = "SCORE: " + std::to_string(params.score) + "  TIME: " + std::to_string((int)params.clearTime) + "s";
	textStats.fontSize = 42.0f;
	textStats.color = {1, 1, 1, 1};

	// 操作ガイド
	auto guide = registry_.create();
	auto& rectGuide = registry_.emplace<RectTransformComponent>(guide);
	rectGuide.pos = {0, 100};
	rectGuide.size = {800, 50};
	rectGuide.anchor = {0.5f, 0.5f};
	auto& textGuide = registry_.emplace<UITextComponent>(guide);
	textGuide.text = "SELECT NEXT ACTION";
	textGuide.fontSize = 24.0f;

	// ボタン (ステージセレクトへ戻る)
	auto toSelect = registry_.create();
	registry_.emplace<NameComponent>(toSelect, "ToSelectButton");
	auto& rectSel = registry_.emplace<RectTransformComponent>(toSelect);
	rectSel.pos = {0, 200};
	rectSel.size = {400, 60};
	rectSel.anchor = {0.5f, 0.5f};
	rectSel.pivot = {0.5f, 0.5f};
	auto& uiBtnSel = registry_.emplace<UIButtonComponent>(toSelect);
	uiBtnSel.normalColor = {0.25f, 0.35f, 0.25f, 1.0f};
	uiBtnSel.hoverColor = {0.4f, 0.6f, 0.4f, 1.0f};
	uiBtnSel.pressedColor = {0.15f, 0.25f, 0.15f, 1.0f};
	registry_.emplace<UIImageComponent>(toSelect).textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
	registry_.emplace<UITextComponent>(toSelect).text = "STAGE SELECT";

	// ボタン (タイトルへ戻る)
	auto toTitle = registry_.create();
	registry_.emplace<NameComponent>(toTitle, "ToTitleButton");
	auto& rectTit = registry_.emplace<RectTransformComponent>(toTitle);
	rectTit.pos = {0, 300};
	rectTit.size = {400, 60};
	rectTit.anchor = {0.5f, 0.5f};
	rectTit.pivot = {0.5f, 0.5f};
	auto& uiBtnTit = registry_.emplace<UIButtonComponent>(toTitle);
	uiBtnTit.normalColor = {0.35f, 0.35f, 0.35f, 1.0f};
	uiBtnTit.hoverColor = {0.55f, 0.55f, 0.55f, 1.0f};
	uiBtnTit.pressedColor = {0.2f, 0.2f, 0.2f, 1.0f};
	registry_.emplace<UIImageComponent>(toTitle).textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
	registry_.emplace<UITextComponent>(toTitle).text = "RETURN TITLE";
}

void ResultScene::Update() {
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	last = now;
	
	ctx_.dt = dt;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.camera = &camera_;
	ctx_.scene = nullptr;
	ctx_.isPlaying = false;

	for (auto& sys : systems_) {
		sys->Update(registry_, ctx_);
	}

	if (ctx_.input->IsMouseTrigger(0)) {
		auto view = registry_.view<UIButtonComponent, NameComponent>();
		for (auto e : view) {
			auto& btn = registry_.get<UIButtonComponent>(e);
			if (btn.isHovered) {
				const auto& name = registry_.get<NameComponent>(e).name;
				if (name == "ToSelectButton") {
					Engine::SceneManager::GetInstance()->RequestChange("Select");
					return;
				} else if (name == "ToTitleButton") {
					Engine::SceneManager::GetInstance()->RequestChange("Title");
					return;
				}
			}
		}
	}
}

void ResultScene::Draw() {
	if (renderer_) renderer_->ResetGameViewport();
	for (auto& sys : systems_) sys->Draw(registry_, ctx_);
}

void ResultScene::DrawUI() {
	for (auto& sys : systems_) sys->DrawUI(registry_, ctx_);
}

void ResultScene::DrawEditor() {}

} // namespace Game
