// SelectScene.h
// ステージ選択画面 (出撃先の紹介と出撃ボタン) のシーン。
#pragma once
#include "IScene.h"
#include "Renderer.h"
#include "WindowDX.h"

namespace Game {

// ステージ選択画面
class SelectScene : public Engine::IScene {
public:
    ~SelectScene() override;
    // UI 専用のシーン: 3D 描画の準備前 (起動直後の軽量な経路) でも動く
    bool RequiresFullRenderer() const override { return false; }
    // IScene の実装
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;
    // エディタ用 (この画面では追加の項目なし)
    void DrawEditor() override;

private:
    // 画面全体の描画
    void DrawMenu();

    // ウィンドウ・描画のエンジン側オブジェクトと、塗りつぶし用の白テクスチャ
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Renderer::TextureHandle whiteTexture_ = 0;
    
};

} // namespace Game
