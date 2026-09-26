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
    void DrawMenu();

    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Renderer::TextureHandle whiteTexture_ = 0;
    
    int selectedIndex_ = 0; // Stage 1, Stage 2, test scene
    
    // Gamepad state tracking
    bool prevA_ = false;
    bool prevUp_ = false;
    bool prevDown_ = false;
    bool stickUp_ = false;
    bool stickDown_ = false;
};

} // namespace Game
