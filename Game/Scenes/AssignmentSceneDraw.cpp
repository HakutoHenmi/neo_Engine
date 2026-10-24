// AssignmentSceneDraw.cpp
// 評価課題デモの描画: 床の線・敵・スキニングモデル・パーティクル・骨のデバッグ表示・操作の案内と、
// GPU パーティクルを調整するエディタのウィンドウ。
#include "../UI/SceneMusic.h"
#include "../UI/GameUI.h"
#include "AssignmentScene.h"
// 入力・パス・シーン管理 (エンジン側)
#include "../../Engine/Input.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include <Xinput.h>
// ゲームパッド (XInput) を直接読むためのライブラリ
#pragma comment(lib, "xinput.lib")

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include "../Data/GameData.h"
// デバッグ用の ImGui の画面
#include <imgui.h>

#include "AssignmentConfig.h"

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {
// assignment.json の設定 (AssignmentConfig.h)
using namespace AssignmentConfig;

// 描画: 床の線・敵・スキニングモデル・パーティクル・骨・UI
void AssignmentScene::Draw() {
    // 描画の窓口かモデルが無ければ何もしない
    if (!renderer_ || !mutantModel_) return;

    // 床の格子の線
    Engine::Vector4 gridColor = {0.3f, 0.3f, 0.3f, 1.0f};
    // -10〜10 の範囲に 1 間隔で縦横の線
    for (int i = -10; i <= 10; ++i) {
        renderer_->DrawLine3D({(float)i, 0.0f, -10.0f}, {(float)i, 0.0f, 10.0f}, gridColor);
        renderer_->DrawLine3D({-10.0f, 0.0f, (float)i}, {10.0f, 0.0f, (float)i}, gridColor);
    }

    // 敵の箱 (被弾時は黄色、攻撃の溜め中は橙) と、攻撃範囲の四角
    if (enemyCubeMesh_ > 0 && enemyHp_ > 0) {
        // 通常は赤
        Engine::Vector4 enemyColor = {0.8f, 0.15f, 0.15f, 1.0f};
        if (enemyHitFlash_ > 0.0f) {
            enemyColor = {1.0f, 1.0f, 0.2f, 1.0f};
        } else if (enemyAttackWindup_ > 0.0f) {
            enemyColor = {1.0f, 0.25f, 0.05f, 1.0f};
        }
        // 箱を描く
        renderer_->DrawMesh(enemyCubeMesh_, enemyCubeTex_, enemyTransform_, enemyColor, "Default");

        // 攻撃範囲 (溜め中は少し広く、濃い色) の四角を床に描く
        const Engine::Vector3 p = enemyTransform_.translate;
        const float r = enemyAttackWindup_ > 0.0f ? 4.4f : 4.0f;
        const Engine::Vector4 rangeColor = enemyAttackWindup_ > 0.0f ? Engine::Vector4{1, 0.1f, 0.05f, 1} : Engine::Vector4{0.8f, 0.2f, 0.2f, 1};
        renderer_->DrawLine3D({p.x - r, 0.03f, p.z - r}, {p.x + r, 0.03f, p.z - r}, rangeColor, true);
        renderer_->DrawLine3D({p.x + r, 0.03f, p.z - r}, {p.x + r, 0.03f, p.z + r}, rangeColor, true);
        renderer_->DrawLine3D({p.x + r, 0.03f, p.z + r}, {p.x - r, 0.03f, p.z + r}, rangeColor, true);
        renderer_->DrawLine3D({p.x - r, 0.03f, p.z + r}, {p.x - r, 0.03f, p.z - r}, rangeColor, true);
    }

    // スケルトンの行列を用意する
    // 骨ごとの行列と、デバッグ表示用の骨情報
    std::vector<Engine::Matrix4x4> skeletonParams(Engine::kMaxBones, Engine::Matrix4x4::Identity());
    std::vector<Engine::Model::DebugBone> debugBones;

    // モデルのデータ
    const auto& data = mutantModel_->GetData();
    
    // 現在と直前のアニメーション
    const Engine::Animation* currAnimPtr = nullptr;
    const Engine::Animation* prevAnimPtr = nullptr;
    
    // 現在と直前のアニメーションを名前で探す
    for (const auto& a : data.animations) {
        if (!currAnimPtr && a.name.find(currentAnim_) != std::string::npos) currAnimPtr = &a;
        if (blendFactor_ > 0.0f && !prevAnimPtr && a.name.find(prevAnim_) != std::string::npos) prevAnimPtr = &a;
    }
    
    // モデル空間の基準行列
    Engine::Matrix4x4 rootTransform = Engine::Matrix4x4::Identity();
    
    // Model::UpdateSkeleton が補間を行う。混ぜる割合は (直前, 現在) の線形補間の t として渡す
    // このシーンの blendFactor_ は直前のアニメーションの残り具合 (1 → 0) なので
    // UpdateSkeleton には 1 - blendFactor_ を渡す (1 で現在、0 で直前のアニメーション)
    
    // 現在のアニメーションがあれば、直前と混ぜて骨の行列を計算する
    if (currAnimPtr) {
        mutantModel_->UpdateSkeleton(data.rootNode, rootTransform, *currAnimPtr, animTime_, prevAnimPtr, prevAnimTime_, 1.0f - blendFactor_, skeletonParams, &debugBones);
    } else {
        // アニメーションが見つからないときは空のアニメーションで初期姿勢にする
        Engine::Animation dummyAnim;
        mutantModel_->UpdateSkeleton(data.rootNode, rootTransform, dummyAnim, 0.0f, nullptr, 0.0f, 0.0f, skeletonParams, &debugBones);
    }
    
    // スキニングメッシュを描画する (複数メッシュ・複数マテリアルは DrawSkinnedMesh 内で処理する)
    // テクスチャ 0 を渡すが、モデル内部のマテリアルとテクスチャの対応が使われる
    // モデルを描く
    renderer_->DrawSkinnedMesh(mutantModelHandle_, 0, transform_, skeletonParams, {1, 1, 1, 1});

    if (particleSys_) {
        // CPU パーティクルの描画
        particleSys_->Draw(camera_, "Particle");
    }
    
    if (gpuParticleSys_) {
        // GPU パーティクルの描画
        // ビュー行列 × 射影行列
        Engine::Matrix4x4 vpMat;
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&vpMat), DirectX::XMMatrixMultiply(camera_.View(), camera_.Proj()));
        
        // シーンの不透明・半透明の描画の後に描くよう、レンダラーに処理を預ける
        renderer_->SetCustomDrawJob([this, vpMat](ID3D12GraphicsCommandList* cmd) {
            gpuParticleSys_->Draw(cmd, vpMat, camera_.Position());
        });
    }

    // 骨のデバッグ表示
    // モデルのワールド行列と、骨の線の色
    Engine::Matrix4x4 worldMat = transform_.ToMatrix();
    Engine::Vector4 boneColor = {1.0f, 1.0f, 0.0f, 1.0f}; // 黄色
    
    // ワールド座標 → 画面座標 (骨の名前を表示する位置)
    auto WorldToScreen = [&](const Engine::Vector3& worldPos) -> std::pair<float, float> {
        // ビュー行列と射影行列で変換する
        DirectX::XMVECTOR p = DirectX::XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        DirectX::XMMATRIX v = camera_.View();
        DirectX::XMMATRIX pr = camera_.Proj();
        p = DirectX::XMVector4Transform(p, v);
        p = DirectX::XMVector4Transform(p, pr);
        DirectX::XMFLOAT4 clip;
        DirectX::XMStoreFloat4(&clip, p);
        // カメラの後ろなら画面外
        if (clip.w <= 0.0001f) return {-1.0f, -1.0f};
        // 正規化デバイス座標
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;
        // Engine::WindowDX::kW / kH は内部解像度
        float screenX = (ndcX + 1.0f) * 0.5f * static_cast<float>(Engine::WindowDX::kW);
        float screenY = (1.0f - ndcY) * 0.5f * static_cast<float>(Engine::WindowDX::kH);
        return {screenX, screenY};
    };

    // 各骨: 親との線・ローカル軸・名前を描き、右手に剣、左手にパーティクルを付ける
    for (const auto& bone : debugBones) {
        // 骨のワールド行列と位置、親の位置
        Engine::Matrix4x4 finalMat = Engine::Matrix4x4::Multiply(bone.globalMatrix, worldMat);
        Engine::Vector3 pCurrent = { finalMat.m[3][0], finalMat.m[3][1], finalMat.m[3][2] };
        Engine::Vector3 pParent = Engine::TransformCoord(bone.parentPos, worldMat);
        
        // 親との線
        renderer_->DrawLine3D(pParent, pCurrent, boneColor, true); // 手前の物に隠れずに描く
        
        // ローカル軸
        // 骨の X・Y・Z 軸
        Engine::Vector3 localX = { finalMat.m[0][0], finalMat.m[0][1], finalMat.m[0][2] };
        Engine::Vector3 localY = { finalMat.m[1][0], finalMat.m[1][1], finalMat.m[1][2] };
        Engine::Vector3 localZ = { finalMat.m[2][0], finalMat.m[2][1], finalMat.m[2][2] };
        
        // 右手: 剣を持たせる
        if (bone.name == "RightHand" || bone.name == "mixamorig:RightHand") {
            // 剣を自然に見せるための手からのオフセット (拡大・回転・移動)
            Engine::Matrix4x4 offset = Engine::Matrix4x4::MakeAffineMatrix(
                {1.0f, 1.0f, 1.0f}, // 拡大
                {0.0f, 0.0f, 0.0f}, // 回転 (前方向の確認のため 0)
                {0.0f, 0.0f, 0.0f} // 移動
            );
            // 剣のワールド行列
            Engine::Matrix4x4 swordWorld = Engine::Matrix4x4::Multiply(offset, finalMat);
            
            // 剣の根元の位置
            swordPos_ = { swordWorld.m[3][0], swordWorld.m[3][1], swordWorld.m[3][2] };
            
            // 剣は手のローカル Y 軸方向を向いているとみなす (Mixamo の右手で一般的)
            Engine::Vector3 swordForward = { swordWorld.m[1][0], swordWorld.m[1][1], swordWorld.m[1][2] };
            
            // 念のため正規化する
            float len = std::sqrt(swordForward.x*swordForward.x + swordForward.y*swordForward.y + swordForward.z*swordForward.z);
            if (len > 0.0001f) {
                swordForward.x /= len; swordForward.y /= len; swordForward.z /= len;
            } else {
                swordForward = {0.0f, 1.0f, 0.0f};
            }
            
            swordDir_ = swordForward;
            
            // 剣は大きいので、根元から長さ分ずらした位置を先端とする
            float swordLength = 5.0f;
            // 剣の先端
            swordTip_ = {
                swordPos_.x + swordForward.x * swordLength,
                swordPos_.y + swordForward.y * swordLength,
                swordPos_.z + swordForward.z * swordLength
            };
            
            // 剣を描く
            if (swordMesh_ > 0) {
                renderer_->DrawMeshInstanced(swordMesh_, swordTex_, swordWorld, {1.0f, 1.0f, 1.0f, 1.0f}, "Default");
            }
        }
        
        // 左手: パーティクルを出す
        if (bone.name == "LeftHand" || bone.name == "mixamorig:LeftHand") {
            if (particleSys_) {
                Engine::Vector3 pos = pCurrent;
                Engine::Vector3 vel = {0.0f, 2.0f, 0.0f}; // 上向き
                Engine::Vector3 acc = {0.0f, -0.5f, 0.0f}; // わずかに下向きの重力
                Engine::Vector3 sScale = {0.3f, 0.3f, 0.3f};
                Engine::Vector3 eScale = {0.0f, 0.0f, 0.0f};
                Engine::Vector4 sColor = {1.0f, 0.6f, 0.1f, 1.0f}; // 橙〜黄色
                Engine::Vector4 eColor = {1.0f, 0.1f, 0.1f, 0.0f}; // 赤へ変わりながら消える
                float life = 0.5f;
                
                // 少しランダムに散らす
                float rx = ((float)(rand() % 100) / 100.0f - 0.5f) * 1.0f;
                float rz = ((float)(rand() % 100) / 100.0f - 0.5f) * 1.0f;
                vel.x += rx;
                vel.z += rz;
                
                // パーティクルを 1 つ出す
                particleSys_->Emit(pos, vel, acc, sScale, eScale, sColor, eColor, life);
            }
        }
        
        // 長さを 1 にする関数
        auto norm = [](Engine::Vector3 v) { float l = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); return l > 0 ? Engine::Vector3{v.x/l, v.y/l, v.z/l} : v; };
        localX = norm(localX);
        localY = norm(localY);
        localZ = norm(localZ);
        
        // 軸の線の長さ。X 赤・Y 緑・Z 青で描く
        float axisLen = 0.5f;
        renderer_->DrawLine3D(pCurrent, {pCurrent.x + localX.x * axisLen, pCurrent.y + localX.y * axisLen, pCurrent.z + localX.z * axisLen}, {1,0,0,1}, true);
        renderer_->DrawLine3D(pCurrent, {pCurrent.x + localY.x * axisLen, pCurrent.y + localY.y * axisLen, pCurrent.z + localY.z * axisLen}, {0,1,0,1}, true);
        renderer_->DrawLine3D(pCurrent, {pCurrent.x + localZ.x * axisLen, pCurrent.y + localZ.y * axisLen, pCurrent.z + localZ.z * axisLen}, {0,0,1,1}, true);
        
        // 骨の名前
        // 画面内なら名前を書く
        auto screenPos = WorldToScreen(pCurrent);
        if (screenPos.first >= 0) {
            renderer_->DrawString(bone.name, screenPos.first, screenPos.second, 0.4f, {1,1,1,1});
        }
    }
    // 左上に体力と操作、右上に現在のポストエフェクトと理由を表示する
    UI::Canvas ui(renderer_);
    // 左上の板と見出し
    ui.Panel({24,24,460,370});
    ui.Text("TRAINING GROUNDS",44,42,34,UI::Lime);
    // プレイヤーの体力と棒
    ui.Text("PLAYER  "+std::to_string(playerHp_)+" / 100",44,93,26);
    ui.Bar({44,132,410,12},playerHp_/100.f);
    // 敵の体力と棒
    ui.Text("ENEMY  "+std::to_string(enemyHp_)+" / 60",44,159,26);
    ui.Bar({44,199,410,12},enemyHp_/60.f,UI::Gold);
    // 操作の案内
    ui.Prompt("keyboard_w","WASD / PAD: MOVE",44,235);
    ui.Prompt("mouse_left","ATTACK / SPACE / PAD A",44,277);
    ui.Prompt("mouse_right","GUARD / SHIFT / PAD LB",44,319);
    ui.Prompt("keyboard_escape","BACK TO TITLE",44,410);
    // 右上の板: 現在の効果と理由
    ui.Panel({820,24,436,112});
    ui.Text("POST EFFECT / "+currentEffect_,840,42,24);
    ui.Text(currentEffectReason_,840,82,18,UI::Muted);
}

// ImGui 用 (使わない)
void AssignmentScene::DrawUI() {}
// エディタ: GPU パーティクルの発生源を調整するウィンドウ
void AssignmentScene::DrawEditor() {
#ifdef USE_IMGUI
    // ウィンドウを開く
    if (ImGui::Begin("GPU Particle Editor")) {
        // 発生源 0 (竜巻) の設定
        if (ImGui::CollapsingHeader("Emitter 0 (Tornado)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!emitters_.empty()) {
                // 位置・出す量・初速・寿命
                auto& e = emitters_[0];
                ImGui::DragFloat3("Position##0", &e.emitPos.x, 0.1f);
                ImGui::DragFloat("Emit Rate##0", &e.emitRate, 10.0f, 0.0f, 10000.0f);
                ImGui::DragFloat3("Velocity##0", &e.emitVel.x, 0.1f);
                ImGui::DragFloat("Life##0", &e.emitLife, 0.1f, 0.1f, 10.0f);
                
                // 出し方 (点・球・箱・メッシュ)
                const char* shapes[] = { "Point", "Sphere", "Box", "Mesh" };
                int currentShape = e.emitterShape;
                if (ImGui::Combo("Shape##0", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
                    e.emitterShape = currentShape;
                }
                // 出す範囲の大きさ
                ImGui::DragFloat3("Extents##0", &e.emitterExtents.x, 0.1f);
                
                // 始まりと終わりの色 (HDR)
                ImGui::ColorEdit4("Color Start##0", &e.colorStart.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                ImGui::ColorEdit4("Color End##0", &e.colorEnd.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                
                // 力場 (なし・重力・風・竜巻) とそのパラメータ
                const char* fields[] = { "None", "Gravity", "Wind", "Tornado" };
                int currentField = e.fieldType;
                if (ImGui::Combo("Field##0", &currentField, fields, IM_ARRAYSIZE(fields))) {
                    e.fieldType = currentField;
                }
                ImGui::DragFloat4("Field Params##0", &e.fieldParams.x, 0.1f);
                
                // 見た目 (通常・軌跡・ライティングあり)
                const char* types[] = { "Default", "Trail", "Lit" };
                int currentType = e.particleType;
                if (ImGui::Combo("Type##0", &currentType, types, IM_ARRAYSIZE(types))) {
                    e.particleType = currentType;
                }
            }
        }
        
        // 発生源 1 (剣のメッシュ) の設定
        if (ImGui::CollapsingHeader("Emitter 1 (Sword Mesh)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (emitters_.size() > 1) {
                auto& e = emitters_[1];
                // 出す量・初速・寿命
                ImGui::DragFloat("Emit Rate##1", &e.emitRate, 10.0f, 0.0f, 10000.0f);
                ImGui::DragFloat3("Velocity##1", &e.emitVel.x, 0.1f);
                ImGui::DragFloat("Life##1", &e.emitLife, 0.1f, 0.1f, 10.0f);
                
                // 出し方 (点・球・箱・メッシュ)
                int currentShape = e.emitterShape;
                const char* shapes[] = { "Point", "Sphere", "Box", "Mesh" };
                if (ImGui::Combo("Shape##1", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
                    e.emitterShape = currentShape;
                }
                
                // 始まりと終わりの色 (HDR)
                ImGui::ColorEdit4("Color Start##1", &e.colorStart.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                ImGui::ColorEdit4("Color End##1", &e.colorEnd.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                
                // 見た目 (通常・軌跡・ライティングあり)
                const char* types[] = { "Default", "Trail", "Lit" };
                int currentType = e.particleType;
                if (ImGui::Combo("Type##1", &currentType, types, IM_ARRAYSIZE(types))) {
                    e.particleType = currentType;
                }
            }
        }
    }
    // ウィンドウを閉じる
    ImGui::End();
#endif
}

} // namespace Game
