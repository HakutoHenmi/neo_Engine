// PlayerActionSystem.h
// 旧アクションゲームのプレイヤー (スライムと缶) の行動。
// 入力から状態 (States/PlayerActionStates.inl) を動かし、缶ごとの能力・弾やエフェクトの生成・体の大きさを管理する。
#pragma once
#include "../States/EntityStateContext.h"
#include "ISystem.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace Game {

// プレイヤーの行動の状態
enum class PlayerActionState : uint32_t {
	Idle = 0,
	SlimeSpike,   // 素早いトゲ攻撃
	SlimeHammer,  // 重い叩きつけ攻撃
	Dodge,        // 回避 (廃止・または予約)
	Liquefy,      // 液状化 (無敵)
	Stagger,      // のけぞり
	Charging,     // 溜め中
	Shoot,        // 発射アクション
	SpikeExplosion, // トゲトゲの大爆発
	FireBreath,   // 炎の缶の攻撃
	DecoyWarp     // 分身ワープ (弧を描いて移動する)
};

// プレイヤーの行動コンポーネント (状態・時間・入力の前回値・回避・缶・炭酸・液状化・各缶の能力の値)
struct PlayerActionComponent : public Component {
	PlayerActionState state = PlayerActionState::Idle;
	float stateTimer = 0.0f;
	float stateDuration = 0.0f;
	bool previousAttack = false, previousHammer = false, previousDodge = false;
	bool shotEmitted = false;
	bool warpMovementEnabled = true;
	float chargeTimer = 0.0f; // 溜めタイマー


	float dodgeDuration = 0.26f;
	float dodgeSpeed = 23.0f;
	float dodgeCooldown = 0.0f;
	DirectX::XMFLOAT3 dodgeDirection = {0, 0, 1};

	float hitStopTimer = 0.0f;
	float totalTime = 0.0f; // ふわふわ揺れる演出用の時間

	CanType currentCan = CanType::None; // 現在入っている缶
	entt::entity canEntity = entt::null; // 缶のエンティティ
	DirectX::XMFLOAT3 canFollowPos = {0.0f, 0.0f, 0.0f};
	bool canFollowInitialized = false;
	float canRevealTimer = 1.0f;
	bool prevRadialMenuOpen = false; // 前フレームのラジアルメニューの開閉

	DirectX::XMFLOAT3 warpStartPos = {0,0,0}; // ワープの開始位置
	DirectX::XMFLOAT3 warpEndPos = {0,0,0};   // ワープの終了位置

	// 炭酸の缶 (ガスの量・狙い・噴射・漂う速度)
	float sodaGas = 100.0f;
	float sodaAimTimer = 0.0f;
	float sodaEmitTimer = 0.0f;
	bool sodaAiming = false;
	DirectX::XMFLOAT3 sodaDriftVelocity = {0.0f, 0.0f, 0.0f};
	// 液状化 (元の速さ・固定位置・流れる向きと速さ)
	float liquefyBaseSpeed = 0.0f;
	bool liquefySpeedApplied = false;
	DirectX::XMFLOAT3 liquefyAnchorPos = {0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT3 liquefyFlowDir = {0.0f, 0.0f, 1.0f};
	float liquefyInitialFlowSpeed = 0.0f;
	bool liquefyLocked = false;
	// 液状化中に流れる速さ (入ったときの勢いを時間で弱める)
	float CurrentLiquefyFlowSpeed() const {
		// 液状化中は位置を固定している。入ったときの勢いは、押している間ずっと
		// 水たまりを押し流すのではなく、だんだん弱まるようにする
		return liquefyInitialFlowSpeed * std::exp(-8.0f * stateTimer);
	}
	// 缶の能力の待ち時間と、氷・磁石の演出の時間
	float canPrimaryCooldown = 0.0f;
	float canSecondaryCooldown = 0.0f;
	float iceTrailEmitTimer = 0.0f;
	float iceSlideBaseSpeed = 0.0f;
	bool iceSlideApplied = false;
	float magnetEffectTimer = 0.0f;

	bool enabled = true;
	PlayerActionComponent() { type = ComponentType::PlayerAction; }
};

// プレイヤーの行動システム
class PlayerActionSystem : public ISystem {
public:
	// 状態を切り替える (前の状態の Exit → 新しい状態の Enter)
	static void ChangeState(entt::registry& registry, entt::entity entity, PlayerActionState next, float duration = 0.0f) {
		auto& action = registry.get<PlayerActionComponent>(entity);
		EntityStateContext lifecycle{registry, entity};
		GetState(action.state).Exit(lifecycle);
		// 攻撃判定とコライダーのずれを戻す
		if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) {
			hitbox->isActive = false;
			hitbox->center.z = 0.0f;
		}
		if (auto* collider = registry.try_get<BoxColliderComponent>(entity))
			collider->center = {0, 0, 0};
		// 状態・経過時間・長さを設定して新しい状態に入る
		action.state = next;
		action.stateTimer = 0.0f;
		action.stateDuration = duration;
		GetState(next).Enter(lifecycle);
	}

	// PLAY 中の更新: 各プレイヤーの入力から行動を決め、最後に弾やエフェクトをまとめて生成する
	void Update(entt::registry& registry, GameContext& ctx) override {
		// PLAY 中でなければ何もしない
		if (!ctx.isPlaying) return;

		// 反復中にエンティティが増えても安全なよう、一覧を複製してから処理する
		auto view = registry.view<PlayerActionComponent, PlayerInputComponent, TransformComponent>();
		std::vector<entt::entity> entities(view.begin(), view.end());
		for (auto entity : entities) {
			if (!registry.valid(entity)) continue;
			// このプレイヤーの行動・入力・トランスフォーム
			auto& pa = registry.get<PlayerActionComponent>(entity);
			auto& pi = registry.get<PlayerInputComponent>(entity);
			auto& tc = registry.get<TransformComponent>(entity);
			// 無効なら何もしない
			if (!pa.enabled || !pi.enabled) continue;

			if (pa.state != PlayerActionState::Liquefy && pa.liquefySpeedApplied) {
				// 液状化を抜けたら、液状化中の速さと固定を元に戻す
				RestoreLiquefySpeed(registry, entity, pa);
			}
			// 液状化の固定も解除する
			if (pa.state != PlayerActionState::Liquefy && pa.liquefyLocked) {
				pa.liquefyLocked = false;
				pa.liquefyInitialFlowSpeed = 0.0f;
			}

			if (pa.hitStopTimer > 0.0f) {
				// ヒットストップ中は何もしない
				pa.hitStopTimer -= ctx.dt;
				if (pa.hitStopTimer < 0.0f) pa.hitStopTimer = 0.0f;
				continue;
			}

			// 回避の待ち時間
			if (pa.dodgeCooldown > 0.0f) pa.dodgeCooldown -= ctx.dt;
			// 各待ち時間を減らす
			pa.canPrimaryCooldown = (std::max)(0.0f, pa.canPrimaryCooldown - ctx.dt);
			pa.canSecondaryCooldown = (std::max)(0.0f, pa.canSecondaryCooldown - ctx.dt);
			// 氷の缶でなくなったら滑走を戻す
			if (pa.currentCan != CanType::Ice && pa.iceSlideApplied) {
				RestoreIceSlide(registry, entity, pa);
			}

			// 状態の経過時間
			pa.stateTimer += ctx.dt;
			pa.totalTime += ctx.dt; // ふわふわ揺れる演出用の時間を進める
			
			// コライダーのずれを既定に戻す
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				bc->center = { 0.0f, 0.0f, 0.0f };
			}

			// ラジアルメニューで缶を切り替える
			bool currentRadialMenuOpen = pi.isRadialMenuOpen;
			// ラジアルメニューを閉じた瞬間に、選んだ缶へ切り替える (後でまとめて行う)
			if (!currentRadialMenuOpen && pa.prevRadialMenuOpen) {
				if (pi.selectedCan != pa.currentCan) {
					pendingCanChanges_.push_back({entity, pi.selectedCan});
				}
			}
			pa.prevRadialMenuOpen = currentRadialMenuOpen;

			// 缶は、移動と物理の後のプレイヤーの最終位置に付いていく


			// このフレームのカメラを引く量と、左クリック (攻撃) の入力
			float targetCamOffset = 0.0f;

			// 攻撃の入力と、押した瞬間か
			bool attackInput = pi.attackRequested; // 左クリック (PlayerInputSystem.h)
			bool attackPressed = attackInput && !pa.previousAttack;
			pa.previousAttack = attackInput;

			// 右クリックでハンマー攻撃（またはデコイ発動）
			// 右クリックの押下・押した瞬間・離した瞬間
			bool hammerInput = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			bool hammerPressed = hammerInput && !pa.previousHammer;
			bool hammerReleased = !hammerInput && pa.previousHammer;
			pa.previousHammer = hammerInput;

			if (pa.currentCan == CanType::Soda) {
				// 炭酸の缶は専用の処理 (攻撃とハンマーの入力はそちらで使う)
				HandleSodaCan(registry, entity, pa, pi, tc, ctx, attackInput, hammerInput, hammerReleased, targetCamOffset);
				attackPressed = false;
				hammerPressed = false;
			} else {
				// 炭酸の缶でなければ狙いを解除する
				pa.sodaAiming = false;
				pa.sodaAimTimer = 0.0f;
			}

			// 氷・磁石・酸・泡の缶は専用の処理
			if (pa.currentCan == CanType::Ice || pa.currentCan == CanType::Magnet ||
				pa.currentCan == CanType::Acid || pa.currentCan == CanType::Bubble) {
				HandleAdvancedCan(registry, entity, pa, pi, tc, ctx, attackPressed, hammerInput, hammerPressed);
				attackPressed = false;
				hammerPressed = false;
			}

			// Shift で回避
			bool dashInput = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
			bool dashPressed = dashInput && !pa.previousDodge;
			pa.previousDodge = dashInput;
			// 液体の攻撃の後の隙は回避で中断できる
			if (dashPressed && pa.dodgeCooldown <= 0.0f && GetState(pa.state).CanCancelToDodge(pa)) {
				pa.chargeTimer = 0.0f;
				StartDodge(registry, entity, pa, pi, tc, ctx);
			}

			// 水の缶で右クリックを押している間は液状化する
			bool waterLiquefyInput = (pa.currentCan == CanType::Water && hammerInput);
			bool liquefyInput = waterLiquefyInput;
			if (waterLiquefyInput) {
				hammerPressed = false;
			}

			// 分身ワープ (黄色の缶を選んでいるとき)
			if (hammerPressed && pa.currentCan == CanType::Thunder) {
				hammerPressed = false; // ハンマー攻撃には派生させない
				
				// 分身は 1 体しか出せないようにする
				bool hasDecoy = false;
				for (auto e : registry.view<TagComponent>()) {
					if (registry.get<TagComponent>(e).tag == TagType::Player && !registry.all_of<PlayerInputComponent>(e)) {
						hasDecoy = true;
						break;
					}
				}

				if (!hasDecoy) {
					// ロックオン中の敵 (いなければ一番近い敵) を探す
					entt::entity targetEnemy = pi.lockedEnemy;
					// ロックオンがない場合は一番近い敵を探す
					if (targetEnemy == entt::null || !registry.valid(targetEnemy)) {
						float minDist = 999999.0f;
						for (auto e : registry.view<TagComponent, TransformComponent>()) {
							if (registry.get<TagComponent>(e).tag == TagType::Enemy) {
								auto& eTc = registry.get<TransformComponent>(e);
								float dx = eTc.translate.x - tc.translate.x;
								float dy = eTc.translate.y - tc.translate.y;
								float dz = eTc.translate.z - tc.translate.z;
								float dist = dx*dx + dy*dy + dz*dz;
								if (dist < minDist) {
									minDist = dist;
									targetEnemy = e;
								}
							}
						}
					}

					// 敵が見つかったら、分身を置いて敵の向こう側へワープする
					if (targetEnemy != entt::null && registry.valid(targetEnemy)) {
						auto& bossTc = registry.get<TransformComponent>(targetEnemy);
						
						// 1. デコイ（分身）の生成を予約
						pendingDecoys_.push_back({ tc.translate, tc.rotate, tc.scale });

						// 2. プレイヤーのワープ設定
						float dx = bossTc.translate.x - tc.translate.x;
						float dz = bossTc.translate.z - tc.translate.z;
						float len = std::sqrt(dx*dx + dz*dz);
						if (len > 0.001f) {
							dx /= len; dz /= len;
						} else {
							dx = 0; dz = 1;
						}

						// ワープ元エフェクト
						pendingWarpEffects_.push_back(tc.translate);

						// ボスの反対側（ボスからさらに奥へ大きく飛び越す）
						float warpDist = 12.0f;
						pa.warpStartPos = tc.translate;
						pa.warpEndPos.x = bossTc.translate.x + dx * warpDist;
						pa.warpEndPos.y = tc.translate.y; // 高さはそのままか床
						pa.warpEndPos.z = bossTc.translate.z + dz * warpDist;
						
						// プレイヤーをボスの方に向かせる
						tc.rotate.y = std::atan2(-dx, -dz);
						
						// 少し無敵を付与
						if (auto* hc = registry.try_get<HealthComponent>(entity)) {
							hc->invincibleTime = 0.8f; // ワープ中〜着地後少し無敵
						}
						// デコイワープステートへ移行 (1.0秒で大きくボスを飛び越える)
						ChangeState(registry, entity, PlayerActionState::DecoyWarp, 1.0f);
					}
				}
			}

			// 状態の更新に渡す情報をまとめて、今の状態の Update を呼ぶ
			StateContext frame{*this, registry, entity, ctx, attackInput, attackPressed, hammerPressed, dashPressed, liquefyInput, targetCamOffset};
			GetState(pa.state).Update(frame);

			// 水の量 (体力) で全体の大きさを変える
			// (すべての変形の後で掛けるので、どの状態でも水の量が反映される)
			if (registry.all_of<HealthComponent>(entity)) {
				auto& hc = registry.get<HealthComponent>(entity);
				// 体力の割合で 40%〜100% の大きさにする
				float waterScale = 0.4f + 0.6f * (std::max(0.0f, hc.Hp()) / hc.MaxHp()); // 最小で40%の大きさ
				tc.scale.x *= waterScale;
				tc.scale.y *= waterScale;
				tc.scale.z *= waterScale;
			}

			// カメラを引く量をなめらかに反映する
			if (registry.all_of<CameraTargetComponent>(entity)) {
				auto& ct = registry.get<CameraTargetComponent>(entity);
				ct.distanceOffset += (targetCamOffset - ct.distanceOffset) * 10.0f * ctx.dt;
			}

			// 反復中の CreateEntity() で TransformComponent のメモリが移動した場合に備えて、参照を取り直す
			auto& currentTc = registry.get<TransformComponent>(entity);

			// 今の状態の攻撃判定を更新する
			EntityStateContext hitboxContext{registry, entity};
			GetState(pa.state).UpdateHitbox(hitboxContext);

			// とても重要: 変形 (大きさの変化) に合わせて、底面が常に地面に付くよう物理の浮かせる量を同期する
			if (registry.all_of<CharacterMovementComponent>(entity)) {
				// 大きさの半分を足元からの高さにする
				auto& cm = registry.get<CharacterMovementComponent>(entity);
				float newOffset = currentTc.scale.y * 0.5f; 
				float diff = newOffset - cm.heightOffset;

				// 接地中であれば、オフセットが変化した分だけ自身のY座標も直接補正する。
				// これをしないと、潰れた瞬間に足元が浮いて重力がかかり、ガクガク（ジッター）する原因になる。
				if (cm.isGrounded) {
					currentTc.translate.y += diff;
				}
				
				cm.heightOffset = newOffset;
			}
		}

		// --- 弾の遅延生成 ---
		// 水の弾を作る (細長い高圧の水流)
		for (const auto& ps : pendingProjectiles_) {
			if (ctx.scene) {
				entt::entity proj = ctx.scene->CreateEntity("PlayerProjectile");
				auto& ptc = registry.get<TransformComponent>(proj);
				ptc.translate = ps.pos;
				ptc.rotate = ps.rot;
				ptc.scale = { 0.8f, 0.8f, 8.0f }; // さらに太く長い超高圧水流ビームに巨大化
				
				// 弾本体の初期化
				// 見た目 (立方体を水色の半透明にする)
				auto& mr = registry.emplace<MeshRendererComponent>(proj);
				mr.modelPath = "Resources/Models/cube/cube.obj";
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.color = { 0.2f, 0.8f, 1.0f, 0.8f }; // 半透明の水色
				if (ctx.renderer) {
					mr.modelHandle = ctx.renderer->LoadObjMesh(mr.modelPath);
					mr.textureHandle = ctx.renderer->LoadTexture2D(mr.texturePath);
				}
				
				// 重力の無い剛体で前へ速く飛ばす
				auto& rb = registry.emplace<RigidbodyComponent>(proj);
				rb.useGravity = false;
				float speed = 120.0f; // 高速で飛ぶ
				rb.velocity = { ps.dir.x * speed, 0.0f, ps.dir.z * speed };
				
				// 当たり判定と攻撃判定
				auto& bc = registry.emplace<BoxColliderComponent>(proj);
				bc.size = { 2.0f, 2.0f, 8.0f }; // コライダーも拡大
				
				auto& hb = registry.emplace<HitboxComponent>(proj);
				hb.isActive = true;
				hb.damage = 30.0f; // ダメージ上昇
				hb.tag = TagType::Player;
				hb.size = { 3.0f, 3.0f, 10.0f }; // ヒットボックスも特大化
				hb.isProjectile = true; // 弾として扱う
				
				// 重要: タグは Projectile に戻し、GetEntitiesByTag(Player) で誤ってプレイヤーと見なされないようにする
				registry.emplace<TagComponent>(proj, TagType::Projectile);
				registry.emplace<AutoDestroyComponent>(proj).timer = 0.5f; // すぐに消える
				
				// 弾の軌跡用パーティクル（勢いのある水しぶき）
				// 水しぶきの粒子
				auto& pe = registry.emplace<ParticleEmitterComponent>(proj);
				pe.emitter.params.name = "ProjTrail";
				pe.emitter.params.emitRate = 400; // 密度を倍増
				pe.emitter.params.lifeTime = 0.3f; // 少し長く残る
				pe.emitter.params.startColor = { 0.8f, 1.0f, 1.0f, 0.8f }; 
				pe.emitter.params.endColor = { 0.2f, 0.8f, 1.0f, 0.0f }; 
				pe.emitter.params.startSize = { 1.5f, 1.5f, 1.5f }; // パーティクルも巨大化
				pe.emitter.params.endSize = { 0.2f, 0.2f, 0.2f };
				pe.emitter.params.useBillboard = true; 
				pe.emitter.params.isAdditive = true; 
				pe.emitter.params.texturePath = "Resources/Textures/white1x1.png"; 
				pe.emitter.params.startVelocity = { -ps.dir.x * 10.0f, 2.0f, -ps.dir.z * 10.0f }; // 激しく散る
				pe.emitter.params.velocityVariance = { 2.0f, 2.0f, 2.0f }; // 散らばり具合をアップ
			}
		}
		// 生成待ちを空にする
		pendingProjectiles_.clear();

		// 缶の弾・酸の水たまり・氷の跡・磁石のエフェクトを作る
		SpawnCanProjectiles(registry, ctx);
		SpawnAcidPools(registry, ctx);
		SpawnIceTrails(registry, ctx);
		SpawnMagnetEffects(registry, ctx);

		// --- 爆発エフェクトの遅延生成 ---
		// 爆発: ヒットエフェクトと、0.5 秒だけの範囲の攻撃判定
		for (const auto& exp : pendingExplosions_) {
			if (ctx.scene) {
				auto effectEntity = ctx.scene->CreateEntity("ExplosionEffectVisual");
				auto& eTc = registry.get<TransformComponent>(effectEntity);
				eTc.translate = exp.pos;
				
				auto& sc = registry.emplace<ScriptComponent>(effectEntity);
				ScriptEntry entry;
				entry.scriptPath = "HitEffectScript";
				entry.parameterData = "isExplosion=1";
				sc.scripts.push_back(entry);
				
				// 爆発の攻撃判定
				auto hitboxEntity = ctx.scene->CreateEntity("ExplosionHitbox");
				registry.emplace<TagComponent>(hitboxEntity, TagType::Player); 
				auto& hTc = registry.get<TransformComponent>(hitboxEntity);
				hTc.translate = exp.pos;

				auto& hb = registry.emplace<HitboxComponent>(hitboxEntity);
				hb.isActive = true;
				hb.size = {8.0f, 4.0f, 8.0f};
				hb.center = {0, 1.0f, 0};
				hb.damage = 40.0f;
				hb.tag = TagType::Player;
				hb.isProjectile = false;

				registry.emplace<AutoDestroyComponent>(hitboxEntity).timer = 0.5f;
			}
		}
		pendingExplosions_.clear();

		// --- ミストの遅延生成 ---
		// ミスト: 一度に 40 粒を出して消える
		for (const auto& mist : pendingMists_) {
			if (ctx.scene) {
				entt::entity m = ctx.scene->CreateEntity("MistEffect");
				auto& mtc = registry.get<TransformComponent>(m);
				mtc.translate = mist.pos;
				
				auto& pe = registry.emplace<ParticleEmitterComponent>(m);
				pe.emitter.params.name = "Mist";
				pe.emitter.params.emitRate = 0;       // 自動放出なし
				pe.emitter.params.burstCount = 40;    // 一気に放出
				pe.emitter.params.lifeTime = 0.6f;
				pe.emitter.params.startColor = { 0.8f, 0.9f, 1.0f, 0.5f };
				pe.emitter.params.endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
				pe.emitter.params.startSize = { 0.2f, 0.2f, 0.2f };
				pe.emitter.params.endSize = { 1.5f, 1.5f, 1.5f };
				pe.emitter.params.startVelocity = { mist.dir.x * 3.0f, 0.5f, mist.dir.z * 3.0f };
				pe.emitter.params.velocityVariance = { 1.0f, 0.5f, 1.0f };
				registry.emplace<AutoDestroyComponent>(m).timer = 0.8f; // パーティクル消滅まで待つ
			}
		}
		pendingMists_.clear();

		// 炭酸ガス: 強さに応じた数の粒を噴射の向きへ出す
		for (const auto& gas : pendingSodaGas_) {
			if (ctx.scene) {
				entt::entity g = ctx.scene->CreateEntity("SodaGas");
				auto& gtc = registry.get<TransformComponent>(g);
				gtc.translate = gas.pos;

				auto& pe = registry.emplace<ParticleEmitterComponent>(g);
				pe.emitter.params.name = "SodaGas";
				pe.emitter.params.emitRate = 0;
				pe.emitter.params.burstCount = static_cast<int>(std::clamp(18.0f * gas.intensity, 8.0f, 36.0f));
				pe.emitter.params.lifeTime = 0.35f;
				pe.emitter.params.startColor = { 0.75f, 1.0f, 0.95f, 0.7f };
				pe.emitter.params.endColor = { 0.2f, 0.9f, 1.0f, 0.0f };
				pe.emitter.params.startSize = { 0.18f, 0.18f, 0.18f };
				pe.emitter.params.endSize = { 0.9f, 0.9f, 0.9f };
				pe.emitter.params.startVelocity = { gas.dir.x * 8.0f, gas.dir.y * 8.0f, gas.dir.z * 8.0f };
				pe.emitter.params.velocityVariance = { 1.5f, 1.0f, 1.5f };
				pe.emitter.params.acceleration = { 0.0f, 1.0f, 0.0f };
				pe.emitter.params.isAdditive = true;
				pe.emitter.params.shaderName = "SoftParticleAdditive";
				pe.emitter.params.texturePath = "Resources/Textures/ball.png";
				registry.emplace<AutoDestroyComponent>(g).timer = 0.5f;
			}
		}
		pendingSodaGas_.clear();

		// --- 炎ブレスの遅延生成 (火炎放射エフェクト) ---
		// 炎: 前方へ炎の粒を出し、さらに前に短い攻撃判定を置く
		for (const auto& fire : pendingFireBreaths_) {
			if (ctx.scene) {
				// パーティクル発生地点 (プレイヤーの少し前方、高さ1m)
				entt::entity flame = ctx.scene->CreateEntity("FlameEffect");
				auto& ftc = registry.get<TransformComponent>(flame);
				ftc.translate = { fire.pos.x + fire.dir.x * 1.5f, fire.pos.y + 1.0f, fire.pos.z + fire.dir.z * 1.5f };
				
				// 炎の粒子
				auto& pe = registry.emplace<ParticleEmitterComponent>(flame);
				pe.emitter.params.name = "Flamethrower";
				pe.emitter.params.emitRate = 60;       
				pe.emitter.params.burstCount = 0;      
				pe.emitter.params.lifeTime = 0.4f;     // 寿命を短くして炎っぽく
				
				// 加算ブレンドでは色が蓄積されるため、最初は明るい黄色〜オレンジ、最後は赤
				pe.emitter.params.startColor = { 1.0f, 0.8f, 0.2f, 1.0f }; 
				pe.emitter.params.endColor = { 1.0f, 0.1f, 0.0f, 0.0f };   
				
				// サイズは中くらいから大きく広がる
				pe.emitter.params.startSize = { 1.5f, 1.5f, 1.5f }; 
				pe.emitter.params.endSize = { 4.0f, 4.0f, 4.0f }; 
				
				pe.emitter.params.startVelocity = { fire.dir.x * 10.0f, 0.0f, fire.dir.z * 10.0f }; 
				pe.emitter.params.velocityVariance = { 2.0f, 1.0f, 2.0f }; 
				
				pe.emitter.params.acceleration = { 0.0f, 2.0f, 0.0f }; // 少し上に昇るようにする
				
				// 黒い背景を透明にするため加算ブレンドにする
				pe.emitter.params.isAdditive = true;
				pe.emitter.params.shaderName = "SoftParticleAdditive";
				pe.emitter.params.texturePath = "Resources/Textures/ball.png"; 
				
				registry.emplace<AutoDestroyComponent>(flame).timer = 0.5f;

				// 火炎の当たり判定 (発生地点から前方に広い空間)
				auto hitboxEntity = ctx.scene->CreateEntity("FireHitbox");
				registry.emplace<TagComponent>(hitboxEntity, TagType::Player); 
				auto& hTc = registry.get<TransformComponent>(hitboxEntity);
				// Hitboxはパーティクルの発生地点よりさらに少し前に置く
				hTc.translate = { ftc.translate.x + fire.dir.x * 2.0f, ftc.translate.y, ftc.translate.z + fire.dir.z * 2.0f };

				// 炎の攻撃判定
				auto& hb = registry.emplace<HitboxComponent>(hitboxEntity);
				hb.isActive = true;
				hb.size = {5.0f, 4.0f, 5.0f}; // 広範囲をカバー
				hb.center = {0, 0, 0};
				hb.damage = 1.5f; 
				hb.tag = TagType::Player;
				hb.isProjectile = false; 

				registry.emplace<AutoDestroyComponent>(hitboxEntity).timer = 0.1f;
			}
		}
		pendingFireBreaths_.clear();

		// 分身を生成する
		// 分身: 元の位置に置き、黄色の流体を出して、敵の狙いを引きつける
		for (const auto& decoy : pendingDecoys_) {
			if (ctx.scene) {
				entt::entity d = ctx.scene->CreateEntity("Decoy");
				auto& dtc = registry.get<TransformComponent>(d);
				dtc.translate = decoy.pos;
				dtc.rotate = decoy.rot;
				dtc.scale = decoy.scale;

				// デコイの見た目（1回だけ大量に流体パーティクルを放出する。毎フレーム出すと重くなるため）
				if (ctx.renderer) {
					Engine::Vector4 decoyColor = { 1.0f, 0.9f, 0.1f, 1.0f }; // 黄色
					// type = 2.0f として放出し、デコイ用の引力コアに集まるようにする
					ctx.renderer->EmitGPUFluid({ decoy.pos.x, decoy.pos.y + 1.0f, decoy.pos.z }, { 0, -2, 0 }, decoyColor, 2000, 2.0f);
				}
				// 敵に狙わせるために Player タグをつける
				registry.emplace<TagComponent>(d, TagType::Player);
				
				// デコイが無敵で数秒耐えるようにする
				auto& hc = registry.emplace<HealthComponent>(d);
				hc.SetHp(9999.0f);
				hc.SetMaxHp(9999.0f);
				
				// 5秒で自動消滅
				registry.emplace<AutoDestroyComponent>(d).timer = 5.0f;

				// デコイオーラエフェクト
				auto& pe = registry.emplace<ParticleEmitterComponent>(d);
				pe.emitter.params.name = "DecoyAura";
				pe.emitter.params.emitRate = 40;
				pe.emitter.params.lifeTime = 0.6f;
				pe.emitter.params.startColor = { 1.0f, 0.9f, 0.2f, 0.8f };
				pe.emitter.params.endColor = { 1.0f, 1.0f, 0.0f, 0.0f };
				pe.emitter.params.startSize = { 1.5f, 1.5f, 1.5f };
				pe.emitter.params.endSize = { 0.1f, 0.1f, 0.1f };
				pe.emitter.params.isAdditive = true;
				pe.emitter.params.texturePath = "Resources/Textures/ball.png";
			}
		}
		pendingDecoys_.clear();

		// ワープのエフェクトを生成する
		// ワープの火花
		for (const auto& wPos : pendingWarpEffects_) {
			if (ctx.scene) {
				entt::entity m = ctx.scene->CreateEntity("WarpEffect");
				auto& mtc = registry.get<TransformComponent>(m);
				mtc.translate = wPos;
				
				auto& pe = registry.emplace<ParticleEmitterComponent>(m);
				pe.emitter.params.name = "WarpSparks";
				pe.emitter.params.emitRate = 0;       
				pe.emitter.params.burstCount = 50;    
				pe.emitter.params.lifeTime = 0.5f;
				pe.emitter.params.startColor = { 1.0f, 0.9f, 0.2f, 1.0f };
				pe.emitter.params.endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
				pe.emitter.params.startSize = { 0.3f, 0.3f, 0.3f };
				pe.emitter.params.endSize = { 1.5f, 1.5f, 1.5f };
				pe.emitter.params.velocityVariance = { 5.0f, 5.0f, 5.0f };
				pe.emitter.params.isAdditive = true;
				registry.emplace<AutoDestroyComponent>(m).timer = 0.6f;
			}
		}
		pendingWarpEffects_.clear();

		// 後回しにしていた缶の切り替えを行う (反復の無効化を防ぐため)
		// まだ有効なプレイヤーの缶を切り替える
		for (const auto& change : pendingCanChanges_) {
			if (registry.valid(change.playerEntity) && registry.all_of<PlayerActionComponent, TransformComponent>(change.playerEntity)) {
				auto& pa = registry.get<PlayerActionComponent>(change.playerEntity);
				auto& tc = registry.get<TransformComponent>(change.playerEntity);
				ChangeCan(registry, change.playerEntity, pa, tc, change.newCan, ctx);
			}
		}
		pendingCanChanges_.clear();
	}

	// PLAY 開始時: 全プレイヤーの行動の状態を初期化する
	void Reset(entt::registry& registry) override {

		// 全プレイヤーの入力・状態・各タイマー・炭酸・液状化・缶の値を初期化する
		auto view = registry.view<PlayerActionComponent>();
		for (auto entity : view) {
			auto& pa = registry.get<PlayerActionComponent>(entity);
			pa.previousAttack = pa.previousHammer = pa.previousDodge = false;
			ChangeState(registry, entity, PlayerActionState::Idle);
			pa.chargeTimer = 0.0f;
			pa.hitStopTimer = 0.0f;
			pa.dodgeCooldown = 0.0f;
			pa.sodaGas = 100.0f;
			pa.sodaAimTimer = 0.0f;
			pa.sodaEmitTimer = 0.0f;
			pa.sodaAiming = false;
			pa.sodaDriftVelocity = {0.0f, 0.0f, 0.0f};
			pa.liquefyBaseSpeed = 0.0f;
			pa.liquefySpeedApplied = false;
			pa.liquefyAnchorPos = {0.0f, 0.0f, 0.0f};
			pa.liquefyFlowDir = {0.0f, 0.0f, 1.0f};
			pa.liquefyInitialFlowSpeed = 0.0f;
			pa.liquefyLocked = false;
			pa.canFollowInitialized = false;
			pa.canRevealTimer = 1.0f;
			pa.canPrimaryCooldown = 0.0f;
			pa.canSecondaryCooldown = 0.0f;
			pa.iceTrailEmitTimer = 0.0f;
			if (pa.iceSlideApplied) RestoreIceSlide(registry, entity, pa);
			pa.magnetEffectTimer = 0.0f;
		}
	}

private:
	// 後でまとめて生成するもののデータ (水の弾・缶の弾・酸の水たまり・氷の跡・磁石・ミスト・炭酸ガス・炎・爆発・分身)
	struct ProjectileSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rot;
		DirectX::XMFLOAT3 dir;
	};
	// 生成待ちの一覧
	std::vector<ProjectileSpawnData> pendingProjectiles_;

	struct CanProjectileSpawnData {
		CanType type = CanType::None;
		DirectX::XMFLOAT3 pos = {0, 0, 0};
		DirectX::XMFLOAT3 dir = {0, 0, 1};
	};
	std::vector<CanProjectileSpawnData> pendingCanProjectiles_;

	struct AcidPoolSpawnData {
		DirectX::XMFLOAT3 pos = {0, 0, 0};
	};
	std::vector<AcidPoolSpawnData> pendingAcidPools_;

	struct IceTrailSpawnData {
		DirectX::XMFLOAT3 pos = {0, 0, 0};
		float yaw = 0.0f;
	};
	std::vector<IceTrailSpawnData> pendingIceTrails_;

	struct MagnetEffectSpawnData {
		DirectX::XMFLOAT3 pos = {0, 0, 0};
		bool pulse = false;
	};
	std::vector<MagnetEffectSpawnData> pendingMagnetEffects_;

	struct MistSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 dir;
		float intensity = 1.0f;
	};
	std::vector<MistSpawnData> pendingMists_;

	struct SodaGasSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 dir;
		float intensity = 1.0f;
	};
	std::vector<SodaGasSpawnData> pendingSodaGas_;

	struct FireBreathSpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 dir;
	};
	std::vector<FireBreathSpawnData> pendingFireBreaths_;

	struct ExplosionSpawnData {
		DirectX::XMFLOAT3 pos;
	};
	std::vector<ExplosionSpawnData> pendingExplosions_;

	// 分身の生成データ
	struct DecoySpawnData {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rot;
		DirectX::XMFLOAT3 scale;
	};
	std::vector<DecoySpawnData> pendingDecoys_;
	std::vector<DirectX::XMFLOAT3> pendingWarpEffects_;

	// 後で行う缶の切り替え
	struct PendingCanChange {
		entt::entity playerEntity;
		CanType newCan;
	};
	std::vector<PendingCanChange> pendingCanChanges_;


	// 液状化を始める: 地面に固定し、カメラの向きへ流れる向きと速さを決める
	void BeginLiquefy(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
		// 液状化の速さを戻し、今の位置を固定の位置にする
		RestoreLiquefySpeed(registry, entity, pa);
		pa.liquefyAnchorPos = tc.translate;
		// 地面より下にならないよう固定の位置を持ち上げ、接地扱いにする
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			if (ctx.scene) {
				float groundHeight = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
				if (groundHeight > -5000.0f) {
					pa.liquefyAnchorPos.y = std::max(pa.liquefyAnchorPos.y, groundHeight + cm->heightOffset);
				}
			}
			cm->isGrounded = true;
		}
		pa.liquefyLocked = true;

		// 流れる向き (カメラの前) と速さ
		DirectX::XMFLOAT3 flowDir = GetCameraForward(tc, ctx);
		float flowSpeed = 0.0f;
		float inputLen = std::sqrt(pi.moveDir.x * pi.moveDir.x + pi.moveDir.y * pi.moveDir.y);
		// 入力があれば、その向きに移動の速さで流れる
		if (inputLen > 0.01f) {
			flowDir = GetCameraMoveDirection(pi, ctx, tc);
			float moveSpeed = 0.0f;
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				moveSpeed = cm->speed;
			}
			flowSpeed = moveSpeed * std::min(1.0f, inputLen);
			tc.rotate.y = std::atan2(flowDir.x, flowDir.z);
		// 入力が無ければ今の速度の向きと速さで流れる
		} else if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
			flowSpeed = std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z);
			if (flowSpeed > 0.01f) {
				flowDir = {rb->velocity.x / flowSpeed, 0.0f, rb->velocity.z / flowSpeed};
			}
		}

		// 流れの初速 (上限 14) を記録し、入力と速度を消す
		pa.liquefyFlowDir = flowDir;
		pa.liquefyInitialFlowSpeed = std::min(flowSpeed, 14.0f);
		pi.moveDir = {0.0f, 0.0f};
		pi.jumpRequested = false;
		if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
			rb->velocity.x = 0.0f;
			rb->velocity.y = 0.0f;
			rb->velocity.z = 0.0f;
		}
	}

	// 液状化を終える
	static void EndLiquefy(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		RestoreLiquefySpeed(registry, entity, pa);
		pa.liquefyLocked = false;
		pa.liquefyInitialFlowSpeed = 0.0f;
	}

	// 液状化中は移動の速さを変える (元の速さを覚えておく)
	void ApplyLiquefySpeed(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		// 最初だけ元の速さを覚え、速くする
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			if (!pa.liquefySpeedApplied) {
				pa.liquefyBaseSpeed = cm->speed;
				pa.liquefySpeedApplied = true;
			}
			cm->speed = std::max(pa.liquefyBaseSpeed * 1.75f, pa.liquefyBaseSpeed + 4.0f);
		}
	}

	// 液状化で変えた移動の速さを元に戻す
	static void RestoreLiquefySpeed(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (pa.liquefySpeedApplied) {
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				cm->speed = pa.liquefyBaseSpeed;
			}
			pa.liquefyBaseSpeed = 0.0f;
			pa.liquefySpeedApplied = false;
		}
	}

	// プレイヤーの前方向 (Y 軸の回転から求める)
	DirectX::XMFLOAT3 GetCameraForward(const TransformComponent& tc, GameContext& ctx) const {
		float yaw = tc.rotate.y;
		// カメラがあればカメラの向き
		if (ctx.camera) {
			yaw = ctx.camera->Rotation().y;
		}
		return { std::sin(yaw), 0.0f, std::cos(yaw) };
	}

	// 入力をカメラ基準のワールドの移動方向に直す
	DirectX::XMFLOAT3 GetCameraMoveDirection(const PlayerInputComponent& pi, GameContext& ctx, const TransformComponent& tc) const {
		// 入力を正規化する (無ければカメラの前)
		float ix = pi.moveDir.x;
		float iz = pi.moveDir.y;
		float len = std::sqrt(ix * ix + iz * iz);
		if (len <= 0.001f) {
			return GetCameraForward(tc, ctx);
		}
		ix /= len;
		iz /= len;

		// カメラのヨーで回す
		float yaw = tc.rotate.y;
		if (ctx.camera) {
			yaw = ctx.camera->Rotation().y;
		}
		float cy = std::cos(yaw);
		float sy = std::sin(yaw);
		return {
			ix * cy + iz * sy,
			0.0f,
			-ix * sy + iz * cy
		};
	}

	// 水平の速さを上限までに抑える
	void ClampHorizontalVelocity(DirectX::XMFLOAT3& velocity, float maxSpeed) const {
		float speedSq = velocity.x * velocity.x + velocity.z * velocity.z;
		float maxSq = maxSpeed * maxSpeed;
		if (speedSq > maxSq) {
			float s = maxSpeed / std::sqrt(speedSq);
			velocity.x *= s;
			velocity.z *= s;
		}
	}

	// 炭酸ガスの噴射を生成待ちに積む
	void QueueSodaGas(const TransformComponent& tc, const DirectX::XMFLOAT3& exhaustDir, float intensity) {
		pendingSodaGas_.push_back({
			{ tc.translate.x, tc.translate.y + 0.25f, tc.translate.z },
			exhaustDir,
			intensity
		});
	}

	// 炭酸の缶: 攻撃でガスを噴いて加速し、右クリックで狙って離すと溜めた分だけ飛び出す
	void HandleSodaCan(
		entt::registry& registry,
		entt::entity entity,
		PlayerActionComponent& pa,
		const PlayerInputComponent& pi,
		TransformComponent& tc,
		GameContext& ctx,
		bool boostInput,
		bool aimInput,
		bool aimReleased,
		float& targetCamOffset) {
		// 漂う速度で動かし、減衰させる
		if (std::abs(pa.sodaDriftVelocity.x) > 0.01f || std::abs(pa.sodaDriftVelocity.z) > 0.01f) {
			tc.translate.x += pa.sodaDriftVelocity.x * ctx.dt;
			tc.translate.z += pa.sodaDriftVelocity.z * ctx.dt;
			float damp = std::max(0.0f, 1.0f - 3.8f * ctx.dt);
			pa.sodaDriftVelocity.x *= damp;
			pa.sodaDriftVelocity.z *= damp;
		}

		// 待機中でメニューを開いていなければガスを使える
		bool canUseGas = pa.state == PlayerActionState::Idle && !pi.isRadialMenuOpen;
		bool usingGas = false;
		pa.sodaEmitTimer += ctx.dt;

		// 使えないときは狙いを解除してガスを回復する
		if (!canUseGas) {
			pa.sodaAiming = false;
			pa.sodaAimTimer = 0.0f;
			pa.sodaGas = std::min(100.0f, pa.sodaGas + 10.0f * ctx.dt);
			return;
		}

		// 剛体とキャラクター移動
		auto* rb = registry.try_get<RigidbodyComponent>(entity);
		auto* cm = registry.try_get<CharacterMovementComponent>(entity);

		// 噴射: 上へ浮かせ、移動の向きへ漂い、ガスを使う
		if (boostInput && pa.sodaGas > 0.0f && rb && cm) {
			DirectX::XMFLOAT3 moveDir = GetCameraMoveDirection(pi, ctx, tc);
			rb->velocity.y = std::min(8.0f, std::max(rb->velocity.y + 18.0f * ctx.dt, 2.5f));
			cm->isGrounded = false;
			pa.sodaDriftVelocity.x += moveDir.x * 7.0f * ctx.dt;
			pa.sodaDriftVelocity.z += moveDir.z * 7.0f * ctx.dt;
			ClampHorizontalVelocity(pa.sodaDriftVelocity, 7.5f);
			pa.sodaGas = std::max(0.0f, pa.sodaGas - 24.0f * ctx.dt);
			targetCamOffset = std::max(targetCamOffset, 2.0f);
			usingGas = true;

			// 一定間隔でガスの粒を出す
			if (pa.sodaEmitTimer >= 0.07f) {
				QueueSodaGas(tc, { -moveDir.x * 0.35f, -1.0f, -moveDir.z * 0.35f }, 0.9f);
				pa.sodaEmitTimer = 0.0f;
			}
		}

		// 狙い: カメラの向きを向き、溜めて、カメラを引く
		if (aimInput) {
			DirectX::XMFLOAT3 forward = GetCameraForward(tc, ctx);
			pa.sodaAiming = true;
			pa.sodaAimTimer = std::min(pa.sodaAimTimer + ctx.dt, 1.2f);
			tc.rotate.y = std::atan2(forward.x, forward.z);
			targetCamOffset = std::max(targetCamOffset, 3.5f);

			if (pa.sodaEmitTimer >= 0.12f) {
				QueueSodaGas(tc, { -forward.x * 0.3f, -0.4f, -forward.z * 0.3f }, 0.45f);
				pa.sodaEmitTimer = 0.0f;
			}
		// 狙いを離したら、溜めとガスの量に応じた強さで前上へ飛び出す
		} else if (pa.sodaAiming) {
			if (aimReleased && pa.sodaGas > 8.0f && rb && cm) {
				DirectX::XMFLOAT3 forward = GetCameraForward(tc, ctx);
				float charge = std::clamp(pa.sodaAimTimer / 1.1f, 0.35f, 1.0f);
				float cost = 28.0f + 22.0f * charge;
				float gasPower = std::clamp(pa.sodaGas / cost, 0.35f, 1.0f);
				float power = charge * gasPower;

				rb->velocity.y = std::max(rb->velocity.y, 12.0f + 8.0f * power);
				cm->isGrounded = false;
				pa.sodaDriftVelocity.x += forward.x * (12.0f + 10.0f * power);
				pa.sodaDriftVelocity.z += forward.z * (12.0f + 10.0f * power);
				ClampHorizontalVelocity(pa.sodaDriftVelocity, 18.0f);
				pa.sodaGas = std::max(0.0f, pa.sodaGas - cost);
				QueueSodaGas(tc, { -forward.x * 0.9f, -0.7f, -forward.z * 0.9f }, 1.8f);
				if (ctx.camera) ctx.camera->StartShake(0.18f, 0.25f);
				usingGas = true;
			}
			pa.sodaAiming = false;
			pa.sodaAimTimer = 0.0f;
		}

		// 使っていないときはガスを回復する
		if (!usingGas && !boostInput && !aimInput) {
			pa.sodaGas = std::min(100.0f, pa.sodaGas + 12.0f * ctx.dt);
		}
	}

	// 能力を出す方向 (ロックオン中は敵の方、そうでなければ前方)
	DirectX::XMFLOAT3 GetAbilityDirection(entt::registry& registry, const PlayerInputComponent& pi,
		const TransformComponent& tc, GameContext& ctx) const {
		// ロックオン中の敵への水平の向き
		if (pi.lockedEnemy != entt::null && registry.valid(pi.lockedEnemy) && registry.all_of<TransformComponent>(pi.lockedEnemy)) {
			const auto& targetTc = registry.get<TransformComponent>(pi.lockedEnemy);
			float dx = targetTc.translate.x - tc.translate.x;
			float dz = targetTc.translate.z - tc.translate.z;
			float len = std::sqrt(dx * dx + dz * dz);
			if (len > 0.001f) return {dx / len, 0.0f, dz / len};
		}
		return GetCameraForward(tc, ctx);
	}

	// 氷の缶の滑走: 移動を速くする (元の速さを覚えておく)
	void ApplyIceSlide(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
			if (!pa.iceSlideApplied) {
				pa.iceSlideBaseSpeed = cm->speed;
				pa.iceSlideApplied = true;
			}
			cm->speed = (std::max)(pa.iceSlideBaseSpeed * 2.1f, pa.iceSlideBaseSpeed + 7.0f);
		}
	}

	// 氷の滑走で変えた移動の速さを元に戻す
	void RestoreIceSlide(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa) {
		if (!pa.iceSlideApplied) return;
		if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) cm->speed = pa.iceSlideBaseSpeed;
		pa.iceSlideBaseSpeed = 0.0f;
		pa.iceSlideApplied = false;
	}

	// 氷・磁石・酸・泡の缶: 攻撃で弾 (磁石は引き寄せ)、右クリックで缶ごとの補助能力
	void HandleAdvancedCan(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa,
		const PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx,
		bool attackPressed, bool secondaryInput, bool secondaryPressed) {
		// メニュー中・待機以外なら滑走を戻して何もしない
		if (pi.isRadialMenuOpen || pa.state != PlayerActionState::Idle) {
			if (pa.iceSlideApplied) RestoreIceSlide(registry, entity, pa);
			return;
		}

		// 能力の向き
		const DirectX::XMFLOAT3 dir = GetAbilityDirection(registry, pi, tc, ctx);
		// 攻撃: 磁石は周りを押し返す波、それ以外は弾を撃つ
		if (attackPressed && pa.canPrimaryCooldown <= 0.0f) {
			if (pa.currentCan == CanType::Magnet) {
				pendingMagnetEffects_.push_back({{tc.translate.x, tc.translate.y + 0.4f, tc.translate.z}, true});
				pa.canPrimaryCooldown = 1.1f;
				if (ctx.camera) ctx.camera->StartShake(0.12f, 0.18f);
			} else {
				pendingCanProjectiles_.push_back({
					pa.currentCan,
					{tc.translate.x + dir.x * 1.5f, tc.translate.y + 0.8f, tc.translate.z + dir.z * 1.5f},
					dir
				});
				pa.canPrimaryCooldown = pa.currentCan == CanType::Bubble ? 0.75f : 0.42f;
			}
		}

		// 氷: 右クリックの間は滑走し、足元に氷の跡を残す
		if (pa.currentCan == CanType::Ice) {
			if (secondaryInput) {
				ApplyIceSlide(registry, entity, pa);
				pa.iceTrailEmitTimer -= ctx.dt;
				if (pa.iceTrailEmitTimer <= 0.0f) {
					// 跡の高さは地面に合わせる
					float trailY = tc.translate.y - 0.72f;
					if (ctx.scene) {
						const float groundY = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 2.5f, static_cast<uint32_t>(entity));
						if (groundY > -5000.0f) trailY = groundY + 0.06f;
					}
					pendingIceTrails_.push_back({{tc.translate.x, trailY, tc.translate.z}, tc.rotate.y});
					pa.iceTrailEmitTimer = 0.13f;
				}
			} else {
				RestoreIceSlide(registry, entity, pa);
			}
		// 磁石: 右クリックの間は周りの敵を引き寄せる (ボスは遅い)
		} else if (pa.currentCan == CanType::Magnet) {
			if (secondaryInput) {
				pa.magnetEffectTimer -= ctx.dt;
				if (pa.magnetEffectTimer <= 0.0f) {
					pendingMagnetEffects_.push_back({{tc.translate.x, tc.translate.y + 0.5f, tc.translate.z}, false});
					pa.magnetEffectTimer = 0.16f;
				}

				auto enemies = registry.view<TagComponent, TransformComponent, HealthComponent>();
				for (auto target : enemies) {
					if (enemies.get<TagComponent>(target).tag != TagType::Enemy) continue;
					auto& health = enemies.get<HealthComponent>(target);
					if (health.IsDead() || !health.enabled) continue;
					auto& targetTc = enemies.get<TransformComponent>(target);
					float dx = tc.translate.x - targetTc.translate.x;
					float dz = tc.translate.z - targetTc.translate.z;
					float dist = std::sqrt(dx * dx + dz * dz);
					if (dist < 2.5f || dist > 32.0f) continue;
					const float pullSpeed = registry.all_of<BossActionComponent>(target) ? 7.0f : 21.0f;
					targetTc.translate.x += (dx / dist) * pullSpeed * ctx.dt;
					targetTc.translate.z += (dz / dist) * pullSpeed * ctx.dt;
				}
			}
		// 酸: 右クリックで前方に酸の水たまりを作る (待ち時間 5 秒)
		} else if (pa.currentCan == CanType::Acid) {
			if (secondaryPressed && pa.canSecondaryCooldown <= 0.0f) {
				const float poolX = tc.translate.x + dir.x * 4.0f;
				const float poolZ = tc.translate.z + dir.z * 4.0f;
				float poolY = tc.translate.y - 0.7f;
				if (ctx.scene) {
					const float groundY = ctx.scene->GetHeightAt(poolX, poolZ, tc.translate.y + 3.0f, static_cast<uint32_t>(entity));
					if (groundY > -5000.0f) poolY = groundY + 0.08f;
				}
				pendingAcidPools_.push_back({{poolX, poolY, poolZ}});
				pa.canSecondaryCooldown = 5.0f;
			}
		// 泡: 右クリックで 1 回分のシールドを張る (待ち時間 9 秒)
		} else if (pa.currentCan == CanType::Bubble) {
			if (secondaryPressed && pa.canSecondaryCooldown <= 0.0f) {
				auto& shield = registry.get_or_emplace<BubbleShieldComponent>(entity);
				shield.timer = 6.0f;
				shield.charges = 1;
				pa.canSecondaryCooldown = 9.0f;
			}
		}
	}

	// 生成待ちの缶の弾を作る
	void SpawnCanProjectiles(entt::registry& registry, GameContext& ctx) {
		for (const auto& spawn : pendingCanProjectiles_) {
			// 弾のエンティティを作り、向きと大きさを決める
			if (!ctx.scene) continue;
			auto projectile = ctx.scene->CreateEntity("CanProjectile");
			auto& tc = registry.get<TransformComponent>(projectile);
			tc.translate = spawn.pos;
			tc.rotate.y = std::atan2(spawn.dir.x, spawn.dir.z);
			tc.scale = spawn.type == CanType::Bubble ? DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f} : DirectX::XMFLOAT3{0.62f, 0.62f, 0.62f};

			// 見た目 (泡はフォースフィールド、それ以外は光る球)
			auto& mesh = registry.emplace<MeshRendererComponent>(projectile);
			mesh.modelPath = "Resources/Models/player_ball/ball.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.useCubemap = false;
			mesh.shaderName = spawn.type == CanType::Bubble ? "ForceField" : "EmissiveGlow";
			if (spawn.type == CanType::Ice) mesh.color = {0.34f, 0.88f, 1.0f, 0.95f};
			if (spawn.type == CanType::Acid) mesh.color = {0.56f, 1.0f, 0.08f, 0.95f};
			if (spawn.type == CanType::Bubble) mesh.color = {1.0f, 0.38f, 0.82f, 0.72f};
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}

			// 重力の無い剛体で飛ばす (缶ごとの速さ)
			auto& rb = registry.emplace<RigidbodyComponent>(projectile);
			rb.useGravity = false;
			const float speed = spawn.type == CanType::Ice ? 68.0f : spawn.type == CanType::Acid ? 56.0f : 48.0f;
			rb.velocity = {spawn.dir.x * speed, 0.0f, spawn.dir.z * speed};
			// トリガーのコライダー
			auto& collider = registry.emplace<BoxColliderComponent>(projectile);
			collider.size = spawn.type == CanType::Bubble ? DirectX::XMFLOAT3{2.4f, 2.4f, 2.4f} : DirectX::XMFLOAT3{1.6f, 1.6f, 1.6f};
			collider.isTrigger = true;

			// 攻撃判定 (缶ごとのダメージ)
			auto& hitbox = registry.emplace<HitboxComponent>(projectile);
			hitbox.isActive = true;
			hitbox.damage = spawn.type == CanType::Ice ? 12.0f : spawn.type == CanType::Acid ? 8.0f : 4.0f;
			hitbox.tag = TagType::Player;
			hitbox.size = spawn.type == CanType::Bubble ? DirectX::XMFLOAT3{3.4f, 3.4f, 3.4f} : DirectX::XMFLOAT3{2.6f, 3.2f, 2.6f};
			hitbox.isProjectile = true;
			registry.emplace<TagComponent>(projectile, TagType::Projectile);
			// 缶の効果 (種類・長さ・強さ)
			auto& effect = registry.emplace<CanAttackEffectComponent>(projectile);
			effect.canType = spawn.type;
			effect.duration = spawn.type == CanType::Ice ? 1.8f : spawn.type == CanType::Acid ? 6.0f : 5.5f;
			effect.strength = 1.0f;
			registry.emplace<AutoDestroyComponent>(projectile).timer = 1.4f;

			// 軌跡の粒子 (缶ごとの色)
			auto& particles = registry.emplace<ParticleEmitterComponent>(projectile);
			particles.emitter.params.name = "CanProjectileTrail";
			particles.emitter.params.emitRate = 75.0f;
			particles.emitter.params.lifeTime = 0.32f;
			particles.emitter.params.startSize = {0.38f, 0.38f, 0.38f};
			particles.emitter.params.endSize = {0.08f, 0.08f, 0.08f};
			particles.emitter.params.startVelocity = {-spawn.dir.x * 2.0f, 0.2f, -spawn.dir.z * 2.0f};
			particles.emitter.params.velocityVariance = {0.6f, 0.6f, 0.6f};
			particles.emitter.params.isAdditive = true;
			particles.emitter.params.shaderName = "SoftParticleAdditive";
			particles.emitter.params.texturePath = "Resources/Textures/ball.png";
			if (spawn.type == CanType::Ice) {
				particles.emitter.params.startColor = {0.78f, 0.98f, 1.0f, 0.9f};
				particles.emitter.params.endColor = {0.16f, 0.58f, 1.0f, 0.0f};
			} else if (spawn.type == CanType::Acid) {
				particles.emitter.params.startColor = {0.62f, 1.0f, 0.12f, 0.9f};
				particles.emitter.params.endColor = {0.12f, 0.48f, 0.02f, 0.0f};
			} else {
				particles.emitter.params.startColor = {1.0f, 0.52f, 0.88f, 0.8f};
				particles.emitter.params.endColor = {0.32f, 0.78f, 1.0f, 0.0f};
			}
		}
		pendingCanProjectiles_.clear();
	}

	// 生成待ちの酸の水たまりを作る
	void SpawnAcidPools(entt::registry& registry, GameContext& ctx) {
		for (const auto& spawn : pendingAcidPools_) {
			if (!ctx.scene) continue;
			// 水たまりのエンティティ (平たい円柱)
			auto pool = ctx.scene->CreateEntity("AcidPool");
			auto& tc = registry.get<TransformComponent>(pool);
			tc.translate = spawn.pos;
			tc.scale = {6.0f, 0.10f, 6.0f};
			auto& mesh = registry.emplace<MeshRendererComponent>(pool);
			mesh.modelPath = "Resources/Models/Cylinder/cylinder.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.shaderName = "EmissiveGlow";
			mesh.useCubemap = false;
			mesh.color = {0.44f, 1.0f, 0.04f, 0.76f};
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}
			// 酸の水たまりのコンポーネントと寿命
			registry.emplace<TagComponent>(pool, TagType::VFX);
			registry.emplace<AcidPoolComponent>(pool).radius = 6.0f;
			registry.emplace<AutoDestroyComponent>(pool).timer = 6.0f;
			// 緑の点光源
			auto& light = registry.emplace<PointLightComponent>(pool);
			light.color = {0.42f, 1.0f, 0.06f};
			light.intensity = 2.4f;
			light.range = 11.0f;
			// 泡立つ粒子
			auto& particles = registry.emplace<ParticleEmitterComponent>(pool);
			particles.emitter.params.name = "AcidPoolBubbles";
			particles.emitter.params.emitRate = 64.0f;
			particles.emitter.params.shape = Engine::EmissionShape::Sphere;
			particles.emitter.params.shapeRadius = 5.2f;
			particles.emitter.params.lifeTime = 0.6f;
			particles.emitter.params.startColor = {0.62f, 1.0f, 0.08f, 0.8f};
			particles.emitter.params.endColor = {0.10f, 0.42f, 0.02f, 0.0f};
			particles.emitter.params.startSize = {0.28f, 0.28f, 0.28f};
			particles.emitter.params.endSize = {0.85f, 0.85f, 0.85f};
			particles.emitter.params.startVelocity = {0.0f, 1.5f, 0.0f};
			particles.emitter.params.velocityVariance = {0.6f, 0.5f, 0.6f};
			particles.emitter.params.isAdditive = true;
			particles.emitter.params.shaderName = "SoftParticleAdditive";
			particles.emitter.params.texturePath = "Resources/Textures/ball.png";
		}
		pendingAcidPools_.clear();
	}

	// 生成待ちの氷の跡を作る
	void SpawnIceTrails(entt::registry& registry, GameContext& ctx) {
		for (const auto& spawn : pendingIceTrails_) {
			if (!ctx.scene) continue;
			// 氷の跡のエンティティ (薄い板)
			auto trail = ctx.scene->CreateEntity("IceTrail");
			auto& tc = registry.get<TransformComponent>(trail);
			tc.translate = spawn.pos;
			tc.rotate.y = spawn.yaw;
			tc.scale = {1.35f, 0.045f, 2.0f};
			auto& mesh = registry.emplace<MeshRendererComponent>(trail);
			mesh.modelPath = "Resources/Models/cube/cube.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.shaderName = "EmissiveGlow";
			mesh.useCubemap = false;
			mesh.color = {0.34f, 0.86f, 1.0f, 0.52f};
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}
			registry.emplace<TagComponent>(trail, TagType::VFX);
			registry.emplace<AutoDestroyComponent>(trail).timer = 1.2f;
		}
		pendingIceTrails_.clear();
	}

	// 生成待ちの磁石のエフェクトを作る
	void SpawnMagnetEffects(entt::registry& registry, GameContext& ctx) {
		for (const auto& spawn : pendingMagnetEffects_) {
			if (!ctx.scene) continue;
			// 押し返す波か引き寄せのオーラ
			auto effect = ctx.scene->CreateEntity(spawn.pulse ? "MagnetRepulse" : "MagnetPullAura");
			auto& tc = registry.get<TransformComponent>(effect);
			tc.translate = spawn.pos;
			tc.scale = spawn.pulse ? DirectX::XMFLOAT3{15.0f, 0.24f, 15.0f} : DirectX::XMFLOAT3{20.0f, 0.12f, 20.0f};
			auto& mesh = registry.emplace<MeshRendererComponent>(effect);
			mesh.modelPath = "Resources/Models/player_ball/ball.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.shaderName = "ForceField";
			mesh.useCubemap = false;
			mesh.color = spawn.pulse ? DirectX::XMFLOAT4{0.88f, 0.28f, 1.0f, 0.48f} : DirectX::XMFLOAT4{0.38f, 0.55f, 1.0f, 0.35f};
			if (ctx.renderer) {
				mesh.modelHandle = ctx.renderer->LoadObjMesh(mesh.modelPath);
				mesh.textureHandle = ctx.renderer->LoadTexture2D(mesh.texturePath);
			}
			registry.emplace<TagComponent>(effect, spawn.pulse ? TagType::Player : TagType::VFX);
			registry.emplace<AutoDestroyComponent>(effect).timer = spawn.pulse ? 0.24f : 0.20f;
			// 押し返す波は攻撃判定と磁石の効果を持つ
			if (spawn.pulse) {
				auto& hitbox = registry.emplace<HitboxComponent>(effect);
				hitbox.isActive = true;
				hitbox.damage = 6.0f;
				hitbox.tag = TagType::Player;
				hitbox.size = {30.0f, 8.0f, 30.0f};
				auto& canEffect = registry.emplace<CanAttackEffectComponent>(effect);
				canEffect.canType = CanType::Magnet;
				canEffect.strength = 14.0f;
			}
			// 粒子
			auto& particles = registry.emplace<ParticleEmitterComponent>(effect);
			particles.emitter.params.name = spawn.pulse ? "MagnetRepulseParticles" : "MagnetPullParticles";
			particles.emitter.params.emitRate = 0.0f;
			particles.emitter.params.burstCount = spawn.pulse ? 48 : 12;
			particles.emitter.params.lifeTime = 0.35f;
			particles.emitter.params.startColor = {0.86f, 0.32f, 1.0f, 0.9f};
			particles.emitter.params.endColor = {0.22f, 0.56f, 1.0f, 0.0f};
			particles.emitter.params.startSize = {0.22f, 0.22f, 0.22f};
			particles.emitter.params.endSize = {0.72f, 0.72f, 0.72f};
			if (spawn.pulse) {
				particles.emitter.params.velocityVariance = {7.0f, 2.0f, 7.0f};
			} else {
				particles.emitter.params.velocityVariance = {9.0f, 2.0f, 9.0f};
			}
			particles.emitter.params.isAdditive = true;
			particles.emitter.params.shaderName = "SoftParticleAdditive";
			particles.emitter.params.texturePath = "Resources/Textures/ball.png";
		}
		pendingMagnetEffects_.clear();
	}

	// 缶を切り替える
	void ChangeCan(entt::registry& registry, entt::entity playerEntity, PlayerActionComponent& pa, TransformComponent& ptc, CanType newCan, GameContext& ctx) {
		// 前の缶のエンティティを消す
		if (pa.canEntity != entt::null && registry.valid(pa.canEntity)) {
			if (ctx.scene) ctx.scene->DestroyObject(static_cast<uint32_t>(pa.canEntity));
		}
		if (pa.iceSlideApplied) RestoreIceSlide(registry, playerEntity, pa);
		// 今の缶を記録する
		pa.currentCan = newCan;
		pa.canEntity = entt::null;
		pa.canFollowPos = ptc.translate;
		pa.canFollowInitialized = true;

		// 缶があれば、スライムの中に入る缶のエンティティを作る
		if (newCan != CanType::None) {
			if (ctx.scene) {
				entt::entity can = ctx.scene->CreateEntity("PlayerCan");
				pa.canEntity = can;
				
				auto& tc = registry.get<TransformComponent>(can);
				tc.scale = {0.3f, 0.4f, 0.3f};
				tc.translate = ptc.translate;
				tc.translate.y += 0.35f; // 最初のフレームからスライムの中に置く
				
				// 見た目 (円柱)
				auto& mr = registry.emplace<MeshRendererComponent>(can);
				mr.modelPath = "Resources/Models/Cylinder/cylinder.obj";
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.useCubemap = true;
				if (newCan == CanType::Soda) {
					mr.color = {0.25f, 1.0f, 0.85f, 1.0f};
				}

				// 缶が地面判定のレイに当たらないよう VFX のタグを付ける
				registry.emplace<TagComponent>(can, TagType::VFX);
				
				// 缶ごとの色
				if (newCan == CanType::Fire) {
					mr.color = {1.0f, 0.2f, 0.1f, 1.0f}; // 赤
				} else if (newCan == CanType::Water) {
					mr.color = {0.2f, 0.5f, 1.0f, 1.0f}; // 青
				} else if (newCan == CanType::Thunder) {
					mr.color = {1.0f, 0.9f, 0.1f, 1.0f}; // 黄
				} else if (newCan == CanType::Ice) {
					mr.color = {0.35f, 0.88f, 1.0f, 1.0f};
				} else if (newCan == CanType::Magnet) {
					mr.color = {0.78f, 0.28f, 1.0f, 1.0f};
				} else if (newCan == CanType::Acid) {
					mr.color = {0.55f, 1.0f, 0.10f, 1.0f};
				} else if (newCan == CanType::Bubble) {
					mr.color = {1.0f, 0.38f, 0.78f, 1.0f};
				}
				
				if (ctx.renderer) {
					mr.modelHandle = ctx.renderer->LoadObjMesh(mr.modelPath);
					mr.textureHandle = ctx.renderer->LoadTexture2D(mr.texturePath);
				}
			}
		}
	}

	// 回避を始める: 入力の向き (カメラ基準) か、入力が無ければ前方へ
	void StartDodge(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
		// 回避の状態にして、待ち時間を付ける
		ChangeState(registry, entity, PlayerActionState::Dodge, pa.dodgeDuration);
		pa.dodgeCooldown = 0.42f;

		float ix = pi.moveDir.x;
		float iz = pi.moveDir.y;
		float len = std::sqrt(ix * ix + iz * iz);

		// 入力があればカメラ基準の向き
		if (len > 0.01f && ctx.camera) {
			auto camRot = ctx.camera->Rotation();
			float cy = std::cos(camRot.y);
			float sy = std::sin(camRot.y);
			float dx = ix * cy + iz * sy;
			float dz = -ix * sy + iz * cy;
			pa.dodgeDirection = { dx, 0.0f, dz };
		// 入力が無ければ後ろ向き
		} else {
			float facing = tc.rotate.y;
			pa.dodgeDirection = { -std::sin(facing), 0.0f, -std::cos(facing) };
		}
	}
// 各状態の定義 (クラスの中に読み込む)
#include "../States/PlayerActionStates.inl"
};

} // namespace Game
