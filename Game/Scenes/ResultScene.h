#pragma once
#include "IScene.h"
#include "Renderer.h"
#include "Camera.h"
#include "WindowDX.h"
#include "../../externals/entt/entt.hpp"
#include "../Systems/ISystem.h"
#include <vector>
#include <memory>

namespace Game {

class ResultScene : public Engine::IScene {
public:
	void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
	void Update() override;
	void Draw() override;
	void DrawUI() override;
	void DrawEditor() override;

private:
	void CreateUI(const Engine::SceneParameters& params);

private:
	Engine::WindowDX* dx_ = nullptr;
	Engine::Renderer* renderer_ = nullptr;
	Engine::Camera camera_;
	
	entt::registry registry_;
	std::vector<std::unique_ptr<ISystem>> systems_;
	GameContext ctx_;
};

} // namespace Game
