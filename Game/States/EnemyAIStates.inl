// EnemyAIStates.inl
// 旧アクションゲームの雑魚敵の状態 (待機・追跡・溜め・攻撃・硬直・怯み)。
// EnemyAISystem の中に読み込まれる。各状態は振る舞いだけを共有し、個体のデータは持たない
// 状態の更新に渡す情報 (システム・レジストリ・対象・共有データ・経過時間・プレイヤーへの差分と距離と角度)
struct StateContext {
	EnemyAISystem& system;
	entt::registry& registry;
	entt::entity entity;
	GameContext& ctx;
	float aiDt, dx, dz, dist, targetAngle;
};
// 状態の基底 (更新と、入る・出るときの情報の型を指定する)
using State = Engine::IState<StateContext, EntityStateContext>;

// 待機: プレイヤーを向き、離れたら追跡、攻撃間隔が経って近ければ溜めへ
class IdleState final : public State {
public:
	// 入ったら通常の色に戻す
	void Enter(EntityStateContext& frame) const override { SetNormalColor(frame); }
	void Update(StateContext& frame) const override {
		// よく使う値を取り出す
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const float aiDt = frame.aiDt;
		const float dist = frame.dist;
		const float targetAngle = frame.targetAngle;
		// プレイヤーの方を向く
		frame.system.SmoothRotate(tc, targetAngle, ai.rotationSpeed, aiDt);

		// 待機の時間を進める
		ai.idleTimer += aiDt;

		// 距離が遠い → 追跡
		if (dist > ai.chaseRange && (!ai.hopper || dist <= ai.aggroRange)) {
			ChangeState(registry, entity, EnemyAIState::Chase);
		}
		// 攻撃間隔が経過 & 攻撃範囲内 → 攻撃予備動作
		else if (ai.idleTimer >= ai.attackInterval && dist <= ai.attackRange) {
			ai.idleTimer = 0.0f;
			ChangeState(registry, entity, EnemyAIState::WindUp);
		}
	}
};

// 追跡: プレイヤーへ近づく。跳ねる敵は段差や崖を越えない。攻撃範囲に入ったら待機へ
class ChaseState final : public State {
public:
	void Update(StateContext& frame) const override {
		// よく使う値を取り出す
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const float aiDt = frame.aiDt;
		const float dx = frame.dx;
		const float dz = frame.dz;
		const float dist = frame.dist;
		const float targetAngle = frame.targetAngle;
		// 跳ねる敵は遠く離れすぎたら待機へ戻る
		if (ai.hopper && dist > ai.aggroRange * 1.25f) {
			ChangeState(registry, entity, EnemyAIState::Idle);
			return;
		}
		// プレイヤーの方を向きながら近づく
		frame.system.SmoothRotate(tc, targetAngle, ai.rotationSpeed, aiDt);

		// プレイヤーへの単位ベクトルの向きに、追跡の速さで進む
		if (dist > 0.5f) {
			float moveSpeed = ai.chaseSpeed * aiDt;
			float nx = dx / dist;
			float nz = dz / dist;
			// 跳ねる敵は移動先の地面の高さを調べる
			if (ai.hopper && ctx.scene) {
				const float next = ctx.scene->GetHeightAt(tc.translate.x + nx * moveSpeed, tc.translate.z + nz * moveSpeed, ai.groundY + 3.0f, static_cast<uint32_t>(entity));
				// 跳ねる敵は崖から落ちたり高い壁を登ったりしない
				if (next > -999.0f && std::abs(next - ai.groundY) < 1.1f) {
					tc.translate.x += nx * moveSpeed;
					tc.translate.z += nz * moveSpeed;
					ai.groundY = next;
				}
			// それ以外の敵はそのまま進む
			} else {
				tc.translate.x += nx * moveSpeed;
				tc.translate.z += nz * moveSpeed;
			}
		}

		// 攻撃範囲に入ったらIdleへ
		if (dist <= ai.attackRange) {
			ai.idleTimer = ai.attackInterval * 0.5f; // 少し待ってから攻撃
			ChangeState(registry, entity, EnemyAIState::Idle);
		}
	}
};

// 溜め: 黄色にして予兆を出し、プレイヤーへ向きを合わせる。時間が来たら攻撃へ
class WindUpState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		// 黄色にし、跳ねる敵以外は予兆のスクリプトを付ける
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		const auto& ai = registry.get<EnemyAIComponent>(entity);
		if (auto* mesh = registry.try_get<MeshRendererComponent>(entity))
			mesh->color = {1, .8f, 0, 1};
		if (!ai.hopper)
			ShowWarningEffect(registry, entity, ai.windUpDuration);
	}
	void Update(StateContext& frame) const override {
		// よく使う値を取り出す
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const float aiDt = frame.aiDt;
		const float targetAngle = frame.targetAngle;
		// 予備動作中: プレイヤーをロックオン
		frame.system.SmoothRotate(tc, targetAngle, ai.rotationSpeed * 0.5f, aiDt);

		if (ai.stateTimer >= ai.windUpDuration) {
			// 攻撃開始！
			ChangeState(registry, entity, EnemyAIState::Attack);
		}
	}
};

// 攻撃: 攻撃判定を有効にして前へ突進する。時間が来たら硬直へ
class AttackState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		// 攻撃判定を有効にし、当たった相手の記録を消す
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) {
			hitbox->isActive = true;
			hitbox->hitTargets.clear();
		}
		const auto& ai = registry.get<EnemyAIComponent>(entity);
		// 跳ねる敵以外は攻撃中に大きくなる
		if (auto* transform = registry.try_get<TransformComponent>(entity); transform && !ai.hopper) {
			transform->scale.x = 2;
			transform->scale.z = 2;
		}
		// 跳ねる敵は通常の色
		if (ai.hopper)
			SetNormalColor(frame);
	}
	// 攻撃が終わったら攻撃判定を無効にする
	void Exit(EntityStateContext& frame) const override {
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity))
			hitbox->isActive = false;
	}
	void Update(StateContext& frame) const override {
		// よく使う値を取り出す
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const float aiDt = frame.aiDt;
		// 攻撃中: 前方に少し突進
		{
			// 向いている方へ進む (跳ねる敵は段差を越えない)
			float facing = tc.rotate.y;
			float thrust = (ai.hopper ? 5.0f : 8.0f) * aiDt;
			const float nextX = tc.translate.x + std::sin(facing) * thrust;
			const float nextZ = tc.translate.z + std::cos(facing) * thrust;
			if (ai.hopper && ctx.scene) {
				const float next = ctx.scene->GetHeightAt(nextX, nextZ, ai.groundY + 3.0f, static_cast<uint32_t>(entity));
				if (next > -999.0f && std::abs(next - ai.groundY) < 1.1f) {
					tc.translate.x = nextX;
					tc.translate.z = nextZ;
					ai.groundY = next;
				}
			} else {
				tc.translate.x = nextX;
				tc.translate.z = nextZ;
			}
		}

		if (ai.stateTimer >= ai.attackDuration) {
			// 攻撃終了 → クールダウン
			ChangeState(registry, entity, EnemyAIState::Cooldown);
		}
	}
};

// 硬直: 攻撃後の隙。時間が来たら待機へ
class CooldownState final : public State {
public:
	void Enter(EntityStateContext& frame) const override { SetNormalColor(frame); }
	void Update(StateContext& frame) const override {
		// よく使う値を取り出す
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		// 攻撃後の隙（プレイヤーの反撃チャンス）
		if (ai.stateTimer >= ai.cooldownDuration) {
			ChangeState(registry, entity, EnemyAIState::Idle);
		}
	}
};

// 怯み: 青くして止まる。時間が来たら待機へ
class StunnedState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		// 青くする
		if (auto* mesh = frame.registry.try_get<MeshRendererComponent>(frame.entity))
			mesh->color = {.5f, .5f, 1, 1};
	}
	void Update(StateContext& frame) const override {
		// 時間が来たら待機へ
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		if (ai.stateTimer >= ai.stunDuration) {
			ChangeState(registry, entity, EnemyAIState::Idle);
		}
	}
};

// 状態の列挙値から状態オブジェクトを返す (範囲外は待機)
static const State& GetState(EnemyAIState state) {
	// 各状態のオブジェクト (変更されないので共有できる)
	static const IdleState idle;
	static const ChaseState chase;
	static const WindUpState windup;
	static const AttackState attack;
	static const CooldownState cooldown;
	static const StunnedState stunned;
	// 状態の列挙値の順に並べる
	static const State* const states[] = {&idle, &chase, &windup, &attack, &cooldown, &stunned};
	const auto index = static_cast<size_t>(state);
	return *states[index < std::size(states) ? index : 0];
}
