#pragma once
#include <algorithm>
#include <cmath>
namespace Engine {
// 保存された個数は従来どおり 60 Hz あたりの意味を保つ。ソルバーの時間の上限に合わせる。
inline int AccumulateFluidEmission(int referenceCount, float dt, float& remainder) {
	if (referenceCount <= 0 || !std::isfinite(dt) || dt <= 0)
		return 0;
	remainder += (std::min)(referenceCount, 16000) * 60.0f * (std::min)(dt, 1.0f / 30.0f);
	int count = static_cast<int>(remainder);
	remainder -= count;
	return count;
}
inline int LimitFluidEmission(int count, int limit, int& emitted) {
	count = (std::max)(0, count);
	if (limit > 0)
		count = (std::min)(count, (std::max)(0, limit - emitted));
	// 上限の無いエミッターには、いずれ桁あふれするカウンターは要らない。
	if (limit > 0)
		emitted += count;
	return count;
}
} // namespace Engine
