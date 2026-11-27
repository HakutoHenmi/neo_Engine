#pragma once
#include "../ObjectTypes.h"
#include "../SceneComponents.h"
#include "../../externals/entt/entt.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace Game {

// 同じシーンの同じエンティティを指す履歴は、復元後の ID を共有する。
// シーンは参照の識別にだけ使い、このクラスからアクセスしない。
class EditorEntityReference {
public:
	EditorEntityReference(const void* scene, entt::entity entity) : scene_(scene) {
		for (auto it = references_.begin(); it != references_.end();) {
			if (it->second.expired())
				it = references_.erase(it);
			else
				++it;
		}
		auto& reference = references_[{scene_, entt::to_integral(entity)}];
		value_ = reference.lock();
		if (!value_) {
			value_ = std::make_shared<entt::entity>(entity);
			reference = value_;
		}
	}
	entt::entity Get() const {
		return *value_;
	}
	void Rebind(entt::entity entity) {
		const auto old = references_.find({scene_, entt::to_integral(*value_)});
		if (old != references_.end() && old->second.lock() == value_)
			references_.erase(old);
		*value_ = entity;
		references_[{scene_, entt::to_integral(entity)}] = value_;
	}

private:
	const void* scene_;
	std::shared_ptr<entt::entity> value_;
	inline static std::map<std::pair<const void*, uint32_t>, std::weak_ptr<entt::entity>> references_;
};

// 単体のスナップショットに含まれない、既存の親と直属の子への接続を保持する。
class EditorEntityRelationships {
public:
	EditorEntityRelationships(const void* scene, SceneComponents& registry, entt::entity entity)
	    : scene_(scene), parent_(scene, ParentOf(registry, entity)) {}
	void Detach(SceneComponents& registry, entt::entity entity) {
		children_.clear();
		for (auto child : registry.view<HierarchyComponent>()) {
			auto& hierarchy = registry.get<HierarchyComponent>(child);
			if (hierarchy.parentId == entity) {
				children_.emplace_back(scene_, child);
				hierarchy.parentId = entt::null;
			}
		}
	}
	void Restore(SceneComponents& registry, entt::entity entity) const {
		const auto parent = parent_.Get();
		registry.get_or_emplace<HierarchyComponent>(entity).parentId =
		    registry.valid(parent) && parent != entity ? parent : entt::null;
		for (const auto& reference : children_) {
			const auto child = reference.Get();
			if (registry.valid(child))
				registry.get_or_emplace<HierarchyComponent>(child).parentId = entity;
		}
	}

private:
	static entt::entity ParentOf(SceneComponents& registry, entt::entity entity) {
		if (registry.valid(entity))
			if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity))
				return hierarchy->parentId;
		return entt::null;
	}
	const void* scene_;
	EditorEntityReference parent_;
	std::vector<EditorEntityReference> children_;
};

} // namespace Game
