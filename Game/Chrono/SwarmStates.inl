// Included by SwarmRules.h after the swarm data and movement helpers.
namespace SwarmStates {

struct Context {
	SwarmEnemy& enemy;
	Vec player, delta;
	float distance, dt, ground;
	bool canAttack;
	SwarmEvent event = SwarmEvent::None;
};
using State = Engine::IState<Context>;

class Spawn final : public State {
public:
	void Update(Context& context) const override {
		if (context.enemy.clock >= .6f)
			ChangeSwarmPhase(context.enemy, SwarmPhase::Chase);
	}
};

class Chase final : public State {
public:
	void Update(Context& context) const override {
		auto& enemy = context.enemy;
		const float dt = context.dt;
		enemy.attackDelay = std::max(0.f, enemy.attackDelay - dt);
		const bool ready = context.canAttack && enemy.attackDelay <= 0;
		Vec approach = context.delta;
		if (!ready) {
			const float hold = (enemy.flying ? (enemy.bomber ? 22.f : 27.f) : 8.f)*enemy.scale;
			Vec planar = context.delta;
			planar.y = 0;
			const Vec radial = context.distance > .01f ? Unit(planar) : Vec{std::cos(float(enemy.id)), 0, std::sin(float(enemy.id))};
			const Vec side{radial.z, 0, -radial.x};
			approach = radial * (context.distance - hold) + side * (enemy.flying ? (enemy.id % 2 ? 5.f : -5.f) : 0.f);
		}
		if (enemy.flying)
			approach.y = context.ground + 14 + float(enemy.id % 5) * 2 + std::sin(enemy.age * 2 + float(enemy.id)) * 1.5f - enemy.at.y;
		enemy.heading = Unit(Lerp(enemy.heading, Unit(approach), 1 - std::exp(-9 * dt)));
		const float speed = ((enemy.flying ? 10.f : 6.f) + SwarmVariation(enemy.id) * 2)*std::sqrt(enemy.scale);
		enemy.at = enemy.at + Unit(approach) * std::min(Length(approach), speed * dt);
		if (ready && context.distance < (enemy.flying ? (enemy.bomber ? 28.f : 32.f) : 9.f)*enemy.scale) {
			enemy.heading = Unit(context.delta);
			enemy.attackAt = context.player;
			if (!enemy.flying)
				enemy.attackAt.y = enemy.at.y;
			++enemy.attacks;
			enemy.struck = false;
			ChangeSwarmPhase(enemy, SwarmPhase::Windup);
		}
	}
};

class Windup final : public State {
public:
	void Update(Context& context) const override {
		auto& enemy = context.enemy;
		if (enemy.bomber) {
			enemy.heading = Unit(context.delta);
			if (enemy.clock > .7f)
				enemy.at = enemy.at + enemy.heading * std::min(std::max(0.f, Length(context.delta) - 1.5f), (12 + SwarmVariation(enemy.id, enemy.attacks) * 3) * context.dt);
			if (enemy.clock >= SwarmWindup(enemy)) {
				ChangeSwarmPhase(enemy, SwarmPhase::Dead);
				context.event = SwarmEvent::Detonate;
			}
		} else if (enemy.clock >= SwarmWindup(enemy)) {
			ChangeSwarmPhase(enemy, SwarmPhase::Dash);
		}
	}
};

class Dash final : public State {
public:
	void Update(Context& context) const override {
		auto& enemy = context.enemy;
		const float remaining = Length(enemy.attackAt - enemy.at);
		enemy.at = enemy.at + enemy.heading * std::min(remaining, SwarmDashSpeed(enemy) * context.dt);
		if (remaining < .3f || enemy.clock >= 2) {
			ChangeSwarmPhase(enemy, SwarmPhase::Recover);
			enemy.attackDelay = 1.5f + SwarmVariation(enemy.id, enemy.attacks + 1) * 2;
		}
	}
};

class Recover final : public State {
public:
	void Update(Context& context) const override {
		auto& enemy = context.enemy;
		if (enemy.flying) {
			enemy.at.y += std::min(std::max(0.f, context.ground + 18 - enemy.at.y), 20 * context.dt);
			enemy.heading.y *= std::exp(-8 * context.dt);
		}
		if (enemy.clock >= 3 + SwarmVariation(enemy.id, enemy.attacks) * 2)
			ChangeSwarmPhase(enemy, SwarmPhase::Chase);
	}
};

class Dead final : public State {
public:
	void Update(Context&) const override {}
};

inline const State& Get(SwarmPhase phase) {
	static const Spawn spawn;
	static const Chase chase;
	static const Windup windup;
	static const Dash dash;
	static const Recover recover;
	static const Dead dead;
	static const State* const states[] = {&spawn, &chase, &windup, &dash, &recover, &dead};
	const auto index = static_cast<size_t>(phase);
	return *states[index < std::size(states) ? index : 0];
}

} // namespace SwarmStates
