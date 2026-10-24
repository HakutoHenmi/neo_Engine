// CanAbilitySystem.h
// 缶の能力による状態異常 (凍結・酸・泡) と、酸の水たまり・泡のシールド、およびそれらの見た目を管理するシステム。
#pragma once

#include "ISystem.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Game {

// 缶の能力のシステム (ISystem を継承し、毎フレーム Update が呼ばれる)
class CanAbilitySystem : public ISystem {
public:
	// PLAY 中: 酸の水たまり → 状態異常 → シールド → 見た目の順に更新する
	void Update(entt::registry& registry, GameContext& ctx) override {
		// PLAY 中でなければ何もしない
		if (!ctx.isPlaying) return;

		UpdateAcidPools(registry, ctx);
		UpdateStatuses(registry, ctx);
		UpdateShields(registry, ctx);
		UpdateVisuals(registry, ctx);
	}

	// PLAY 開始時: 状態異常の時間をすべて 0 にする
	void Reset(entt::registry& registry) override {
		// 状態異常を持つすべてのエンティティの時間を消す
		auto statusView = registry.view<CanStatusComponent>();
		for (auto entity : statusView) {
			auto& status = statusView.get<CanStatusComponent>(entity);
			status.freezeTimer = 0.0f;
			status.acidTimer = 0.0f;
			status.acidTickTimer = 0.0f;
			status.bubbleTimer = 0.0f;
		}
	}

private:
	// 見た目のエンティティを作る依頼 (反復の後でまとめて作る)
	struct VisualRequest {
		// 見た目を付ける対象と種類
		entt::entity target = entt::null;
		CanVisualKind kind = CanVisualKind::IceStatus;
	};

	// このフレームに作る見た目の依頼の一覧
	std::vector<VisualRequest> visualRequests_;

	// 敵のタグを持つか
	static bool IsEnemy(entt::registry& registry, entt::entity entity) {
		const auto* tag = registry.try_get<TagComponent>(entity);
		return tag && tag->tag == TagType::Enemy;
	}

	// 酸の水たまり: 脈打つ見た目にし、0.55 秒ごとに範囲内の敵へ酸を付けてダメージを与える
	void UpdateAcidPools(entt::registry& registry, GameContext& ctx);

	// 状態異常の時間を減らし、酸のダメージ・泡で浮かせる・色の変更・見た目の維持を行う
	void UpdateStatuses(entt::registry& registry, GameContext& ctx);

	// 状態異常の時間が残っていれば見た目を作る依頼を出し、終わっていれば見た目を消す
	void MaintainStatusVisual(entt::registry& registry, entt::entity target, float timer,
		entt::entity& visual, CanVisualKind kind, std::vector<entt::entity>& destroyVisuals) {
		// 残り時間があり、見た目がまだ無ければ作る依頼を出す
		if (timer > 0.0f) {
			if (visual == entt::null || !registry.valid(visual)) {
				visualRequests_.push_back({target, kind});
			}
		// 時間が切れたら見た目を消す一覧に入れ、記録を外す
		} else if (visual != entt::null) {
			if (registry.valid(visual)) destroyVisuals.push_back(visual);
			visual = entt::null;
		}
	}

	// 泡のシールド: 時間と回数が残っていれば見た目を保ち、無くなったら外す
	void UpdateShields(entt::registry& registry, GameContext& ctx);

	// 見た目を対象に追従させ、種類ごとの大きさと位置にする (シールドは残り 2 秒で点滅)
	void UpdateVisuals(entt::registry& registry, GameContext& ctx);

	// 依頼された見た目のエンティティを作る (メッシュ・色・粒子) と、対象にその見た目を記録する
	void CreateRequestedVisuals(entt::registry& registry, GameContext& ctx);

	// エンティティをまとめて破棄する
	static void DestroyEntities(entt::registry& registry, const std::vector<entt::entity>& entities) {
		for (auto entity : entities) {
			if (registry.valid(entity)) registry.destroy(entity);
		}
	}
};

} // namespace Game
