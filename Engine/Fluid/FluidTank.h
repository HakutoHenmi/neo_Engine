#pragma once
#include "../Math/Matrix4x4.h"
#include <array>

namespace Engine {
struct FluidTankPanel {
	Vector3 center;
	Vector3 size; // 外寸 (標準の立方体は [-1, 1] にまたがる)。
};
inline std::array<FluidTankPanel, 5> MakeFluidTankPanels(float width, float height, float depth, float thickness) {
	// 原点は内側の床。高さが正の水が底の上に乗る。
	return {
	    {{{0, -thickness * .5f, 0}, {width + 2 * thickness, thickness, depth + 2 * thickness}},
	     {{-(width + thickness) * .5f, height * .5f, 0}, {thickness, height, depth + 2 * thickness}},
	     {{(width + thickness) * .5f, height * .5f, 0}, {thickness, height, depth + 2 * thickness}},
	     {{0, height * .5f, -(depth + thickness) * .5f}, {width, height, thickness}},
	     {{0, height * .5f, (depth + thickness) * .5f}, {width, height, thickness}}}};
}
} // namespace Engine
