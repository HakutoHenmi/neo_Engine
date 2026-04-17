#include "TitleScene.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Input.h"
#include "../../Engine/Audio.h"
#include "../Editor/EditorUI.h"
#include "imgui.h"

namespace Game {

void TitleScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	(void)params;
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();

	camera_.Initialize();
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);

	uiSystem_ = std::make_unique<UISystem>();
	registry_.clear();
	mainEntities_.clear();
	settingsEntities_.clear();

	// コンテキストの初期化
	ctx_.dt = 1.0f / 60.0f;
	ctx_.camera = &camera_;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.isPlaying = true;
	ctx_.scene = nullptr;
	ctx_.viewportOffset = {0.0f, 0.0f};
	ctx_.viewportSize = {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH};

	CreateMainMenu();
	CreateSettingsMenu();

	// 初期状態はメインメニュー表示
	state_ = MenuState::Main;
	for (auto e : mainEntities_) registry_.get<RectTransformComponent>(e).enabled = true;
	for (auto e : settingsEntities_) registry_.get<RectTransformComponent>(e).enabled = false;
}

entt::entity TitleScene::CreateButton(const std::string& text, float yPos, entt::entity parent) {
	auto entity = registry_.create();
	
	// Transformer
	auto& rect = registry_.emplace<RectTransformComponent>(entity);
	rect.pos = {0.0f, yPos};
	rect.size = {300.0f, 60.0f};
	rect.anchor = {0.0f, 0.0f}; // 左端基準
	rect.pivot = {0.0f, 0.5f};
	rect.enabled = true;

	if (parent != entt::null) {
		registry_.emplace<HierarchyComponent>(entity, parent);
	}

	// Button
	auto& btn = registry_.emplace<UIButtonComponent>(entity);
	btn.normalColor = {0.2f, 0.2f, 0.2f, 0.8f};
	btn.hoverColor = {0.4f, 0.4f, 0.4f, 1.0f};
	btn.pressedColor = {0.1f, 0.1f, 0.1f, 1.0f};

	// Text
	auto& txt = registry_.emplace<UITextComponent>(entity);
	txt.text = text;
	txt.fontSize = 32.0f;
	txt.color = {1.0f, 1.0f, 1.0f, 1.0f};

	// Image (Background)
	auto& img = registry_.emplace<UIImageComponent>(entity);
	img.color = {1.0f, 1.0f, 1.0f, 1.0f}; // Multiply with button color

	return entity;
}

void TitleScene::CreateMainMenu() {
	auto parent = registry_.create();
	auto& pRect = registry_.emplace<RectTransformComponent>(parent);
	pRect.pos = {100.0f, 300.0f}; // 左寄せで下げた位置
	pRect.size = {0,0};
	pRect.anchor = {0.0f, 0.0f};
	mainEntities_.push_back(parent);

	// タイトルテキスト
	auto titleText = registry_.create();
	auto& titleRect = registry_.emplace<RectTransformComponent>(titleText);
	titleRect.pos = {50.0f, -150.0f}; // タイトルは上部に配置
	registry_.emplace<HierarchyComponent>(titleText, parent);
	auto& txt = registry_.emplace<UITextComponent>(titleText);
	txt.text = "TD Engine Project";
	txt.fontSize = 80.0f;
	txt.color = {1.0f, 0.8f, 0.2f, 1.0f};
	mainEntities_.push_back(titleText);

	btnStart_ = CreateButton("Start Game", 0.0f, parent);
	btnSettings_ = CreateButton("Settings", 80.0f, parent);
	btnExit_ = CreateButton("Exit", 160.0f, parent);

	mainEntities_.push_back(btnStart_);
	mainEntities_.push_back(btnSettings_);
	mainEntities_.push_back(btnExit_);
}

void TitleScene::CreateSettingsMenu() {
	auto parent = registry_.create();
	auto& pRect = registry_.emplace<RectTransformComponent>(parent);
	pRect.pos = {100.0f, 300.0f}; // MainMenuと同じ位置基準
	pRect.size = {0,0};
	pRect.anchor = {0.0f, 0.0f};
	pRect.enabled = false;
	settingsEntities_.push_back(parent);

	// タイトルテキスト
	auto titleText = registry_.create();
	auto& titleRect = registry_.emplace<RectTransformComponent>(titleText);
	titleRect.pos = {50.0f, -150.0f};
	titleRect.enabled = false;
	registry_.emplace<HierarchyComponent>(titleText, parent);
	auto& txt = registry_.emplace<UITextComponent>(titleText);
	txt.text = "Settings";
	txt.fontSize = 80.0f;
	settingsEntities_.push_back(titleText);

	// フルスクリーン
	btnFullscreen_ = CreateButton("Fullscreen: OFF", 0.0f, parent);
	registry_.get<RectTransformComponent>(btnFullscreen_).enabled = false;
	textFullscreen_ = btnFullscreen_; // 同じエンティティのテキストを更新

	// BGM 音量
	auto bgmLabel = registry_.create();
	auto& bgmRect = registry_.emplace<RectTransformComponent>(bgmLabel);
	bgmRect.pos = {0.0f, 80.0f};
	bgmRect.enabled = false;
	registry_.emplace<HierarchyComponent>(bgmLabel, parent);
	auto& bgmTxt = registry_.emplace<UITextComponent>(bgmLabel);
	bgmTxt.text = "BGM Volume";
	bgmTxt.fontSize = 32.0f;
	textBGM_ = bgmLabel;

	btnBGMMinus_ = CreateButton("-", 80.0f, parent);
	registry_.get<RectTransformComponent>(btnBGMMinus_).size = {60.0f, 60.0f};
	registry_.get<RectTransformComponent>(btnBGMMinus_).pos = {310.0f, 80.0f};
	registry_.get<RectTransformComponent>(btnBGMMinus_).enabled = false;
	
	btnBGMPlus_ = CreateButton("+", 80.0f, parent);
	registry_.get<RectTransformComponent>(btnBGMPlus_).size = {60.0f, 60.0f};
	registry_.get<RectTransformComponent>(btnBGMPlus_).pos = {380.0f, 80.0f};
	registry_.get<RectTransformComponent>(btnBGMPlus_).enabled = false;

	// SE 音量
	auto seLabel = registry_.create();
	auto& seRect = registry_.emplace<RectTransformComponent>(seLabel);
	seRect.pos = {0.0f, 160.0f};
	seRect.enabled = false;
	registry_.emplace<HierarchyComponent>(seLabel, parent);
	auto& seTxt = registry_.emplace<UITextComponent>(seLabel);
	seTxt.text = "SE Volume";
	seTxt.fontSize = 32.0f;
	textSE_ = seLabel;

	btnSEMinus_ = CreateButton("-", 160.0f, parent);
	registry_.get<RectTransformComponent>(btnSEMinus_).size = {60.0f, 60.0f};
	registry_.get<RectTransformComponent>(btnSEMinus_).pos = {310.0f, 160.0f};
	registry_.get<RectTransformComponent>(btnSEMinus_).enabled = false;
	
	btnSEPlus_ = CreateButton("+", 160.0f, parent);
	registry_.get<RectTransformComponent>(btnSEPlus_).size = {60.0f, 60.0f};
	registry_.get<RectTransformComponent>(btnSEPlus_).pos = {380.0f, 160.0f};
	registry_.get<RectTransformComponent>(btnSEPlus_).enabled = false;

	btnBack_ = CreateButton("Back", 260.0f, parent);
	registry_.get<RectTransformComponent>(btnBack_).enabled = false;

	settingsEntities_.push_back(bgmLabel);
	settingsEntities_.push_back(btnFullscreen_);
	settingsEntities_.push_back(btnBGMMinus_);
	settingsEntities_.push_back(btnBGMPlus_);
	settingsEntities_.push_back(seLabel);
	settingsEntities_.push_back(btnSEMinus_);
	settingsEntities_.push_back(btnSEPlus_);
	settingsEntities_.push_back(btnBack_);
}

void TitleScene::Update() {
	// UIシステムの更新（主にボタン押下フラグの更新）
	uiSystem_->Draw(registry_, ctx_); 
	
	auto* input = Engine::Input::GetInstance();
	bool isClicked = input->IsMouseTrigger(0);

	if (state_ == MenuState::Main) {
		if (isClicked) {
			if (registry_.get<UIButtonComponent>(btnStart_).isHovered) {
				Engine::SceneManager::GetInstance()->RequestChange("Select");
			} else if (registry_.get<UIButtonComponent>(btnSettings_).isHovered) {
				state_ = MenuState::Settings;
				for (auto e : mainEntities_) registry_.get<RectTransformComponent>(e).enabled = false;
				for (auto e : settingsEntities_) registry_.get<RectTransformComponent>(e).enabled = true;
			} else if (registry_.get<UIButtonComponent>(btnExit_).isHovered) {
				PostQuitMessage(0);
			}
		}
	} else if (state_ == MenuState::Settings) {
		auto* audio = Engine::Audio::GetInstance();
		
		// フルスクリーンボタンテキストの更新
		if (dx_) {
			std::string fsText = dx_->IsFullscreen() ? "Fullscreen: ON" : "Fullscreen: OFF";
			registry_.get<UITextComponent>(textFullscreen_).text = fsText;
		}

		// 音量テキストの更新
		if (audio) {
			int bgmVol = static_cast<int>(audio->GetMasterBGMVolume() * 100);
			registry_.get<UITextComponent>(textBGM_).text = "BGM Volume: " + std::to_string(bgmVol) + "%";
			
			int seVol = static_cast<int>(audio->GetMasterSEVolume() * 100);
			registry_.get<UITextComponent>(textSE_).text = "SE Volume: " + std::to_string(seVol) + "%";
		}

		if (isClicked) {
			if (registry_.get<UIButtonComponent>(btnBack_).isHovered) {
				state_ = MenuState::Main;
				for (auto e : mainEntities_) registry_.get<RectTransformComponent>(e).enabled = true;
				for (auto e : settingsEntities_) registry_.get<RectTransformComponent>(e).enabled = false;
			} else if (registry_.get<UIButtonComponent>(btnFullscreen_).isHovered) {
				if (dx_) dx_->ToggleFullscreen();
			} else if (registry_.get<UIButtonComponent>(btnBGMMinus_).isHovered) {
				if (audio) audio->SetMasterBGMVolume(audio->GetMasterBGMVolume() - 0.1f);
			} else if (registry_.get<UIButtonComponent>(btnBGMPlus_).isHovered) {
				if (audio) audio->SetMasterBGMVolume(audio->GetMasterBGMVolume() + 0.1f);
			} else if (registry_.get<UIButtonComponent>(btnSEMinus_).isHovered) {
				if (audio) audio->SetMasterSEVolume(audio->GetMasterSEVolume() - 0.1f);
			} else if (registry_.get<UIButtonComponent>(btnSEPlus_).isHovered) {
				if (audio) audio->SetMasterSEVolume(audio->GetMasterSEVolume() + 0.1f);
			}
		}
	}
}

void TitleScene::Draw() {
	if (renderer_) {
		renderer_->ResetGameViewport();
		// ★追加: ポストエフェクトを無効化
		renderer_->SetPostProcessEnabled(false);
	}
}

void TitleScene::DrawEditor() {
}

} // namespace Game
