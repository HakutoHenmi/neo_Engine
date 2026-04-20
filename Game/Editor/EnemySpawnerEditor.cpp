#include "EnemySpawnerEditor.h"
#include "EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../../externals/imgui/imgui.h"

namespace Game {

void EnemySpawnerEditor::DrawUI() {
    // ゲーム固有機能を削除済み（将来の拡張用にスタブを残す）
}

void EnemySpawnerEditor::UpdateAndDraw(GameScene* /*scene*/, Engine::Renderer* /*renderer*/,
                                        const ImVec2& /*gameImageMin*/, const ImVec2& /*gameImageMax*/,
                                        float /*tW*/, float /*tH*/) {
    // ゲーム固有機能を削除済み
}

} // namespace Game
