// AssignmentScene.cpp
// 評価課題デモの初期化と毎フレームの更新: 入力・アニメーション補間・移動・カメラ・パーティクルの発生源。
// 戦闘とポストエフェクトは AssignmentSceneCombat.cpp、描画とエディタは AssignmentSceneDraw.cpp。
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

AssignmentScene::~AssignmentScene() {
    // GPU リソースの後片付けはレンダラーが行う
}

    // 初期化: カメラ・ライト・モデル・パーティクル・敵と戦闘の状態を準備する
    void AssignmentScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
        // パラメータは使わない
        (void)params;
        dx_ = dx;
    // BGM (セレクトと同じ曲)
    Music::Play(Music::Select);
        renderer_ = Engine::Renderer::GetInstance();
        
        // カメラ (視野角 45 度・画面の縦横比・描画範囲 0.1〜10000)
        camera_.Initialize();
        camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 10000.0f);
        
        // ライトの設定
        renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});
        renderer_->SetDirectionalLight({0, -1, 0.5f}, {1, 1, 1}, true);
        
        // 武器と敵のモデルを読み込む
        const auto& config = Config();
        swordMesh_ = renderer_->LoadObjMesh(config.sword);
        swordTex_  = renderer_->LoadTexture2D(config.swordTexture);
        enemyCubeMesh_ = renderer_->LoadObjMesh(config.enemy);
        enemyCubeTex_ = renderer_->LoadTexture2D(config.enemyTexture);

        // CPU パーティクルの初期化 (最大数・板のモデル・丸い画像)
        particleSys_ = std::make_unique<Engine::ParticleSystem>();
        particleSys_->Initialize(*renderer_, config.cpuParticles, config.particleModel, config.particleTexture, true, true);

        // GPU パーティクルの初期化と、発生源 (0: 竜巻、1: 剣のメッシュから出す炎)
        gpuParticleSys_ = std::make_unique<Engine::GPUParticleSystem>();
        gpuParticleSys_->Initialize(renderer_->GetDevice(), config.gpuParticles);
        emitters_ = config.emitters;

    // スキニングとアニメーション補間にはミュータントのモデルを使う
    mutantModelHandle_ = renderer_->LoadObjMesh(config.mutant);
    if (mutantModelHandle_ > 0) {
        // 歩き・攻撃のアニメーションを追加で読み込む
        for (const auto& animation : config.mutantAnimations) renderer_->LoadAdditionalAnimation(mutantModelHandle_, animation);
        // モデルの所有権は Renderer にあるので、shared_ptr では削除しない
        mutantModel_ = std::shared_ptr<Engine::Model>(renderer_->GetModel(mutantModelHandle_), [](Engine::Model*){});
        // マテリアルにテクスチャを順番に割り当てる
        if (!config.mutantTextures.empty()) {
            for (int i = 0; i < config.materialCount; ++i) {
                const auto texture = renderer_->LoadTexture2D(config.mutantTextures[size_t(i) % config.mutantTextures.size()]);
                mutantModel_->SetMaterialSrv(i, renderer_->GetTextureSrvGpu(texture));
            }
        }
    }

    // プレイヤーと敵の初期配置、戦闘の状態
    transform_.translate = {0.0f, 0.0f, 0.0f};
    transform_.scale = {1.0f, 1.0f, 1.0f};
    transform_.rotate = {0.0f, 0.0f, 0.0f}; // オイラー角

    enemyTransform_.translate = config.enemyPosition;
    enemyTransform_.scale = config.enemyScale;
    enemyTransform_.rotate = {0.0f, 0.0f, 0.0f};
    playerHp_ = config.playerHp;
    enemyHp_ = config.enemyHp;
    enemyAttackCooldown_ = 1.0f;
    enemyAttackWindup_ = 0.0f;
    enemyAttackArmed_ = false;
    enemyHitFlash_ = 0.0f;
    playerHitFlash_ = 0.0f;
    
    // 最初のアニメーション
    currentAnim_ = config.animation;
    prevAnim_ = config.animation;
    
    // カメラの距離と角度
    cameraDistance_ = config.cameraDistance;
    cameraAngleX_ = config.cameraPitch;
    cameraAngleY_ = 0.0f;

    // ポストエフェクトの状態をリセット
    postEffectQueue_.clear();
    currentEffect_ = "Default";
    previousEffect_ = "Default";
    currentEffectReason_ = "Normal gameplay";
    postEffectStrength_ = 0.0f;
}

// 更新: 入力・アニメーション・移動・カメラ・戦闘・パーティクル
void AssignmentScene::Update() {
    float dt = 1.0f / 60.0f; // 簡単のため固定の時間刻みで進める
    
    // 入力
    auto* input = Engine::Input::GetInstance();
    
    // Esc でタイトルへ戻る
    if (input->Trigger(0x01)) { // Esc キー
        Engine::SceneManager::GetInstance()->RequestChange("Title");
        return;
    }
    
    // 数字キーでポストエフェクトを手動で試す。プレイ中の出来事もこの列に効果を追加する
    if (input->Trigger(0x0B)) QueuePostEffect("Dissolve", 3.5f, "Manual key 0: Dissolve");
    if (input->Trigger(0x02)) QueuePostEffect("Default", 0.45f, "Manual key 1: Default");
    if (input->Trigger(0x03)) QueuePostEffect("Grayscale", 3.2f, "Manual key 2: Grayscale");
    if (input->Trigger(0x04)) QueuePostEffect("GaussianFilter", 0.85f, "Manual key 3: GaussianFilter");
    if (input->Trigger(0x05)) QueuePostEffect("OutlinePost", 0.85f, "Manual key 4: LuminanceBasedOutline");
    if (input->Trigger(0x06)) QueuePostEffect("DepthBasedOutline", 0.85f, "Manual key 5: DepthBasedOutline");
    if (input->Trigger(0x07)) QueuePostEffect("RadialBlur", 0.85f, "Manual key 6: RadialBlur");
    if (input->Trigger(0x08)) QueuePostEffect("Vignetting", 0.85f, "Manual key 7: Vignetting");
    if (input->Trigger(0x09)) QueuePostEffect("BoxFilter", 0.85f, "Manual key 8: BoxFilter");
    if (input->Trigger(0x0A)) QueuePostEffect("Random", 0.85f, "Manual key 9: Random");
    
    // CPU パーティクルの更新
    if (particleSys_) {
        particleSys_->Update(dt);
    }

    // ゲームパッドの左スティック (XInput)
    // ゲームパッドのスティックとボタン (小さな傾きは無視する)
    float lx = 0.0f, ly = 0.0f;
    float rx = 0.0f, ry = 0.0f;
    bool aPressed = false;
    bool guardHeld = false;
    // パッド 0 番の状態を読む
    XINPUT_STATE state = {};
    if (XInputGetState(0, &state) == ERROR_SUCCESS) {
        // 左スティック (-1〜1)
        float normLX = std::fmaxf(-1, (float)state.Gamepad.sThumbLX / 32767);
        float normLY = std::fmaxf(-1, (float)state.Gamepad.sThumbLY / 32767);
        lx = (abs(normLX) < 0.2f ? 0.0f : normLX);
        ly = (abs(normLY) < 0.2f ? 0.0f : normLY);
        
        // 右スティック (-1〜1)
        float normRX = std::fmaxf(-1, (float)state.Gamepad.sThumbRX / 32767);
        float normRY = std::fmaxf(-1, (float)state.Gamepad.sThumbRY / 32767);
        rx = (abs(normRX) < 0.2f ? 0.0f : normRX);
        ry = (abs(normRY) < 0.2f ? 0.0f : normRY);
        
        // A ボタン (攻撃) と LB (ガード)
        aPressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        guardHeld = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    }
    
    // キーボードの入力を足し合わせる
    if (input->Down(0x11)) ly += 1.0f; // W
    if (input->Down(0x1F)) ly -= 1.0f; // S
    if (input->Down(0x1E)) lx -= 1.0f; // A
    if (input->Down(0x20)) lx += 1.0f; // D
    
    // 矢印キーでカメラを回す
    if (input->Down(0xC8)) ry += 1.0f; // 上矢印
    if (input->Down(0xD0)) ry -= 1.0f; // 下矢印
    if (input->Down(0xCB)) rx -= 1.0f; // 左矢印
    if (input->Down(0xCD)) rx += 1.0f; // 右矢印
    
    if (input->Trigger(0x39)) aPressed = true; // Space で攻撃
    if (input->Down(0x2A) || input->Down(0x36)) guardHeld = true; // 左右の Shift でガード
    if (input->IsMouseTrigger(0)) aPressed = true; // マウス左ボタンで攻撃
    if (input->IsMouseDown(1)) guardHeld = true; // マウス右ボタンでガード
    
    // -1〜1 に収める
    lx = std::fmaxf(-1.0f, std::fminf(1.0f, lx));
    ly = std::fmaxf(-1.0f, std::fminf(1.0f, ly));
    rx = std::fmaxf(-1.0f, std::fminf(1.0f, rx));
    ry = std::fmaxf(-1.0f, std::fminf(1.0f, ry));
    
    // スティックの倒し具合とガード・移動状態
    float magnitude = std::sqrt(lx * lx + ly * ly);
    isGuarding_ = guardHeld && !isAttacking_;
    
    // 前のフレームで動いていたか
    bool wasMoving = isMoving_;
    // 今動いているか
    isMoving_ = (magnitude > 0.0f);
    
    // 攻撃の開始: 攻撃アニメーションへ切り替える
    bool attackStarted = false;
    if (aPressed && !isAttacking_) {
        attackStarted = true;
        isAttacking_ = true;
        // 今のアニメーションを直前として覚え、攻撃へ切り替える
        prevAnim_ = currentAnim_;
        prevAnimTime_ = animTime_;
        currentAnim_ = "attack1";
        animTime_ = 0.0f;
        blendFactor_ = 1.0f;
        
        // 攻撃の風圧で竜巻を押す
        // 向いている方向へ竜巻の速度を足す
        float pushForce = 20.0f;
        float dirX = std::sin(transform_.rotate.y);
        float dirZ = std::cos(transform_.rotate.y);
        tornadoVel_.x += dirX * pushForce;
        tornadoVel_.z += dirZ * pushForce;
    }
    
    // 竜巻の速度に摩擦をかける
    tornadoVel_.x *= 0.95f;
    tornadoVel_.y *= 0.95f;
    tornadoVel_.z *= 0.95f;
    
    // 攻撃中以外: 移動の開始・停止で歩き/待機のアニメーションへ切り替える
    if (!isAttacking_) {
        if (isMoving_ && !wasMoving) {
            // 移動を始めた
            prevAnim_ = currentAnim_;
            prevAnimTime_ = animTime_;
            currentAnim_ = "Mutant Walking";
            animTime_ = 0.0f;
            blendFactor_ = 1.0f; // 混ぜる割合を 1.0 から 0.0 へ下げていく
        } else if (!isMoving_ && wasMoving) {
            // 止まった
            prevAnim_ = currentAnim_;
            prevAnimTime_ = animTime_;
            currentAnim_ = "Mutant Idle";
            animTime_ = 0.0f;
            blendFactor_ = 1.0f;
        }
    }
    
    // 混ぜる割合を減らす
    if (blendFactor_ > 0.0f) {
        blendFactor_ -= dt * 4.0f; // 0.25 秒で混ぜ終わる
        if (blendFactor_ < 0.0f) blendFactor_ = 0.0f;
    }
    
    // アニメーションの更新
    // アニメーションの再生時刻を進める (現在のクリップと、混ぜている直前のクリップ)
    if (mutantModel_) {
        // モデルのアニメーションの一覧
        const auto& data = mutantModel_->GetData();
        // 現在のアニメーションの再生時刻を進める
        // それぞれ進めたか
        bool currentUpdated = false;
        bool prevUpdated = false;
        // 名前で探す
        for (const auto& a : data.animations) {
            if (!currentUpdated && a.name.find(currentAnim_) != std::string::npos) {
                animTime_ += dt * a.ticksPerSecond * 0.5f; // デモでは重く遅めの動きにするため半分の速さで再生する
                if (animTime_ > a.duration) {
                    animTime_ = std::fmod(animTime_, a.duration);
                    // 攻撃アニメーションが終わったら歩き/待機に戻る
                    if (isAttacking_ && currentAnim_ == "attack1") {
                        isAttacking_ = false;
                        prevAnim_ = currentAnim_;
                        prevAnimTime_ = animTime_;
                        currentAnim_ = isMoving_ ? "Mutant Walking" : "Mutant Idle";
                        animTime_ = 0.0f;
                        blendFactor_ = 1.0f;
                    }
                }
                // 現在のクリップは進めた
                currentUpdated = true;
            }
            // 混ぜている間は、直前のクリップも同じように進める
            if (blendFactor_ > 0.0f && !prevUpdated && a.name.find(prevAnim_) != std::string::npos) {
                prevAnimTime_ += dt * a.ticksPerSecond * 0.5f;
                if (prevAnimTime_ > a.duration) {
                    prevAnimTime_ = std::fmod(prevAnimTime_, a.duration);
                }
                prevUpdated = true;
            }
        }
    }

    // 入力に合わせて位置と向きを更新する
    if (isMoving_ && !isAttacking_) {
        // 移動量 (毎秒 3 × 倒し具合)
        float speed = 3.0f * magnitude * dt;
        
        // カメラの向きを基準にした移動方向
        float moveAngle = std::atan2(lx, ly);
        float targetAngle = cameraAngleY_ + moveAngle;
        
        // その向きへ進める
        transform_.translate.x += std::sin(targetAngle) * speed;
        transform_.translate.z += std::cos(targetAngle) * speed;
        
        // 移動方向を向く (オイラー角)
        transform_.rotate.y = targetAngle;
    }
    
    // カメラ操作 (右スティックとマウス)
    // 右スティックで回す
    if (std::abs(rx) > 0.2f) cameraAngleY_ += rx * dt * 2.0f;
    if (std::abs(ry) > 0.2f) cameraAngleX_ -= ry * dt * 2.0f;
    // マウスの移動で回し、ホイールで距離を変える (5〜24)
    cameraAngleY_ += input->GetMouseDeltaX() * 0.005f;
    cameraAngleX_ += input->GetMouseDeltaY() * 0.005f;
    cameraDistance_ -= input->GetMouseWheelDelta() * 1.0f;
    if (cameraDistance_ < 5.0f) cameraDistance_ = 5.0f;
    if (cameraDistance_ > 24.0f) cameraDistance_ = 24.0f;
    
    // カメラの上下の角度を制限する
    if (cameraAngleX_ < -1.5f) cameraAngleX_ = -1.5f;
    if (cameraAngleX_ > 1.5f) cameraAngleX_ = 1.5f;
    
    // カメラは頭の少し上を見ながら、角度と距離から決まる位置に置く
    // カメラ位置の更新
    Engine::Vector3 targetPos = transform_.translate;
    targetPos.y += 4.0f; // 頭から胸のあたりを見る
    
    // 角度と距離からカメラのずれを求める
    Engine::Vector3 camOffset;
    camOffset.x = std::sin(cameraAngleY_) * std::cos(cameraAngleX_) * cameraDistance_;
    camOffset.y = -std::sin(cameraAngleX_) * cameraDistance_;
    camOffset.z = std::cos(cameraAngleY_) * std::cos(cameraAngleX_) * cameraDistance_;
    
    // 注視点からずれの分だけ離す
    Engine::Vector3 camPos = targetPos;
    camPos.x -= camOffset.x;
    camPos.y -= camOffset.y;
    camPos.z -= camOffset.z;
    
    // 位置を決めて注視点を向く
    camera_.SetPosition({camPos.x, camPos.y, camPos.z});
    camera_.LookAt({targetPos.x, targetPos.y, targetPos.z}, {0, 1, 0});
    camera_.Tick(dt);
    
    // 描画に使うカメラを渡す
    renderer_->SetCamera(camera_);

    // 戦闘とポストエフェクトの更新
    UpdateEnemyCombat(dt, attackStarted);
    UpdatePostEffects(dt);
    
    // GPU パーティクルの発生源の更新
    if (gpuParticleSys_ && !emitters_.empty()) {
        // 竜巻 (発生源 0) の位置を速度で動かす
        // 竜巻の位置
        Engine::Vector3 tornadoPos = { emitters_[0].emitPos.x, emitters_[0].emitPos.y, emitters_[0].emitPos.z };
        tornadoPos.x += tornadoVel_.x * dt;
        tornadoPos.y += tornadoVel_.y * dt;
        tornadoPos.z += tornadoVel_.z * dt;
        
        // 見えない壁で跳ね返してステージ内に留める
        if (tornadoPos.x > 15.0f) { tornadoPos.x = 15.0f; tornadoVel_.x *= -0.5f; }
        if (tornadoPos.x < -15.0f) { tornadoPos.x = -15.0f; tornadoVel_.x *= -0.5f; }
        if (tornadoPos.z > 15.0f) { tornadoPos.z = 15.0f; tornadoVel_.z *= -0.5f; }
        if (tornadoPos.z < -15.0f) { tornadoPos.z = -15.0f; tornadoVel_.z *= -0.5f; }
        
        // 動かした位置を書き戻す
        emitters_[0].emitPos = { tornadoPos.x, tornadoPos.y, tornadoPos.z };
        if (emitters_[0].fieldType == 3) {
            emitters_[0].fieldPos = { tornadoPos.x, tornadoPos.y, tornadoPos.z }; // 竜巻の力場も発生源と同じ位置に置く
        }
        
        // メッシュの発生源 (1) は剣に追従させて外向きに飛ばす
        emitters_[1].emitPos = { swordPos_.x, swordPos_.y, swordPos_.z };
        
        // 剣の向きへ一定の速さで飛ばす
        float shootSpeed = 10.0f;
        emitters_[1].emitVel = { swordDir_.x * shootSpeed, swordDir_.y * shootSpeed, swordDir_.z * shootSpeed };
        
        emitters_[1].emitterShape = 3; // メッシュの表面から出す
        emitters_[1].emitterExtents.z = 0.25f;
        if (swordMesh_ > 0) {
            // 剣のモデルの頂点を、メッシュの発生源として渡す
            Engine::Model* swordModel = renderer_->GetModel(swordMesh_);
            if (swordModel) {
                gpuParticleSys_->SetEmitterMesh(swordModel->GetVertexBufferAddr(), swordModel->GetVertexCount(), sizeof(Engine::VertexData));
            }
        }
        
        // GPU パーティクルを更新する
        gpuParticleSys_->Update(renderer_->GetCommandList(), dt, emitters_);
    }
}

} // namespace Game
