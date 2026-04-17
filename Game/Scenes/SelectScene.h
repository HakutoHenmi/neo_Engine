#pragma once
#include "IScene.h"
#include "Renderer.h"
#include "Camera.h"
#include "WindowDX.h"
#include "../../externals/entt/entt.hpp"
#include "../Systems/ISystem.h"
#include "../ObjectTypes.h"
#include <vector>
#include <memory>

namespace Game {

class SelectScene : public Engine::IScene {
public:
	void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
	void Update() override;
	void Draw() override;
	void DrawUI() override;
	void DrawEditor() override;

private:
	void CreateUI();

private:
	Engine::WindowDX* dx_ = nullptr;
	Engine::Renderer* renderer_ = nullptr;
	Engine::Camera camera_;
	
	entt::registry registry_;
	std::vector<std::unique_ptr<ISystem>> systems_;
	GameContext ctx_;

	// ステージ情報
	struct StageInfo {
		std::string name;
		std::string path;
		std::string description;
	};
	std::vector<StageInfo> stages_;
};

} // namespace Game
