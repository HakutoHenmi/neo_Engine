#pragma once
#include "System.h"
#include "Coordinator.h"
#include "../Graphics/Renderer.h"
#include <memory>

namespace TDEngine {
namespace ECS {

class RenderSystem : public System {
public:
	// Coordinatorへの参照を受け取って初期化する
	void Init(Coordinator* coordinator);

	// 毎フレームの描画処理（Rendererの描画キューへの登録）
	void Draw();

private:
	Coordinator* coordinator_ = nullptr;
};

} // namespace ECS
} // namespace TDEngine
