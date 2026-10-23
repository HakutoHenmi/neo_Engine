// EntityStateContext.h
// 状態パターン (IState) の各状態へ渡す共通の情報 (レジストリと対象のエンティティ)。
#pragma once
#include "../../Engine/IState.h"
#include <iterator>
#include "../../externals/entt/entt.hpp"

namespace Game {

// 状態の処理に渡す文脈
struct EntityStateContext {
	entt::registry& registry;
	entt::entity entity;
};

} // namespace Game
