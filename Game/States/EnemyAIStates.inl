// Included inside EnemyAISystem; each state shares behavior, never actor data.
struct StateContext {
	EnemyAISystem& system;
	entt::registry& registry;
	entt::entity entity;
	GameContext& ctx;
	float aiDt, dx, dz, dist, targetAngle;
};
using State = Engine::IState<StateContext, EntityStateContext>;

class IdleState final : public State {
public:
	void Enter(EntityStateContext& frame) const override { SetNormalColor(frame); }
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const float aiDt = frame.aiDt;
		const float dist = frame.dist;
		const float targetAngle = frame.targetAngle;
		// プレイヤーの方を向く
		frame.system.SmoothRotate(tc, targetAngle, ai.rotationSpeed, aiDt);

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

class ChaseState final : public State {
public:
	void Update(StateContext& frame) const override {
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
		if (ai.hopper && dist > ai.aggroRange * 1.25f) {
			ChangeState(registry, entity, EnemyAIState::Idle);
			return;
		}
		// プレイヤーの方を向きながら近づく
		frame.system.SmoothRotate(tc, targetAngle, ai.rotationSpeed, aiDt);

		if (dist > 0.5f) {
			float moveSpeed = ai.chaseSpeed * aiDt;
			float nx = dx / dist;
			float nz = dz / dist;
			if (ai.hopper && ctx.scene) {
				const float next = ctx.scene->GetHeightAt(tc.translate.x + nx * moveSpeed, tc.translate.z + nz * moveSpeed, ai.groundY + 3.0f, static_cast<uint32_t>(entity));
				// A hopping enemy must not run off a ledge or climb a tall wall.
				if (next > -999.0f && std::abs(next - ai.groundY) < 1.1f) {
					tc.translate.x += nx * moveSpeed;
					tc.translate.z += nz * moveSpeed;
					ai.groundY = next;
				}
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

class WindUpState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		const auto& ai = registry.get<EnemyAIComponent>(entity);
		if (auto* mesh = registry.try_get<MeshRendererComponent>(entity))
			mesh->color = {1, .8f, 0, 1};
		if (!ai.hopper)
			ShowWarningEffect(registry, entity, ai.windUpDuration);
	}
	void Update(StateContext& frame) const override {
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

class AttackState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		auto& registry = frame.registry;
		const auto entity = frame.entity;
		if (auto* hitbox = registry.try_get<HitboxComponent>(entity)) {
			hitbox->isActive = true;
			hitbox->hitTargets.clear();
		}
		const auto& ai = registry.get<EnemyAIComponent>(entity);
		if (auto* transform = registry.try_get<TransformComponent>(entity); transform && !ai.hopper) {
			transform->scale.x = 2;
			transform->scale.z = 2;
		}
		if (ai.hopper)
			SetNormalColor(frame);
	}
	void Exit(EntityStateContext& frame) const override {
		if (auto* hitbox = frame.registry.try_get<HitboxComponent>(frame.entity))
			hitbox->isActive = false;
	}
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ctx = frame.ctx;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		auto& tc = frame.registry.get<TransformComponent>(frame.entity);
		const float aiDt = frame.aiDt;
		// 攻撃中: 前方に少し突進
		{
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

class CooldownState final : public State {
public:
	void Enter(EntityStateContext& frame) const override { SetNormalColor(frame); }
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		// 攻撃後の隙（プレイヤーの反撃チャンス）
		if (ai.stateTimer >= ai.cooldownDuration) {
			ChangeState(registry, entity, EnemyAIState::Idle);
		}
	}
};

class StunnedState final : public State {
public:
	void Enter(EntityStateContext& frame) const override {
		if (auto* mesh = frame.registry.try_get<MeshRendererComponent>(frame.entity))
			mesh->color = {.5f, .5f, 1, 1};
	}
	void Update(StateContext& frame) const override {
		auto& registry = frame.registry;
		auto entity = frame.entity;
		auto& ai = frame.registry.get<EnemyAIComponent>(frame.entity);
		if (ai.stateTimer >= ai.stunDuration) {
			ChangeState(registry, entity, EnemyAIState::Idle);
		}
	}
};

static const State& GetState(EnemyAIState state) {
	static const IdleState idle;
	static const ChaseState chase;
	static const WindUpState windup;
	static const AttackState attack;
	static const CooldownState cooldown;
	static const StunnedState stunned;
	static const State* const states[] = {&idle, &chase, &windup, &attack, &cooldown, &stunned};
	const auto index = static_cast<size_t>(state);
	return *states[index < std::size(states) ? index : 0];
}
