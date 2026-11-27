#pragma once
#include "ECS.h"
#include <array>
#include <cassert>
#include <unordered_map>

namespace TDEngine {
namespace ECS {

// ComponentManager が具体的なコンポーネントの型を知らずに配列の一覧を
// 持てるよう、共通のインターフェースが必要になる。
class IComponentArray {
public:
	virtual ~IComponentArray() = default;
	virtual void EntityDestroyed(Entity entity) = 0;
};

template<typename T> class ComponentArray : public IComponentArray {
public:
	void InsertData(Entity entity, T component) {
		assert(
		    entityToIndexMap_.find(entity) == entityToIndexMap_.end() &&
		    "Component added to same entity more than once.");

		// 新しい要素を末尾に置く
		size_t newIndex = size_;
		entityToIndexMap_[entity] = newIndex;
		indexToEntityMap_[newIndex] = entity;
		componentArray_[newIndex] = component;
		size_++;
	}

	void RemoveData(Entity entity) {
		assert(entityToIndexMap_.find(entity) != entityToIndexMap_.end() && "Removing non-existent component.");

		// 隙間を作らないよう、末尾の要素を削除した場所へ移す
		size_t indexOfRemovedEntity = entityToIndexMap_[entity];
		size_t indexOfLastElement = size_ - 1;
		componentArray_[indexOfRemovedEntity] = componentArray_[indexOfLastElement];

		// 移動した場所を指すよう対応表を更新する
		Entity entityOfLastElement = indexToEntityMap_[indexOfLastElement];
		entityToIndexMap_[entityOfLastElement] = indexOfRemovedEntity;
		indexToEntityMap_[indexOfRemovedEntity] = entityOfLastElement;

		entityToIndexMap_.erase(entity);
		indexToEntityMap_.erase(indexOfLastElement);
		size_--;
	}

	T& GetData(Entity entity) {
		assert(entityToIndexMap_.find(entity) != entityToIndexMap_.end() && "Retrieving non-existent component.");
		return componentArray_[entityToIndexMap_[entity]];
	}

	void EntityDestroyed(Entity entity) override {
		if (entityToIndexMap_.find(entity) != entityToIndexMap_.end()) {
			RemoveData(entity);
		}
	}

private:
	// コンポーネントを詰めて並べた配列 (コンポーネントの型ごとの SoA)
	std::array<T, MAX_ENTITIES> componentArray_;

	// エンティティ ID から配列の番号への対応表
	std::unordered_map<Entity, size_t> entityToIndexMap_;

	// 配列の番号からエンティティ ID への対応表
	std::unordered_map<size_t, Entity> indexToEntityMap_;

	size_t size_{};
};

} // namespace ECS
} // namespace TDEngine
