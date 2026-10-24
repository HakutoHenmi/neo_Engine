// PlayerActionSystem.cpp
// PlayerActionSystem の関数の実装 (宣言とコンポーネントの定義は PlayerActionSystem.h)。
// 状態クラス (PlayerActionStates.inl) が GameScene を使うので、先に読み込む
#include "../Scenes/GameScene.h"
#include "../CanLoadout.h"
#include "PlayerActionSystem.h"

namespace Game {

void PlayerActionSystem::ChangeState(entt::registry& registry, entt::entity entity, PlayerActionState next, float duration) {
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

void PlayerActionSystem::Update(entt::registry& registry, GameContext& ctx) {
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

void PlayerActionSystem::Reset(entt::registry& registry) {

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

void PlayerActionSystem::BeginLiquefy(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
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

DirectX::XMFLOAT3 PlayerActionSystem::GetCameraMoveDirection(const PlayerInputComponent& pi, GameContext& ctx, const TransformComponent& tc) const {
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

void PlayerActionSystem::SpawnCanProjectiles(entt::registry& registry, GameContext& ctx) {
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

void PlayerActionSystem::SpawnAcidPools(entt::registry& registry, GameContext& ctx) {
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

void PlayerActionSystem::SpawnIceTrails(entt::registry& registry, GameContext& ctx) {
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

void PlayerActionSystem::SpawnMagnetEffects(entt::registry& registry, GameContext& ctx) {
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

void PlayerActionSystem::ChangeCan(entt::registry& registry, entt::entity playerEntity, PlayerActionComponent& pa, TransformComponent& ptc, CanType newCan, GameContext& ctx) {
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

void PlayerActionSystem::StartDodge(entt::registry& registry, entt::entity entity, PlayerActionComponent& pa, PlayerInputComponent& pi, TransformComponent& tc, GameContext& ctx) {
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

} // namespace Game
