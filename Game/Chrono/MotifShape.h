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
// ---- (J1) 型紙の土台と、物を並べる所 ----
// 土台: 型紙ごとに決まったスライムの形。1 つ目の物は、土台の比率 (幅・高さ・長さ) だけを ±MotifBaseFlex まで変える (外形がぶれないように)
inline constexpr float MotifBaseFlex = .15f;
// 1 つ目の物の比率から、土台の向きごとの倍率 (3 つをかけると 1)。物の前後左右の長い方を長さ (z)、短い方を幅 (x) とみる
inline Vec MotifBaseScale(const ItemProfile* body) {
	if (!body)
		return {1, 1, 1};
	const Vec s = body->size;
	const float len = std::max({s.x, s.z, 1e-3f}), wid = std::max(std::min(s.x, s.z), 1e-3f), h = std::max(s.y, 1e-3f);
	const float mean = std::cbrt(len * wid * h);
	auto f = [&](float v) { return std::clamp(std::pow(v / mean, .35f), 1 - MotifBaseFlex, 1 + MotifBaseFlex); };
	Vec k{f(wid), f(h), f(len)};
	const float c = std::cbrt(k.x * k.y * k.z);
	return k * (1 / c);
}
// 土台の部品を作る (scale は MotifBaseScale。部品の part は 0 = スライム)。太さの下限は呼ぶ側で SdfApplyFloor
inline std::vector<SdfPart> BuildMotifBase(const Motif& motif, Vec scale) {
	using namespace MotifShapeDetail;
	std::vector<SdfPart> parts;
	for (const auto& b : motif.base) {
		SdfPart s;
		s.kind = b.kind;
		s.center = Mul(b.center, scale);
		s.mirror = b.mirror;
		s.part = 0;
		// 軸の向き w = 世界の軸 a、u・v は残りの 2 つ (a の次、その次)
		const int a = std::clamp(b.axis, 0, 2), u = (a + 1) % 3, v = (a + 2) % 3;
		if (b.kind == SdfKind::Cone) {
			const float across = (Comp(scale, u) + Comp(scale, v)) * .5f;
			s.half = {b.half.x * across, b.half.y * across, b.half.z * Comp(scale, a)};
			s.axes = {AxisVec(u), AxisVec(v), AxisVec(a)};
		} else if (b.kind == SdfKind::Capsule) {
			const Vec h = Mul(b.half, scale);
			s.half = {Comp(h, u), Comp(h, v), Comp(h, a)};
			s.axes = {AxisVec(u), AxisVec(v), AxisVec(a)};
		} else {
			s.half = Mul(b.half, scale);
		}
		s.round = b.round * (scale.x + scale.y + scale.z) / 3;
		parts.push_back(s);
	}
	return parts;
}
// 並べる所の 1 つの点: 場所・外への向き (面の法線)・並びの向き
struct MotifSocketPoint {
	Vec pos{}, normal{0, 1, 0}, tangent{1, 0, 0};
};
// 並べる所に count 個の点を作る (右側。mirror の所は呼ぶ側で x を反転して左にも置く)。
// 点は土台の面に寄せる (データの点が少しずれていても面の上に乗る)
inline std::vector<MotifSocketPoint>
    MotifSocketPoints(const MotifSocket& socket, int count, Vec scale, const std::vector<SdfPart>& base, float blend) {
	using namespace MotifShapeDetail;
	using ShapeGrammarDetail::Norm;
	std::vector<MotifSocketPoint> points;
	count = std::max(1, count);
	auto distance = [&](Vec p) { return SdfShapeDistance(base, p, blend); };
	auto gradient = [&](Vec p) {
		const float e = .02f;
		return Norm(
		    {distance(p + Vec{e, 0, 0}) - distance(p - Vec{e, 0, 0}),
		     distance(p + Vec{0, e, 0}) - distance(p - Vec{0, e, 0}),
		     distance(p + Vec{0, 0, e}) - distance(p - Vec{0, 0, e})});
	};
	for (int i = 0; i < count; ++i) {
		MotifSocketPoint q;
		if (socket.pattern == MotifPattern::Ring) {
			// 軸が y なら u = x・v = z (90 度が前)、x なら u = z・v = y、z なら u = x・v = y
			const Vec a = socket.axis;
			const Vec u = std::abs(a.y) > .5f ? Vec{1, 0, 0} : std::abs(a.x) > .5f ? Vec{0, 0, 1} : Vec{1, 0, 0};
			const Vec v = std::abs(a.y) > .5f ? Vec{0, 0, 1} : Vec{0, 1, 0};
			const bool closed = std::abs(socket.angle1 - socket.angle0) >= 359.f;
			const float t = closed ? float(i) / float(count) : count == 1 ? .5f : float(i) / float(count - 1);
			const float th = (socket.angle0 + (socket.angle1 - socket.angle0) * t) * 3.14159265f / 180.f;
			q.pos = socket.at + u * (socket.radius1 * std::cos(th)) + v * (socket.radius2 * std::sin(th));
			q.tangent = Norm(u * (-socket.radius1 * std::sin(th)) + v * (socket.radius2 * std::cos(th)));
		} else if (socket.pattern == MotifPattern::Line) {
			const float t = count == 1 ? .5f : float(i) / float(count - 1);
			q.pos = socket.from + (socket.to - socket.from) * t;
			q.tangent = Norm(socket.to - socket.from);
		} else {
			q.pos = socket.at;
		}
		q.pos = Mul(q.pos, scale);
		q.tangent = Norm(Mul(q.tangent, scale));
		// 面に寄せる (2 回)。外への向きは、データの normal か、面の向き
		for (int k = 0; k < 2; ++k)
			q.pos = q.pos - gradient(q.pos) * distance(q.pos);
		q.normal = SdfLength(socket.normal) > 0 ? Norm(socket.normal) : gradient(q.pos);
		points.push_back(q);
	}
	return points;
}
// ---- (J2) 並べる所に物を並べて埋め込む ----
//  スライムの面は粗い (体積 CraftShapeVolume に太さの下限 CraftMinThick なので、体の幅に読める出っぱりは 2 つくらい)。
//  そこで、スライムは土台の決まった形だけを作り、並べる物は本物のモデル (細かさが流体に縛られない) を、土台の面に半分沈めて置く。
//  ここでは置き方 (場所・向き・大きさ) だけを決める (描くのはゲームの側。J3)
//  - 役は並べた順で決まる: 1 つ目 = 土台の比率・2 つ目 = 繰り返し (並べる所に何個も)・3 つ目 = 紋章 (1 つ)。配慮の物は置かない (nullptr)
//  - 個数と大きさ: 1 つ目の物に比べて小さい物ほど多く (4・5・7 個)。並べる所の長さを個数で割り、間隔は大きさの MotifPitch 倍。
//    大きさ (並びの向きの半分の大きさ) は MotifModelMin〜Max
//  - 向き: 長い物は、輪では外へ放射 (車輪のスポーク)・列では列を横切る向き。そのほかの物は一番薄い向きを外 (面の向き) へ、一番長い向きを並びの向きへ。
//    立つ物を横向きの面 (紋章) に置くときは、上下を保つ。どれも面に沿って寝かせるので、面から出るのは物の厚みの半分まで
//  - 埋め込み: 物の真ん中を、太さの下限を入れた土台の面から、外への向きの半分の厚みの MotifModelOut 倍だけ外に置く (6〜7 割が沈む)
inline constexpr float MotifPitch = 1.15f, MotifModelMin = .15f, MotifModelMax = .34f, MotifEmblemSize = .3f,
                       MotifModelOut = .4f;
inline constexpr int MotifRepeatMin = 3, MotifRepeatMax = 8;
// (J3) 並べたモデルを出す時: 形になってからの秒 (液体が形に寄るのを待つ) と、出てくるまでの秒
inline constexpr float MotifModelDelay = .35f, MotifModelFade = .3f;
// (J3) 液体の見える面は、表面張力で形の面より少し内側になる (特に左右の端)。並べる所の面を、太さの下限の半分の MotifSurfaceInset 倍だけ内側とみる
inline constexpr float MotifSurfaceInset = .5f;
// 置く物 1 つ: 3 つの中の番号・場所・向き (axes[m] が物の場の向き m の世界の向き)・場の座標から世界への倍率・
// 左右に映した物か (向きが鏡になる)・繰り返しか紋章か
struct MotifModel {
	int item = 0;
	Vec pos{};
	std::array<Vec, 3> axes{Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}};
	float scale = 1;
	bool mirrored = false, emblem = false;
};
namespace MotifShapeDetail {
inline float VecDot(Vec a, Vec b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
// 物の場の向き m を、世界のどの向きにするか (n は外、spread は長い物を伸ばす向き、along は並びの向き)
inline std::array<Vec, 3> ItemAxes(Vec h, bool longItem, bool upright, Vec n, Vec spread, Vec along) {
	using ShapeGrammarDetail::Cross;
	using ShapeGrammarDetail::Norm;
	auto flat = [&](Vec v) { return Norm(v - n * VecDot(v, n)); };
	std::array<int, 3> order{0, 1, 2};
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return Comp(h, a) > Comp(h, b); });
	std::array<Vec, 3> axes{};
	if (upright && std::abs(n.y) < .5f) {
		// 上下を保つ: 場の y は上。x と z の薄い方を外へ
		const Vec up = Norm(Vec{0, 1, 0} - n * n.y), side = Norm(Cross(up, n));
		const bool xThin = h.x <= h.z;
		axes[1] = up;
		axes[xThin ? 0 : 2] = n;
		axes[xThin ? 2 : 0] = side;
		return axes;
	}
	const Vec first = flat(longItem ? spread : along);
	const Vec third = Norm(Cross(n, first));
	axes[size_t(order[0])] = first;
	axes[size_t(order[2])] = n;
	axes[size_t(order[1])] = third;
	return axes;
}
} // namespace MotifShapeDetail
// 並べる所の長さ (輪は弧の長さ、列は端から端)
inline float MotifSocketLength(const MotifSocket& s, Vec scale) {
	using namespace MotifShapeDetail;
	if (s.pattern == MotifPattern::Ring) {
		const float span = std::abs(s.angle1 - s.angle0) * 3.14159265f / 180.f;
		const float across = (scale.x + scale.y + scale.z) / 3;
		return span * (s.radius1 + s.radius2) * .5f * across;
	}
	if (s.pattern == MotifPattern::Line)
		return SdfLength(Mul(s.to - s.from, scale));
	return 0;
}
// 土台の部品を作り (part 1・太さの下限を入れる)、並べる物の置き方を models に返す。items は 3 つの物 (置かない物は nullptr。
// items[0] は土台の比率だけ)。左右の所の物は、右と左 (鏡) の 2 つを返す。最後に一番低い所を床 (y = 0) にそろえる (物も一緒に)
inline std::vector<SdfPart> BuildSocketParts(
    const Motif& motif, const std::array<const ItemProfile*, 3>& items, float minHalf,
    std::vector<MotifModel>* models = nullptr) {
	using namespace MotifShapeDetail;
	using ShapeGrammarDetail::Cross;
	using ShapeGrammarDetail::Norm;
	const auto& fields = SdfFields();
	const Vec scale = MotifBaseScale(items[0]);
	std::vector<SdfPart> parts = BuildMotifBase(motif, scale);
	for (auto& s : parts) {
		s.part = 1;
		SdfApplyFloor(s, minHalf);
	}
	std::vector<MotifModel> placed;
	for (const auto& socket : motif.sockets) {
		const int item = socket.emblem ? 2 : 1;
		const ItemProfile* prof = items[size_t(item)];
		if (!prof || prof->field < 0 || prof->field >= int(fields.size()))
			continue;
		const Vec h = fields[size_t(prof->field)].half;
		const float big = std::max({h.x, h.y, h.z, 1e-4f});
		int count = 1;
		float size = MotifEmblemSize;
		if (!socket.emblem) {
			// 1 つ目の物に比べて小さい物ほど多く並べる
			const float ratio = items[0] ? prof->realSize / std::max(1e-4f, items[0]->realSize) : 1.f;
			const int want = ratio < .5f ? 7 : ratio < 1.f ? 5 : 4;
			const float length = MotifSocketLength(socket, scale);
			const bool closed =
			    socket.pattern == MotifPattern::Ring && std::abs(socket.angle1 - socket.angle0) >= 359.f;
			const float gaps = float(closed ? want : want - 1);
			size = std::clamp(length / (2 * MotifPitch * std::max(1.f, gaps)), MotifModelMin, MotifModelMax);
			count =
			    std::clamp(int(length / (2 * MotifPitch * size)) + (closed ? 0 : 1), MotifRepeatMin, MotifRepeatMax);
		}
		const bool longItem = prof->shape == ItemShapeClass::Long;
		for (const auto& q : MotifSocketPoints(socket, count, scale, parts, CraftBlend)) {
			// 長い物を伸ばす向き: 輪は中心から外 (放射)、列は列を横切る向き、点は上 (面が上向きなら前)
			Vec spread{0, 1, 0};
			if (socket.pattern == MotifPattern::Ring)
				spread = Norm(q.pos - Mul(socket.at, scale));
			else if (socket.pattern == MotifPattern::Line)
				spread = Norm(Cross(q.normal, q.tangent));
			else if (std::abs(q.normal.y) > .9f)
				spread = {0, 0, 1};
			MotifModel m;
			m.item = item;
			m.axes = ItemAxes(h, longItem, prof->Has(ItemUpright), q.normal, spread, q.tangent);
			m.scale = size / big;
			const float out =
			    m.scale * (std::abs(VecDot(m.axes[0], q.normal)) * h.x + std::abs(VecDot(m.axes[1], q.normal)) * h.y +
			               std::abs(VecDot(m.axes[2], q.normal)) * h.z);
			m.pos = q.pos + q.normal * (out * MotifModelOut - minHalf * MotifSurfaceInset);
			m.emblem = socket.emblem;
			placed.push_back(m);
			if (socket.mirror) {
				MotifModel l = m;
				l.pos.x = -l.pos.x;
				for (auto& a : l.axes)
					a.x = -a.x;
				l.mirrored = true;
				placed.push_back(l);
			}
		}
	}
	// 床にそろえる
	float low = 1e9f;
	for (const auto& p : parts)
		low = std::min(low, p.center.y - SdfExtent(p).y);
	for (auto& p : parts) {
		p.center.y -= low;
		p.pivot = p.center;
	}
	for (auto& m : placed)
		m.pos.y -= low;
	if (models)
		*models = std::move(placed);
	return parts;
}
} // namespace Game::Chrono
