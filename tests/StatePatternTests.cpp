#include "../Game/Scenes/GameScene.h"
#include "../Game/Systems/BossActionSystem.h"
#include "../Game/Systems/CleanupSystem.h"
#include "../Game/Systems/CombatSystem.h"
#include "../Game/Systems/EnemyAISystem.h"
#include "../Game/Systems/HealthSystem.h"
#include "../Game/Systems/PlayerActionSystem.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

using namespace Game;

// WinMain supplies this optional startup logger in the application. The tests
// link the real game code while providing their own console entry point.
void LogFileMain(const char*) {}

static entt::entity MakePlayer(entt::registry& registry) {
	auto entity = registry.create();
	registry.emplace<TransformComponent>(entity).translate = {0, 0, 3};
	registry.emplace<TagComponent>(entity, TagType::Player);
	registry.emplace<PlayerInputComponent>(entity);
	registry.emplace<PlayerActionComponent>(entity);
	registry.emplace<HealthComponent>(entity);
	return entity;
}

static entt::entity MakeBoss(entt::registry& registry) {
	auto entity = registry.create();
	registry.emplace<TransformComponent>(entity);
	registry.emplace<TagComponent>(entity, TagType::Enemy);
	registry.emplace<HitboxComponent>(entity);
	registry.emplace<MeshRendererComponent>(entity);
	auto& boss = registry.emplace<BossActionComponent>(entity);
	boss.patterns.emplace_back();
	boss.currentPatternIndex = 0;
	return entity;
}

static entt::entity MakeBeam(entt::registry& registry, entt::entity owner) {
	auto entity = registry.create();
	registry.emplace<TransformComponent>(entity);
	registry.emplace<TagComponent>(entity, TagType::Enemy);
	registry.emplace<HitboxComponent>(entity).isActive = true;
	registry.emplace<MeshRendererComponent>(entity);
	registry.emplace<ParticleEmitterComponent>(entity).emitter.params.emitRate = 150;
	registry.get<BossActionComponent>(owner).currentBeamEntity = entity;
	return entity;
}

static void TestPlayerInterrupts() {
	entt::registry registry;
	const auto player = MakePlayer(registry);
	auto& movement = registry.emplace<CharacterMovementComponent>(player);
	auto& action = registry.get<PlayerActionComponent>(player);
	auto& hitbox = registry.emplace<HitboxComponent>(player);
	auto& collider = registry.emplace<BoxColliderComponent>(player);

	for (bool enabled : {true, false}) {
		movement.enabled = enabled;
		PlayerActionSystem::ChangeState(registry, player, PlayerActionState::DecoyWarp, 1);
		assert(!movement.enabled);
		hitbox.isActive = true;
		collider.center.z = 9;
		action.chargeTimer = 2;
		PlayerActionSystem::ChangeState(registry, player, PlayerActionState::Stagger, .3f);
		assert(movement.enabled == enabled);
		assert(!hitbox.isActive && collider.center.z == 0 && action.chargeTimer == 0);
		assert(action.stateTimer == 0 && action.stateDuration == .3f);
	}

	movement.enabled = true;
	PlayerActionSystem::ChangeState(registry, player, PlayerActionState::Liquefy);
	action.liquefyLocked = action.liquefySpeedApplied = true;
	action.liquefyBaseSpeed = 8;
	action.liquefyInitialFlowSpeed = 12;
	movement.speed = 18;
	PlayerActionSystem::ChangeState(registry, player, PlayerActionState::Stagger, .3f);
	assert(movement.speed == 8 && !action.liquefyLocked && !action.liquefySpeedApplied);
	assert(action.liquefyInitialFlowSpeed == 0);

	PlayerActionSystem::ChangeState(registry, player, PlayerActionState::DecoyWarp, 1);
	PlayerActionSystem system;
	system.Reset(registry);
	assert(movement.enabled && action.state == PlayerActionState::Idle && action.stateDuration == 0);
	std::cout << "PASS player: warp/stagger/reset, movement ownership, liquefy cleanup, attack cancellation\n";
}

static void TestShotAndHitboxTiming() {
	entt::registry registry;
	const auto first = MakePlayer(registry), second = MakePlayer(registry);
	PlayerActionSystem system;
	GameContext context;
	context.isPlaying = true;
	for (auto entity : {first, second})
		PlayerActionSystem::ChangeState(registry, entity, PlayerActionState::Shoot, .3f);
	// The old first-frame heuristic repeats the shot when a short frame is
	// followed by a much longer frame. Both actors must still shoot once.
	for (float dt : {.01f, .10f, .001f, .02f}) {
		context.dt = dt;
		system.Update(registry, context);
	}
	assert(registry.get<HealthComponent>(first).Hp() == 95);
	assert(registry.get<HealthComponent>(second).Hp() == 95);
	PlayerActionSystem::ChangeState(registry, first, PlayerActionState::Shoot, .3f);
	system.Update(registry, context);
	assert(registry.get<HealthComponent>(first).Hp() == 90);
	assert(registry.get<HealthComponent>(second).Hp() == 95);

	auto& hitbox = registry.emplace<HitboxComponent>(first);
	PlayerActionSystem::ChangeState(registry, first, PlayerActionState::SlimeSpike, .26f);
	context.dt = .03f;
	system.Update(registry, context);
	assert(hitbox.isActive && hitbox.damage == 30);
	hitbox.hitTargets.push_back(second);
	system.Update(registry, context);
	assert(hitbox.hitTargets.size() == 1);
	PlayerActionSystem::ChangeState(registry, first, PlayerActionState::Stagger, .3f);
	assert(!hitbox.isActive && hitbox.center.z == 0);
	std::cout << "PASS player: one shot per entry across variable dt and multiple actors, hit windows and history\n";
}

static void TestIndependentInput() {
	entt::registry registry;
	const auto first = MakePlayer(registry), second = MakePlayer(registry);
	registry.get<PlayerInputComponent>(first).attackRequested = true;
	registry.get<PlayerInputComponent>(second).attackRequested = true;
	GameContext context;
	context.isPlaying = true;
	context.dt = .01f;
	PlayerActionSystem system;
	system.Update(registry, context);
	assert(registry.get<PlayerActionComponent>(first).state == PlayerActionState::SlimeSpike);
	assert(registry.get<PlayerActionComponent>(second).state == PlayerActionState::SlimeSpike);
	registry.get<HealthComponent>(first).SetHp(50);
	PlayerActionSystem::ChangeState(registry, first, PlayerActionState::Stagger, .3f);
	system.Update(registry, context);
	const auto scale = registry.get<TransformComponent>(first).scale;
	system.Update(registry, context);
	assert(std::abs(registry.get<TransformComponent>(first).scale.y - scale.y) < .00001f);
	std::cout << "PASS player: independent press edges and stable stagger pose under HP scaling\n";
}

static void TestEnemyCycle() {
	entt::registry registry;
	const auto player = MakePlayer(registry);
	const auto enemy = registry.create();
	registry.emplace<TransformComponent>(enemy);
	registry.emplace<MeshRendererComponent>(enemy);
	auto& hitbox = registry.emplace<HitboxComponent>(enemy);
	auto& ai = registry.emplace<EnemyAIComponent>(enemy);
	ai.attackInterval = 0;
	ai.windUpDuration = .05f;
	ai.attackDuration = .15f;
	ai.cooldownDuration = .05f;
	EnemyAISystem system;
	GameContext context;
	context.isPlaying = true;
	context.dt = .05f;
	system.Update(registry, context);
	assert(ai.state == EnemyAIState::WindUp && !hitbox.isActive);
	system.Update(registry, context);
	assert(ai.state == EnemyAIState::Attack && hitbox.isActive && hitbox.hitTargets.empty());
	hitbox.hitTargets.push_back(player);
	auto& status = registry.emplace<CanStatusComponent>(enemy);
	status.freezeTimer = 1;
	system.Update(registry, context);
	assert(!hitbox.isActive && ai.stateTimer == 0);
	status.freezeTimer = 0;
	system.Update(registry, context);
	assert(hitbox.isActive && hitbox.hitTargets.size() == 1);
	EnemyAISystem::ChangeState(registry, enemy, EnemyAIState::Stunned);
	assert(!hitbox.isActive && registry.get<MeshRendererComponent>(enemy).color.z == 1);
	ai.stunDuration = .05f;
	system.Update(registry, context);
	assert(ai.state == EnemyAIState::Idle);
	system.Reset(registry);
	assert(ai.stateTimer == 0 && ai.idleTimer == 0 && !hitbox.isActive);
	std::cout << "PASS enemy: windup/attack, freeze/thaw without repeat hits, stun and reset\n";
}

static void TestBossInterrupts() {
	entt::registry registry;
	MakePlayer(registry);
	const auto bossEntity = MakeBoss(registry);
	auto& boss = registry.get<BossActionComponent>(bossEntity);
	GameContext context;
	context.isPlaying = true;
	context.dt = .01f;
	BossActionSystem system;
	CleanupSystem cleanup;

	for (auto next : {BossState::Cooldown, BossState::Down, BossState::Stunned, BossState::Dead}) {
		boss.currentPatternIndex = 0;
		BossActionSystem::ChangeState(registry, bossEntity, BossState::Attack);
		const auto beam = MakeBeam(registry, bossEntity);
		BossActionSystem::ChangeState(registry, bossEntity, next);
		assert(!registry.get<HitboxComponent>(bossEntity).isActive && boss.currentBeamEntity == entt::null);
		// Physical deletion waits for CleanupSystem so CombatSystem's packed
		// component references remain valid throughout the damage iteration.
		assert(registry.valid(beam) && !registry.get<HitboxComponent>(beam).isActive);
		assert(!registry.get<MeshRendererComponent>(beam).enabled);
		assert(registry.get<ParticleEmitterComponent>(beam).emitter.params.emitRate == 0);
		cleanup.Update(registry, context);
		assert(!registry.valid(beam));
		system.Reset(registry);
	}

	boss.currentPatternIndex = 0;
	BossActionSystem::ChangeState(registry, bossEntity, BossState::Attack);
	auto& status = registry.emplace<CanStatusComponent>(bossEntity);
	status.freezeTimer = 1;
	system.Update(registry, context);
	assert(!registry.get<HitboxComponent>(bossEntity).isActive);
	status.freezeTimer = 0;
	system.Update(registry, context);
	assert(registry.get<HitboxComponent>(bossEntity).isActive);
	const auto beam = MakeBeam(registry, bossEntity);
	system.Reset(registry);
	assert(!registry.valid(beam) && boss.state == BossState::Idle);
	std::cout << "PASS boss: attack interruption/death, deferred beam cleanup, freeze/thaw and reset\n";
}

static void TestBossPatternBounds() {
	for (auto type : {BossAttackType::Thrust, BossAttackType::BeamSweep}) {
		for (unsigned seed = 0; seed < 64; ++seed) {
			entt::registry registry;
			const auto player = MakePlayer(registry);
			registry.get<TransformComponent>(player).translate.z = 30;
			const auto entity = MakeBoss(registry);
			auto& boss = registry.get<BossActionComponent>(entity);
			boss.patterns[0].type = type; // No JumpPress; BeamSweep also has no close pattern.
			boss.stateTimer = 1.1f;
			std::srand(seed);
			BossActionSystem system;
			GameContext context;
			context.isPlaying = true;
			context.dt = .01f;
			system.Update(registry, context);
			assert(boss.currentPatternIndex == 0);
			boss.currentPatternIndex = 100;
			system.Update(registry, context);
			assert(boss.state == BossState::Idle && boss.currentPatternIndex == -1);
			boss.patterns.clear();
			BossActionSystem::ChangeState(registry, entity, BossState::Attack);
			system.Update(registry, context);
			assert(boss.state == BossState::Idle);
		}
	}
	std::cout << "PASS boss: missing jump/close patterns, removed patterns and upper index bounds\n";
}

static void TestCombatAndDeathIntegration() {
	entt::registry registry;
	const auto player = MakePlayer(registry);
	auto& attack = registry.emplace<HitboxComponent>(player);
	attack.isActive = true;
	attack.damage = 10;
	attack.tag = TagType::Player;
	const auto boss = MakeBoss(registry);
	registry.emplace<HealthComponent>(boss);
	registry.emplace<BoxColliderComponent>(boss);
	BossActionSystem::ChangeState(registry, boss, BossState::Attack);
	auto beam = MakeBeam(registry, boss);
	const auto part = registry.create();
	registry.emplace<TransformComponent>(part).translate = {0, 0, 3};
	registry.emplace<TagComponent>(part, TagType::Enemy);
	registry.emplace<HurtboxComponent>(part);
	auto& bodyPart = registry.emplace<BodyPartComponent>(part);
	bodyPart.parentEntity = boss;
	bodyPart.hp = 1;
	GameContext context;
	context.isPlaying = true;
	context.dt = .01f;
	CombatSystem combat;
	combat.Update(registry, context);
	assert(bodyPart.isDestroyed && registry.get<BossActionComponent>(boss).state == BossState::Down);
	assert(!registry.get<HitboxComponent>(beam).isActive && attack.hitTargets.size() == 1);
	CleanupSystem cleanup;
	cleanup.Update(registry, context);
	assert(!registry.valid(beam));

	BossActionSystem::ChangeState(registry, boss, BossState::Idle);
	registry.get<BossActionComponent>(boss).currentPatternIndex = 0;
	BossActionSystem::ChangeState(registry, boss, BossState::Attack);
	beam = MakeBeam(registry, boss);
	registry.get<HealthComponent>(boss).SetHp(0);
	HealthSystem health;
	health.Update(registry, context);
	assert(registry.get<BossActionComponent>(boss).state == BossState::Dead);
	assert(!registry.get<HitboxComponent>(beam).isActive);
	std::cout << "PASS integration: combat part destruction and health death use the state lifecycle\n";
}

int main() {
	TestPlayerInterrupts();
	TestShotAndHitboxTiming();
	TestIndependentInput();
	TestEnemyCycle();
	TestBossInterrupts();
	TestBossPatternBounds();
	TestCombatAndDeathIntegration();
}
