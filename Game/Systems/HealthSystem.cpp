// HealthSystem.cpp
// HealthSystem の関数の実装 (宣言は HealthSystem.h)。
#include "../Scenes/GameScene.h"
#include "HealthSystem.h"

namespace Game {

void HealthSystem::Update(entt::registry& registry, GameContext& ctx) {
	// PLAY 中でなければ何もしない
	if (!ctx.isPlaying) return;

	// 体力を持つすべてのエンティティを処理する
	auto view = registry.view<HealthComponent>();
	for (auto entity : view) {
		auto& hc = registry.get<HealthComponent>(entity);
		// 無効・死亡済みはとばす
		if (!hc.enabled || hc.IsDead()) continue;

		// 演出タイマーの更新
		if (hc.hitFlashTimer > 0.0f) {
			hc.hitFlashTimer -= ctx.dt;
			if (registry.all_of<MeshRendererComponent>(entity)) {
				auto& mr = registry.get<MeshRendererComponent>(entity);
				
				// 初回ヒット時に元の色を保存
				if (!hc.baseColorSaved) {
					hc.baseColor = mr.color;
					hc.baseColorSaved = true;
				}

				// 光る時間が終わったら元の色に戻す
				if (hc.hitFlashTimer <= 0.0f) {
					hc.hitFlashTimer = 0.0f;
					mr.color = hc.baseColor; // 元の色に戻す
				} else {
					// フラッシュ中（白く光らせる）
					mr.color = { 2.0f, 2.0f, 2.0f, 1.0f }; 
				}
			}
		}

		// ヒットストップと無敵時間を減らす
		if (hc.hitStopTimer > 0.0f) {
			hc.hitStopTimer -= ctx.dt;
			if (hc.hitStopTimer < 0.0f) hc.hitStopTimer = 0.0f;
		}

		// 無敵時間を減らす
		if (hc.invincibleTime > 0.0f) {
			hc.invincibleTime -= ctx.dt;
			if (hc.invincibleTime < 0.0f) hc.invincibleTime = 0.0f;
		}

		// ダメージ検知用の簡易ロジック
		// 前のフレームの体力と比べて、減っていればダメージの数字を出す
		uint32_t eid = static_cast<uint32_t>(entity);
		if (lastHp_.find(eid) != lastHp_.end()) {
			float diff = lastHp_[eid] - hc.Hp();
			if (diff > 0.1f) {
				// ダメージポップアップの生成
				auto dmgEntity = registry.create();
				auto& dnc = registry.emplace<DamageNumberComponent>(dmgEntity);
				// ダメージ量と表示の時間 (1 秒)
				dnc.damage = diff;
				dnc.lifetime = 1.0f;
				dnc.maxLifetime = 1.0f;
				// プレイヤーなら赤、敵なら白
				if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Player) {
					dnc.color = {1.0f, 0.2f, 0.2f};
				} else {
					dnc.color = {1.0f, 1.0f, 1.0f};
				}
				// 発生位置
				if (registry.all_of<TransformComponent>(entity)) {
					const auto& tc = registry.get<TransformComponent>(entity);
					dnc.startPos = {tc.translate.x, tc.translate.y + 1.5f, tc.translate.z};
				}
			}
		}
		// 今の体力を記録する
		lastHp_[eid] = hc.Hp();

		// 体力が 0 になったときの死亡処理
		if (hc.Hp() <= 0.0f && !hc.IsDead()) {
			// 敵かプレイヤーかを、タグと入力コンポーネントから判定する
			bool isEnemy = false;
			bool isPlayer = registry.all_of<PlayerInputComponent>(entity);
			if (registry.all_of<TagComponent>(entity)) {
				const auto tag = registry.get<TagComponent>(entity).tag;
				isEnemy = tag == TagType::Enemy;
				isPlayer = isPlayer || tag == TagType::Player;
			}

			// 敵の場合 (ボスは死亡の状態へ、雑魚はディゾルブで消す)
			if (isEnemy && registry.all_of<MeshRendererComponent>(entity)) {
				if (registry.all_of<BossActionComponent>(entity)) {
					// ボスの場合は専用の死亡ステートに移行する（BossTestScript側でアニメーションを再生して消去する）
					auto& boss = registry.get<BossActionComponent>(entity);
					if (boss.state != BossState::Dead) {
						BossActionSystem::ChangeState(registry, entity, BossState::Dead);

						// 攻撃と被弾の判定・当たり判定を止める
						if (registry.all_of<HurtboxComponent>(entity)) registry.get<HurtboxComponent>(entity).enabled = false;
						if (registry.all_of<HitboxComponent>(entity)) registry.get<HitboxComponent>(entity).isActive = false;
						if (registry.all_of<BoxColliderComponent>(entity)) registry.get<BoxColliderComponent>(entity).enabled = false;
						
						hc.enabled = false;
					}
				} else {
					// 雑魚: ディゾルブのシェーダーに切り替えて消していく
					auto& mr = registry.get<MeshRendererComponent>(entity);
					if (mr.shaderName != "Dissolve") {
						mr.shaderName = "Dissolve";
						mr.color.w = 1.0f; // ディゾルブ開始は不透明から

						if (!registry.all_of<AutoDestroyComponent>(entity)) {
							registry.emplace<AutoDestroyComponent>(entity).timer = 1.5f; // 1.5秒かけて消滅
						}

						// 攻撃・被弾判定の無効化
						if (registry.all_of<HurtboxComponent>(entity)) registry.get<HurtboxComponent>(entity).enabled = false;
						if (registry.all_of<HitboxComponent>(entity)) registry.get<HitboxComponent>(entity).isActive = false;
						if (registry.all_of<BoxColliderComponent>(entity)) registry.get<BoxColliderComponent>(entity).enabled = false;
						
						// AI更新停止
						if (registry.all_of<EnemyAIComponent>(entity)) registry.get<EnemyAIComponent>(entity).enabled = false;

						hc.enabled = false; // 以降のダメージ処理を止める（isDeadはfalseのまま残す）
					}
				}
			// 敵以外: プレイヤーなら死亡の演出を始め、死亡の状態にする
			} else {
				if (isPlayer) {
					BeginPlayerDeath(registry, entity, hc, ctx);
				}
				hc.SetDead(true);
			}
		}
	}

	// --- ダメージ数字コンポーネントの更新 ---
	entt::entity playerEntity = entt::null;
	TransformComponent* playerTransform = nullptr;
	HealthComponent* playerHealth = nullptr;
	// 回収の判定に使うプレイヤーの位置と体力を探す
	auto playerView = registry.view<PlayerInputComponent, TransformComponent, HealthComponent>();
	auto playerIt = playerView.begin();
	if (playerIt != playerView.end()) {
		auto entity = *playerIt;
		playerEntity = entity;
		playerTransform = &playerView.get<TransformComponent>(entity);
		playerHealth = &playerView.get<HealthComponent>(entity);
	}

	// 落とした液体: 寿命を減らし、プレイヤーの近くなら引き寄せて回収する
	auto fluidView = registry.view<LostFluidPickupComponent, TransformComponent>();
	for (auto entity : fluidView) {
		auto& fluid = fluidView.get<LostFluidPickupComponent>(entity);
		auto& tc = fluidView.get<TransformComponent>(entity);
		// 無効な液体はとばす
		if (!fluid.enabled) continue;

		// 寿命を減らし、切れたかを調べる
		bool lifetimeExpired = false;
		if (fluid.lifetime > 0.0f) {
			fluid.lifetime -= ctx.dt;
			lifetimeExpired = (fluid.lifetime <= 0.0f);
		}
		// 引き寄せが始まるまでの待ち時間を減らす
		if (fluid.magnetDelay > 0.0f) {
			fluid.magnetDelay -= ctx.dt;
		}

		// 寿命が切れたか持ち主がいなければ消す
		bool shouldDestroy = false;
		if (lifetimeExpired || !registry.valid(fluid.owner)) {
			shouldDestroy = true;
		}

		// プレイヤーが落とした液体なら、近づいたときに引き寄せて回収する
		if (!shouldDestroy && playerEntity != entt::null && fluid.owner == playerEntity && playerTransform && playerHealth) {
			// プレイヤーの体の中心への差と、距離の 2 乗 (3 次元と水平)
			float dx = playerTransform->translate.x - tc.translate.x;
			float dy = (playerTransform->translate.y + 0.8f) - tc.translate.y;
			float dz = playerTransform->translate.z - tc.translate.z;
			float distSq = dx * dx + dy * dy + dz * dz;
			float groundDistSq = dx * dx + dz * dz;

			// 引き寄せの範囲内なら、近いほど強くプレイヤーへ引っぱる
			if (fluid.magnetDelay <= 0.0f && groundDistSq < fluid.magnetRadius * fluid.magnetRadius) {
				float dist = std::sqrt((std::max)(distSq, 0.0001f));
				float groundDist = std::sqrt((std::max)(groundDistSq, 0.0001f));
				float pull = 42.0f * (1.0f - std::clamp(groundDist / fluid.magnetRadius, 0.0f, 1.0f));
				fluid.velocity.x += (dx / dist) * pull * ctx.dt;
				fluid.velocity.y += (dy / dist) * pull * ctx.dt;
				fluid.velocity.z += (dz / dist) * pull * ctx.dt;
			}

			// 回収の範囲内なら、体力とスタミナを回復し、見た目の液体をプレイヤーへ戻す
			if (fluid.magnetDelay <= 0.0f && groundDistSq < fluid.collectRadius * fluid.collectRadius) {
				playerHealth->RecoverHp(fluid.hpRestore);
				playerHealth->RecoverStamina(fluid.staminaRestore);
				if (ctx.renderer) {
					ctx.renderer->AbsorbLostGPUFluidGroup(fluid.visualGroupId);
				}
				shouldDestroy = true;
			}
		}

		// 回収されていない液体は重力で落ち、地面で弾む。見た目の GPU 流体の位置も合わせる
		if (!shouldDestroy) {
			// 重力で落とし、水平は少しずつ減速する
			fluid.velocity.y -= 18.0f * ctx.dt;
			fluid.velocity.x *= 0.995f;
			fluid.velocity.z *= 0.995f;

			// 速度の分だけ位置を進める
			tc.translate.x += fluid.velocity.x * ctx.dt;
			tc.translate.y += fluid.velocity.y * ctx.dt;
			tc.translate.z += fluid.velocity.z * ctx.dt;

			// 床 (高さ 0.25) より下に行ったら弾ませる
			if (tc.translate.y < 0.25f) {
				tc.translate.y = 0.25f;
				fluid.velocity.y *= -0.25f;
				fluid.velocity.x *= 0.72f;
				fluid.velocity.z *= 0.72f;
			}

			// 見た目の GPU 流体のまとまりを今の位置へ合わせる
			fluid.visualEmitTimer = 0.0f;
			if (ctx.renderer) {
				Engine::Vector3 visualPos = {tc.translate.x, tc.translate.y, tc.translate.z};
				ctx.renderer->SyncLostGPUFluidGroup(fluid.visualGroupId, visualPos);
			}
		}

		// 消す液体は持ち主の回収できる量から引いてから破棄する
		if (shouldDestroy && ctx.scene) {
			if (registry.valid(fluid.owner) && registry.all_of<HealthComponent>(fluid.owner)) {
				auto& ownerHealth = registry.get<HealthComponent>(fluid.owner);
				ownerHealth.recoverableFluid = (std::max)(0.0f, ownerHealth.recoverableFluid - fluid.hpRestore);
			}
			fluid.enabled = false;
			ctx.scene->DestroyObject(static_cast<uint32_t>(entity));
		}
	}

	// ダメージの数字の表示時間を減らし、切れたら消す
	auto dmgView = registry.view<DamageNumberComponent>();
	for (auto entity : dmgView) {
		auto& dnc = dmgView.get<DamageNumberComponent>(entity);
		dnc.lifetime -= ctx.dt;
		if (dnc.lifetime <= 0.0f) {
			if (ctx.scene) {
				ctx.scene->DestroyObject(static_cast<uint32_t>(entity));
			}
		}
	}
}

void HealthSystem::Reset(entt::registry& registry) {
	// すべての体力: 無敵・死亡・回収できる量・被弾回数を戻し、体力が 0 なら回復する
	auto view = registry.view<HealthComponent>();
	for (auto entity : view) {
		auto& hc = registry.get<HealthComponent>(entity);
		hc.invincibleTime = 0.0f;
		hc.SetDead(false);
		hc.recoverableFluid = 0.0f;
		hc.damageTakenCount = 0;
		hc.RestoreHpIfDepleted();
	}
	// 落とした液体をすべて破棄する
	auto fluidView = registry.view<LostFluidPickupComponent>();
	std::vector<entt::entity> fluidsToDestroy;
	for (auto entity : fluidView) {
		fluidsToDestroy.push_back(entity);
	}
	for (auto entity : fluidsToDestroy) {
		if (registry.valid(entity)) {
			registry.destroy(entity);
		}
	}
	// 前のフレームの体力の記録を消す
	lastHp_.clear();
}

void HealthSystem::BeginPlayerDeath(entt::registry& registry, entt::entity entity, HealthComponent& hc, GameContext& ctx) {
	// 入力を止めて中身を消す
	if (auto* pi = registry.try_get<PlayerInputComponent>(entity)) {
		pi->enabled = false;
		pi->moveDir = {0.0f, 0.0f};
		pi->jumpRequested = false;
		pi->attackRequested = false;
		pi->cameraYaw = 0.0f;
		pi->cameraPitch = 0.0f;
		pi->lockedEnemy = entt::null;
		pi->isRadialMenuOpen = false;
	}
	// 行動を待機に戻して止める
	if (auto* pa = registry.try_get<PlayerActionComponent>(entity)) {
		pa->enabled = false;
		PlayerActionSystem::ChangeState(registry, entity, PlayerActionState::Idle);
		pa->sodaAiming = false;
		pa->sodaDriftVelocity = {0.0f, 0.0f, 0.0f};
	}
	// 移動と剛体を止める
	if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
		cm->enabled = false;
		cm->velocityY = 0.0f;
	}
	if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
		rb->velocity = {0.0f, 0.0f, 0.0f};
		rb->enabled = false;
	}
	// 攻撃・被弾・当たり判定と表示を止める
	if (auto* hb = registry.try_get<HitboxComponent>(entity)) {
		hb->isActive = false;
	}
	if (auto* hurt = registry.try_get<HurtboxComponent>(entity)) {
		hurt->enabled = false;
	}
	if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
		bc->enabled = false;
	}
	if (auto* mr = registry.try_get<MeshRendererComponent>(entity)) {
		mr->enabled = false;
	}

	// 回収できる量を 0 にして、液体を飛び散らせる
	hc.recoverableFluid = 0.0f;
	SpawnPlayerDeathBurst(registry, entity, ctx);
}

void HealthSystem::SpawnPlayerDeathBurst(entt::registry& registry, entt::entity owner, GameContext& ctx) {
	if (!registry.valid(owner) || !registry.all_of<TransformComponent>(owner)) return;

	// 体の中心 (足元から 0.85 上) を起点にする
	const auto& ownerTc = registry.get<TransformComponent>(owner);
	const DirectX::XMFLOAT3 origin = {
		ownerTc.translate.x,
		ownerTc.translate.y + 0.85f,
		ownerTc.translate.z
	};

	// 72 個の液体を出す。見た目は GPU 流体の粒子を 10 個ずつ割り当てる
	const int pickupCount = 72;
	uint32_t firstVisualGroupId = 0;
	if (ctx.renderer) {
		const Engine::Vector3 pos = {origin.x, origin.y, origin.z};
		const Engine::Vector3 vel = {0.0f, 0.75f, 0.0f};
		firstVisualGroupId = ctx.renderer->ExtractGPUFluidFromPlayer(pos, vel, pickupCount * 10);
	}

	// 周りに均等に、少しずつずらした角度で飛ばす
	for (int i = 0; i < pickupCount; ++i) {
		const float t = static_cast<float>(i) / static_cast<float>(pickupCount);
		const float angle = t * 6.283185307f + (Hash01(static_cast<uint32_t>(i) * 193U + 7U) - 0.5f) * 0.65f;
		// 上向きの成分 (少し上へ跳ねる)
		const float lift = 0.18f + Hash01(static_cast<uint32_t>(i) * 271U + 11U) * 0.42f;
		const float outX = std::cos(angle);
		const float outY = lift;
		const float outZ = std::sin(angle);
		// 向きを正規化する
		const float outLen = std::sqrt(outX * outX + outY * outY + outZ * outZ);
		const float nx = outLen > 0.001f ? outX / outLen : 1.0f;
		const float ny = outLen > 0.001f ? outY / outLen : 0.25f;
		const float nz = outLen > 0.001f ? outZ / outLen : 0.0f;
		// 飛ぶ速さと、出す位置の中心からの距離 (ばらつきを付ける)
		const float speed = 8.5f + Hash01(static_cast<uint32_t>(i) * 401U + 29U) * 6.0f;
		const float startDist = 0.35f + Hash01(static_cast<uint32_t>(i) * 313U + 17U) * 0.45f;

		// 液体のエンティティを作る
		auto pickup = registry.create();
		registry.emplace<NameComponent>(pickup, "PlayerDeathFluid");

		auto& tc = registry.emplace<TransformComponent>(pickup);
		tc.translate = {
			origin.x + nx * startDist,
			origin.y + ny * startDist,
			origin.z + nz * startDist
		};

		// 回収はできない液体として設定する (寿命 1.7 秒で消える)
		auto& lf = registry.emplace<LostFluidPickupComponent>(pickup);
		lf.owner = owner;
		lf.hpRestore = 0.0f;
		lf.staminaRestore = 0.0f;
		lf.lifetime = 1.7f;
		lf.magnetDelay = 99.0f;
		lf.magnetRadius = 0.0f;
		lf.collectRadius = 0.0f;
		lf.visualGroupId = firstVisualGroupId + static_cast<uint32_t>(i);
		lf.velocity = {
			nx * speed,
			2.4f + ny * speed,
			nz * speed
		};

		// VFX のタグを付ける
		registry.emplace<TagComponent>(pickup, TagType::VFX);
	}
}

} // namespace Game
