#pragma once

#include "ISystem.h"
#include "PlayerActionSystem.h"
#include "../../Engine/Renderer.h"
#include "../ObjectTypes.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace Game {

class PostProcessSystem : public ISystem {
public:
    void Update(entt::registry& registry, GameContext& ctx) override {
        Engine::Renderer* renderer = ctx.renderer;
        if (!renderer) return;

        Engine::Renderer::PostProcessParams target{};
        target.time = renderer->GetPostProcessParams().time;

        std::string desiredEffect = "Default";
        float desiredStrength = 0.0f;

        bool isDead = false;
        bool isLowHealth = false;
        bool isHit = false;
        bool isLiquidated = false;
        bool isDodging = false;
        bool isStrongAttack = false;
        bool hasLockedTarget = false;
        bool waterCanActive = false;

        entt::entity playerEntity = entt::null;
        auto playerView = registry.view<TagComponent, TransformComponent>();
        playerView.each([&](entt::entity entity, const TagComponent& tag, const TransformComponent&) {
            if (playerEntity == entt::null && tag.tag == TagType::Player) {
                playerEntity = entity;
            }
        });

        if (playerEntity != entt::null && registry.valid(playerEntity)) {
            if (auto* hc = registry.try_get<HealthComponent>(playerEntity)) {
                const float maxHp = hc->maxHp > 0.0f ? hc->maxHp : 1.0f;
                const float hpRate = std::clamp(hc->hp / maxHp, 0.0f, 1.0f);
                isDead = hc->isDead || hc->hp <= 0.0f;
                isLowHealth = hpRate < 0.30f;
                isHit = hc->hitFlashTimer > 0.0f;
            }

            if (auto* pi = registry.try_get<PlayerInputComponent>(playerEntity)) {
                hasLockedTarget = pi->lockedEnemy != entt::null && registry.valid(pi->lockedEnemy);
                waterCanActive = pi->selectedCan == CanType::Water;
            }

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

            if (auto* tc = registry.try_get<TransformComponent>(playerEntity)) {
                if (!hasLastPlayerPos_) {
                    lastPlayerPos_ = tc->translate;
                    hasLastPlayerPos_ = true;
                }

                const float dx = tc->translate.x - lastPlayerPos_.x;
                const float dy = tc->translate.y - lastPlayerPos_.y;
                const float dz = tc->translate.z - lastPlayerPos_.z;
                const float dt = ctx.dt > 0.0001f ? ctx.dt : 1.0f / 60.0f;
                const float speed = std::sqrt(dx * dx + dy * dy + dz * dz) / dt;
                lastPlayerPos_ = tc->translate;

                const float speedRate = std::clamp((speed - 8.0f) / 14.0f, 0.0f, 1.0f);
                speedEffectIntensity_ = Lerp(speedEffectIntensity_, speedRate, std::clamp(ctx.dt * 8.0f, 0.0f, 1.0f));
            }
        }

        bool hasRiver = false;
        auto riverView = registry.view<RiverComponent>();
        riverView.each([&](entt::entity, const RiverComponent& river) {
            hasRiver = hasRiver || (river.enabled && river.points.size() >= 2);
        });

        bool hasWaterEmitter = false;
        auto fluidEmitterView = registry.view<FluidEmitterComponent>();
        fluidEmitterView.each([&](entt::entity, const FluidEmitterComponent& emitter) {
            hasWaterEmitter = hasWaterEmitter || (emitter.enabled && emitter.fluidType >= 0.5f && emitter.emitCountPerFrame > 0);
        });

        float waterStrength = 0.0f;
        if (hasRiver) waterStrength = std::max(waterStrength, 0.28f);
        if (hasWaterEmitter) waterStrength = std::max(waterStrength, 0.45f);
        if (waterCanActive) waterStrength = std::max(waterStrength, 0.58f);
        if (isLiquidated) waterStrength = std::max(waterStrength, 0.78f);

        if (isDead) {
            desiredEffect = "Vignetting";
            desiredStrength = 1.0f;
            target.vignette = 3.0f;
        } else if (isHit) {
            desiredEffect = "Random";
            desiredStrength = 0.35f;
            target.noiseStrength = 0.14f;
            target.chromaShift = 0.003f;
        } else if (waterStrength > 0.0f) {
            desiredEffect = "FlowingWaterPost";
            desiredStrength = waterStrength;
            target.distortion = 0.18f + waterStrength * 0.55f;
            target.chromaShift = 0.002f + waterStrength * 0.004f;
            target.noiseStrength = 0.02f + waterStrength * 0.03f;
            target.vignette = 0.12f * waterStrength;
        } else if (isLowHealth) {
            desiredEffect = "Vignetting";
            desiredStrength = 0.65f;
            target.vignette = 2.2f;
        } else if (isDodging || isStrongAttack || speedEffectIntensity_ > 0.3f || actionPulseTimer_ > 0.0f) {
            desiredEffect = "RadialBlur";
            desiredStrength = std::clamp(std::max(speedEffectIntensity_, actionPulseTimer_), 0.35f, 0.85f);
            target.distortion = desiredStrength * 0.7f;
            target.vignette = desiredStrength * 0.25f;
        } else if (hasLockedTarget) {
            desiredEffect = "OutlinePost";
            desiredStrength = 0.45f;
            target.chromaShift = 0.0015f;
        }

        if (actionPulseTimer_ > 0.0f) {
            actionPulseTimer_ = std::max(0.0f, actionPulseTimer_ - ctx.dt * 1.8f);
        }

        UpdateActiveEffect(renderer, desiredEffect, desiredStrength, target, ctx.dt);
    }

    void Draw(entt::registry&, GameContext&) override {}

    void Reset(entt::registry&) override {
        currentParams_ = Engine::Renderer::PostProcessParams{};
        activeEffect_ = "Default";
        pendingEffect_ = "Default";
        actionPulseTimer_ = 0.0f;
        speedEffectIntensity_ = 0.0f;
        hasLastPlayerPos_ = false;
    }

private:
    static float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    void UpdateActiveEffect(
        Engine::Renderer* renderer,
        const std::string& desiredEffect,
        float desiredStrength,
        Engine::Renderer::PostProcessParams target,
        float dt) {
        const float t = std::clamp(dt * 6.0f, 0.0f, 1.0f);
        pendingEffect_ = desiredEffect;

        if (pendingEffect_ != activeEffect_ && pendingEffect_ != "Default") {
            activeEffect_ = pendingEffect_;
            currentParams_.san = 0.0f;
        }

        const float targetSan = pendingEffect_ == "Default" ? 0.0f : desiredStrength;
        target.san = targetSan;

        currentParams_.noiseStrength = Lerp(currentParams_.noiseStrength, target.noiseStrength, t);
        currentParams_.distortion = Lerp(currentParams_.distortion, target.distortion, t);
        currentParams_.chromaShift = Lerp(currentParams_.chromaShift, target.chromaShift, t);
        currentParams_.vignette = Lerp(currentParams_.vignette, target.vignette, t);
        currentParams_.scanline = Lerp(currentParams_.scanline, target.scanline, t);
        currentParams_.san = Lerp(currentParams_.san, target.san, t);
        currentParams_.time = target.time;

        if (pendingEffect_ == "Default" && currentParams_.san <= 0.025f) {
            activeEffect_ = "Default";
            currentParams_ = Engine::Renderer::PostProcessParams{};
            currentParams_.time = target.time;
        }

        renderer->SetPostProcessParams(currentParams_);
        renderer->SetPostEffect(activeEffect_);
    }

    Engine::Renderer::PostProcessParams currentParams_{};
    std::string activeEffect_ = "Default";
    std::string pendingEffect_ = "Default";
    DirectX::XMFLOAT3 lastPlayerPos_{0.0f, 0.0f, 0.0f};
    bool hasLastPlayerPos_ = false;
    float actionPulseTimer_ = 0.0f;
    float speedEffectIntensity_ = 0.0f;
};

} // namespace Game
