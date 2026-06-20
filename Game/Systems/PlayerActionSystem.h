#pragma once
#include "ISystem.h"
#include <cmath>

namespace Game {

enum class PlayerActionState : uint32_t {
	Idle = 0,
	SlimeSpike,   // 素早いトゲ攻撃
	SlimeHammer,  // 重い叩きつけ攻撃
	Dodge,        // 回避 (廃止・または予約)
	Liquefy,      // 液状化 (無敵)
	Stagger,      // のけぞり
	Charging,     // 溜め中
	Shoot,        // 発射アクション
	SpikeExplosion // ★追加: トゲトゲ大爆発
};

struct PlayerActionComponent : public Component {
	PlayerActionState state = PlayerActionState::Idle;
	float stateTimer = 0.0f;
	float stateDuration = 0.0f;
	float chargeTimer = 0.0f; // 溜めタイマー


	float dodgeDuration = 0.4f;
	float dodgeSpeed = 15.0f;
	float dodgeCooldown = 0.0f;
	DirectX::XMFLOAT3 dodgeDirection = {0, 0, 1};

	float hitStopTimer = 0.0f;

	bool enabled = true;
	PlayerActionComponent() { type = ComponentType::PlayerAction; }
};

class PlayerActionSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		auto view = registry.view<PlayerActionComponent, PlayerInputComponent, TransformComponent>();
		std::vector<entt::entity> entities(view.begin(), view.end());
		for (auto entity : entities) {
			if (!registry.valid(entity)) continue;
			auto& pa = registry.get<PlayerActionComponent>(entity);
			auto& pi = registry.get<PlayerInputComponent>(entity);
			auto& tc = registry.get<TransformComponent>(entity);
			if (!pa.enabled || !pi.enabled) continue;

			if (pa.hitStopTimer > 0.0f) {
				pa.hitStopTimer -= ctx.dt;
				if (pa.hitStopTimer < 0.0f) pa.hitStopTimer = 0.0f;
				continue;
			}

			if (pa.dodgeCooldown > 0.0f) pa.dodgeCooldown -= ctx.dt;

			pa.stateTimer += ctx.dt;

			bool attackInput = pi.attackRequested; // 左クリック (PlayerInputSystem.h)
			bool attackPressed = attackInput && !prevAttack_;
			prevAttack_ = attackInput;

			// 右クリックでハンマー攻撃
			bool hammerInput = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			bool hammerPressed = hammerInput && !prevHammer_;
			prevHammer_ = hammerInput;

			// Shift長押しで液状化
			bool liquefyInput = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

			float targetCamOffset = 0.0f; // ★追加: カメラの一時的な距離オフセット

			switch (pa.state) {
			case PlayerActionState::Idle:
			{
				bool isMoving = (std::abs(pi.moveDir.x) > 0.01f || std::abs(pi.moveDir.y) > 0.01f);
				bool isGrounded = true;
				if (registry.all_of<CharacterMovementComponent>(entity)) {
					isGrounded = registry.get<CharacterMovementComponent>(entity).isGrounded;
				}

				if (!isGrounded) {
					// 空中：縦に伸びる
					tc.scale.x = 0.9f;
					tc.scale.y = 1.2f;
					tc.scale.z = 0.9f;
				} else if (isMoving) {
					// 移動中：地面を這うように平べったく進む（跳ねない）
					// 高さは一定にしつつ、XとZを少し伸縮させて這っている感を出します
					float slither = std::sin(pa.stateTimer * 12.0f);
					tc.scale.x = 1.3f - slither * 0.05f;
					tc.scale.y = 0.65f; // 高さを固定（跳ねない）
					tc.scale.z = 1.3f + slither * 0.05f;
				} else {
					// 待機時：ゆっくり呼吸しておまんじゅう型
					float breathe = std::sin(pa.stateTimer * 3.0f);
					tc.scale.x = 1.2f + breathe * 0.05f;
					tc.scale.y = 0.8f - breathe * 0.05f;
					tc.scale.z = 1.2f + breathe * 0.05f;
				}

				if (liquefyInput) {
					TransitionTo(pa, PlayerActionState::Liquefy, 0.0f); // 長押し状態
				} else if (attackPressed) {
					TransitionTo(pa, PlayerActionState::Charging, 0.0f); // 溜め状態へ移行
				} else if (hammerPressed) {
					TransitionTo(pa, PlayerActionState::SlimeHammer, 1.0f);
				}
				break;
			}

			case PlayerActionState::Charging:
			{
				pa.chargeTimer += ctx.dt;
				// 溜め中の演出（小刻みに震えつつ、徐々に潰れる）
				float shake = std::sin(pa.chargeTimer * 50.0f) * 0.05f;
				tc.scale.x = 1.2f + shake;
				tc.scale.y = std::max(0.4f, 0.8f - pa.chargeTimer * 0.4f); // ペシャンコに
				tc.scale.z = 1.2f + shake;
				
				// ★追加: 溜めている間、徐々にカメラを引く（最大6.0m）
				targetCamOffset = std::min(pa.chargeTimer * 5.0f, 6.0f);

				// ★追加: 溜め中に右クリック（ハンマーボタン）が押されたら爆発派生
				if (hammerPressed) {
					TransitionTo(pa, PlayerActionState::SpikeExplosion, 0.5f); // 0.5秒の爆発アクション
					pa.chargeTimer = 0.0f;
				}
				// 左クリックを離したかどうかの判定 (入力が途切れたら)
				else if (!attackInput) {
					if (pa.chargeTimer >= 0.4f) { // 一定時間以上で遠距離発射
						TransitionTo(pa, PlayerActionState::Shoot, 0.3f);
						pa.chargeTimer = 0.0f;
					} else { // 短い場合は通常の近接攻撃
						TransitionTo(pa, PlayerActionState::SlimeSpike, 0.4f);
						pa.chargeTimer = 0.0f;
					}
				}
				break;
			}

			case PlayerActionState::SpikeExplosion:
			{
				float progress = pa.stateTimer / pa.stateDuration;
				targetCamOffset = 6.0f * (1.0f - progress); // ★追加: 爆発中は引いた状態から徐々に元に戻る
				
				// アニメーション: 一瞬縮んでから全方位に膨張
				if (progress < 0.2f) {
					float shrink = progress / 0.2f;
					tc.scale.x = std::lerp(1.2f, 0.4f, shrink);
					tc.scale.y = std::lerp(0.8f, 0.2f, shrink);
					tc.scale.z = std::lerp(1.2f, 0.4f, shrink);
				} else {
					float exp = (progress - 0.2f) / 0.8f;
					float pop = std::sin(exp * DirectX::XM_PI); // 0 -> 1 -> 0
					tc.scale.x = 1.0f + (pop * 2.5f);
					tc.scale.y = 0.8f + (pop * 1.5f);
					tc.scale.z = 1.0f + (pop * 2.5f);
				}

				// 爆発の瞬間にエフェクトとHitboxを生成
				if (pa.stateTimer >= 0.1f && pa.stateTimer - ctx.dt < 0.1f) {
					// 1. エフェクト用エンティティ（スクリプトに管理させ、自動削除させない）
					auto effectEntity = registry.create();
					registry.emplace<NameComponent>(effectEntity, "ExplosionEffectVisual");
					auto& eTc = registry.emplace<TransformComponent>(effectEntity);
					eTc.translate = tc.translate;
					
					auto& sc = registry.emplace<ScriptComponent>(effectEntity);
					ScriptEntry entry;
					entry.scriptPath = "HitEffectScript";
					entry.parameterData = "isExplosion=1"; // 専用フラグ
					sc.scripts.push_back(entry);
					
					// 2. 攻撃判定用（Hitbox）エンティティ（短時間で消える）
					auto hitboxEntity = registry.create();
					registry.emplace<NameComponent>(hitboxEntity, "ExplosionHitbox");
					registry.emplace<TagComponent>(hitboxEntity).tag = TagType::Player; 
					auto& hTc = registry.emplace<TransformComponent>(hitboxEntity);
					hTc.translate = tc.translate;

					auto& hb = registry.emplace<HitboxComponent>(hitboxEntity);
					hb.isActive = true;
					hb.size = {8.0f, 4.0f, 8.0f}; // 半径4m程度の範囲攻撃
					hb.center = {0, 1.0f, 0};
					hb.damage = 40.0f; // 高めのダメージ
					hb.tag = TagType::Player;
					hb.isProjectile = false; // 弾ではないが、触手エフェクト用に近接扱い

					// Hitboxエンティティは少ししたら消す
					registry.emplace<AutoDestroyComponent>(hitboxEntity).timer = 0.5f;
					
					// カメラシェイク
					if (ctx.camera) ctx.camera->StartShake(0.3f, 0.4f);
				}

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;
			}

			case PlayerActionState::Shoot:
			{
				// 発射の反動演出 (前方に伸びる)
				float progress = pa.stateTimer / pa.stateDuration;
				float recoil = std::sin(progress * 3.14159f);
				tc.scale.z = 1.2f + (recoil * 0.8f);
				tc.scale.y = 0.8f + (recoil * 0.2f);
				tc.scale.x = 1.2f - (recoil * 0.2f);

				// 状態に入った最初のフレームで弾を生成予約
				if (pa.stateTimer <= ctx.dt * 1.5f && pa.stateTimer > 0.0f) { 
					float facing = tc.rotate.y;
					float dx = std::sin(facing);
					float dz = std::cos(facing);
					ProjectileSpawnData ps;
					ps.pos = { tc.translate.x + dx * 1.5f, tc.translate.y + 0.8f, tc.translate.z + dz * 1.5f };
					ps.rot = tc.rotate;
					ps.dir = { dx, 0.0f, dz };
					pendingProjectiles_.push_back(ps);
				}

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;
			}

			case PlayerActionState::SlimeSpike:
			{
				// 素早く前方に伸びるトゲ攻撃
				float progress = pa.stateTimer / pa.stateDuration;
				float spike = 0.0f;
				if (progress < 0.2f) {
					spike = progress / 0.2f; // 急に伸びる
				} else {
					spike = 1.0f - ((progress - 0.2f) / 0.8f); // 戻る
				}
				tc.scale.x = 1.2f - (spike * 0.7f);
				tc.scale.y = 0.8f - (spike * 0.3f);
				tc.scale.z = 1.2f + (spike * 3.0f); // 鋭く伸びる

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
			}
			break;

			case PlayerActionState::SlimeHammer:
			{
				// 膨らんでから前方に叩きつけるハンマー攻撃
				float progress = pa.stateTimer / pa.stateDuration;
				float smash = 0.0f;
				if (progress < 0.5f) {
					smash = progress / 0.5f; // 膨らんで振り下ろす
				} else {
					smash = 1.0f - ((progress - 0.5f) / 0.5f);
				}
				float scaleBase = 1.0f + (1.2f * smash); // 大きく膨らむ
				tc.scale.x = 1.2f * scaleBase;
				tc.scale.y = 0.8f * scaleBase * (1.0f - smash * 0.6f); // 縦に潰れる
				tc.scale.z = 1.2f * scaleBase * (1.0f + smash * 0.6f); // 前方に伸びる

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
			}
			break;

			case PlayerActionState::Liquefy:
			{
				if (!liquefyInput) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
					break;
				}

				// スムーズにペシャンコになる
				float t = std::min(1.0f, pa.stateTimer * 8.0f);
				tc.scale.x = 1.2f + (1.8f * t); // 1.2 -> 3.0
				tc.scale.y = 0.8f - (0.65f * t); // 0.8 -> 0.15
				tc.scale.z = 1.2f + (1.8f * t); // 1.2 -> 3.0

				// 無敵状態を維持
				if (registry.all_of<HealthComponent>(entity)) {
					auto& hc = registry.get<HealthComponent>(entity);
					hc.invincibleTime = 0.2f; // 毎フレーム少し先まで無敵を更新
				}
			}
			break;

			case PlayerActionState::Dodge:
			{
				// 回避移動
				tc.translate.x += pa.dodgeDirection.x * pa.dodgeSpeed * ctx.dt;
				tc.translate.z += pa.dodgeDirection.z * pa.dodgeSpeed * ctx.dt;

				// シュッと伸び縮みしながら移動
				float progress = pa.stateTimer / pa.stateDuration;
				float stretch = std::sin(progress * 3.14159f); 
				tc.scale.x = 1.2f - (stretch * 0.5f);
				tc.scale.y = 0.8f - (stretch * 0.3f);
				tc.scale.z = 1.2f + (stretch * 1.5f);

				// 無敵時間
				if (registry.all_of<HealthComponent>(entity)) {
					auto& hc = registry.get<HealthComponent>(entity);
					if (pa.stateTimer >= 0.05f && pa.stateTimer <= 0.3f) {
						hc.invincibleTime = 0.1f;
					}
				}

				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
			}
			break;

			case PlayerActionState::Stagger:
				if (pa.stateTimer >= pa.stateDuration) {
					TransitionTo(pa, PlayerActionState::Idle, 0.0f);
				}
				break;
			}

			// ★追加: カメラのズームアウトオフセットの適用（滑らかに）
			if (registry.all_of<CameraTargetComponent>(entity)) {
				auto& ct = registry.get<CameraTargetComponent>(entity);
				ct.distanceOffset += (targetCamOffset - ct.distanceOffset) * 10.0f * ctx.dt;
			}

			// Hitbox（攻撃判定）の更新
			if (registry.all_of<HitboxComponent>(entity)) {
				auto& hb = registry.get<HitboxComponent>(entity);
				if (pa.state == PlayerActionState::SlimeSpike) {
					bool wasActive = hb.isActive;
					hb.isActive = (pa.stateTimer >= 0.1f && pa.stateTimer <= 0.25f);
					if (!wasActive && hb.isActive) hb.hitTargets.clear();
					hb.damage = 15.0f;
					hb.size = {1.5f, 1.5f, 5.0f}; // 縦長の当たり判定
				} else if (pa.state == PlayerActionState::SlimeHammer) {
					bool wasActive = hb.isActive;
					hb.isActive = (pa.stateTimer >= 0.4f && pa.stateTimer <= 0.6f);
					if (!wasActive && hb.isActive) hb.hitTargets.clear();
					hb.damage = 40.0f;
					hb.size = {5.0f, 4.0f, 5.0f}; // 巨大な当たり判定
				} else {
					hb.isActive = false;
				}
			}

			// ★超重要: 変形（スケール変化）に合わせて、常に底面が地面にくっつくように物理の浮遊オフセットを動的に同期する
			if (registry.all_of<CharacterMovementComponent>(entity)) {
				auto& cm = registry.get<CharacterMovementComponent>(entity);
				float newOffset = tc.scale.y * 0.5f; 
				float diff = newOffset - cm.heightOffset;

				// 接地中であれば、オフセットが変化した分だけ自身のY座標も直接補正する。
				// これをしないと、潰れた瞬間に足元が浮いて重力がかかり、ガクガク（ジッター）する原因になる。
				if (cm.isGrounded) {
					tc.translate.y += diff;
				}
				
				cm.heightOffset = newOffset;
			}
		}

		// --- 弾の遅延生成 ---
		for (const auto& ps : pendingProjectiles_) {
			if (ctx.scene) {
				entt::entity proj = ctx.scene->CreateEntity("PlayerProjectile");
				auto& ptc = registry.get<TransformComponent>(proj);
				ptc.translate = ps.pos;
				ptc.rotate = ps.rot;
				ptc.scale = { 0.6f, 0.6f, 0.6f };
				
				// 弾本体の初期化（GameScene側で星形に差し替えられるまでのフォールバック）
				auto& mr = registry.emplace<MeshRendererComponent>(proj);
				mr.modelPath = "Resources/Models/cube/cube.obj";
				mr.texturePath = "Resources/Models/cube/white1x1.png";
				mr.color = { 0.2f, 0.8f, 1.0f, 0.99f }; // 0.99fで顔消し
				if (ctx.renderer) {
					mr.modelHandle = ctx.renderer->LoadObjMesh(mr.modelPath);
					mr.textureHandle = ctx.renderer->LoadTexture2D(mr.texturePath);
				}
				
				auto& rb = registry.emplace<RigidbodyComponent>(proj);
				rb.useGravity = false;
				float speed = 30.0f;
				rb.velocity = { ps.dir.x * speed, 0.0f, ps.dir.z * speed };
				
				auto& bc = registry.emplace<BoxColliderComponent>(proj);
				bc.size = { 1.2f, 1.2f, 1.2f };
				
				auto& hb = registry.emplace<HitboxComponent>(proj);
				hb.isActive = true;
				hb.damage = 30.0f;
				hb.tag = TagType::Player;
				hb.size = { 1.5f, 1.5f, 1.5f };
				hb.isProjectile = true; // ★追加
				
				// ★重要: タグは Projectile に戻し、GetEntitiesByTag(Player) で誤認されるのを防ぐ
				registry.emplace<TagComponent>(proj, TagType::Projectile);
				registry.emplace<AutoDestroyComponent>(proj).timer = 2.0f;
				
				// 弾の軌跡用パーティクル
				auto& pe = registry.emplace<ParticleEmitterComponent>(proj);
				pe.emitter.params.name = "ProjTrail";
				pe.emitter.params.emitRate = 80; // 密度を高くしてビームのようにする
				pe.emitter.params.lifeTime = 0.3f;
				pe.emitter.params.startColor = { 0.0f, 1.0f, 1.0f, 1.0f }; 
				pe.emitter.params.endColor = { 1.0f, 0.0f, 1.0f, 0.0f }; 
				pe.emitter.params.startSize = { 0.4f, 0.4f, 0.4f }; // 大幅に縮小
				pe.emitter.params.endSize = { 0.05f, 0.05f, 0.05f };
				pe.emitter.params.useBillboard = false; 
				pe.emitter.params.isAdditive = false; 
				pe.emitter.params.texturePath = "Resources/Textures/white1x1.png"; 
				pe.emitter.params.startVelocity = {0, 0, 0};
				pe.emitter.params.velocityVariance = {0.3f, 0.3f, 0.3f}; // 少し散らす
				pe.emitter.params.angularVelocity = { 15.0f, 15.0f, 15.0f }; 
				pe.emitter.params.angularVelocityVariance = { 10.0f, 10.0f, 10.0f };
			}
		}
		pendingProjectiles_.clear();
	}

	void Reset(entt::registry& registry) override {
		prevAttack_ = false;
		prevHammer_ = false;
		prevDodge_ = false;

		auto view = registry.view<PlayerActionComponent>();
		for (auto entity : view) {
			auto& pa = registry.get<PlayerActionComponent>(entity);
			pa.state = PlayerActionState::Idle;
			pa.stateTimer = 0.0f;
			pa.hitStopTimer = 0.0f;
			pa.dodgeCooldown = 0.0f;
		}
	}

private:
	struct ProjectileSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rot;
		DirectX::XMFLOAT3 dir;
	};
	std::vector<ProjectileSpawnData> pendingProjectiles_;

	bool prevAttack_ = false;
	bool prevHammer_ = false;
	bool prevDodge_ = false;

	void TransitionTo(PlayerActionComponent& pa, PlayerActionState newState, float duration) {
		pa.state = newState;
		pa.stateTimer = 0.0f;
		pa.stateDuration = duration;
	}

	void StartDodge(PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
		TransitionTo(pa, PlayerActionState::Dodge, pa.dodgeDuration);
		pa.dodgeCooldown = 0.6f;

		float ix = pi.moveDir.x;
		float iz = pi.moveDir.y;
		float len = std::sqrt(ix * ix + iz * iz);

		if (len > 0.01f && ctx.camera) {
			auto camRot = ctx.camera->Rotation();
			float cy = std::cos(camRot.y);
			float sy = std::sin(camRot.y);
			float dx = ix * cy + iz * sy;
			float dz = -ix * sy + iz * cy;
			pa.dodgeDirection = { dx, 0.0f, dz };
		} else {
			float facing = tc.rotate.y;
			pa.dodgeDirection = { -std::sin(facing), 0.0f, -std::cos(facing) };
		}
	}
};

} // namespace Game
