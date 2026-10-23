// SwarmStates.inl
// 群れの敵の状態パターン。SwarmRules.h から、敵のデータと移動の補助の後で読み込まれる。
namespace SwarmStates {

// 状態の更新に渡す情報 (敵・プレイヤー・差分・距離・経過時間・地面・攻撃できるか・出来事)
struct Context {
	SwarmEnemy& enemy;
	Vec player, delta;
	float distance, dt, ground;
	bool canAttack;
	SwarmEvent event = SwarmEvent::None;
};
// 状態の基底クラス (エンジンの IState を使う)
using State = Engine::IState<Context>;

// 出現: 0.6 秒後に追跡へ
class Spawn final : public State {
public:
	void Update(Context& context) const override {
		if (context.enemy.clock >= .6f)
			ChangeSwarmPhase(context.enemy, SwarmPhase::Chase);
	}
};

// 追跡: 攻撃できないときは一定の距離を保って回り込み、できるときは近づいて射程に入ったら予告へ
class Chase final : public State {
public:
	void Update(Context& context) const override {
		auto& enemy = context.enemy;
		const float dt = context.dt;
		// 次の攻撃までの待ち時間を減らし、攻撃できるかを決める
		enemy.attackDelay = std::max(0.f, enemy.attackDelay - dt);
		const bool ready = context.canAttack && enemy.attackDelay <= 0;
		// 近づく向き
		Vec approach = context.delta;
		if (!ready) {
			// 攻撃できないときに保つ距離 (飛ぶ敵は遠く、自爆の敵は少し近く)
			const float hold = (enemy.flying ? (enemy.bomber ? 22.f : 27.f) : 8.f)*enemy.scale;
			Vec planar = context.delta;
			planar.y = 0;
			// プレイヤーからの向きと横の向き
			const Vec radial = context.distance > .01f ? Unit(planar) : Vec{std::cos(float(enemy.id)), 0, std::sin(float(enemy.id))};
			const Vec side{radial.z, 0, -radial.x};
			// 保つ距離まで近づき、飛ぶ敵は左右に回り込む
			approach = radial * (context.distance - hold) + side * (enemy.flying ? (enemy.id % 2 ? 5.f : -5.f) : 0.f);
		}
		// 飛ぶ敵は地面から一定の高さを揺れながら飛ぶ
		if (enemy.flying)
			approach.y = context.ground + 14 + float(enemy.id % 5) * 2 + std::sin(enemy.age * 2 + float(enemy.id)) * 1.5f - enemy.at.y;
		// 向きをなめらかに合わせる
		enemy.heading = Unit(Lerp(enemy.heading, Unit(approach), 1 - std::exp(-9 * dt)));
		// 速さ (飛ぶ敵は速い。大きいほど速い)
		const float speed = ((enemy.flying ? 10.f : 6.f) + SwarmVariation(enemy.id) * 2)*std::sqrt(enemy.scale);
		// 近づく (行き過ぎないようにする)
		enemy.at = enemy.at + Unit(approach) * std::min(Length(approach), speed * dt);
		// 攻撃できて射程に入ったら、プレイヤーの今の位置を攻撃の地点にして予告へ
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

// 予告: 自爆の敵は近づいて時間が来たら爆発、それ以外は時間が来たら突進へ
class Windup final : public State {
public:
	void Update(Context& context) const override {
		auto& enemy = context.enemy;
		// 自爆の敵: プレイヤーの方を向き、0.7 秒後から近づく
		if (enemy.bomber) {
			enemy.heading = Unit(context.delta);
			if (enemy.clock > .7f)
				enemy.at = enemy.at + enemy.heading * std::min(std::max(0.f, Length(context.delta) - 1.5f), (12 + SwarmVariation(enemy.id, enemy.attacks) * 3) * context.dt);
			// 予告の時間が来たら爆発する
			if (enemy.clock >= SwarmWindup(enemy)) {
				ChangeSwarmPhase(enemy, SwarmPhase::Dead);
				context.event = SwarmEvent::Detonate;
			}
		} else if (enemy.clock >= SwarmWindup(enemy)) {
			ChangeSwarmPhase(enemy, SwarmPhase::Dash);
		}
	}
};

// 突進: 記録した地点へまっすぐ進み、着いたら回復へ
class Dash final : public State {
public:
	void Update(Context& context) const override {
		auto& enemy = context.enemy;
		// 攻撃の地点までの残りの距離
		const float remaining = Length(enemy.attackAt - enemy.at);
		// 突進の速さで進む
		enemy.at = enemy.at + enemy.heading * std::min(remaining, SwarmDashSpeed(enemy) * context.dt);
		// 着いたか 2 秒経ったら回復へ。次の攻撃までの待ち時間を決める
		if (remaining < .3f || enemy.clock >= 2) {
			ChangeSwarmPhase(enemy, SwarmPhase::Recover);
			enemy.attackDelay = 1.5f + SwarmVariation(enemy.id, enemy.attacks + 1) * 2;
		}
	}
};

// 回復: 飛ぶ敵は高度を戻し、時間が来たら追跡へ
class Recover final : public State {
public:
	void Update(Context& context) const override {
		auto& enemy = context.enemy;
		// 飛ぶ敵は元の高さへ戻り、向きを水平に戻す
		if (enemy.flying) {
			enemy.at.y += std::min(std::max(0.f, context.ground + 18 - enemy.at.y), 20 * context.dt);
			enemy.heading.y *= std::exp(-8 * context.dt);
		}
		// 時間が来たら追跡へ
		if (enemy.clock >= 3 + SwarmVariation(enemy.id, enemy.attacks) * 2)
			ChangeSwarmPhase(enemy, SwarmPhase::Chase);
	}
};

// 死亡: 何もしない
class Dead final : public State {
public:
	void Update(Context&) const override {}
};

// 状態の列挙値から状態オブジェクトを返す (範囲外は出現)
inline const State& Get(SwarmPhase phase) {
	// 各状態のオブジェクト (変更されないので共有できる)
	static const Spawn spawn;
	static const Chase chase;
	static const Windup windup;
	static const Dash dash;
	static const Recover recover;
	static const Dead dead;
	// 状態の列挙値の順に並べる
	static const State* const states[] = {&spawn, &chase, &windup, &dash, &recover, &dead};
	const auto index = static_cast<size_t>(phase);
	return *states[index < std::size(states) ? index : 0];
}

} // namespace SwarmStates
