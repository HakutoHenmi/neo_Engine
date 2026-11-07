// SelectScene.h
// ステージ選択画面のシーン。巻尺の上にステージの範囲と最高記録を見せる。
#pragma once
#include "IScene.h"
#include "Renderer.h"
#include "WindowDX.h"
#include "../UI/ScaleUI.h"
#include <chrono>
#include <vector>

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
    // 選んでいるステージへ出発する (未解放なら揺らして知らせる)
    void Confirm();

    // ウィンドウ・描画のエンジン側オブジェクト
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    // ステージの一覧・選んでいる番号・スライムの見た目の位置と幅
    std::vector<UI::Scale::StageView> stages_;
    int selected_ = 0;
    float slimeX_ = 0, slimeWidth_ = 140;
    // 画面を開いてからの秒・未解放を決定したときの揺れ・出発の経過 (負なら出発前)
    float age_ = 0, shake_ = 0, leaving_ = -1;
    std::chrono::steady_clock::time_point lastTime_{};
};

} // namespace Game
