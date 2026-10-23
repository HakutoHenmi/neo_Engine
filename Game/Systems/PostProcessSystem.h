// PostProcessSystem.h
// 旧アクションゲームの状況 (死亡・被弾・水・低体力・速さ・ロックオン) に応じて
// 画面全体のポストエフェクトを選び、パラメータをなめらかに切り替えるシステム。
#pragma once

#include "ISystem.h"
#include "PlayerActionSystem.h"
#include "../../Engine/Renderer.h"
#include "../ObjectTypes.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace Game {

// ポストエフェクトのシステム
class PostProcessSystem : public ISystem {
public:
    // プレイヤーの状態から使うエフェクトと強さを決める
    void Update(entt::registry& registry, GameContext& ctx) override {
        // レンダラーが無ければ何もしない
        Engine::Renderer* renderer = ctx.renderer;
        if (!renderer) return;

        // 目標のパラメータ (時刻だけは今の値を引き継ぐ)
        Engine::Renderer::PostProcessParams target{};
        target.time = renderer->GetPostProcessParams().time;

        // 使いたいエフェクトの名前と強さ
        std::string desiredEffect = "Default";
        float desiredStrength = 0.0f;

        // プレイヤーの状態を表すフラグ
        bool isDead = false;
        bool isLowHealth = false;
        bool isHit = false;
        bool isLiquidated = false;
        bool isDodging = false;
        bool isStrongAttack = false;
        bool hasLockedTarget = false;
        bool waterCanActive = false;

        // プレイヤーを探す
        entt::entity playerEntity = entt::null;
        auto playerView = registry.view<TagComponent, TransformComponent>();
        playerView.each([&](entt::entity entity, const TagComponent& tag, const TransformComponent&) {
            if (playerEntity == entt::null && tag.tag == TagType::Player) {
                playerEntity = entity;
            }
        });

        if (playerEntity != entt::null && registry.valid(playerEntity)) {
            // 体力: 死亡・低体力 (30% 未満)・被弾の光
            if (auto* hc = registry.try_get<HealthComponent>(playerEntity)) {
                // 最大体力に対する今の体力の割合
                const float maxHp = hc->MaxHp() > 0.0f ? hc->MaxHp() : 1.0f;
                const float hpRate = std::clamp(hc->Hp() / maxHp, 0.0f, 1.0f);
                isDead = hc->IsDead() || hc->Hp() <= 0.0f;
                isLowHealth = hpRate < 0.30f;
                isHit = hc->hitFlashTimer > 0.0f;
            }

            // ロックオン中か・水の缶を選んでいるか
            if (auto* pi = registry.try_get<PlayerInputComponent>(playerEntity)) {
                hasLockedTarget = pi->lockedEnemy != entt::null && registry.valid(pi->lockedEnemy);
                waterCanActive = pi->selectedCan == CanType::Water;
            }

            // 行動: 液状化・回避・強攻撃。ヒットストップの間は動きの演出を強める
            if (auto* pa = registry.try_get<PlayerActionComponent>(playerEntity)) {
                isLiquidated = pa->state == PlayerActionState::Liquefy;
                isDodging = pa->state == PlayerActionState::Dodge;
                isStrongAttack = pa->state == PlayerActionState::SlimeHammer ||
                    pa->state == PlayerActionState::SlimeSpike ||
                    pa->state == PlayerActionState::SpikeExplosion;
                waterCanActive = waterCanActive || pa->currentCan == CanType::Water;

                if (pa->hitStopTimer > 0.0f) {
                    actionPulseTimer_ = std::max(actionPulseTimer_, 0.8f);
                }
            }

            // 前フレームとの差から移動の速さを求め、速さの演出の強さへ反映する
            if (auto* tc = registry.try_get<TransformComponent>(playerEntity)) {
                // 最初のフレームは今の位置を前回の位置にする
                if (!hasLastPlayerPos_) {
                    lastPlayerPos_ = tc->translate;
                    hasLastPlayerPos_ = true;
                }

                // 1 秒あたりの移動の速さ
                const float dx = tc->translate.x - lastPlayerPos_.x;
                const float dy = tc->translate.y - lastPlayerPos_.y;
                const float dz = tc->translate.z - lastPlayerPos_.z;
                const float dt = ctx.dt > 0.0001f ? ctx.dt : 1.0f / 60.0f;
                const float speed = std::sqrt(dx * dx + dy * dy + dz * dz) / dt;
                lastPlayerPos_ = tc->translate;

                // 速さ 8〜22 を 0〜1 の強さにし、なめらかに追従させる
                const float speedRate = std::clamp((speed - 8.0f) / 14.0f, 0.0f, 1.0f);
                speedEffectIntensity_ = Lerp(speedEffectIntensity_, speedRate, std::clamp(ctx.dt * 8.0f, 0.0f, 1.0f));
            }
        }

        // 川や水の発生源があるか
        bool hasRiver = false;
        auto riverView = registry.view<RiverComponent>();
        riverView.each([&](entt::entity, const RiverComponent& river) {
            hasRiver = hasRiver || (river.enabled && river.points.size() >= 2);
        });

        // 水の流体の発生源があるか
        bool hasWaterEmitter = false;
        auto fluidEmitterView = registry.view<FluidEmitterComponent>();
        fluidEmitterView.each([&](entt::entity, const FluidEmitterComponent& emitter) {
            hasWaterEmitter = hasWaterEmitter || (emitter.enabled && emitter.fluidType >= 0.5f && emitter.emitCountPerFrame > 0);
        });

        // 水の演出の強さ (状況が水に近いほど強い)
        float waterStrength = 0.0f;
        if (hasRiver) waterStrength = std::max(waterStrength, 0.28f);
        if (hasWaterEmitter) waterStrength = std::max(waterStrength, 0.45f);
        if (waterCanActive) waterStrength = std::max(waterStrength, 0.58f);
        if (isLiquidated) waterStrength = std::max(waterStrength, 0.78f);

        // 優先順位: 死亡 > 被弾 > 水 > 低体力 > 速さ・攻撃 > ロックオン
        if (isDead) {
            // 死亡: 強い周辺減光
            desiredEffect = "Vignetting";
            desiredStrength = 1.0f;
            target.vignette = 3.0f;
        } else if (isHit) {
            // 被弾: ノイズと色ずれ
            desiredEffect = "Random";
            desiredStrength = 0.35f;
            target.noiseStrength = 0.14f;
            target.chromaShift = 0.003f;
        } else if (waterStrength > 0.0f) {
            // 水: 流れる水の歪み (強さに応じて歪みと色ずれを増やす)
            desiredEffect = "FlowingWaterPost";
            desiredStrength = waterStrength;
            target.distortion = 0.18f + waterStrength * 0.55f;
            target.chromaShift = 0.002f + waterStrength * 0.004f;
            target.noiseStrength = 0.02f + waterStrength * 0.03f;
            target.vignette = 0.12f * waterStrength;
        } else if (isLowHealth) {
            // 低体力: 周辺減光
            desiredEffect = "Vignetting";
            desiredStrength = 0.65f;
            target.vignette = 2.2f;
        } else if (isDodging || isStrongAttack || speedEffectIntensity_ > 0.3f || actionPulseTimer_ > 0.0f) {
            // 回避・強攻撃・速い移動: 放射状のぼかし
            desiredEffect = "RadialBlur";
            desiredStrength = std::clamp(std::max(speedEffectIntensity_, actionPulseTimer_), 0.35f, 0.85f);
            target.distortion = desiredStrength * 0.7f;
            target.vignette = desiredStrength * 0.25f;
        } else if (hasLockedTarget) {
            // ロックオン: 輪郭線
            desiredEffect = "OutlinePost";
            desiredStrength = 0.45f;
            target.chromaShift = 0.0015f;
        }

        // 動きの演出の時間を減らす
        if (actionPulseTimer_ > 0.0f) {
            actionPulseTimer_ = std::max(0.0f, actionPulseTimer_ - ctx.dt * 1.8f);
        }

        // 選んだエフェクトへなめらかに切り替える
        UpdateActiveEffect(renderer, desiredEffect, desiredStrength, target, ctx.dt);
    }

    // 描画では何もしない
    void Draw(entt::registry&, GameContext&) override {}

    // PLAY 開始時: エフェクトを既定に戻す
    void Reset(entt::registry&) override {
        currentParams_ = Engine::Renderer::PostProcessParams{};
        activeEffect_ = "Default";
        pendingEffect_ = "Default";
        actionPulseTimer_ = 0.0f;
        speedEffectIntensity_ = 0.0f;
        hasLastPlayerPos_ = false;
    }

private:
    // 線形補間
    static float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    // エフェクトを切り替え、各パラメータを目標へなめらかに近づけてレンダラーへ渡す
    void UpdateActiveEffect(
        Engine::Renderer* renderer,
        const std::string& desiredEffect,
        float desiredStrength,
        Engine::Renderer::PostProcessParams target,
        float dt) {
        // 補間の割合 (1 秒で約 6 回分近づく)
        const float t = std::clamp(dt * 6.0f, 0.0f, 1.0f);
        pendingEffect_ = desiredEffect;

        // 別のエフェクトへ切り替えるときは強さを 0 から始める
        if (pendingEffect_ != activeEffect_ && pendingEffect_ != "Default") {
            activeEffect_ = pendingEffect_;
            currentParams_.san = 0.0f;
        }

        // 強さの目標 (既定へ戻すときは 0)
        const float targetSan = pendingEffect_ == "Default" ? 0.0f : desiredStrength;
        target.san = targetSan;

        // 各パラメータを目標へ近づける
        currentParams_.noiseStrength = Lerp(currentParams_.noiseStrength, target.noiseStrength, t);
        currentParams_.distortion = Lerp(currentParams_.distortion, target.distortion, t);
        currentParams_.chromaShift = Lerp(currentParams_.chromaShift, target.chromaShift, t);
        currentParams_.vignette = Lerp(currentParams_.vignette, target.vignette, t);
        currentParams_.scanline = Lerp(currentParams_.scanline, target.scanline, t);
        currentParams_.san = Lerp(currentParams_.san, target.san, t);
        currentParams_.time = target.time;

        // 既定へ戻すときは、十分に弱まってから切り替える
        if (pendingEffect_ == "Default" && currentParams_.san <= 0.025f) {
            activeEffect_ = "Default";
            currentParams_ = Engine::Renderer::PostProcessParams{};
            currentParams_.time = target.time;
        }

        // レンダラーへパラメータとエフェクトを渡す
        renderer->SetPostProcessParams(currentParams_);
        renderer->SetPostEffect(activeEffect_);
    }

    // 現在のパラメータ・使用中のエフェクト・前回のプレイヤー位置・演出の時間
    Engine::Renderer::PostProcessParams currentParams_{};
    std::string activeEffect_ = "Default";
    std::string pendingEffect_ = "Default";
    DirectX::XMFLOAT3 lastPlayerPos_{0.0f, 0.0f, 0.0f};
    bool hasLastPlayerPos_ = false;
    float actionPulseTimer_ = 0.0f;
    float speedEffectIntensity_ = 0.0f;
};

} // namespace Game
