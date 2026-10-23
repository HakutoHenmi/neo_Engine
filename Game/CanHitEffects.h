// CanHitEffects.h
// 旧アクションゲームの缶の攻撃が敵に当たったときの追加効果 (磁力・氷・酸・泡)。
// 缶の種類ごとに効果クラスを用意し、FindCanHitEffect で取り出して適用する。
#pragma once
// コンポーネントと缶の種類の定義
#include "ObjectTypes.h"
#include <algorithm>
#include <cmath>

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

// 缶の追加効果の共通インターフェース
class ICanHitEffect {
public:
    virtual ~ICanHitEffect() = default;
    // CombatSystem が敵の判定と部位の解決を済ませてから呼ぶ
    virtual void Apply(entt::registry& registry, entt::entity target,
                       const DirectX::XMFLOAT3& source, const CanAttackEffectComponent& effect) const = 0;
};

// 磁力: 攻撃の発生源から敵を水平方向へ押し出す (ボスは効きを弱める)
class MagnetHitEffect final : public ICanHitEffect {
public:
    void Apply(entt::registry& registry, entt::entity target,
               const DirectX::XMFLOAT3& source, const CanAttackEffectComponent& effect) const override {
        // 敵の位置
        auto& transform = registry.get<TransformComponent>(target);
        // 発生源から敵への水平方向の差
        const float dx = transform.translate.x - source.x;
        const float dz = transform.translate.z - source.z;
        // その距離
        const float length = std::sqrt(dx * dx + dz * dz);
        // 重なっているときは向きが決まらないので動かさない
        if (length > 0.001f) {
            // ボスには 35% の強さしか効かない
            const float strength = registry.all_of<BossActionComponent>(target) ? effect.strength * 0.35f : effect.strength;
            // 発生源から離れる向きへ押し出す
            transform.translate.x += (dx / length) * strength;
            transform.translate.z += (dz / length) * strength;
        }
    }
};

// 氷: 敵を一定時間凍らせる (ボスは時間を短くする)
class IceHitEffect final : public ICanHitEffect {
public:
    void Apply(entt::registry& registry, entt::entity target,
               const DirectX::XMFLOAT3&, const CanAttackEffectComponent& effect) const override {
        // 状態異常のコンポーネント (無ければ付ける)
        auto& status = registry.get_or_emplace<CanStatusComponent>(target);
        // ボスは凍る時間が 55%
        const float duration = registry.all_of<BossActionComponent>(target) ? effect.duration * 0.55f : effect.duration;
        // 既に凍っているときは長い方を残す
        status.freezeTimer = (std::max)(status.freezeTimer, duration);
    }
};

// 酸: 継続ダメージの時間を設定し、ダメージ間隔をリセットする
class AcidHitEffect final : public ICanHitEffect {
public:
    void Apply(entt::registry& registry, entt::entity target,
               const DirectX::XMFLOAT3&, const CanAttackEffectComponent& effect) const override {
        // 状態異常のコンポーネント (無ければ付ける)
        auto& status = registry.get_or_emplace<CanStatusComponent>(target);
        // 酸の時間は長い方を残す
        status.acidTimer = (std::max)(status.acidTimer, effect.duration);
        // すぐに 1 回目のダメージが入るよう間隔を 0 に戻す
        status.acidTickTimer = 0.0f;
    }
};

// 泡: 敵を泡に包んで浮かせる。浮く前の高さを一度だけ記録する
class BubbleHitEffect final : public ICanHitEffect {
public:
    void Apply(entt::registry& registry, entt::entity target,
               const DirectX::XMFLOAT3&, const CanAttackEffectComponent& effect) const override {
        // 状態異常のコンポーネント (無ければ付ける)
        auto& status = registry.get_or_emplace<CanStatusComponent>(target);
        // ボスは泡の時間が 55%
        const float duration = registry.all_of<BossActionComponent>(target) ? effect.duration * 0.55f : effect.duration;
        // 泡の時間は長い方を残す
        status.bubbleTimer = (std::max)(status.bubbleTimer, duration);
        // 初めて泡に入ったときだけ、元の高さを覚える (泡が消えたらここへ戻す)
        if (!status.bubblePositionSaved) {
            status.bubbleBaseY = registry.get<TransformComponent>(target).translate.y;
            status.bubblePositionSaved = true;
        }
    }
};

// 缶の種類に対応する効果を返す (効果の無い種類は nullptr)
inline const ICanHitEffect* FindCanHitEffect(CanType type) {
    // 効果ごとの実体を 1 つずつ作っておき、使い回す
    static const MagnetHitEffect magnet;
    static const IceHitEffect ice;
    static const AcidHitEffect acid;
    static const BubbleHitEffect bubble;
    // 缶の種類で分ける
    switch (type) {
    case CanType::Magnet: return &magnet;
    case CanType::Ice: return &ice;
    case CanType::Acid: return &acid;
    case CanType::Bubble: return &bubble;
    default: return nullptr;
    }
}

} // namespace Game
