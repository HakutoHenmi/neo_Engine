#pragma once
#include "GameScene.h"
namespace Game {
class SlimePlayerDemoScene : public GameScene {
public:
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override {
        auto demo = params; demo.stagePath = Data::SlimePlayerDemoStage;
        GameScene::Initialize(dx, demo);
    }
};
}
