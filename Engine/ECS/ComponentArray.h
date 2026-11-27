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
		    entityToIndexMap.find(entity) == entityToIndexMap.end() &&
		    "Component added to same entity more than once.");

		// 新しい要素を末尾に置く
		size_t newIndex = m_size;
		entityToIndexMap[entity] = newIndex;
		indexToEntityMap[newIndex] = entity;
		componentArray[newIndex] = component;
		m_size++;
	}

	void RemoveData(Entity entity) {
		assert(entityToIndexMap.find(entity) != entityToIndexMap.end() && "Removing non-existent component.");

		// 隙間を作らないよう、末尾の要素を削除した場所へ移す
		size_t indexOfRemovedEntity = entityToIndexMap[entity];
		size_t indexOfLastElement = m_size - 1;
		componentArray[indexOfRemovedEntity] = componentArray[indexOfLastElement];

		// 移動した場所を指すよう対応表を更新する
		Entity entityOfLastElement = indexToEntityMap[indexOfLastElement];
		entityToIndexMap[entityOfLastElement] = indexOfRemovedEntity;
		indexToEntityMap[indexOfRemovedEntity] = entityOfLastElement;

		entityToIndexMap.erase(entity);
		indexToEntityMap.erase(indexOfLastElement);
		m_size--;
	}

	T& GetData(Entity entity) {
		assert(entityToIndexMap.find(entity) != entityToIndexMap.end() && "Retrieving non-existent component.");
		return componentArray[entityToIndexMap[entity]];
	}

	void EntityDestroyed(Entity entity) override {
		if (entityToIndexMap.find(entity) != entityToIndexMap.end()) {
			RemoveData(entity);
		}
	}

private:
	// コンポーネントを詰めて並べた配列 (コンポーネントの型ごとの SoA)
	std::array<T, MAX_ENTITIES> componentArray;

	// エンティティ ID から配列の番号への対応表
	std::unordered_map<Entity, size_t> entityToIndexMap;

	// 配列の番号からエンティティ ID への対応表
	std::unordered_map<size_t, Entity> indexToEntityMap;

	size_t m_size{};
};

} // namespace ECS
} // namespace TDEngine
