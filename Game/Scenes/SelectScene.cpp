#include "SelectScene.h"
#include "../../Engine/SceneManager.h"
#include "../Systems/UISystem.h"
#include "../../Engine/Input.h"
#include <chrono>

namespace Game {

void SelectScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	(void)params;
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();

	camera_.Initialize();
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);
	camera_.SetPosition(0, 0, -10);

	// ステージリストのセットアップ
	stages_.push_back({"Stage 1: Main City", "Resources/Scenes/scene.json", "Standard TD map"});
	stages_.push_back({"Stage 2: TPS Arena", "Resources/Scenes/TPS_Scene.json", "Action oriented map"});
	stages_.push_back({"Stage 3: Tower Defense", "Resources/Scenes/TowerScene.json", "Defend the core"});

	// システムのセットアップ
	systems_.clear();
	systems_.push_back(std::make_unique<UISystem>());

	CreateUI();
}

void SelectScene::CreateUI() {
	registry_.clear();

	// 背景
	auto bg = registry_.create();
	registry_.emplace<NameComponent>(bg, "Background");
	auto& rectBg = registry_.emplace<RectTransformComponent>(bg);
	rectBg.pos = {0, 0};
	rectBg.size = {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH};
	rectBg.anchor = {0.5f, 0.5f};
	rectBg.pivot = {0.5f, 0.5f};
	auto& imgBg = registry_.emplace<UIImageComponent>(bg);
	imgBg.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
	imgBg.color = {0.1f, 0.1f, 0.15f, 1.0f};

	// タイトル
	auto title = registry_.create();
	registry_.emplace<NameComponent>(title, "TitleText");
	auto& rectTitle = registry_.emplace<RectTransformComponent>(title);
	rectTitle.pos = {0, -400};
	rectTitle.size = {800, 100};
	rectTitle.anchor = {0.5f, 0.5f};
	rectTitle.pivot = {0.5f, 0.5f};
	auto& textTitle = registry_.emplace<UITextComponent>(title);
	textTitle.text = "SELECT STAGE";
	textTitle.fontSize = 64.0f;
	textTitle.color = {1, 1, 1, 1};

	// ステージボタン
	float startY = -150.0f;
	float spacing = 120.0f;
	for (size_t i = 0; i < stages_.size(); ++i) {
		auto btn = registry_.create();
		registry_.emplace<NameComponent>(btn, "StageButton_" + std::to_string(i));
		
		auto& rect = registry_.emplace<RectTransformComponent>(btn);
		rect.pos = {0, startY + i * spacing};
		rect.size = {500, 60};
		rect.anchor = {0.5f, 0.5f};
		rect.pivot = {0.5f, 0.5f};

		auto& uiBtn = registry_.emplace<UIButtonComponent>(btn);
		uiBtn.normalColor = {0.25f, 0.25f, 0.35f, 1.0f};
		uiBtn.hoverColor = {0.4f, 0.4f, 0.6f, 1.0f};
		uiBtn.pressedColor = {0.15f, 0.15f, 0.25f, 1.0f};

		auto& img = registry_.emplace<UIImageComponent>(btn);
		img.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		
		auto& text = registry_.emplace<UITextComponent>(btn);
		text.text = stages_[i].name;
		text.fontSize = 32.0f;
		text.color = {1, 1, 1, 1};
		
		// 識別用タグの代わりに名前を使うか、新しくタグを付ける
		registry_.emplace<TagComponent>(btn, TagType::Default);
		registry_.emplace<VariableComponent>(btn).SetString("Path", stages_[i].path);
	}

	// 戻るボタン
	auto backBtn = registry_.create();
	registry_.emplace<NameComponent>(backBtn, "BackButton");
	auto& rectBack = registry_.emplace<RectTransformComponent>(backBtn);
	rectBack.pos = {-800, 450};
	rectBack.size = {200, 60};
	rectBack.anchor = {0.5f, 0.5f};
	rectBack.pivot = {0.5f, 0.5f};
	
	auto& btn = registry_.emplace<UIButtonComponent>(backBtn);
	btn.normalColor = {0.4f, 0.15f, 0.15f, 1.0f};
	btn.hoverColor = {0.6f, 0.2f, 0.2f, 1.0f};
	btn.pressedColor = {0.3f, 0.1f, 0.1f, 1.0f};

	registry_.emplace<UIImageComponent>(backBtn); // 枠線はUISystemで自動描画される
	registry_.emplace<UITextComponent>(backBtn).text = "BACK";
}

void SelectScene::Update() {
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	last = now;

	ctx_.dt = dt;
	ctx_.renderer = renderer_;
	ctx_.camera = &camera_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.scene = nullptr;
	ctx_.isPlaying = false;

	for (auto& sys : systems_) {
		sys->Update(registry_, ctx_);
	}

	// ボタンクリック判定
	if (ctx_.input->IsMouseTrigger(0)) {
		auto view = registry_.view<UIButtonComponent, NameComponent>();
		for (auto e : view) {
			auto& btn = registry_.get<UIButtonComponent>(e);
			if (btn.isHovered) {
				const auto& name = registry_.get<NameComponent>(e).name;
				if (name.find("StageButton_") != std::string::npos) {
					if (registry_.all_of<VariableComponent>(e)) {
						std::string path = registry_.get<VariableComponent>(e).GetString("Path");
						Engine::SceneParameters p;
						p.stagePath = path;
						Engine::SceneManager::GetInstance()->RequestChange("Game", p);
						return; 
					}
				} else if (name == "BackButton") {
					Engine::SceneManager::GetInstance()->RequestChange("Title");
					return;
				}
			}
		}
	}
}

void SelectScene::Draw() {
	if (renderer_) {
		renderer_->ResetGameViewport();
	}

	for (auto& sys : systems_) {
		sys->Draw(registry_, ctx_);
	}
}

void SelectScene::DrawUI() {
	for (auto& sys : systems_) {
		sys->DrawUI(registry_, ctx_);
	}
}

void SelectScene::DrawEditor() {}

} // namespace Game
