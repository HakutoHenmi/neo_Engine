// CanAbilitySystem.cpp
// CanAbilitySystem の関数の実装 (宣言は CanAbilitySystem.h)。
#include "../Scenes/GameScene.h"
#include "CanAbilitySystem.h"

namespace Game {

void CanAbilitySystem::UpdateAcidPools(entt::registry& registry, GameContext& ctx) {
	// 酸の水たまりとトランスフォームを持つエンティティを順に処理する
	auto poolView = registry.view<AcidPoolComponent, TransformComponent>();
	for (auto poolEntity : poolView) {
		auto& pool = poolView.get<AcidPoolComponent>(poolEntity);
		auto& poolTc = poolView.get<TransformComponent>(poolEntity);
		// 脈打ちの時間を進め、0.90〜1.02 の倍率で大きさを揺らす
		pool.pulseTime += ctx.dt;
		const float pulse = 0.96f + std::sin(pool.pulseTime * 5.5f) * 0.06f;
		poolTc.scale.x = pool.radius * pulse;
		poolTc.scale.z = pool.radius * pulse;
		// 色を脈打ちに合わせて明るくする
		if (auto* mesh = registry.try_get<MeshRendererComponent>(poolEntity)) {
			mesh->color = {0.38f + pulse * 0.08f, 1.0f, 0.03f, 0.72f + (pulse - 0.90f)};
		}
		// 点光源の明るさも脈打ちに合わせる
		if (auto* light = registry.try_get<PointLightComponent>(poolEntity)) {
			light->intensity = 2.1f + (pulse - 0.90f) * 4.0f;
		}
		// ダメージを与える間隔のタイマーを減らし、まだなら次の水たまりへ
		pool.tickTimer -= ctx.dt;
		if (pool.tickTimer > 0.0f) continue;
		pool.tickTimer = 0.55f;

		// 体力とタグを持つエンティティから敵だけを調べる
		auto targetView = registry.view<TagComponent, TransformComponent, HealthComponent>();
		for (auto target : targetView) {
			if (targetView.get<TagComponent>(target).tag != TagType::Enemy) continue;
			// 死んでいる・無効な敵はとばす
			auto& health = targetView.get<HealthComponent>(target);
			if (health.IsDead() || !health.enabled) continue;
			// 水平の距離が水たまりの半径より遠ければとばす
			const auto& targetTc = targetView.get<TransformComponent>(target);
			const float dx = targetTc.translate.x - poolTc.translate.x;
			const float dz = targetTc.translate.z - poolTc.translate.z;
			if (dx * dx + dz * dz > pool.radius * pool.radius) continue;

			// 敵に酸の状態を付ける (残り時間は 5.5 秒以上にする)
			auto& status = registry.get_or_emplace<CanStatusComponent>(target);
			status.acidTimer = (std::max)(status.acidTimer, 5.5f);
			// サンドバッグモードでなければ 2 ダメージ
			if (!ctx.isSandbagMode) {
				health.ApplyDamage(2.0f);
			}
			// 被弾の光を少しだけ出す
			health.hitFlashTimer = (std::max)(health.hitFlashTimer, 0.05f);
		}
	}
}

void CanAbilitySystem::UpdateStatuses(entt::registry& registry, GameContext& ctx) {
	// 後で外す状態異常と、後で消す見た目の一覧
	std::vector<entt::entity> removeStatuses;
	std::vector<entt::entity> destroyVisuals;
	// 状態異常を持つエンティティを順に処理する
	auto view = registry.view<CanStatusComponent, TransformComponent>();

	for (auto entity : view) {
		auto& status = view.get<CanStatusComponent>(entity);
		auto& tc = view.get<TransformComponent>(entity);
		// 体力とメッシュは無いこともある
		auto* health = registry.try_get<HealthComponent>(entity);
		auto* mesh = registry.try_get<MeshRendererComponent>(entity);

		// 元の色を一度だけ保存する
		if (mesh && !status.baseColorSaved) {
			status.baseColor = health && health->baseColorSaved ? health->baseColor : mesh->color;
			status.baseColorSaved = true;
		}

		// 各状態異常の残り時間を減らす (0 未満にはしない)
		status.freezeTimer = (std::max)(0.0f, status.freezeTimer - ctx.dt);
		status.acidTimer = (std::max)(0.0f, status.acidTimer - ctx.dt);
		status.bubbleTimer = (std::max)(0.0f, status.bubbleTimer - ctx.dt);

		// 酸: 0.6 秒ごとにダメージ (サンドバッグモードでは与えない)
		if (status.acidTimer > 0.0f && health && health->enabled && !health->IsDead()) {
			status.acidTickTimer -= ctx.dt;
			if (status.acidTickTimer <= 0.0f) {
				status.acidTickTimer = 0.6f;
				if (!ctx.isSandbagMode && IsEnemy(registry, entity)) {
					health->ApplyDamage(3.0f);
				}
				health->hitFlashTimer = (std::max)(health->hitFlashTimer, 0.04f);
			}
		}

		// 泡: 元の高さから持ち上げて上下に揺らす
		if (status.bubbleTimer > 0.0f) {
			// 泡に入った最初のフレームの高さを覚えておく
			if (!status.bubblePositionSaved) {
				status.bubbleBaseY = tc.translate.y;
				status.bubblePositionSaved = true;
			}
			// 重力で落ちないよう縦の速度を 0 にし、空中扱いにする
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
				rb->velocity.y = 0.0f;
			}
			if (auto* movement = registry.try_get<CharacterMovementComponent>(entity)) {
				movement->isGrounded = false;
			}
			// ボスは低めに、雑魚は高めに持ち上げて sin で揺らす
			const bool isBoss = registry.all_of<BossActionComponent>(entity);
			const float liftHeight = isBoss ? 1.4f : 2.4f;
			const float bobHeight = isBoss ? 0.16f : 0.32f;
			tc.translate.y = status.bubbleBaseY + liftHeight + std::sin(status.bubbleTimer * 4.5f) * bobHeight;
		// 泡が消えたら地面の高さへ戻す
		} else if (status.bubblePositionSaved) {
			float restoreY = status.bubbleBaseY;
			auto* movement = registry.try_get<CharacterMovementComponent>(entity);
			// 元の高さより少し上から地面の高さを調べる
			if (ctx.scene) {
				const float rayStartY = (std::max)(tc.translate.y, status.bubbleBaseY) + 4.0f;
				const float groundY = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, rayStartY,
					static_cast<uint32_t>(entity));
				// 地面が見つかれば、その上に足の高さを合わせて接地させる
				if (groundY > -5000.0f) {
					restoreY = groundY + (movement ? movement->heightOffset : 0.0f);
					if (movement) movement->isGrounded = true;
				}
			}
			// 高さを戻し、縦の速度を消して、泡の位置の記録を解除する
			tc.translate.y = restoreY;
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) rb->velocity.y = 0.0f;
			status.bubblePositionSaved = false;
		}

		// 被弾の光っていない間は、状態異常の色 (泡 > 凍結 > 酸) にする
		if (mesh && status.baseColorSaved && health && health->hitFlashTimer <= 0.0f) {
			if (status.bubbleTimer > 0.0f) {
				mesh->color = {0.58f, 0.88f, 1.0f, status.baseColor.w};
			} else if (status.freezeTimer > 0.0f) {
				mesh->color = {0.42f, 0.84f, 1.0f, status.baseColor.w};
			} else if (status.acidTimer > 0.0f) {
				mesh->color = {0.55f, 1.0f, 0.18f, status.baseColor.w};
			} else {
				mesh->color = status.baseColor;
			}
		}

		// 凍結・酸・泡の見た目をそれぞれ保つか消す
		MaintainStatusVisual(registry, entity, status.freezeTimer, status.iceVisual, CanVisualKind::IceStatus, destroyVisuals);
		MaintainStatusVisual(registry, entity, status.acidTimer, status.acidVisual, CanVisualKind::AcidStatus, destroyVisuals);
		MaintainStatusVisual(registry, entity, status.bubbleTimer, status.bubbleVisual, CanVisualKind::BubbleStatus, destroyVisuals);

		// 状態異常がすべて終わったら色を戻してコンポーネントを外す
		if (status.freezeTimer <= 0.0f && status.acidTimer <= 0.0f && status.bubbleTimer <= 0.0f) {
			if (mesh && status.baseColorSaved) mesh->color = status.baseColor;
			removeStatuses.push_back(entity);
		}
	}

	// 反復が終わってから見た目を消し、依頼された見た目を作り、状態異常を外す
	DestroyEntities(registry, destroyVisuals);
	CreateRequestedVisuals(registry, ctx);
	for (auto entity : removeStatuses) {
		if (registry.valid(entity) && registry.all_of<CanStatusComponent>(entity)) {
			registry.remove<CanStatusComponent>(entity);
		}
	}
}

void CanAbilitySystem::UpdateShields(entt::registry& registry, GameContext& ctx) {
	std::vector<entt::entity> removeShields;
	std::vector<entt::entity> destroyVisuals;
	auto view = registry.view<BubbleShieldComponent>();
	for (auto entity : view) {
		auto& shield = view.get<BubbleShieldComponent>(entity);
		// シールドの残り時間を減らす
		shield.timer = (std::max)(0.0f, shield.timer - ctx.dt);
		// 時間と回数が残っていて見た目が無ければ作る依頼を出す
		if (shield.timer > 0.0f && shield.charges > 0) {
			if (shield.visualEntity == entt::null || !registry.valid(shield.visualEntity)) {
				visualRequests_.push_back({entity, CanVisualKind::BubbleShield});
			}
		// 切れたシールドは見た目を消してコンポーネントを外す
		} else {
			if (shield.visualEntity != entt::null && registry.valid(shield.visualEntity)) {
				destroyVisuals.push_back(shield.visualEntity);
			}
			removeShields.push_back(entity);
		}
	}

	// 反復の後で見た目の破棄・作成・シールドの取り外しを行う
	DestroyEntities(registry, destroyVisuals);
	CreateRequestedVisuals(registry, ctx);
	for (auto entity : removeShields) {
		if (registry.valid(entity) && registry.all_of<BubbleShieldComponent>(entity)) {
			registry.remove<BubbleShieldComponent>(entity);
		}
	}
}

void CanAbilitySystem::UpdateVisuals(entt::registry& registry, GameContext& ctx) {
	std::vector<entt::entity> destroyVisuals;
	auto view = registry.view<CanStatusVisualComponent, TransformComponent>();
	// 状態異常の見た目のエンティティを順に処理する
	for (auto entity : view) {
		auto& visual = view.get<CanStatusVisualComponent>(entity);
		auto& visualTc = view.get<TransformComponent>(entity);
		// 付いている対象が消えていれば見た目も消す
		if (visual.target == entt::null || !registry.valid(visual.target) || !registry.all_of<TransformComponent>(visual.target)) {
			destroyVisuals.push_back(entity);
			continue;
		}

		// 対象の位置に合わせ、ゆっくり回す (酸は遅く、それ以外は速く)
		const auto& targetTc = registry.get<TransformComponent>(visual.target);
		visualTc.translate = targetTc.translate;
		visualTc.rotate.y += ctx.dt * (visual.kind == CanVisualKind::AcidStatus ? 0.6f : 1.5f);
		// 対象の大きさ (一番大きい軸、最低 1) を基準にする
		const float targetRadius = (std::max)({std::abs(targetTc.scale.x), std::abs(targetTc.scale.y), std::abs(targetTc.scale.z), 1.0f});

		switch (visual.kind) {
		// 凍結: 対象の上寄りに、少し大きい氷の球
		case CanVisualKind::IceStatus:
			visualTc.translate.y += targetRadius * 0.35f;
			visualTc.scale = {targetRadius * 1.25f, targetRadius * 1.25f, targetRadius * 1.25f};
			break;
		// 酸: 足元に平たい円柱
		case CanVisualKind::AcidStatus:
			visualTc.translate.y -= targetRadius * 0.45f;
			visualTc.scale = {targetRadius * 1.7f, 0.05f, targetRadius * 1.7f};
			break;
		// 泡: 対象を包む大きな球
		case CanVisualKind::BubbleStatus:
			visualTc.translate.y += targetRadius * 0.25f;
			visualTc.scale = {targetRadius * 1.7f, targetRadius * 1.7f, targetRadius * 1.7f};
			break;
		// シールド: 対象の周りの大きな球を、少し膨らませたり縮めたりする
		case CanVisualKind::BubbleShield:
			visualTc.translate.y += 0.4f;
			{
				const float shieldRadius = (std::max)(4.8f, targetRadius * 3.8f);
				const float breathe = 1.0f + std::sin(ctx.dt + shieldRadius + visualTc.rotate.y * 2.0f) * 0.025f;
				visualTc.scale = {shieldRadius * breathe, shieldRadius * breathe, shieldRadius * breathe};
				// 残り 2 秒になったら点滅させて切れそうなことを知らせる
				if (auto* mesh = registry.try_get<MeshRendererComponent>(entity)) {
					if (const auto* shield = registry.try_get<BubbleShieldComponent>(visual.target)) {
						if (shield->timer <= 2.0f) {
							const float blink = 0.5f + 0.5f * std::sin(shield->timer * 18.0f);
							mesh->color = {1.0f, 0.16f + blink * 0.30f, 0.28f + blink * 0.34f, 0.22f + blink * 0.58f};
						} else {
							mesh->color = {1.0f, 0.34f, 0.78f, 0.48f};
						}
					}
				}
			}
			break;
		}
	}
	DestroyEntities(registry, destroyVisuals);
}

void CanAbilitySystem::CreateRequestedVisuals(entt::registry& registry, GameContext& ctx) {
	// 依頼ごとに見た目のエンティティを 1 つ作る
	for (const auto& request : visualRequests_) {
		if (!registry.valid(request.target) || !registry.all_of<TransformComponent>(request.target)) continue;

		// 名前・トランスフォーム・対象とのつながり・VFX のタグを付ける
		auto visual = registry.create();
		registry.emplace<NameComponent>(visual, "CanStatusVisual");
		auto& tc = registry.emplace<TransformComponent>(visual);
		tc.translate = registry.get<TransformComponent>(request.target).translate;
		auto& link = registry.emplace<CanStatusVisualComponent>(visual);
		link.target = request.target;
		link.kind = request.kind;
		registry.emplace<TagComponent>(visual, TagType::VFX);

		// メッシュ: 酸は円柱、それ以外は球。光るシェーダーを使う
		auto& mesh = registry.emplace<MeshRendererComponent>(visual);
		mesh.modelPath = request.kind == CanVisualKind::AcidStatus
			? "Resources/Models/Cylinder/cylinder.obj"
			: "Resources/Models/player_ball/ball.obj";
		mesh.texturePath = "Resources/Textures/white1x1.png";
		mesh.useCubemap = false;
		mesh.shaderName = request.kind == CanVisualKind::AcidStatus ? "EmissiveGlow" : "ForceField";
		// 種類ごとの色
		switch (request.kind) {
		case CanVisualKind::IceStatus: mesh.color = {0.30f, 0.86f, 1.0f, 0.34f}; break;
		case CanVisualKind::AcidStatus: mesh.color = {0.42f, 1.0f, 0.06f, 0.72f}; break;
		case CanVisualKind::BubbleStatus: mesh.color = {0.62f, 0.82f, 1.0f, 0.42f}; break;
		case CanVisualKind::BubbleShield: mesh.color = {1.0f, 0.34f, 0.78f, 0.48f}; break;
		}
		// モデルとテクスチャを読み込む
		if (ctx.renderer) {
			mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
			mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
		}

		// 周りに漂う粒子 (種類ごとの色で、上へ流れて消える)
		auto& particles = registry.emplace<ParticleEmitterComponent>(visual);
		particles.emitter.params.name = "CanStatusAura";
		particles.emitter.params.emitRate = request.kind == CanVisualKind::AcidStatus ? 18.0f : 10.0f;
		particles.emitter.params.lifeTime = 0.45f;
		particles.emitter.params.startSize = {0.20f, 0.20f, 0.20f};
		particles.emitter.params.endSize = {0.55f, 0.55f, 0.55f};
		particles.emitter.params.velocityVariance = {1.2f, 1.2f, 1.2f};
		particles.emitter.params.acceleration = {0.0f, 0.8f, 0.0f};
		particles.emitter.params.isAdditive = true;
		particles.emitter.params.shaderName = "SoftParticleAdditive";
		particles.emitter.params.texturePath = "Resources/Textures/ball.png";
		if (request.kind == CanVisualKind::AcidStatus) {
			particles.emitter.params.startColor = {0.55f, 1.0f, 0.10f, 0.8f};
			particles.emitter.params.endColor = {0.12f, 0.48f, 0.03f, 0.0f};
		} else if (request.kind == CanVisualKind::IceStatus) {
			particles.emitter.params.startColor = {0.75f, 0.98f, 1.0f, 0.8f};
			particles.emitter.params.endColor = {0.16f, 0.62f, 1.0f, 0.0f};
		} else {
			particles.emitter.params.startColor = {1.0f, 0.48f, 0.86f, 0.72f};
			particles.emitter.params.endColor = {0.35f, 0.82f, 1.0f, 0.0f};
		}

		// 作った見た目を対象に記録する (二重に作らないため)
		if (request.kind == CanVisualKind::BubbleShield) {
			if (auto* shield = registry.try_get<BubbleShieldComponent>(request.target)) shield->visualEntity = visual;
		} else if (auto* status = registry.try_get<CanStatusComponent>(request.target)) {
			if (request.kind == CanVisualKind::IceStatus) status->iceVisual = visual;
			if (request.kind == CanVisualKind::AcidStatus) status->acidVisual = visual;
			if (request.kind == CanVisualKind::BubbleStatus) status->bubbleVisual = visual;
		}
	}
	// 依頼の一覧を空にする
	visualRequests_.clear();
}

} // namespace Game
