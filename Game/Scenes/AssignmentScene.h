// AssignmentScene.h
// 評価課題の確認用デモシーン。
// スキニングモデルのアニメーション補間・骨のデバッグ表示・武器の追従・
// 手から出るパーティクル・GPU パーティクル・ポストエフェクトの切り替えをまとめて確認できる。
#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Renderer.h"
#include "Model.h"
#include "Transform.h"
#include "WindowDX.h"
#include "Particle.h"
#include "GPUParticle.h"
#include "../ObjectTypes.h"
#include <string>
#include <vector>

namespace Game {

// 評価課題デモのシーン
class AssignmentScene : public Engine::IScene {
public:
    ~AssignmentScene() override;
    // IScene の実装 (DrawEditor は GPU パーティクルの調整ウィンドウ)
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;
    void DrawEditor() override;

private:
    // 時間で切り替わるポストエフェクトの予約 (効果名・残り時間・長さ・理由の説明)
    struct PostEffectEvent {
        std::string effect;
        float timer = 0.0f;
        float duration = 0.0f;
        std::string reason;
    };

    // ポストエフェクトを予約する / 毎フレーム更新する
    void QueuePostEffect(const std::string& effect, float duration, const std::string& reason);
    void UpdatePostEffects(float dt);
    // 練習用の敵 (赤い箱) との戦闘を更新する
    void UpdateEnemyCombat(float dt, bool attackPressed);
    // 水平方向 (XZ) の距離
    float DistanceXZ(const Engine::Vector3& a, const Engine::Vector3& b) const;

    // エンジン側のオブジェクトとカメラ
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Camera camera_;

    // 評価課題用のモデル (スキニングするミュータントと、敵の箱)
    uint32_t mutantModelHandle_ = 0;
    std::shared_ptr<Engine::Model> mutantModel_;
    uint32_t enemyCubeMesh_ = 0;
    uint32_t enemyCubeTex_ = 0;

    // 剣とパーティクル
    uint32_t swordMesh_ = 0;
    uint32_t swordTex_ = 0;
    // 左手から出すパーティクルの発生間隔と CPU パーティクル
    float particleTimer_ = 0.0f;
    std::unique_ptr<Engine::ParticleSystem> particleSys_;
    // 剣の根元・先端・向き (GPU パーティクルの発生位置に使う)
    Engine::Vector3 swordPos_ = {0.0f, 0.0f, 0.0f};
    Engine::Vector3 swordTip_ = {0.0f, 0.0f, 0.0f};
    Engine::Vector3 swordDir_ = {0.0f, 1.0f, 0.0f};
    
    // 竜巻などに使う GPU パーティクル
    std::unique_ptr<Engine::GPUParticleSystem> gpuParticleSys_;
    // GPU パーティクルの発生源 (0: 竜巻, 1: 剣のメッシュ)
    std::vector<Engine::GPUParticleEmitterData> emitters_;

    // プレイヤー (ミュータント) の位置・回転・拡大
    Engine::Transform transform_;
    
    // アニメーションの状態 (現在と直前のクリップ・再生時刻・混ぜる割合)
    float animTime_ = 0.0f;
    float prevAnimTime_ = 0.0f;
    float blendFactor_ = 0.0f;
    std::string currentAnim_ = "Mutant Idle";
    std::string prevAnim_ = "";
    
    // 移動中 / 攻撃中か
    bool isMoving_ = false;
    bool isAttacking_ = false;
    
    // カメラの角度 (縦・横) と距離
    float cameraAngleX_ = 0.0f;
    float cameraAngleY_ = 0.0f;
    float cameraDistance_ = 5.0f;
    
    // 竜巻の速度 (攻撃の風圧で押される)
    Engine::Vector3 tornadoVel_ = {0.0f, 0.0f, 0.0f};

    // ポストエフェクトを試すための簡単な戦闘の相手
    Engine::Transform enemyTransform_;
    // 体力・敵の攻撃の待ち時間と溜め時間・被弾時の点滅
    int playerHp_ = 100;
    int enemyHp_ = 60;
    float enemyAttackCooldown_ = 1.5f;
    float enemyAttackWindup_ = 0.0f;
    float enemyHitFlash_ = 0.0f;
    float playerHitFlash_ = 0.0f;
    // 敵の攻撃が発動待ちか / プレイヤーに気付いているか / ガード中か
    bool enemyAttackArmed_ = false;
    bool enemyAware_ = false;
    bool isGuarding_ = false;

    // ポストエフェクトの状態。短いイベントは予約の列で、それ以外はプレイ中の状態から効果を選ぶ
    std::string currentEffect_ = "Default";
    std::string previousEffect_ = "Default";
    std::string currentEffectReason_ = "Normal gameplay";
    float postEffectStrength_ = 0.0f;
    std::vector<PostEffectEvent> postEffectQueue_;
};

} // namespace Game
