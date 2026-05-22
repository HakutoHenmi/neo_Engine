#pragma once
#include "IScene.h"
#include "../../externals/entt/entt.hpp"
#include "../Systems/ISystem.h"
#include "../../Engine/Camera.h"
#include <memory>

namespace Game {

class TitleScene : public Engine::IScene {
public:
    TitleScene() = default;
    ~TitleScene() override = default;

    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;

private:
    Engine::WindowDX* dx_ = nullptr;
    entt::registry registry_;
    GameContext ctx_;
    std::unique_ptr<ISystem> uiSystem_;
    Engine::Camera camera_; // 2D投影用ダミー
    
    entt::entity btnStart_;
    entt::entity btnSettings_;
    entt::entity btnExit_;
};

} // namespace Game
