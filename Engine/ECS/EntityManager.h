#pragma once
#include "ECS.h"
#include <queue>
#include <array>
#include <cassert>

namespace TDEngine {
namespace ECS {

class EntityManager {
public:
	// 使える全エンティティ ID で待ち行列を初期化する
	EntityManager();

	// エンティティを作り、ID を割り当てる
	Entity CreateEntity();

	// エンティティを破棄し、シグネチャを消して ID を待ち行列に戻す
	void DestroyEntity(Entity entity);

	// エンティティのシグネチャ (持っているコンポーネント) を設定する
	void SetSignature(Entity entity, Signature signature);

	// エンティティのシグネチャを取得する
	Signature GetSignature(Entity entity);

private:
	// 使われていないエンティティ ID の待ち行列
	std::queue<Entity> availableEntities_{};

	// 番号がエンティティ ID に対応するシグネチャの配列
	std::array<Signature, MAX_ENTITIES> signatures_{};

	// 生きているエンティティの総数
	uint32_t livingEntityCount_{};
};

} // namespace ECS
} // namespace TDEngine
