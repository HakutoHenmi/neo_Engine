#include "PreparationCamera.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

#include "PhaseSystemScript.h"

namespace Game {

void PreparationCamera::Start(entt::entity /*entity*/, GameScene* /*scene*/) {}

void PreparationCamera::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity))
		return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity))
		return;

	if (PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
		UpdateMovement(entity, scene, dt);
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity))scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = true;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = true;
		// cameraTargets はECS化が必要だが、一旦省略
	} else {
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity))scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = false;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = false;
	}
}

void PreparationCamera::UpdateMovement(entt::entity entity, GameScene* scene, float /*dt*/) {
	if (!scene || !scene->GetRegistry().valid(entity))
		return;

	auto& registry = scene->GetRegistry();
	if (!registry.all_of<PlayerInputComponent>(entity))
		return;

	scene->GetRegistry().get<CharacterMovementComponent>(entity).speed = 20.0f; 
}

void PreparationCamera::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PreparationCamera);

} // namespace Game