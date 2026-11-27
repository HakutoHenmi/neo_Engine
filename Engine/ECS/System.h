#pragma once
#include "ECS.h"
#include <set>

namespace TDEngine {
namespace ECS {

class System {
public:
	const std::set<Entity>& Entities() const {
		return entities_;
	}
	void AddEntity(Entity entity) {
		entities_.insert(entity);
	}
	void RemoveEntity(Entity entity) {
		entities_.erase(entity);
	}
	virtual ~System() = default;
	virtual void Update(float) {} // 更新処理を持たないシステムの既定動作。
private:
	std::set<Entity> entities_;
};

} // namespace ECS
} // namespace TDEngine
