#pragma once
#include "ECS.h"
#include "ComponentManager.h"
#include "EntityManager.h"
#include "SystemManager.h"
#include "../Core/JobSystem.h"
#include <memory>
#include <vector>
#include <functional>

namespace TDEngine {
namespace ECS {

class Coordinator {
public:
	void Init() {
		componentManager_ = std::make_unique<ComponentManager>();
		entityManager_ = std::make_unique<EntityManager>();
		systemManager_ = std::make_unique<SystemManager>();
	}

	// エンティティの操作
	Entity CreateEntity() {
		return entityManager_->CreateEntity();
	}

	void DestroyEntity(Entity entity) {
		entityManager_->DestroyEntity(entity);

		componentManager_->EntityDestroyed(entity);
		systemManager_->EntityDestroyed(entity);
	}

	// コンポーネントの操作
	template<typename T> void RegisterComponent() {
		componentManager_->RegisterComponent<T>();
	}

	template<typename T> void AddComponent(Entity entity, T component) {
		componentManager_->AddComponent<T>(entity, component);

		auto signature = entityManager_->GetSignature(entity);
		signature.set(componentManager_->GetComponentType<T>(), true);
		entityManager_->SetSignature(entity, signature);

		systemManager_->EntitySignatureChanged(entity, signature);
	}

	template<typename T> void RemoveComponent(Entity entity) {
		componentManager_->RemoveComponent<T>(entity);

		auto signature = entityManager_->GetSignature(entity);
		signature.set(componentManager_->GetComponentType<T>(), false);
		entityManager_->SetSignature(entity, signature);

		systemManager_->EntitySignatureChanged(entity, signature);
	}

	template<typename T> T& GetComponent(Entity entity) {
		return componentManager_->GetComponent<T>(entity);
	}

	template<typename T> ComponentType GetComponentType() {
		return componentManager_->GetComponentType<T>();
	}

	// システムの操作
	template<typename T> std::shared_ptr<T> RegisterSystem() {
		return systemManager_->RegisterSystem<T>();
	}

	template<typename T> void SetSystemSignature(Signature signature) {
		systemManager_->SetSignature<T>(signature);
	}

	// システムを並列に更新する
	void ParallelUpdate(const std::vector<std::shared_ptr<System>>& systems, float dt) {
		for (auto& system : systems) {
			Engine::JobSystem::Execute([system, dt]() { system->Update(dt); });
		}
		Engine::JobSystem::Wait();
	}

private:
	std::unique_ptr<ComponentManager> componentManager_;
	std::unique_ptr<EntityManager> entityManager_;
	std::unique_ptr<SystemManager> systemManager_;
};

} // namespace ECS
} // namespace TDEngine
