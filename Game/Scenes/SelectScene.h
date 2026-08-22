#pragma once
#include "IScene.h"
#include "Renderer.h"
#include "WindowDX.h"

namespace Game {

class SelectScene : public Engine::IScene {
public:
    ~SelectScene() override;
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;
    void DrawEditor() override;

private:
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    
    int selectedIndex_ = 0; // 0: GameScene, 1: AssignmentScene
    
    // Gamepad state tracking
    bool prevA_ = false;
    bool prevUp_ = false;
    bool prevDown_ = false;
    bool stickUp_ = false;
    bool stickDown_ = false;
};

} // namespace Game
