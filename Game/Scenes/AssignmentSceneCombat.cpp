// AssignmentSceneCombat.cpp
// 評価課題デモの戦闘とポストエフェクト: 練習用の敵 (赤い箱) の行動とダメージ、
// 状況に合わせたポストエフェクトの選択・予約・フェード。
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

} // namespace Game
