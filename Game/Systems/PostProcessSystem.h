#pragma once
#include "ISystem.h"
#include "../../Engine/Renderer.h"
#include "../ObjectTypes.h"
#include "PlayerActionSystem.h" // PlayerActionState 等を使用
#include <algorithm>
#include <cmath>

namespace Game {

class PostProcessSystem : public ISystem {
public:
    void Update(entt::registry& registry, GameContext& ctx) override {
        auto* renderer = ctx.renderer;
        if (!renderer) return;

        // --- 1. ベースパラメータの設定 ---
        Engine::Renderer::PostProcessParams target;
        target.noiseStrength = 0.002f;
        target.vignette = 0.08f;
        target.chromaShift = 0.0f;
        target.distortion = 0.0f;
        target.san = 0.0f;
        target.scanline = 0.0f;

        // 状態フラグ
        bool isLowHealth = false;
        bool isDead = false;
        bool hasLockedTarget = false;

        // プレイヤーの状態を取得
        auto playerView = registry.view<TagComponent, HealthComponent, PlayerInputComponent>();
        entt::entity playerEntity = entt::null;
        for (auto entity : playerView) {
            if (playerView.get<TagComponent>(entity).tag == TagType::Player) {
                playerEntity = entity;
                break;
            }
        }

        if (playerEntity != entt::null) {
            auto& hc = playerView.get<HealthComponent>(playerEntity);
            auto& pi = playerView.get<PlayerInputComponent>(playerEntity);

            isDead = hc.isDead;
            float hpRate = hc.hp / (hc.maxHp > 0 ? hc.maxHp : 1.0f);
            isLowHealth = (hpRate < 0.3f);

            // 被弾によるパルス
            if (hc.hitFlashTimer > 0.0f) {
                hitPulseTimer_ = 0.4f;
            }

            hasLockedTarget = (pi.lockedEnemy != entt::null && registry.valid(pi.lockedEnemy));

            // アクション（強攻撃など）の検出
            if (registry.all_of<PlayerActionComponent>(playerEntity)) {
                auto& pa = registry.get<PlayerActionComponent>(playerEntity);
                
                // 1. 強攻撃の開始時にパルスを発生させる
                bool isStrongAttack = (pa.state == PlayerActionState::ChargeAttack1 || 
                                       pa.state == PlayerActionState::ChargeAttack2 || 
                                       pa.state == PlayerActionState::ChargeAttack3 ||
                                       pa.state == PlayerActionState::Attack3); // 3段目も強攻撃扱い
                
                if (isStrongAttack && pa.stateTimer < 0.1f) {
                    float power = (pa.state == PlayerActionState::ChargeAttack3) ? 1.0f : 0.5f;
                    actionPulseTimer_ = std::max(actionPulseTimer_, power); 
                }

                // 2. ヒットストップ中（攻撃が当たった瞬間）に強烈なブラーをかける
                if (pa.hitStopTimer > 0.0f) {
                    hitStopPulse_ = std::lerp(hitStopPulse_, 1.5f, std::clamp(ctx.dt * 20.0f, 0.0f, 1.0f));
                } else {
                    hitStopPulse_ = std::lerp(hitStopPulse_, 0.0f, std::clamp(ctx.dt * 15.0f, 0.0f, 1.0f));
                }
            }

            // ダッシュ（速度）ブラー
            if (registry.all_of<TransformComponent>(playerEntity)) {
                auto& tc = registry.get<TransformComponent>(playerEntity);
                static DirectX::XMFLOAT3 lastPos = tc.translate;
                float dx = tc.translate.x - lastPos.x;
                float dy = tc.translate.y - lastPos.y;
                float dz = tc.translate.z - lastPos.z;
                float speed = std::sqrt(dx*dx + dy*dy + dz*dz) / (ctx.dt > 0 ? ctx.dt : 0.016f);
                lastPos = tc.translate;

                float speedThreshold = 8.0f;
                float speedRate = std::clamp((speed - speedThreshold) / 12.0f, 0.0f, 1.2f);
                speedEffectIntensity_ = std::lerp(speedEffectIntensity_, speedRate, std::clamp(ctx.dt * 10.0f, 0.0f, 1.0f));
            }
        }

        // --- 2. パラメータ合成 ---
        if (isDead) {
            target.scanline = 1.0f; 
            target.vignette = 0.5f;
        } 
        else {
            if (hasLockedTarget) {
                target.chromaShift = 0.4f; 
            }

            // ラジアルブラーの合成 (速度 + 被弾 + アクションパルス + ヒットストップ)
            float radialEffect = speedEffectIntensity_;
            
            // アクションパルス (強攻撃)
            if (actionPulseTimer_ > 0.0f) {
                radialEffect = std::max(radialEffect, actionPulseTimer_ * 1.2f);
                actionPulseTimer_ -= ctx.dt * 2.0f; // 素早く減衰
            }

            // 被弾パルス
            if (hitPulseTimer_ > 0.0f) {
                radialEffect = std::max(radialEffect, hitPulseTimer_ * 2.0f);
                hitPulseTimer_ -= ctx.dt;
            }

            // ヒットストップ（ヒットの瞬間）
            radialEffect = std::max(radialEffect, hitStopPulse_);

            target.distortion = radialEffect;

            if (isLowHealth) {
                target.scanline = 0.25f;
                target.vignette = 0.25f;
            }
        }

        // パラメータの補完
        auto Lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        float lerpSpeed = std::clamp(ctx.dt * 10.0f, 0.0f, 1.0f);

        currentParams_.noiseStrength = Lerp(currentParams_.noiseStrength, target.noiseStrength, lerpSpeed);
        currentParams_.distortion = Lerp(currentParams_.distortion, target.distortion, lerpSpeed);
        currentParams_.chromaShift = Lerp(currentParams_.chromaShift, target.chromaShift, lerpSpeed);
        currentParams_.vignette = Lerp(currentParams_.vignette, target.vignette, lerpSpeed);
        currentParams_.scanline = Lerp(currentParams_.scanline, target.scanline, lerpSpeed);

        currentParams_.time = renderer->GetPostProcessParams().time;
        renderer->SetPostProcessParams(currentParams_);
        renderer->SetPostEffect("Rich"); 
    }

    void Draw(entt::registry& /*registry*/, GameContext& /*ctx*/) override {}

    void Reset(entt::registry& /*registry*/) override {
        currentParams_ = Engine::Renderer::PostProcessParams();
        currentParams_.vignette = 0.08f;
        hitPulseTimer_ = 0.0f;
        actionPulseTimer_ = 0.0f;
        hitStopPulse_ = 0.0f;
        speedEffectIntensity_ = 0.0f;
    }

private:
    Engine::Renderer::PostProcessParams currentParams_;
    float hitPulseTimer_ = 0.0f;
    float actionPulseTimer_ = 0.0f;
    float hitStopPulse_ = 0.0f;
    float speedEffectIntensity_ = 0.0f;
};

} // namespace Game
