// MotifShape.h
// 姿の型紙に当てはめた 3 つの物から、武器の形の部品を作る (H3。エンジンに依存しない)。
//  - 枠に入った物は、物の形そのもの (ItemProfile の距離の場) を、枠の置き方 (MotifPlace) の向きと大きさで置く。
//    主な枠の物の一番長い向きの半分を 1 とし、ほかの枠は scale 倍。向きは、物の一番長い向き・一番短い向きを long・thin の向きへ回す
//    (keep の枠は物の向きのまま: 立った物 (だるま・招き猫) を胴や頭にするとき)
//  - 空いた枠は、fill の形 (だ円・カプセル・箱) をスライムだけで作る。fill の無い枠は作らない (主な枠はいつも作る)
//  - 置く所: 土台の枠 (on。無ければ主な枠) の真ん中から、土台の半分の大きさ x at、自分の半分の大きさ x off だけずらす。左右の枠は右に置いて鏡で 2 つ
//  - 太さの下限は SdfApplyFloor (物の形は薄い向きの引き伸ばしと細い所の太らせ)
//  - 形の決まり (WeaponShapeGrammar.h と同じ考え。形として良くない並びを作らない):
//    1. 真ん中の線上の部品は、体の前後の端から出る長さを、体の半分の大きさの ShapeFrontReach 倍と、その部品の太さまでにする (細い棒を前後に長く出さない)
//    2. 上への突き出しは、体の上から体の高さの半分まで (左右の部品で土台のある物は、土台の上から土台の高さの半分まで)
//    3. 左右の組の部品は、体の前後の真ん中寄り (前後 ShapePairReach x 体の長さの半分まで) に置く
//  - 最後に一番低い所を床 (y = 0) にそろえる
#pragma once
#include "MotifRules.h"
#include "WeaponShapeGrammar.h"
#include <algorithm>
#include <array>
#include <vector>
namespace Game::Chrono {
namespace MotifShapeDetail {
inline Vec AxisVec(int a) {
	return a == 0 ? Vec{1, 0, 0} : a == 1 ? Vec{0, 1, 0} : Vec{0, 0, 1};
}
inline float Comp(Vec v, int a) {
	return a == 0 ? v.x : a == 1 ? v.y : v.z;
}
inline Vec Mul(Vec a, Vec b) {
	return {a.x * b.x, a.y * b.y, a.z * b.z};
}
// 物の場の向き (場の x y z) を、世界のどの向きにするか。axes[m] が場の向き m の世界の向き
// (立つ物 (盆栽・だるま・消火栓) は、上下を保って、前後左右だけ回す。枠が一番長い向きを上下にしたいときだけ倒す)
inline std::array<Vec, 3> Orient(const MotifPlace& pl, Vec h, bool upright) {
	std::array<Vec, 3> axes{AxisVec(0), AxisVec(1), AxisVec(2)};
	if (pl.keep || (pl.longAxis < 0 && pl.thinAxis < 0))
		return axes;
	if (upright && pl.longAxis != 1) {
		// 前後左右の長い方を long の向きへ (long が上下でも無いときは、thin の向きに短い方を)
		const bool xLonger = h.x >= h.z;
		const int want = pl.longAxis == 0 || pl.longAxis == 2 ? pl.longAxis
		                 : pl.thinAxis == 0                   ? 2
		                 : pl.thinAxis == 2                   ? 0
		                                                      : -1;
		if (want >= 0 && (want == 0) != xLonger)
			axes = {AxisVec(2), AxisVec(1), AxisVec(0)};
		return axes;
	}
	std::array<int, 3> order{0, 1, 2};
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return Comp(h, a) > Comp(h, b); });
	std::array<int, 3> to{-1, -1, -1};
	std::array<bool, 3> taken{};
	if (pl.longAxis >= 0) {
		to[size_t(order[0])] = pl.longAxis;
		taken[size_t(pl.longAxis)] = true;
	}
	if (pl.thinAxis >= 0 && !taken[size_t(pl.thinAxis)]) {
		to[size_t(order[2])] = pl.thinAxis;
		taken[size_t(pl.thinAxis)] = true;
	}
	// 残りの向きは、なるべく元の向きのまま
	for (int m : order)
		if (to[size_t(m)] < 0 && !taken[size_t(m)]) {
			to[size_t(m)] = m;
			taken[size_t(m)] = true;
		}
	for (int m : order)
		for (int w = 0; w < 3 && to[size_t(m)] < 0; ++w)
			if (!taken[size_t(w)]) {
				to[size_t(m)] = w;
				taken[size_t(w)] = true;
			}
	for (int m = 0; m < 3; ++m)
		axes[size_t(m)] = AxisVec(to[size_t(m)]);
	return axes;
}
// スライムだけで作る形 (half は世界の向きの半分の大きさ)。カプセルは一番長い向きを軸にする
inline SdfPart Fill(SdfKind kind, Vec half) {
	SdfPart s;
	s.kind = kind;
	s.half = half;
	s.round = std::min({half.x, half.y, half.z}) * .6f;
	if (kind == SdfKind::Capsule) {
		int w = 2;
		if (half.x > Comp(half, w))
			w = 0;
		if (half.y > Comp(half, w))
			w = 1;
		const int u = w == 0 ? 1 : 0, v = 3 - w - u;
		s.axes = {AxisVec(u), AxisVec(v), AxisVec(w)};
		s.half = {Comp(half, u), Comp(half, v), Comp(half, w)};
	}
	return s;
}
} // namespace MotifShapeDetail

// 型紙に当てはめた物から部品を作る。items は 3 つの物のプロフィール (無い物は nullptr)。
// 部品の part は、その部品になった物の番号 (1〜3)。スライムだけの部品は 0。parts[0] は主な枠
inline std::vector<SdfPart> BuildMotifParts(
    const Motif& motif, const MotifChoice& choice, const std::array<const ItemProfile*, 3>& items, float minHalf) {
	using namespace MotifShapeDetail;
	const auto& fields = SdfFields();
	const int slots = int(motif.slots.size());
	std::vector<int> itemOf(size_t(slots), -1), partOf(size_t(slots), -1);
	for (int i = 0; i < 3; ++i)
		if (choice.slotOf[size_t(i)] >= 0 && choice.slotOf[size_t(i)] < slots)
			itemOf[size_t(choice.slotOf[size_t(i)])] = i;
	int main = 0;
	for (int s = 0; s < slots; ++s)
		if (motif.slots[size_t(s)].main) {
			main = s;
			break;
		}
	std::vector<int> order{main};
	for (int s = 0; s < slots; ++s)
		if (s != main)
			order.push_back(s);
	std::vector<SdfPart> parts;
	for (int s : order) {
		const auto& slot = motif.slots[size_t(s)];
		const auto& pl = slot.place;
		const int it = itemOf[size_t(s)];
		const ItemProfile* prof = it >= 0 ? items[size_t(it)] : nullptr;
		SdfPart p;
		if (prof && prof->field >= 0 && prof->field < int(fields.size())) {
			const Vec h = fields[size_t(prof->field)].half;
			const float big = std::max({h.x, h.y, h.z, 1e-4f});
			p.kind = SdfKind::Item;
			p.field = prof->field;
			p.scale = (s == main ? 1.f : pl.scale) / big;
			p.axes = Orient(pl, h, prof->Has(ItemUpright));
			p.half = h * p.scale;
			p.part = it + 1;
		} else if (pl.hasFill || s == main) {
			p = Fill(pl.hasFill ? pl.fillKind : SdfKind::Ellipsoid, pl.hasFill ? pl.fillHalf : Vec{.8f, .6f, .8f});
			p.part = 0;
		} else
			continue;
		p.mirror = slot.mirror;
		p.motion = pl.motion;
		SdfApplyFloor(p, minHalf);
		if (!parts.empty()) {
			const SdfPart& base =
			    pl.on >= 0 && partOf[size_t(pl.on)] >= 0 ? parts[size_t(partOf[size_t(pl.on)])] : parts[0];
			p.center = base.center + Mul(pl.at, SdfExtent(base)) + Mul(pl.off, SdfExtent(p));
		}
		partOf[size_t(s)] = int(parts.size());
		parts.push_back(p);
	}
	// 形の決まり
	const SdfPart& body = parts[0];
	const Vec bc = body.center, be = SdfExtent(body);
	const float bodyTop = bc.y + be.y * (1 + ShapeTopReach), reach = ShapeFrontReach * std::max(be.x, be.z);
	for (size_t i = 1; i < parts.size(); ++i) {
		auto& p = parts[i];
		const Vec e = SdfExtent(p);
		if (!p.mirror) {
			// 1. 真ん中の線上の部品: 前後に出る長さは、reach と、その部品の太さ (幅と高さの大きい方) まで
			p.center.x = 0;
			const float allow = std::min(reach, 2 * std::max(e.x, e.y));
			const float front = p.center.z + e.z - (bc.z + be.z), back = (bc.z - be.z) - (p.center.z - e.z);
			if (front > allow)
				p.center.z -= front - allow;
			if (back > allow)
				p.center.z += back - allow;
		} else {
			// 3. 左右の組は前後の真ん中寄り
			p.center.z = std::clamp(p.center.z, bc.z - be.z * ShapePairReach, bc.z + be.z * ShapePairReach);
			p.center.x = std::max(p.center.x, e.x * .5f);
		}
	}
	// 2. 上への突き出し (土台のある左右の部品は、土台を決めたあとで)
	for (int pass = 0; pass < 2; ++pass)
		for (int s : order) {
			const int at = partOf[size_t(s)];
			if (at <= 0)
				continue;
			auto& p = parts[size_t(at)];
			const auto& pl = motif.slots[size_t(s)].place;
			const bool onBase = p.mirror && pl.on >= 0 && partOf[size_t(pl.on)] >= 0;
			if ((pass == 1) != onBase)
				continue;
			float top = bodyTop;
			if (onBase) {
				const SdfPart& base = parts[size_t(partOf[size_t(pl.on)])];
				top = std::max(top, base.center.y + SdfExtent(base).y * (1 + ShapeTopReach));
			}
			const float h = SdfExtent(p).y;
			if (p.center.y + h > top)
				p.center.y = std::max(top - h, bc.y);
		}
	// 床にそろえる・動く部品の支点 (翼は体の横の付け根)
	float low = 1e9f;
	for (const auto& p : parts)
		low = std::min(low, p.center.y - SdfExtent(p).y);
	for (auto& p : parts) {
		p.center.y -= low;
		p.pivot = p.motion == SdfMotion::Flap ? Vec{bc.x + be.x * .9f, p.center.y, p.center.z} : p.center;
	}
	return parts;
}
} // namespace Game::Chrono
