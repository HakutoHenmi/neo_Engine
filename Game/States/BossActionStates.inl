// BossActionStates.inl
// 旧アクションゲームのボスの状態 (待機・追跡・溜め・攻撃・硬直・怯み・ダウン・死亡・走り)。
// BossActionSystem の中に読み込まれる。各状態は振る舞いだけを共有し、個体のデータは持たない
// 状態の更新に渡す情報 (システム・レジストリ・対象・共有データ・プレイヤーの位置・差分・距離・角度)
struct StateContext {
	BossActionSystem& system;
	entt::registry& registry;
	entt::entity entity;
	GameContext& ctx;
	DirectX::XMFLOAT3 playerPos;
	float dx, dz, dist, targetAngle;
};
// 状態の基底 (更新と、入る・出るときの情報の型を指定する)
using State = Engine::IState<StateContext, EntityStateContext>;

// 攻撃パターンを選んでいるか
static bool HasPattern(const BossActionComponent& boss) { return boss.currentPatternIndex >= 0 && static_cast<size_t>(boss.currentPatternIndex) < boss.patterns.size(); }
// 攻撃をやめる: 攻撃判定を無効にし、ビームがあれば止めて削除を予約する
static void StopAttack(EntityStateContext& frame) {
	auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
	// 自分の攻撃判定を止める
	if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity))
		hitbox->isActive = false;
	// ビームを外す
	const auto beam = boss.currentBeamEntity;
	boss.currentBeamEntity = entt::null;
	if (frame.registry.valid(beam)) {
		// 戦闘の処理が攻撃判定とトランスフォームの格納領域を反復している途中で呼ばれることがあるため、
		// ここではすぐに無効化だけ行い、実際の削除は反復の後に CleanupSystem に任せる
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(beam))
			hitbox->isActive = false;
		if (auto* mesh = frame.registry.try_get<MeshRendererComponent>(beam))
			mesh->enabled = false;
		if (auto* emitter = frame.registry.try_get<ParticleEmitterComponent>(beam))
			emitter->emitter.params.emitRate = 0;
		frame.registry.get_or_emplace<AutoDestroyComponent>(beam).timer = 0;
	}
}

// 指定の種類の攻撃パターンの番号 (無ければ -1)
static int FindPattern(const BossActionComponent& boss, BossAttackType type) {
	for (size_t i = 0; i < boss.patterns.size(); ++i) {
		if (boss.patterns[i].type == type)
			return static_cast<int>(i);
	}
	return -1;
}
// 近距離用の攻撃パターンを無作為に選ぶ (ジャンプとビーム以外。無ければ全体から)
static int ChooseClosePattern(const BossActionComponent& boss) {
	// ジャンプとビーム以外を候補に集める
	std::vector<int> candidates;
	for (size_t i = 0; i < boss.patterns.size(); ++i) {
		const auto type = boss.patterns[i].type;
		if (type != BossAttackType::JumpPress && type != BossAttackType::BeamSweep)
			candidates.push_back(static_cast<int>(i));
	}
	return candidates.empty() ? static_cast<int>(rand() % boss.patterns.size()) : candidates[rand() % candidates.size()];
}

// 待機: プレイヤーを向き、1 秒経ったら次の攻撃を決める
// (20% でビーム、遠ければ走りかジャンプ、近ければ近距離攻撃。届かなければ追跡)
class IdleState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		// 攻撃を止め、攻撃パターンを選んでいない状態にする
		StopAttack(frame);
		frame.registry.get<BossActionComponent>(frame.entity).currentPatternIndex = -1;
	}
	void Update(StateContext& frame) const override {
		// プレイヤーの方へ向く
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		auto& transform = frame.registry.get<TransformComponent>(frame.entity);
		frame.system.SmoothRotate(transform, frame.targetAngle, boss.rotationSpeed, frame.ctx.dt, boss);
		// 1 秒経つまで、またはパターンが無ければ何もしない
		if (boss.stateTimer <= 1 || boss.patterns.empty())
			return;
		// 20% の確率でビームを選ぶ
		const int beam = FindPattern(boss, BossAttackType::BeamSweep);
		if (rand() % 100 < 20 && beam >= 0) {
			boss.currentPatternIndex = beam;
			ChangeState(frame.registry, frame.entity, BossState::WindUp);
			return;
		}
		// 遠ければ半々でジャンプか走り
		if (frame.dist >= 10) {
			const bool jump = rand() % 2 == 0;
			const int selected = jump ? FindPattern(boss, BossAttackType::JumpPress) : -1;
			boss.currentPatternIndex = selected >= 0 ? selected : ChooseClosePattern(boss);
			ChangeState(frame.registry, frame.entity, selected >= 0 ? BossState::WindUp : BossState::Run);
			return;
		}
		// 近ければ近距離の攻撃を選び、射程外なら追跡、射程内なら溜めへ
		boss.currentPatternIndex = ChooseClosePattern(boss);
		ChangeState(frame.registry, frame.entity, frame.dist > boss.patterns[boss.currentPatternIndex].range ? BossState::Chase : BossState::WindUp);
	}
};

// 追跡: 歩いて近づき、攻撃の射程に入ったら溜めへ
class ChaseState final : public State {
public:
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const auto dx = frame.dx;
		const auto dz = frame.dz;
		const auto dist = frame.dist;
		const auto targetAngle = frame.targetAngle;
		// プレイヤーの方へ向く
		frame.system.SmoothRotate(tc, targetAngle, boss.rotationSpeed, ctx.dt, boss);

		// もし歩いて追いかけている最中にプレイヤーに逃げられ、再び距離が離れた場合は走りかジャンプに切り替える
		if (dist >= 10.0f) {
			if (rand() % 2 == 0) {
				// ジャンプ攻撃へ切り替え
				const int jumpIndex = FindPattern(boss, BossAttackType::JumpPress);
				if (jumpIndex != -1) {
					boss.currentPatternIndex = jumpIndex;
					ChangeState(registry, entity, BossState::WindUp);
				}
			} else {
				// 走るモードへ切り替え（攻撃対象の近接パターンは維持したまま）
				ChangeState(registry, entity, BossState::Run);
			}
			return;
		}

		// プレイヤーへ歩いて近づく
		if (dist > 0.5f) {
			float nx = dx / dist;
			float nz = dz / dist;
			tc.translate.x += nx * boss.chaseSpeed * ctx.dt;
			tc.translate.z += nz * boss.chaseSpeed * ctx.dt;
		}
		// 選んだ攻撃の射程に入ったら溜めへ
		if (HasPattern(boss)) {
			if (dist <= boss.patterns[boss.currentPatternIndex].range) {
				ChangeState(registry, entity, BossState::WindUp);
			}
		}
	}
};

// 溜め: 予兆を出し、ゆっくり向きを合わせる。溜め時間が来たら攻撃へ
class WindUpState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		// 選んだ攻撃の溜め時間だけ予兆を出す
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		if (HasPattern(boss))
			ShowWarningEffect(frame.registry, frame.entity, boss.patterns[boss.currentPatternIndex].windUpDuration);
	}
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const auto targetAngle = frame.targetAngle;
		// ゆっくり (通常の 30%) 向きを合わせる
		frame.system.SmoothRotate(tc, targetAngle, boss.rotationSpeed * 0.3f, ctx.dt, boss);
		// 溜め時間が来たら攻撃へ
		if (HasPattern(boss)) {
			auto& p = boss.patterns[boss.currentPatternIndex];
			if (boss.stateTimer >= p.windUpDuration) {
				ChangeState(registry, entity, BossState::Attack);
			}
		}
	}
};

// 攻撃: 攻撃判定を有効にし、攻撃の種類ごとの動きをする。攻撃時間が来たら硬直へ
class AttackState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		// 攻撃判定を有効にし、パターンのダメージにして、当たった相手の記録を消す
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		if (HasPattern(boss)) {
			if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity)) {
				hitbox->isActive = true;
				hitbox->damage = boss.patterns[boss.currentPatternIndex].damage;
				hitbox->hitTargets.clear();
			}
		}
	}
	// 攻撃をやめる
	void Exit(EntityStateContext& frame) const override { StopAttack(frame); }
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const auto playerPos = frame.playerPos;
		const auto dist = frame.dist;
		// ビームの攻撃: ビームのエンティティを作り、左から右へ薙ぎ払う
		if (HasPattern(boss)) {
			auto& p = boss.patterns[boss.currentPatternIndex];
			if (p.type == BossAttackType::BeamSweep) {
				// ビームの生成
				if (boss.currentBeamEntity == entt::null) {
					// ビームのエンティティ (円柱のメッシュ)
					boss.currentBeamEntity = registry.create();
					registry.emplace<TransformComponent>(boss.currentBeamEntity);
					auto& beamMr = registry.emplace<MeshRendererComponent>(boss.currentBeamEntity);
					beamMr.modelPath = "Resources/Models/Cylinder/cylinder.obj";
					beamMr.texturePath = "";
					beamMr.shaderName = "EnergyBeam";
					beamMr.color = {0.1f, 0.8f, 1.0f, 0.8f}; // 青白い発光色

					if (ctx.renderer) {
						beamMr.modelHandle = ctx.renderer->LoadObjMesh(beamMr.modelPath);
					}

					// ビームの攻撃判定
					auto& hb = registry.emplace<HitboxComponent>(boss.currentBeamEntity);
					hb.damage = p.damage;
					hb.isActive = true;
					hb.isProjectile = false;
					hb.tag = TagType::Enemy;
					hb.center = {0.0f, 0.0f, 0.0f}; // 中心をTransformに合わせる

					registry.emplace<TagComponent>(boss.currentBeamEntity, TagType::Enemy);

					// ビームの周りに散らすパーティクルエフェクト
					auto& pe = registry.emplace<ParticleEmitterComponent>(boss.currentBeamEntity);
					pe.emitter.params.name = "BeamAura";
					pe.emitter.params.emitRate = 150.0f;
					pe.emitter.params.lifeTime = 0.4f;
					pe.emitter.params.lifeTimeVariance = 0.2f;
					pe.emitter.params.startSize = {2.0f, 2.0f, 2.0f};
					pe.emitter.params.endSize = {0.0f, 0.0f, 0.0f};
					pe.emitter.params.startColor = {0.1f, 0.8f, 1.0f, 1.0f}; // ビームと同系色
					pe.emitter.params.endColor = {0.0f, 0.2f, 1.0f, 0.0f};
					pe.emitter.params.shape = Engine::EmissionShape::Sphere;
					pe.emitter.params.shapeRadius = 1.0f; // スケールで引き伸ばされるので小さめでOK
					pe.emitter.params.startVelocity = {0.0f, 2.0f, 0.0f};
					pe.emitter.params.velocityVariance = {5.0f, 5.0f, 5.0f};
					pe.emitter.params.shaderName = "SoftParticleAdditive";
				}

				// ビームのスイープ（左から右へ）
				if (boss.currentBeamEntity != entt::null && registry.valid(boss.currentBeamEntity)) {
					auto& beamTc = registry.get<TransformComponent>(boss.currentBeamEntity);

					// ボスの向いている方向ベクトル
					float bossYaw = tc.rotate.y;
					float rightX = std::cos(bossYaw);
					float rightZ = -std::sin(bossYaw);
					float forwardX = std::sin(bossYaw);
					float forwardZ = std::cos(bossYaw);

					// ボスの頭の位置（アニメーションに合わせて微調整）
					// 横ズレや前後ズレがある場合はここの数値を調整します
					float offsetX = 0.0f; // 横方向のズレ（プラスで右、マイナスで左）
					float offsetZ = 3.0f; // 前方向のズレ（頭が前に出ている分）
					float offsetY = 5.5f; // 高さ

					float headX = tc.translate.x + (rightX * offsetX) + (forwardX * offsetZ);
					float headY = tc.translate.y + offsetY;
					float headZ = tc.translate.z + (rightZ * offsetX) + (forwardZ * offsetZ);

					float progress = boss.stateTimer / p.activeDuration;
					float sweepAngle = -DirectX::XM_PIDIV4 + (DirectX::XM_PIDIV2 * progress); // -45度 〜 +45度

					// プレイヤーへの高低差を計算し、ビームのピッチ角（上下角）を求める
					float targetY = playerPos.y + 1.0f; // プレイヤーの胸〜頭あたりを狙う
					float beamDy = targetY - headY;
					float pitchAngle = std::atan2(-beamDy, dist);

					float finalYaw = bossYaw + sweepAngle;
					beamTc.rotate.x = DirectX::XM_PIDIV2 + pitchAngle; // 前方に倒しつつ、プレイヤーの高さへ向ける
					beamTc.rotate.y = finalYaw;
					beamTc.rotate.z = 0.0f;

					beamTc.scale = {1.5f, 50.0f, 1.5f}; // 長さ50

					// シリンダーモデルの中心が原点にある場合、そのまま回転させると後ろにもビームが飛び出してしまう。
					// そのため、ビームの長さの半分だけ向いている方向にずらして、根本が頭になるようにする。
					float beamLengthHalf = 50.0f; // モデルの元のサイズによって変わるため、合わない場合はここを調整
					float dirX = std::sin(finalYaw) * std::cos(pitchAngle);
					float dirY = -std::sin(pitchAngle);
					float dirZ = std::cos(finalYaw) * std::cos(pitchAngle);

					beamTc.translate.x = headX + dirX * beamLengthHalf;
					beamTc.translate.y = headY + dirY * beamLengthHalf;
					beamTc.translate.z = headZ + dirZ * beamLengthHalf;

					// AABB（当たり判定）の更新
					// CombatSystemがY軸回転しか考慮しないため、サイズをワールド座標のバウンディングボックスに合わせる
					if (registry.all_of<HitboxComponent>(boss.currentBeamEntity)) {
						auto& hb = registry.get<HitboxComponent>(boss.currentBeamEntity);
						hb.size.x = 3.0f;                                          // ビームの幅
						hb.size.y = std::abs(dirY * 50.0f) + 3.0f;                 // 上下の厚み
						hb.size.z = std::abs(std::cos(pitchAngle) * 50.0f) + 3.0f; // 水平面での長さ
					}
				}
			// ジャンプの攻撃
			} else if (p.type == BossAttackType::JumpPress) {
				// ジャンプ攻撃中は、離陸直後から前半（0.45）まで前進する
				if (boss.stateTimer > 0.1f && boss.stateTimer < p.activeDuration * 0.45f) {
					float facing = tc.rotate.y;
					tc.translate.x += std::sin(facing) * p.thrustForce * ctx.dt;
					tc.translate.z += std::cos(facing) * p.thrustForce * ctx.dt;
				}
			// それ以外の攻撃
			} else {
				// 通常の攻撃の前半だけ前進させる
				if (boss.stateTimer < p.activeDuration * 0.5f) {
					float facing = tc.rotate.y;
					tc.translate.x += std::sin(facing) * p.thrustForce * ctx.dt;
					tc.translate.z += std::cos(facing) * p.thrustForce * ctx.dt;
				}
			}

			// 攻撃時間が来たら硬直へ
			if (boss.stateTimer >= p.activeDuration) {
				ChangeState(registry, entity, BossState::Cooldown);
			}
		}
	}
};

// 硬直: 攻撃後の隙。時間が来たら待機へ
class CooldownState final : public State {
public:
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		// 選んだ攻撃の回復時間が来たら待機へ
		if (HasPattern(boss)) {
			auto& p = boss.patterns[boss.currentPatternIndex];
			if (boss.stateTimer >= p.recoveryDuration) {
				ChangeState(registry, entity, BossState::Idle);
			}
		}
	}
};

// 怯み (パリィされたとき): 時間が来たら待機へ
class StunnedState final : public State {
public:
	// 攻撃を止める
	void Enter(EntityStateContext& frame) const override { StopAttack(frame); }
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		if (boss.stateTimer >= boss.stunDuration) {
			ChangeState(registry, entity, BossState::Idle);
		}
	}
};

// ダウン
class DownState final : public State {
public:
	// 攻撃を止める
	void Enter(EntityStateContext& frame) const override { StopAttack(frame); }
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		// 部位破壊時などの大ダウン
		if (boss.stateTimer >= boss.stunDuration * 2.0f) {
			ChangeState(registry, entity, BossState::Idle);
		}
	}
};

// 死亡: 何もしない
class DeadState final : public State {
public:
	// 攻撃を止めて、何もしない
	void Enter(EntityStateContext& frame) const override { StopAttack(frame); }
	void Update(StateContext&) const override {}
};

// 走り: 速く近づき、射程に入ったら溜めへ
class RunState final : public State {
public:
	void Update(StateContext& frame) const override {
		// よく使う値
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& boss = frame.registry.get<BossActionComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const auto dx = frame.dx;
		const auto dz = frame.dz;
		const auto dist = frame.dist;
		const auto targetAngle = frame.targetAngle;
		// 通常の 1.5 倍の速さで向きを合わせる
		frame.system.SmoothRotate(tc, targetAngle, boss.rotationSpeed * 1.5f, ctx.dt, boss);
		if (dist > 0.5f) {
			float nx = dx / dist;
			float nz = dz / dist;
			float runSpeed = boss.chaseSpeed * 3.0f; // 走る速度
			tc.translate.x += nx * runSpeed * ctx.dt;
			tc.translate.z += nz * runSpeed * ctx.dt;
		}
		// 選んだ攻撃の射程に入ったら溜めへ
		if (HasPattern(boss)) {
			if (dist <= boss.patterns[boss.currentPatternIndex].range) {
				ChangeState(registry, entity, BossState::WindUp);
			}
		}
	}
};

// 状態の列挙値から状態オブジェクトを返す (範囲外は待機)
static const State& GetState(BossState state) {
	// 各状態のオブジェクト (変更されないので共有できる)
	static const IdleState idle;
	static const ChaseState chase;
	static const WindUpState windup;
	static const AttackState attack;
	static const CooldownState cooldown;
	static const StunnedState stunned;
	static const DownState down;
	static const DeadState dead;
	static const RunState run;
	static const State* const states[] = {&idle, &chase, &windup, &attack, &cooldown, &stunned, &down, &dead, &run};
	static_assert(std::size(states) == static_cast<size_t>(BossState::Run) + 1);
	const auto index = static_cast<size_t>(state);
	return *states[index < std::size(states) ? index : 0];
}
