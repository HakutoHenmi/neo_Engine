// GameOverScene.h
// ゲームオーバー (探索失敗) の結果画面のシーン。
#pragma once
#include "IScene.h"

namespace Game {

// 結果画面。リトライかステージ選択に戻る
class GameOverScene : public Engine::IScene {
public:
    GameOverScene() = default;
    ~GameOverScene() override = default;

    // IScene の実装
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;

private:
    Engine::WindowDX* dx_ = nullptr;
};

} // namespace Game
