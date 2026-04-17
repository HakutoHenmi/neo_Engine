#pragma once
#include "../Scenes/GameScene.h"
#include "../../Engine/Renderer.h"
#include "../../externals/imgui/imgui.h"

namespace Game {

// ★ エネミースポナー設置エディター
// メッシュコライダーのある地面をクリックしてスポナーを配置する
class EnemySpawnerEditor {
public:
    // メニューバー等に表示するUI（モード切替ボタン）
    void DrawUI();

    // 毎フレームの更新＆プレビュー描画
    // Gameウィンドウ内でのレイキャスト、クリック配置、選択中スポナーのプレビューを行う
    void UpdateAndDraw(GameScene* scene, Engine::Renderer* renderer,
                       const ImVec2& gameImageMin, const ImVec2& gameImageMax,
                       float tW, float tH);

    bool IsSpawnerMode() const { return spawnerMode_; }
    void SetSpawnerMode(bool mode) { this->spawnerMode_ = mode; }

private:
    bool spawnerMode_ = false;
};

} // namespace Game
