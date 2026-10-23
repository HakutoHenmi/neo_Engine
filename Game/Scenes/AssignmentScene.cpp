// AssignmentScene.cpp
// 評価課題デモの処理: 入力・アニメーション補間・カメラ・練習用の敵との戦闘・
// ポストエフェクトの切り替え・パーティクル・骨のデバッグ表示。
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

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

namespace {
// assignment.json の中身 (素材・パーティクルの発生源・開始時の値・戦闘の値)
struct AssignmentData {
    std::string sword, swordTexture, enemy, enemyTexture, particleModel, particleTexture, mutant;
    std::vector<std::string> mutantAnimations, mutantTextures;
    int materialCount = 0;
    uint32_t cpuParticles = 0, gpuParticles = 0;
    std::vector<Engine::GPUParticleEmitterData> emitters;
    int playerHp = 0, enemyHp = 0;
    Engine::Vector3 enemyPosition{}, enemyScale{};
    float cameraDistance = 0, cameraPitch = 0;
    std::string animation;
    float noticeRange = 0, loseRange = 0, attackRange = 0, attackFacing = 0;
    float enemyAttackRange = 0, enemyHitRange = 0, enemyWindup = 0, enemyCooldown = 0;
    int attackDamage = 0, enemyDamage = 0, enemyGuardedDamage = 0;
};
// assignment.json を読み込む (最初に使われたときに 1 回だけ)
const AssignmentData& Config() {
    static const AssignmentData data = [] {
        const char* file = "assignment.json";
        const auto& json = Data::Load(file);
        AssignmentData out;
        auto f3 = [&](const nlohmann::json& v) { auto a = Data::ReadArray<float, 3>(v, file); return DirectX::XMFLOAT3{a[0], a[1], a[2]}; };
        auto f4 = [&](const nlohmann::json& v) { auto a = Data::ReadArray<float, 4>(v, file); return DirectX::XMFLOAT4{a[0], a[1], a[2], a[3]}; };
        const auto& assets = Data::Require(json, "assets", file);
        out.sword = assets.at("sword").get<std::string>(); out.swordTexture = assets.at("swordTexture").get<std::string>();
        out.enemy = assets.at("enemy").get<std::string>(); out.enemyTexture = assets.at("enemyTexture").get<std::string>();
        out.particleModel = assets.at("particleModel").get<std::string>(); out.particleTexture = assets.at("particleTexture").get<std::string>();
        out.mutant = assets.at("mutant").get<std::string>();
        out.mutantAnimations = Data::ReadStrings(assets.at("mutantAnimations"));
        out.mutantTextures = Data::ReadStrings(assets.at("mutantTextures"));
        out.materialCount = assets.at("materialCount").get<int>();
        const auto& particles = Data::Require(json, "particles", file);
        out.cpuParticles = particles.at("cpuMax").get<uint32_t>(); out.gpuParticles = particles.at("gpuMax").get<uint32_t>();
        for (const auto& e : particles.at("emitters")) {
            Engine::GPUParticleEmitterData emitter = {};
            emitter.emitPos = f3(e.at("position")); emitter.emitRate = e.at("rate").get<float>();
            emitter.emitVel = f3(e.at("velocity")); emitter.emitLife = e.at("life").get<float>();
            emitter.emitterShape = e.at("shape").get<uint32_t>(); emitter.emitterExtents = f3(e.at("extents"));
            emitter.colorStart = f4(e.at("colorStart")); emitter.colorEnd = f4(e.at("colorEnd"));
            emitter.fieldType = e.at("field").get<uint32_t>(); emitter.fieldPos = emitter.emitPos;
            emitter.fieldParams = f4(e.at("fieldParams")); emitter.particleType = e.at("type").get<uint32_t>();
            out.emitters.push_back(emitter);
        }
        if (out.emitters.size() < 2) throw std::runtime_error("assignment.json: particles.emitters needs the tornado and the sword emitter");
        const auto& start = Data::Require(json, "start", file);
        out.playerHp = start.at("playerHp").get<int>(); out.enemyHp = start.at("enemyHp").get<int>();
        auto v3 = [&](const nlohmann::json& v) { auto a = Data::ReadArray<float, 3>(v, file); return Engine::Vector3{a[0], a[1], a[2]}; };
        out.enemyPosition = v3(start.at("enemyPosition")); out.enemyScale = v3(start.at("enemyScale"));
        out.cameraDistance = start.at("cameraDistance").get<float>(); out.cameraPitch = start.at("cameraPitch").get<float>();
        out.animation = start.at("animation").get<std::string>();
        const auto& combat = Data::Require(json, "combat", file);
        out.noticeRange = combat.at("noticeRange").get<float>(); out.loseRange = combat.at("loseRange").get<float>();
        out.attackDamage = combat.at("attackDamage").get<int>(); out.attackRange = combat.at("attackRange").get<float>();
        out.attackFacing = combat.at("attackFacing").get<float>();
        out.enemyAttackRange = combat.at("enemyAttackRange").get<float>(); out.enemyHitRange = combat.at("enemyHitRange").get<float>();
        out.enemyWindup = combat.at("enemyWindup").get<float>(); out.enemyCooldown = combat.at("enemyCooldown").get<float>();
        out.enemyDamage = combat.at("enemyDamage").get<int>(); out.enemyGuardedDamage = combat.at("enemyGuardedDamage").get<int>();
        return out;
    }();
    return data;
}
} // namespace

AssignmentScene::~AssignmentScene() {
    // GPU リソースの後片付けはレンダラーが行う
}

// XZ 平面上の距離 (高さを無視する)
float AssignmentScene::DistanceXZ(const Engine::Vector3& a, const Engine::Vector3& b) const {
    // X と Z の差
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    // 高さを除いた距離
    return std::sqrt(dx * dx + dz * dz);
}

// ポストエフェクトを予約の列の最後に追加する
void AssignmentScene::QueuePostEffect(const std::string& effect, float duration, const std::string& reason) {
    // 効果の名前・残り時間・長さ・理由を入れる
    PostEffectEvent ev;
    ev.effect = effect;
    ev.timer = duration;
    ev.duration = duration;
    ev.reason = reason;
    // 列の最後に並べる
    postEffectQueue_.push_back(ev);
}

// ポストエフェクトを選び、強さ (フェードイン・アウト) を計算してレンダラーへ渡す
void AssignmentScene::UpdatePostEffects(float dt) {
    // 0〜1 に収める / 滑らかな 0〜1 カーブ (smoothstep)
    auto saturateFloat = [](float v) {
        return (std::max)(0.0f, (std::min)(1.0f, v));
    };
    auto smooth01 = [&](float v) {
        v = saturateFloat(v);
        return v * v * (3.0f - 2.0f * v);
    };

    // このフレームの効果の強さ (0〜1)
    float effectStrength = 0.0f;

    // 予約がある場合: 先頭の予約を表示し、始まりと終わりをフェードさせる
    if (!postEffectQueue_.empty()) {
        // 先頭の予約を今の効果にする
        PostEffectEvent& ev = postEffectQueue_.front();
        currentEffect_ = ev.effect;
        currentEffectReason_ = ev.reason;

        // 効果ごとのフェード時間 (Dissolve は長め、Grayscale は中くらい、その他は短め)
        const float elapsed = (std::max)(0.0f, ev.duration - ev.timer);
        const float fadeTime = ev.effect == "Dissolve"
            ? (std::min)(1.35f, ev.duration * 0.45f)
            : ev.effect == "Grayscale"
            ? (std::min)(0.60f, ev.duration * 0.35f)
            : (std::min)(0.30f, ev.duration * 0.35f);
        // Default なら強さ 0
        if (currentEffect_ == "Default") {
            effectStrength = 0.0f;
        // 始まりはフェードイン、終わりはフェードアウト (小さい方を使う)
        } else if (fadeTime > 0.001f) {
            const float fadeIn = smooth01(elapsed / fadeTime);
            const float fadeOut = smooth01(ev.timer / fadeTime);
            effectStrength = (std::min)(fadeIn, fadeOut);
        // フェード時間がほぼ 0 なら最初から最大
        } else {
            effectStrength = 1.0f;
        }

        // 残り時間を減らし、終わったら次の予約へ (無ければ通常に戻る)
        ev.timer -= dt;
        if (ev.timer <= 0.0f) {
            postEffectQueue_.erase(postEffectQueue_.begin());
            if (!postEffectQueue_.empty()) {
                currentEffect_ = postEffectQueue_.front().effect;
                currentEffectReason_ = postEffectQueue_.front().reason;
            } else {
                currentEffect_ = "Default";
                currentEffectReason_ = "Normal gameplay";
            }
        }
    // 予約が無い場合: 今の状態から効果を選ぶ (上ほど優先)
    } else {
        // 敵との距離と、選ぶ効果 (最初は通常)
        const float distToEnemy = DistanceXZ(transform_.translate, enemyTransform_.translate);
        std::string desiredEffect = "Default";
        std::string desiredReason = "Normal gameplay";
        // 敵を倒した: 溶けて消える
        if (enemyHp_ <= 0) {
            desiredEffect = "Dissolve";
            desiredReason = "Enemy cube is dissolving after defeat";
        // HP が少ない: 画面の周りを暗くする
        } else if (playerHp_ <= 30) {
            desiredEffect = "Vignetting";
            desiredReason = "Low HP danger state";
        // 敵の攻撃の予兆: 深度で輪郭線
        } else if (enemyAttackWindup_ > 0.0f) {
            desiredEffect = "DepthBasedOutline";
            desiredReason = "Enemy attack telegraph";
        // ガード中: ぼかし
        } else if (isGuarding_) {
            desiredEffect = "BoxFilter";
            desiredReason = "Guard focus state";
        // 攻撃中: 放射状のぶれ
        } else if (isAttacking_) {
            desiredEffect = "RadialBlur";
            desiredReason = "Player attack momentum";
        // 敵の近く: 明るさで輪郭線
        } else if (enemyAware_ && distToEnemy < 7.0f) {
            desiredEffect = "OutlinePost";
            desiredReason = "Enemy lock-on range";
        }

        // 通常へ戻るときは徐々に弱め、それ以外は新しい効果に切り替える
        const bool fadingToDefault = desiredEffect == "Default" && currentEffect_ != "Default";
        // 新しい効果に切り替え、強さを 0 から上げ直す
        if (!fadingToDefault && currentEffect_ != desiredEffect) {
            currentEffect_ = desiredEffect;
            currentEffectReason_ = desiredReason;
            previousEffect_ = desiredEffect;
            postEffectStrength_ = 0.0f;
        // 同じ効果なら理由だけ更新する
        } else if (!fadingToDefault) {
            currentEffectReason_ = desiredReason;
        } else {
            currentEffectReason_ = "Returning to normal gameplay";
        }

        // 強さを目標値へ毎フレーム少しずつ近づける
        const float targetStrength = desiredEffect == "Default" ? 0.0f : 1.0f;
        // 毎秒 5 の割合で近づける
        const float follow = saturateFloat(dt * 5.0f);
        postEffectStrength_ += (targetStrength - postEffectStrength_) * follow;
        // 通常に戻る途中で十分弱まったら、完全に通常へ戻す
        if (fadingToDefault && postEffectStrength_ <= 0.02f) {
            currentEffect_ = "Default";
            currentEffectReason_ = desiredReason;
            previousEffect_ = "Default";
            postEffectStrength_ = 0.0f;
        }
        // この強さを使う
        effectStrength = postEffectStrength_;
    }

    // 描画の窓口が無ければ何もしない
    if (!renderer_) return;

    // 選んだ効果と、効果ごとのパラメータをレンダラーへ渡す
    renderer_->SetPostEffect(currentEffect_);

    // 時間だけ引き継いで、パラメータを初期値に戻す
    auto params = renderer_->GetPostProcessParams();
    const float t = params.time + dt;
    params = Engine::Renderer::PostProcessParams{};
    params.time = t;
    // 効果の強さ
    params.san = effectStrength;

    // 放射状のぶれ: 歪みと周辺減光
    if (currentEffect_ == "RadialBlur") {
        params.distortion = 0.9f * effectStrength;
        params.vignette = 0.45f * effectStrength;
    // 周辺減光を強く
    } else if (currentEffect_ == "Vignetting") {
        params.vignette = 2.5f * effectStrength;
    // ノイズと色ずれ
    } else if (currentEffect_ == "Random") {
        params.noiseStrength = 0.18f * effectStrength;
        params.chromaShift = 0.003f * effectStrength;
    // 溶けるときは周辺も暗く
    } else if (currentEffect_ == "Dissolve") {
        params.vignette = 0.8f * effectStrength;
    // ぼかしのときは少し歪ませる
    } else if (currentEffect_ == "GaussianFilter") {
        params.distortion = 0.2f * effectStrength;
    // 輪郭線のときは少し色をずらす
    } else if (currentEffect_ == "DepthBasedOutline" || currentEffect_ == "OutlinePost") {
        params.chromaShift = 0.002f * effectStrength;
    }

    renderer_->SetPostProcessParams(params);
}

// 練習用の敵 (赤い箱) の行動とダメージ処理
void AssignmentScene::UpdateEnemyCombat(float dt, bool attackPressed) {
    // プレイヤーと敵の位置、その距離
    const Engine::Vector3 playerPos = transform_.translate;
    const Engine::Vector3 enemyPos = enemyTransform_.translate;
    const float distToEnemy = DistanceXZ(playerPos, enemyPos);

    // 近づくと敵が気付く (気付いた瞬間に白黒の演出)。離れると見失う
    const auto& config = Config();
    if (distToEnemy < config.noticeRange && enemyHp_ > 0) {
        if (!enemyAware_) {
            enemyAware_ = true;
            QueuePostEffect("Grayscale", 3.2f, "Enemy cube spotted the player");
        }
    } else if (distToEnemy > config.loseRange) {
        enemyAware_ = false;
    }

    // 各タイマーを進める
    if (enemyAttackCooldown_ > 0.0f) enemyAttackCooldown_ -= dt;
    if (enemyHitFlash_ > 0.0f) enemyHitFlash_ -= dt;
    if (playerHitFlash_ > 0.0f) playerHitFlash_ -= dt;

    // 攻撃: 正面の近くにいる敵へダメージ。倒すと Dissolve の演出
    if (attackPressed) {
        QueuePostEffect("RadialBlur", 0.65f, "Player attack lunge");

        // プレイヤーの向きと、敵への向きの内積で正面かを判定する
        const float forwardX = std::sin(transform_.rotate.y);
        const float forwardZ = std::cos(transform_.rotate.y);
        const float toEnemyX = enemyPos.x - playerPos.x;
        const float toEnemyZ = enemyPos.z - playerPos.z;
        const float toLen = std::sqrt(toEnemyX * toEnemyX + toEnemyZ * toEnemyZ);
        const float facing = toLen > 0.001f ? (forwardX * toEnemyX + forwardZ * toEnemyZ) / toLen : 1.0f;

        // 生きていて、近く (5.5 以内) で正面なら 20 ダメージ
        if (enemyHp_ > 0 && distToEnemy < config.attackRange && facing > config.attackFacing) {
            enemyHp_ = (std::max)(0, enemyHp_ - config.attackDamage);
            enemyHitFlash_ = 0.35f;
            QueuePostEffect("GaussianFilter", 0.7f, "Hit stop on successful player attack");

            // 倒したら演出を出し、攻撃の状態を止める
            if (enemyHp_ <= 0) {
                QueuePostEffect("Dissolve", 4.0f, "Enemy cube defeated");
                enemyAttackWindup_ = 0.0f;
                enemyAttackArmed_ = false;
                enemyAware_ = false;
                enemyAttackCooldown_ = 2.0f;
            }
        }
    }

    // 倒した後: しばらく経つと元の位置に復活する
    if (enemyHp_ <= 0) {
        enemyAttackCooldown_ -= dt;
        // 倒れてから一定時間で HP を戻し、元の位置に置き直す
        if (enemyAttackCooldown_ <= -1.5f) {
            enemyHp_ = config.enemyHp;
            enemyTransform_.translate = config.enemyPosition;
            QueuePostEffect("Grayscale", 2.6f, "Enemy cube respawned and reacquired the player");
            enemyAware_ = false;
            enemyAttackCooldown_ = 1.2f;
        }
        return;
    }

    // 近くにいれば攻撃の溜めを始める
    if (distToEnemy < config.enemyAttackRange && enemyAttackCooldown_ <= 0.0f && enemyAttackWindup_ <= 0.0f) {
        enemyAttackWindup_ = config.enemyWindup;
        enemyAttackArmed_ = true;
    }

    // 溜めが終わったら攻撃を出す。ガード中はダメージを減らす
    if (enemyAttackWindup_ > 0.0f) {
        enemyAttackWindup_ -= dt;
        // 溜めが終わった瞬間に、待ち時間を設定する
        if (enemyAttackWindup_ <= 0.0f && enemyAttackArmed_) {
            enemyAttackArmed_ = false;
            enemyAttackCooldown_ = config.enemyCooldown;

            // 届く距離なら、ガード中は 4、それ以外は 15 ダメージ
            if (distToEnemy < config.enemyHitRange && playerHp_ > 0) {
                const int damage = isGuarding_ ? config.enemyGuardedDamage : config.enemyDamage;
                playerHp_ = (std::max)(0, playerHp_ - damage);
                playerHitFlash_ = 0.45f;
                // ガードしたか被弾したかで演出を変える
                if (isGuarding_) {
                    QueuePostEffect("BoxFilter", 0.75f, "Guard blocked most of the enemy attack");
                } else {
                    QueuePostEffect("Random", 0.75f, "Player took damage glitch");
                }

                // プレイヤーの HP が 0 になったら回復して原点に戻す
                if (playerHp_ <= 0) {
                    QueuePostEffect("Vignetting", 1.0f, "Player HP reached zero");
                    playerHp_ = config.playerHp;
                    transform_.translate = {0.0f, 0.0f, 0.0f};
                }
            } else {
                // 外れたときの演出
                QueuePostEffect("OutlinePost", 0.7f, "Enemy cube attack missed; lock-on remains");
            }
        }
    }
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
