#pragma once
#include "ECS.h"
#include "System.h"
#include <unordered_map>
#include <memory>
#include <cassert>
#include <typeinfo>

namespace TDEngine {
namespace ECS {

class SystemManager {
public:
	template<typename T> std::shared_ptr<T> RegisterSystem() {
		const char* typeName = typeid(T).name();

		assert(systems_.find(typeName) == systems_.end() && "Registering system more than once.");

		auto system = std::make_shared<T>();
		systems_.insert({typeName, system});
		return system;
	}

	template<typename T> void SetSignature(Signature signature) {
		const char* typeName = typeid(T).name();

		assert(systems_.find(typeName) != systems_.end() && "System used before registered.");

		signatures_.insert({typeName, signature});
	}

	void EntityDestroyed(Entity entity) {
		for (auto const& pair : systems_) {
			auto const& system = pair.second;
			system->RemoveEntity(entity);
		}
	}

	void EntitySignatureChanged(Entity entity, Signature entitySignature) {
		for (auto const& pair : systems_) {
			auto const& type = pair.first;
			auto const& system = pair.second;
			auto const& systemSignature = signatures_[type];

			// エンティティのシグネチャがシステムのシグネチャに合えば一覧に入れる
			if ((entitySignature & systemSignature) == systemSignature) {
				system->AddEntity(entity);
			}
			// 合わなければ一覧から外す
			else {
				system->RemoveEntity(entity);
			}
		}
	}

private:
	std::unordered_map<const char*, Signature> signatures_{};
	std::unordered_map<const char*, std::shared_ptr<System>> systems_{};
};

} // namespace ECS
} // namespace TDEngine
