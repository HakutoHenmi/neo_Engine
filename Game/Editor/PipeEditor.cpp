#include "PipeEditor.h"
#include "EditorUI.h"
#include "../../externals/imgui/imgui.h"
#include <cmath>

namespace Game {

// ====== DefaultPipeBehavior ======
void DefaultPipeBehavior::OnGeneratePipe(entt::registry& registry, entt::entity outPipe, const Engine::Vector3& start, const Engine::Vector3& end, float length, Engine::Renderer* renderer) {
    registry.emplace_or_replace<NameComponent>(outPipe).name = "PipeSegment";
    auto& tc = registry.emplace_or_replace<TransformComponent>(outPipe);
    
    Engine::Vector3 diff = end - start;
    Engine::Vector3 dir = Engine::Normalize(diff);
    
    // シリンダーは高さ3.0なので、長さを合わせるには 3.0 で割る。また、球体にめり込ませるために少し短くする。
    float cyLen = (length - 0.2f) / 3.0f;
    if (cyLen < 0.01f) cyLen = 0.01f;

    // 中間地点に設置
    Engine::Vector3 center = start + diff * 0.5f;
    tc.translate = {center.x, center.y, center.z};
    tc.scale = {0.35f, cyLen, 0.35f};

    auto euler = Engine::LookRotation(dir);
    tc.rotate = {euler.x - 3.14159265f * 0.5f, euler.y, euler.z}; // シリンダーを倒すためにPitchから90度減算

    auto& mr = registry.emplace_or_replace<MeshRendererComponent>(outPipe);
    mr.modelPath = "Resources/Models/Cylinder/cylinder.obj";
    if (renderer) mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
    mr.shaderName = "Toon"; 
}

void DefaultPipeBehavior::OnGenerateJoint(entt::registry& registry, entt::entity outJoint, const Engine::Vector3& position, Engine::Renderer* renderer) {
    registry.emplace_or_replace<NameComponent>(outJoint).name = "PipeJoint";
    auto& tc = registry.emplace_or_replace<TransformComponent>(outJoint);
    tc.translate = { position.x, position.y, position.z };
    tc.scale = { 1.0f, 1.0f, 1.0f };
    
    auto& mr = registry.emplace_or_replace<MeshRendererComponent>(outJoint);
    mr.modelPath = "Resources/Models/player_ball/ball.obj";
    if (renderer) mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
    mr.shaderName = "Toon"; 
}

void DefaultPipeBehavior::OnPlacementComplete(GameScene* /*scene*/, const Engine::Vector3& start, const Engine::Vector3& end) {
    EditorUI::Log("Pipe placed from (" + std::to_string(start.x) + "," + std::to_string(start.z) + ") to (" + std::to_string(end.x) + "," + std::to_string(end.z) + ")");
}


// ====== PipeEditor ======
PipeEditor::PipeEditor() {
    behavior_ = std::make_shared<DefaultPipeBehavior>();
}

void PipeEditor::ClearPreview(GameScene* scene) {
    auto& registry = scene->GetRegistry();
    if (registry.valid(previewPipeId_)) registry.destroy(previewPipeId_);
    if (registry.valid(previewJointId_)) registry.destroy(previewJointId_);
    previewPipeId_ = entt::null;
    previewJointId_ = entt::null;
}

void PipeEditor::DrawUI() {
    if (ImGui::Button(pipeMode_ ? "Pipe Mode [ON]" : "Pipe Mode [OFF]")) {
        SetPipeMode(!pipeMode_);
    }
    
    if (pipeMode_) {
        ImGui::SameLine();
        ImGui::Checkbox("Snap Angle", &useAngleSnap_);
        if (useAngleSnap_) {
            ImGui::SameLine();
            ImGui::PushItemWidth(80);
            ImGui::SliderFloat("Step##Angle", &snapAngleStep_, 5.0f, 90.0f, "%.0f deg");
            ImGui::PopItemWidth();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Node Snap", &useNodeSnap_);
        if (useNodeSnap_) {
            ImGui::SameLine();
            ImGui::PushItemWidth(80);
            ImGui::SliderFloat("Dist##NodeSnap", &nodeSnapThreshold_, 0.1f, 5.0f, "%.1f");
            ImGui::PopItemWidth();
        }
    }
}

void PipeEditor::UpdateAndDraw(GameScene* scene, Engine::Renderer* renderer, const ImVec2& gameImageMin, const ImVec2& /*gameImageMax*/, float tW, float tH) {
    if (!scene || scene->IsPlaying() || !pipeMode_ || !behavior_) {
        if (previewJointId_ != entt::null || previewPipeId_ != entt::null) ClearPreview(scene);
        return;
    }

    ImVec2 mousePos = ImGui::GetMousePos();
    float localX = mousePos.x - gameImageMin.x;
    float localY = mousePos.y - gameImageMin.y;
    bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

    if (insideImage) {
        auto viewMat = scene->GetCamera().View();
        auto projMat = scene->GetCamera().Proj();
        DirectX::XMVECTOR rayOrig, rayDir;
        EditorUI::ScreenToWorldRay(localX, localY, tW, tH, viewMat, projMat, rayOrig, rayDir);

        float bestDist = FLT_MAX;
        Engine::Vector3 hitPoint = {0, 0, 0};
        bool hitTerrain = false;

        auto& registry = scene->GetRegistry();
        registry.view<NameComponent, TransformComponent>().each([&](entt::entity e, const NameComponent& nameC, const TransformComponent& tc) {
            bool isTerrain = (nameC.name.find("Terrain") != std::string::npos) || (nameC.name.find("Floor") != std::string::npos);
            if (!isTerrain) return;

            Engine::Model* model = nullptr;
            if (registry.all_of<GpuMeshColliderComponent>(e)) {
                model = renderer->GetModel(registry.get<GpuMeshColliderComponent>(e).meshHandle);
            }
            if (!model && registry.all_of<MeshRendererComponent>(e)) {
                auto& mr = registry.get<MeshRendererComponent>(e);
                if (mr.modelHandle != 0) model = renderer->GetModel(mr.modelHandle);
            }
            if (model) {
                float d; Engine::Vector3 hp;
                if (model->RayCast(rayOrig, rayDir, tc.GetTransform().ToMatrix(), d, hp)) {
                    if (d < bestDist) {
                        bestDist = d;
                        hitPoint = hp;
                        hitTerrain = true;
                    }
                }
            }
        });

        if (hasPipeStart_ && !hitTerrain) {
            float height = pipeStartNode_.y + 0.5f;
            float dirY = DirectX::XMVectorGetY(rayDir);
            if (std::abs(dirY) > 1e-6f) {
                float t = (height - DirectX::XMVectorGetY(rayOrig)) / dirY;
                if (t > 0 && t < bestDist) {
                    bestDist = t;
                    DirectX::XMVECTOR pVec = DirectX::XMVectorAdd(rayOrig, DirectX::XMVectorScale(rayDir, t));
                    hitPoint = {DirectX::XMVectorGetX(pVec), height, DirectX::XMVectorGetZ(pVec)};
                    hitTerrain = true;
                }
            }
        }

        if (hitTerrain) {
            Engine::Vector3 endNode = hitPoint;
            Engine::Vector3 startPos = pipeStartNode_;
            if (hasPipeStart_) startPos.y += 0.5f;

            // 地面に当たった場合は少し浮かせる
            if (!hasPipeStart_ || std::abs(endNode.y - startPos.y) > 0.01f) endNode.y += 0.5f;

            // スナップ処理
            if (hasPipeStart_ && useAngleSnap_) {
                Engine::Vector3 diffSnap = endNode - startPos;
                float lengthXZ = std::sqrt(diffSnap.x*diffSnap.x + diffSnap.z*diffSnap.z);
                if (lengthXZ > 0.001f) {
                    float angle = std::atan2(diffSnap.z, diffSnap.x);
                    float stepRad = snapAngleStep_ * 3.14159265f / 180.0f;
                    float snappedAngle = std::round(angle / stepRad) * stepRad;
                    endNode.x = startPos.x + std::cos(snappedAngle) * lengthXZ;
                    endNode.z = startPos.z + std::sin(snappedAngle) * lengthXZ;
                }
            }

            if (useNodeSnap_) {
                float bestXDist = nodeSnapThreshold_;
                float bestZDist = nodeSnapThreshold_;
                float snapX = endNode.x;
                float snapZ = endNode.z;
                registry.view<NameComponent, TransformComponent>().each([&]([[maybe_unused]] entt::entity e, const NameComponent& nameC, const TransformComponent& tc) {
                    if (nameC.name == "PipeJoint" || nameC.name == "_PreviewJoint") {
                        float distX = std::abs(tc.translate.x - endNode.x);
                        float distZ = std::abs(tc.translate.z - endNode.z);
                        if (distX < bestXDist) { bestXDist = distX; snapX = tc.translate.x; }
                        if (distZ < bestZDist) { bestZDist = distZ; snapZ = tc.translate.z; }
                    }
                });
                endNode.x = snapX;
                endNode.z = snapZ;
            }

            endNode = behavior_->ApplyCustomSnapping(scene, endNode);

            // プレビューと配置
            if (hasPipeStart_) {
                float dx = endNode.x - startPos.x;
                float dy = endNode.y - startPos.y;
                float dz = endNode.z - startPos.z;
                float length = std::sqrt(dx*dx + dy*dy + dz*dz);

                ClearPreview(scene);

                entt::entity previewJoint = registry.create();
                behavior_->OnGenerateJoint(registry, previewJoint, endNode, renderer);
                registry.get<NameComponent>(previewJoint).name = "_PreviewJoint";
                previewJointId_ = previewJoint;
                if (registry.all_of<MeshRendererComponent>(previewJoint)) {
                    auto& mr = registry.get<MeshRendererComponent>(previewJoint);
                    mr.color = {0.8f, 0.8f, 1.0f, 0.6f};
                    mr.shaderName = "SolidColor";
                }

                entt::entity previewPipe = registry.create();
                behavior_->OnGeneratePipe(registry, previewPipe, startPos, endNode, length, renderer);
                registry.get<NameComponent>(previewPipe).name = "_PreviewPipe";
                previewPipeId_ = previewPipe;
                if (registry.all_of<MeshRendererComponent>(previewPipe)) {
                    auto& mr = registry.get<MeshRendererComponent>(previewPipe);
                    mr.color = {0.8f, 1.0f, 0.8f, 0.6f};
                    mr.shaderName = "SolidColor";
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    ClearPreview(scene);
                    
                    entt::entity finalJoint = registry.create();
                    behavior_->OnGenerateJoint(registry, finalJoint, endNode, renderer);

                    entt::entity finalPipe = registry.create();
                    behavior_->OnGeneratePipe(registry, finalPipe, startPos, endNode, length, renderer);
                    
                    behavior_->OnPlacementComplete(scene, startPos, endNode);
                    pipeStartNode_ = {endNode.x, endNode.y - 0.5f, endNode.z};
                }
            } else {
                ClearPreview(scene);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    pipeStartNode_ = hitPoint;
                    hasPipeStart_ = true;

                    entt::entity joint = registry.create();
                    behavior_->OnGenerateJoint(registry, joint, {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z}, renderer);
                    EditorUI::Log("Pipe start placed.");
                }
            }
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ClearPreview(scene);
        hasPipeStart_ = false;
        pipeStartNode_ = {0, 0, 0};
    }
}

} // namespace Game

