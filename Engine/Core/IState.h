#pragma once

namespace Engine {

// 状態オブジェクトは振る舞いだけを持つ。アクターごとのデータはコンテキスト側に置くので、
// 変更されない 1 つの状態インスタンスを多くの ECS エンティティで安全に共有できる。
template<class UpdateContext, class LifecycleContext = UpdateContext> class IState {
public:
	virtual ~IState() = default;
	virtual void Enter(LifecycleContext&) const {}
	virtual void Update(UpdateContext&) const = 0;
	virtual void Exit(LifecycleContext&) const {}
};

} // namespace Engine
