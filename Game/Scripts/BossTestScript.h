// BossTestScript.h
// 旧アクションゲームのボス (ミュータント) の見た目と演出を担当するスクリプト。
// ボスの状態 (BossActionComponent) に合わせてアニメーションを切り替え、攻撃の予兆などを出す。
#pragma once
#include "BaseScript.h"
#include <DirectXMath.h>
#include <unordered_map>
#include <string>

namespace Game {

// ボスの演出スクリプト
class BossTestScript : public BaseScript {
public:
    void Start(entt::entity entity, GameScene* scene) override;
    void Update(entt::entity entity, GameScene* scene, float dt) override;
    void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
    // 元の大きさ (演出で伸縮させた後に戻すため)
    DirectX::XMFLOAT3 originalScale_ = {1.0f, 1.0f, 1.0f};

    BossState prevBossState_ = BossState::Idle; // 前フレームのボスの状態 (状態が変わった瞬間を検出する)
    std::unordered_map<std::string, std::string> animNameCache_; // アニメーション名の検索結果のキャッシュ (短い名前 → モデル内の正式名)
};

} // namespace Game
