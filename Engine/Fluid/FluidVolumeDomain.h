#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Engine::FluidVolumeDomain {
// 再構成の半径と、フィルター / クロスフェード用のセル 4 つ分を確保する。
// 粒子数が一定のまま、半径と解像度を一緒に大きくする。
inline float FitCell(
    float width, float height, float depth, uint32_t columns, uint32_t rows, float radius = 1.5f,
    float minimum = .18f) {
	return (
	    std::
	        max)({minimum, (width + 2 * radius) / float(columns - 8), (depth + 2 * radius) / float(columns - 8), (height + 2 * radius) / float(rows - 8)});
}
inline float StableCell(float required, float previous) {
	// 呼吸や向きの変化のような小さな変化で、毎フレーム格子全体の大きさを変えてはいけない。
	// 大きくするのはすぐ、小さくするのは十分に縮んだときだけにする。
	if (previous >= required && required > previous * .85f)
		return previous;
	return std::ceil((required - 1e-6f) / .01f) * .01f;
}
inline float Origin(float low, float high, float cell, uint32_t size) {
	// 体に合わせたローカルのボリュームは連続的に追従させる。原点をワールドのボクセルに
	// 合わせると、大きな武器でサンプリングの位相が目に見える段差で変わってしまった。
	// TemporalDensity はすでに任意の前回 / 今回の原点の間で再投影している。
	return (low + high) * .5f - float(size) * cell * .5f;
}
} // namespace Engine::FluidVolumeDomain
