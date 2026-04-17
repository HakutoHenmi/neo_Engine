#pragma once
#include "IScene.h"
#include "Renderer.h"
#include "WindowDX.h"
#include "../../externals/entt/entt.hpp"
#include "../ObjectTypes.h"
#include "../Systems/UISystem.h"
#include <vector>

namespace Game {

class TitleScene : public Engine::IScene {
public:
	void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
	void Update() override;
	void Draw() override;
	void DrawEditor() override;

private:
	// メニュー状態
	enum class MenuState {
		Main,
		Settings
	};

	void CreateMainMenu();
	void CreateSettingsMenu();
	entt::entity CreateButton(const std::string& text, float yPos, entt::entity parent);

	Engine::WindowDX* dx_ = nullptr;
	Engine::Renderer* renderer_ = nullptr;
	Engine::Camera camera_;
	
	entt::registry registry_;
	std::unique_ptr<UISystem> uiSystem_;
	GameContext ctx_;
	
	MenuState state_ = MenuState::Main;
	
	// ボタンのエンティティ
	entt::entity btnStart_ = entt::null;
	entt::entity btnSettings_ = entt::null;
	entt::entity btnExit_ = entt::null;
	
	entt::entity btnFullscreen_ = entt::null;
	entt::entity btnBGMMinus_ = entt::null;
	entt::entity btnBGMPlus_ = entt::null;
	entt::entity btnSEMinus_ = entt::null;
	entt::entity btnSEPlus_ = entt::null;
	entt::entity btnBack_ = entt::null;
	
	// テキスト更新用
	entt::entity textFullscreen_ = entt::null;
	entt::entity textBGM_ = entt::null;
	entt::entity textSE_ = entt::null;
	
	std::vector<entt::entity> mainEntities_;
	std::vector<entt::entity> settingsEntities_;
};

} // namespace Game
