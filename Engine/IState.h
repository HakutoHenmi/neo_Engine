#pragma once

namespace Engine {

// State objects contain behavior only. Per-actor data belongs to the context,
// so one immutable state instance can safely serve many ECS entities.
template<class UpdateContext, class LifecycleContext = UpdateContext> class IState {
public:
	virtual ~IState() = default;
	virtual void Enter(LifecycleContext&) const {}
	virtual void Update(UpdateContext&) const = 0;
	virtual void Exit(LifecycleContext&) const {}
};

} // namespace Engine
