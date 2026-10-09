#pragma once
#include "ObjectTypes.h"
#include <algorithm>
#include <cmath>

namespace Game {

class ICanHitEffect {
public:
    virtual ~ICanHitEffect() = default;
    // CombatSystem validates the enemy and resolves body parts before dispatch.
    virtual void Apply(entt::registry& registry, entt::entity target,
                       const DirectX::XMFLOAT3& source, const CanAttackEffectComponent& effect) const = 0;
};

class MagnetHitEffect final : public ICanHitEffect {
public:
    void Apply(entt::registry& registry, entt::entity target,
               const DirectX::XMFLOAT3& source, const CanAttackEffectComponent& effect) const override {
        auto& transform = registry.get<TransformComponent>(target);
        const float dx = transform.translate.x - source.x;
        const float dz = transform.translate.z - source.z;
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length > 0.001f) {
            const float strength = registry.all_of<BossActionComponent>(target) ? effect.strength * 0.35f : effect.strength;
            transform.translate.x += (dx / length) * strength;
            transform.translate.z += (dz / length) * strength;
        }
    }
};

class IceHitEffect final : public ICanHitEffect {
public:
    void Apply(entt::registry& registry, entt::entity target,
               const DirectX::XMFLOAT3&, const CanAttackEffectComponent& effect) const override {
        auto& status = registry.get_or_emplace<CanStatusComponent>(target);
        const float duration = registry.all_of<BossActionComponent>(target) ? effect.duration * 0.55f : effect.duration;
        status.freezeTimer = (std::max)(status.freezeTimer, duration);
    }
};

class AcidHitEffect final : public ICanHitEffect {
public:
    void Apply(entt::registry& registry, entt::entity target,
               const DirectX::XMFLOAT3&, const CanAttackEffectComponent& effect) const override {
        auto& status = registry.get_or_emplace<CanStatusComponent>(target);
        status.acidTimer = (std::max)(status.acidTimer, effect.duration);
        status.acidTickTimer = 0.0f;
    }
};

class BubbleHitEffect final : public ICanHitEffect {
public:
    void Apply(entt::registry& registry, entt::entity target,
               const DirectX::XMFLOAT3&, const CanAttackEffectComponent& effect) const override {
        auto& status = registry.get_or_emplace<CanStatusComponent>(target);
        const float duration = registry.all_of<BossActionComponent>(target) ? effect.duration * 0.55f : effect.duration;
        status.bubbleTimer = (std::max)(status.bubbleTimer, duration);
        if (!status.bubblePositionSaved) {
            status.bubbleBaseY = registry.get<TransformComponent>(target).translate.y;
            status.bubblePositionSaved = true;
        }
    }
};

inline const ICanHitEffect* FindCanHitEffect(CanType type) {
    static const MagnetHitEffect magnet;
    static const IceHitEffect ice;
    static const AcidHitEffect acid;
    static const BubbleHitEffect bubble;
    switch (type) {
    case CanType::Magnet: return &magnet;
    case CanType::Ice: return &ice;
    case CanType::Acid: return &acid;
    case CanType::Bubble: return &bubble;
    default: return nullptr;
    }
}

} // namespace Game
