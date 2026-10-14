#pragma once
#include "Matrix4x4.h"
#include <array>

namespace Engine {
struct FluidTankPanel {
    Vector3 center;
    Vector3 size; // Full dimensions; the stock cube spans [-1, 1].
};
inline std::array<FluidTankPanel, 5> MakeFluidTankPanels(float width, float height, float depth, float thickness) {
    // Origin is the inner floor, so positive-height water rests above the base.
    return {{
        {{0, -thickness * .5f, 0}, {width + 2 * thickness, thickness, depth + 2 * thickness}},
        {{-(width + thickness) * .5f, height * .5f, 0}, {thickness, height, depth + 2 * thickness}},
        {{ (width + thickness) * .5f, height * .5f, 0}, {thickness, height, depth + 2 * thickness}},
        {{0, height * .5f, -(depth + thickness) * .5f}, {width, height, thickness}},
        {{0, height * .5f,  (depth + thickness) * .5f}, {width, height, thickness}}
    }};
}
}
