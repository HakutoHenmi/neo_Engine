#include "QuadTree.h"
#include <algorithm>

namespace Engine {

PhysicsQuadTreeNode::PhysicsQuadTreeNode(QuadRect bounds, int depth, int maxDepth, int maxObjects)
    : bounds_(bounds), depth_(depth), maxDepth_(maxDepth), maxObjects_(maxObjects) {}

void PhysicsQuadTreeNode::Clear() {
	objects_.clear();
	// unique_ptr を空にすると子ノード (とその子孫) も解放される
	for (auto& node : nodes_) {
		node.reset();
	}
}

void PhysicsQuadTreeNode::Split() {
	const float subWidth = bounds_.w / 2.0f;
	const float subHeight = bounds_.h / 2.0f;
	const float x = bounds_.x;
	const float z = bounds_.z;
	const int childDepth = depth_ + 1;

	const QuadRect childBounds[kChildCount] = {
	    {x + subWidth, z, subWidth, subHeight},             // 右上
	    {x, z, subWidth, subHeight},                        // 左上
	    {x, z + subHeight, subWidth, subHeight},            // 左下
	    {x + subWidth, z + subHeight, subWidth, subHeight}, // 右下
	};
	for (int i = 0; i < kChildCount; ++i) {
		nodes_[i] = std::make_unique<PhysicsQuadTreeNode>(childBounds[i], childDepth, maxDepth_, maxObjects_);
	}
}

int PhysicsQuadTreeNode::GetIndex(const QuadRect& rect) const {
	const double verticalMidpoint = bounds_.x + (bounds_.w / 2.0);
	const double horizontalMidpoint = bounds_.z + (bounds_.h / 2.0);

	// 完全に属する象限を特定する
	const bool topQuadrant = (rect.z < horizontalMidpoint && rect.z + rect.h < horizontalMidpoint);
	const bool bottomQuadrant = (rect.z > horizontalMidpoint);
	const bool leftQuadrant = (rect.x < verticalMidpoint && rect.x + rect.w < verticalMidpoint);
	const bool rightQuadrant = (rect.x > verticalMidpoint);

	if (topQuadrant) {
		if (leftQuadrant) {
			return 1;
		}
		if (rightQuadrant) {
			return 0;
		}
	} else if (bottomQuadrant) {
		if (leftQuadrant) {
			return 2;
		}
		if (rightQuadrant) {
			return 3;
		}
	}
	// 境界をまたぐ場合はどの子にも入れない
	return kNoChild;
}

void PhysicsQuadTreeNode::Insert(const PhysicsQuadTreeObject& obj) {
	if (HasChildren()) {
		const int index = GetIndex(obj.GetRect());
		if (index != kNoChild) {
			nodes_[index]->Insert(obj);
			return;
		}
	}

	objects_.push_back(obj);

	if (objects_.size() <= static_cast<size_t>(maxObjects_) || depth_ >= maxDepth_) {
		return;
	}
	if (!HasChildren()) {
		Split();
	}

	// 子ノードに完全に収まるオブジェクトを子へ移し、境界をまたぐものだけ残す
	size_t i = 0;
	while (i < objects_.size()) {
		const int index = GetIndex(objects_[i].GetRect());
		if (index == kNoChild) {
			++i;
			continue;
		}
		const PhysicsQuadTreeObject moveObj = objects_[i];
		objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(i));
		nodes_[index]->Insert(moveObj);
	}
}

void PhysicsQuadTreeNode::Query(const QuadRect& range, std::vector<uint32_t>& foundIds) const {
	if (!bounds_.Intersects(range)) {
		return;
	}

	// 現在のノードが保持する境界をまたぐオブジェクトと判定処理
	for (const auto& obj : objects_) {
		if (range.Intersects(obj.GetRect())) {
			foundIds.push_back(obj.id);
		}
	}

	if (!HasChildren()) {
		return;
	}
	for (const auto& node : nodes_) {
		node->Query(range, foundIds);
	}
}

PhysicsQuadTree::PhysicsQuadTree(float minX, float minZ, float maxX, float maxZ, int maxDepth, int maxObjects)
    : root_(
          std::make_unique<PhysicsQuadTreeNode>(
              QuadRect{minX, minZ, maxX - minX, maxZ - minZ}, 0, maxDepth, maxObjects)) {}

void PhysicsQuadTree::Clear() {
	root_->Clear();
}

void PhysicsQuadTree::Insert(uint32_t id, float minX, float minZ, float maxX, float maxZ) {
	PhysicsQuadTreeObject obj;
	obj.id = id;
	obj.minX = minX;
	obj.maxX = maxX;
	obj.minZ = minZ;
	obj.maxZ = maxZ;
	root_->Insert(obj);
}

void PhysicsQuadTree::Query(float minX, float minZ, float maxX, float maxZ, std::vector<uint32_t>& foundIds) const {
	const QuadRect range{minX, minZ, maxX - minX, maxZ - minZ};
	root_->Query(range, foundIds);
}

} // namespace Engine
