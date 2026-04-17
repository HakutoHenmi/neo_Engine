#include "EnemySpawnerEditor.h"
#include "EditorUI.h"
#include "../Scripts/EnemySpawnerScript.h"
#include "../Scripts/ScriptEngine.h"
#include "../../externals/imgui/imgui.h"
#include <cfloat>

namespace Game {

void EnemySpawnerEditor::DrawUI() {
    ImGui::SameLine();
    if (ImGui::Button(spawnerMode_ ? "Spawner [ON]" : "Spawner [OFF]")) {
        spawnerMode_ = !spawnerMode_;
        if (spawnerMode_) {
            EditorUI::Log("Enemy Spawner placement mode ON - click on terrain to place spawners");
        } else {
            EditorUI::Log("Enemy Spawner placement mode OFF");
        }
    }
}

void EnemySpawnerEditor::UpdateAndDraw(GameScene* scene, Engine::Renderer* renderer,
                                        const ImVec2& gameImageMin, const ImVec2& /*gameImageMax*/,
                                        float tW, float tH) {
    if (!scene || scene->IsPlaying()) return;

    // ========== 全スポナープレビュー描画 (ゲーム停止中は常に表示) ==========
    auto& registry = scene->GetRegistry();
    registry.view<ScriptComponent, TransformComponent>().each([&](entt::entity entity, ScriptComponent& sc, TransformComponent& tc) {
        for (auto& entry : sc.scripts) {
            if (!entry.instance && !entry.scriptPath.empty() && !scene->IsPlaying()) {
                entry.instance = ScriptEngine::GetInstance()->CreateScript(entry.scriptPath);
                if (entry.instance) {
                    entry.instance->Start(entity, scene);
                }
            }
            if (entry.instance) {
                auto* spawner = dynamic_cast<EnemySpawnerScript*>(entry.instance.get());
                if (spawner) {
                    spawner->DrawSpawnPreview(tc.translate);
                }
            }
        }
    });

    // ========== 配置モードでなければここで終了 ==========
    if (!spawnerMode_) return;

    ImVec2 mousePos = ImGui::GetMousePos();
    float localX = mousePos.x - gameImageMin.x;
    float localY = mousePos.y - gameImageMin.y;
    bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

    if (!insideImage) return;

    // マウスカーソルをクロスヘアに変更 (視覚的フィードバック)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        auto viewMat = scene->camera_.View();
        auto projMat = scene->camera_.Proj();
        DirectX::XMVECTOR rayOrig, rayDir;
        EditorUI::ScreenToWorldRay(localX, localY, tW, tH, viewMat, projMat, rayOrig, rayDir);

        // メッシュコライダーを持つ全オブジェクトに対してレイキャスト
        float bestDist = FLT_MAX;
        Engine::Vector3 hitPoint = {0, 0, 0};
        bool hitTerrain = false;

        scene->GetRegistry().view<NameComponent, TransformComponent>().each([&]([[maybe_unused]] entt::entity e, [[maybe_unused]] const NameComponent& nc, const TransformComponent& tc) {
            Engine::Model* model = nullptr;
            if (scene->GetRegistry().all_of<GpuMeshColliderComponent>(e)) {
                model = renderer->GetModel(scene->GetRegistry().get<GpuMeshColliderComponent>(e).meshHandle);
            }
            if (!model && scene->GetRegistry().all_of<MeshRendererComponent>(e)) {
                auto& mr = scene->GetRegistry().get<MeshRendererComponent>(e);
                if (mr.modelHandle != 0) model = renderer->GetModel(mr.modelHandle);
            }
            if (model) {
                float dist; Engine::Vector3 hp;
                if (model->RayCast(rayOrig, rayDir, tc.GetTransform().ToMatrix(), dist, hp)) {
                    if (dist < bestDist) {
                        bestDist = dist;
                        hitPoint = hp;
                        hitTerrain = true;
                    }
                }
            }
        });

        if (hitTerrain) {
            // ヒットした位置にスポナーオブジェクトを生成
            // ヒットした位置にスポナーオブジェクトを生成
            entt::entity spawnerObj = scene->GetRegistry().create();
            scene->GetRegistry().emplace<NameComponent>(spawnerObj).name = "EnemySpawner";
            auto& tc = scene->GetRegistry().emplace<TransformComponent>(spawnerObj);
            tc.translate = {hitPoint.x, hitPoint.y, hitPoint.z};
            tc.scale = {1.0f, 1.0f, 1.0f};

            // EnemySpawnerScript をアタッチ
            auto& sc = scene->GetRegistry().emplace<ScriptComponent>(spawnerObj);
            sc.scripts.push_back({ "EnemySpawnerScript", "", nullptr });

            // 新しく配置したスポナーを選択状態にする
            scene->SetSelectedEntity(spawnerObj);

            EditorUI::Log("Spawner placed at (" +
                          std::to_string(hitPoint.x) + ", " +
                          std::to_string(hitPoint.y) + ", " +
                          std::to_string(hitPoint.z) + ")");
        }
    }
}

} // namespace Game
