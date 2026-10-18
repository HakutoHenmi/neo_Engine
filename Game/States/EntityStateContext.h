#pragma once
#include "../../Engine/IState.h"
#include <iterator>
#include "../../externals/entt/entt.hpp"

namespace Game {

struct EntityStateContext {
	entt::registry& registry;
	entt::entity entity;
};

} // namespace Game
