#include "EntityManager.h"

namespace TDEngine {
namespace ECS {

EntityManager::EntityManager() {
	for (Entity entity = 0; entity < MAX_ENTITIES; ++entity) {
		availableEntities_.push(entity);
	}
}

Entity EntityManager::CreateEntity() {
	assert(livingEntityCount_ < MAX_ENTITIES && "Too many entities in existence.");

	// 待ち行列の先頭から ID を取り出す
	Entity id = availableEntities_.front();
	availableEntities_.pop();
	++livingEntityCount_;

	return id;
}

void EntityManager::DestroyEntity(Entity entity) {
	assert(entity < MAX_ENTITIES && "Entity out of range.");

	// 破棄したエンティティのシグネチャを無効にする
	signatures_[entity].reset();

	// 破棄した ID を待ち行列の末尾に戻す
	availableEntities_.push(entity);
	--livingEntityCount_;
}

void EntityManager::SetSignature(Entity entity, Signature signature) {
	assert(entity < MAX_ENTITIES && "Entity out of range.");
	signatures_[entity] = signature;
}

Signature EntityManager::GetSignature(Entity entity) {
	assert(entity < MAX_ENTITIES && "Entity out of range.");
	return signatures_[entity];
}

} // namespace ECS
} // namespace TDEngine
