#pragma once
#include "IScene.h"

namespace Game {

class GameOverScene : public Engine::IScene {
public:
    GameOverScene() = default;
    ~GameOverScene() override = default;

    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;

private:
    Engine::WindowDX* dx_ = nullptr;
};

} // namespace Game
