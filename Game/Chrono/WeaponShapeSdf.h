// WeaponShapeSdf.h
// 変形武器の形を、なめらかな基本形 (球・だ円体・カプセル・角の丸い箱) で作る (F2。エンジンに依存しない)。
//  - 形は「距離の関数」(SDF: 外なら正、中なら負の、表面までの距離) で表し、部品どうしはなめらかにつなぐ (smooth union)。
//    つなぎ目が丸くふくらむので、液体のスライムらしく、表面でならされても形が崩れにくい
//  - 物の形 (モデルを箱に分けたもの) は、主軸 (分散の向き) で 1 つの基本形に置きかえる:
//    長い物はカプセル、平たい物は角の丸い板、それ以外はだ円体。一番長い向きを前 (+z)、一番短い向きを上下 (y) にそろえる
//  - 太さの下限: どの部品も、最後の大きさで太さ minThick 以上にする (細い所は液体の表面で消えるため。F1 で測った値)
#pragma once
#include "ChronoRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace Game::Chrono {
// (Cone は w の向きの円すい台: 根もと (-half.z) の半径 half.x、先 (+half.z) の半径 half.y。先は丸める)
// (Item は物の形そのもの: 物のモデルから作った距離の場 (SdfFields の field 番目) を scale 倍して置く。H1 の作り直し)
enum class SdfKind : uint8_t { Ellipsoid, Capsule, RoundBox, Cone, Item };
// 物の形の距離の場: 箱の並び (lo・cell・nx ny nz)・表面までの距離 (外が正、中が負)・その所の太さ (中に入る一番大きな球の直径。外は近くの物の太さ)。
// 場の座標は、物の範囲の真ん中が 0 (half は範囲の半分の大きさ)
struct SdfField {
	Vec lo{}, half{};
	float cell = 1;
	int nx = 0, ny = 0, nz = 0;
	std::vector<float> dist, thick;
	float maxThick = 1;
	size_t At(int x, int y, int z) const {
		return size_t((z * ny + y) * nx + x);
	}
	// なめらかに (8 つの箱から) 引く。範囲の外は、範囲のふちの値に、ふちまでの距離を足す
	void Sample(Vec p, float& d, float& t) const {
		const Vec hi = lo + Vec{float(nx - 1), float(ny - 1), float(nz - 1)} * cell;
		const Vec c{std::clamp(p.x, lo.x, hi.x), std::clamp(p.y, lo.y, hi.y), std::clamp(p.z, lo.z, hi.z)};
		const float fx = (c.x - lo.x) / cell, fy = (c.y - lo.y) / cell, fz = (c.z - lo.z) / cell;
		const int x0 = std::min(int(fx), nx - 2), y0 = std::min(int(fy), ny - 2), z0 = std::min(int(fz), nz - 2);
		const float ax = fx - float(x0), ay = fy - float(y0), az = fz - float(z0);
		auto lerp = [&](const std::vector<float>& f) {
			float v = 0;
			for (int k = 0; k < 8; ++k) {
				const int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
				v += f[At(x0 + dx, y0 + dy, z0 + dz)] * (dx ? ax : 1 - ax) * (dy ? ay : 1 - ay) * (dz ? az : 1 - az);
			}
			return v;
		};
		const Vec off = p - c;
		const float outside = std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z);
		d = lerp(dist) + outside;
		// (範囲の外は太さが分からない所: 0。ふちの太さを使うと、太らせが範囲の外へどこまでも伸びて、細い棒や板ができる)
		t = outside > 0 ? 0.f : lerp(thick);
	}
};
// 物の形の、細い所を太らせる量 (太さの下限に対する割合。1 なら下限まで)
inline constexpr float SdfItemThicken = .5f;
inline std::vector<SdfField>& SdfFields() {
	static std::vector<SdfField> fields;
	return fields;
}
// 部品の動き (F5): 無し・はばたく (翼。支点の前後の軸のまわりに上下)・回る (羽根。支点の上下の軸のまわり)・
// 縮む (ばねの足。支点 (床) へ向けて縮む)・ゆらぐ (炎。支点のまわりに大きさが揺れる)・弾む (分身の玉。上下に揺れる)
enum class SdfMotion : uint8_t { None, Flap, Rotor, Leg, Flicker, Bob };
// 基本形: 種類・中心・向き (右 u・上 v・前 w の単位ベクトル)・半分の大きさ (u v w の向き。カプセルは w が長さの半分 (端の球を含む)、半径は u)・
// 角の丸み (箱)・部品の番号 (1 本体・2 前・3 左右)・左右に鏡で 1 つずつ置くか
// (front は体の前の端に付く太く短い先 (ドリル・砲口)、flat は低く平たい部品 (水たまり)。どちらも真ん中の線上で体の外へ出てよい部品)
struct SdfPart {
	SdfKind kind = SdfKind::Ellipsoid;
	Vec center{};
	std::array<Vec, 3> axes{Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}};
	Vec half{1, 1, 1};
	float round = .1f;
	int part = 1;
	bool mirror = false, front = false, flat = false;
	SdfMotion motion = SdfMotion::None;
	Vec pivot{};
	// (物の形 (Item) だけ: 場の番号・場の座標から置く大きさへの倍率・太らせる下限 (半分の太さ。場の座標))
	int field = -1;
	float scale = 1, thicken = 0;
	Vec stretch{1, 1, 1};
	float Thickness() const {
		return 2 * std::min({half.x, half.y, half.z});
	}
};
inline float SdfLength(Vec v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
// 部品 1 つの距離 (mirror は呼ぶ側で p.x を反転して 2 回呼ぶ)
inline float SdfPartDistance(const SdfPart& s, Vec p) {
	const Vec d = p - s.center;
	const Vec q{
	    d.x * s.axes[0].x + d.y * s.axes[0].y + d.z * s.axes[0].z,
	    d.x * s.axes[1].x + d.y * s.axes[1].y + d.z * s.axes[1].z,
	    d.x * s.axes[2].x + d.y * s.axes[2].y + d.z * s.axes[2].z};
	switch (s.kind) {
	case SdfKind::Ellipsoid: {
		// だ円体の近似の距離 (Inigo Quilez): k0 (k0 - 1) / k1
		const Vec r = s.half;
		const float k0 = SdfLength({q.x / r.x, q.y / r.y, q.z / r.z}),
		            k1 = SdfLength({q.x / (r.x * r.x), q.y / (r.y * r.y), q.z / (r.z * r.z)});
		return k1 > 1e-6f ? k0 * (k0 - 1) / k1 : -std::min({r.x, r.y, r.z});
	}
	case SdfKind::Capsule: {
		// w の向きの線分 (長さの半分 half.z - 半径) から半径 half.x。上下の太さは half.y で少しつぶせる
		const float radius = s.half.x, len = std::max(0.f, s.half.z - radius),
		            squash = s.half.y / std::max(1e-4f, radius);
		const float z = std::clamp(q.z, -len, len);
		const Vec e{q.x, q.y / std::max(.2f, squash), q.z - z};
		return (SdfLength(e) - radius) * std::min(1.f, squash);
	}
	case SdfKind::RoundBox: {
		const float r = std::min({s.round, s.half.x, s.half.y, s.half.z});
		const Vec a{std::abs(q.x) - s.half.x + r, std::abs(q.y) - s.half.y + r, std::abs(q.z) - s.half.z + r};
		const Vec o{std::max(a.x, 0.f), std::max(a.y, 0.f), std::max(a.z, 0.f)};
		return SdfLength(o) + std::min(std::max({a.x, a.y, a.z}), 0.f) - r;
	}
	case SdfKind::Item: {
		// 物の形: 場を引き、太さの下限より細い所だけ、足りない分だけ外へ太らせる
		const auto& fields = SdfFields();
		if (s.field < 0 || s.field >= int(fields.size()))
			return 1e9f;
		// (stretch は薄い向きだけの引き伸ばし。輪郭を崩さずに太さの下限を満たす)
		float fd = 0, ft = 0;
		const Vec k{s.scale * s.stretch.x, s.scale * s.stretch.y, s.scale * s.stretch.z};
		fields[size_t(s.field)].Sample(Vec{q.x / k.x, q.y / k.y, q.z / k.z}, fd, ft);
		// (太さの下限より細い所だけ、足りない分を太らせる。太さは、薄い向きへ引き伸ばした分も入れて数える (平たい物の輪郭は太らない)。
		//  四角い物の角は太さを立方体で測るので太く、物から離れた所 (太さが分からない所) は太らせない)
		const float wide = ft * std::max({s.stretch.x, s.stretch.y, s.stretch.z});
		return (fd - (ft > 0 ? std::max(0.f, s.thicken - wide * .5f) : 0.f)) * std::min({k.x, k.y, k.z});
	}
	case SdfKind::Cone: {
		// 円すい台の距離 (Inigo Quilez の sdCappedCone)。軸は w
		const float h = s.half.z, r1 = s.half.x, r2 = s.half.y;
		const float qx = std::sqrt(q.x * q.x + q.y * q.y), qy = q.z;
		const float k1x = r2, k1y = h, k2x = r2 - r1, k2y = 2 * h;
		const float cax = qx - std::min(qx, qy < 0 ? r1 : r2), cay = std::abs(qy) - h;
		const float t =
		    std::clamp(((k1x - qx) * k2x + (k1y - qy) * k2y) / std::max(1e-6f, k2x * k2x + k2y * k2y), 0.f, 1.f);
		const float cbx = qx - k1x + k2x * t, cby = qy - k1y + k2y * t;
		const float sign = (cbx < 0 && cay < 0) ? -1.f : 1.f;
		return sign * std::sqrt(std::min(cax * cax + cay * cay, cbx * cbx + cby * cby));
	}
	}
	return 1e9f;
}
// 部品の、ワールドの向きごとの半分の大きさ (向きを回した部品は少し大きめに見積もる)
inline Vec SdfExtent(const SdfPart& s) {
	const Vec h =
	    s.kind == SdfKind::Cone ? Vec{std::max(s.half.x, s.half.y), std::max(s.half.x, s.half.y), s.half.z} : s.half;
	Vec e{};
	for (int k = 0; k < 3; ++k) {
		const Vec a = s.axes[size_t(k)];
		const float v = k == 0 ? h.x : k == 1 ? h.y : h.z;
		e = e + Vec{std::abs(a.x) * v, std::abs(a.y) * v, std::abs(a.z) * v};
	}
	return e;
}
// 太さの下限 (半分の大きさ minHalf): 円すいは根もとの半径と長さだけ (先は細くてよい。丸めて残る)
inline void SdfApplyFloor(SdfPart& s, float minHalf) {
	if (s.kind == SdfKind::Cone) {
		s.half.x = std::max(s.half.x, minHalf);
		s.half.y = std::max(s.half.y, minHalf * .6f);
		s.half.z = std::max(s.half.z, minHalf);
		return;
	}
	if (s.kind == SdfKind::Item) {
		// 物の形: 全体の厚みが下限より薄い向き (将棋の駒の厚み・鉛筆の太さ) だけを引き伸ばす (輪郭はそのまま)。
		// それでも細い所 (柄・骨組みの棒) は、その所だけ下限まで太らせる
		const auto& fields = SdfFields();
		if (s.field >= 0 && s.field < int(fields.size())) {
			const Vec h = fields[size_t(s.field)].half * s.scale;
			s.stretch = {
			    std::max(1.f, minHalf / std::max(1e-6f, h.x)), std::max(1.f, minHalf / std::max(1e-6f, h.y)),
			    std::max(1.f, minHalf / std::max(1e-6f, h.z))};
			s.half = {h.x * s.stretch.x, h.y * s.stretch.y, h.z * s.stretch.z};
		}
		s.thicken = minHalf * SdfItemThicken / std::max(1e-6f, s.scale);
		return;
	}
	s.half = {std::max(s.half.x, minHalf), std::max(s.half.y, minHalf), std::max(s.half.z, minHalf)};
	s.round = std::max(s.round, std::min(s.half.x, s.half.y) * .6f);
}
// なめらかなつなぎ (多項式の smooth min)。k が大きいほど、つなぎ目が太く丸くなる
inline float SdfSmoothMin(float a, float b, float k) {
	if (k <= 0)
		return std::min(a, b);
	const float h = std::clamp(.5f + .5f * (b - a) / k, 0.f, 1.f);
	return b + (a - b) * h - k * h * (1 - h);
}
// 全部の部品をつないだ距離と、一番近い部品の番号
// (index に一番近い部品の並びの番号)
inline float SdfShapeDistance(
    const std::vector<SdfPart>& parts, Vec p, float blend, int* owner = nullptr, int* index = nullptr) {
	float d = 1e9f, best = 1e9f;
	int who = 0, at = 0;
	for (size_t i = 0; i < parts.size(); ++i) {
		const auto& s = parts[i];
		for (float m : {1.f, -1.f}) {
			if (m < 0 && !s.mirror)
				continue;
			const float e = SdfPartDistance(s, {p.x * m, p.y, p.z});
			d = d > 1e8f ? e : SdfSmoothMin(d, e, blend);
			if (e < best) {
				best = e;
				who = s.part;
				at = int(i);
			}
		}
	}
	if (owner)
		*owner = who;
	if (index)
		*index = at;
	return d;
}

// ---- 物の形を基本形 1 つに置きかえる ----
// 対称な 3x3 の行列の固有ベクトル (ヤコビ法)。values は大きい順、vectors[i] がその向き
inline void SdfEigen(std::array<std::array<float, 3>, 3> a, std::array<float, 3>& values, std::array<Vec, 3>& vectors) {
	std::array<std::array<float, 3>, 3> v{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
	for (int sweep = 0; sweep < 24; ++sweep) {
		int p = 0, q = 1;
		float big = std::abs(a[0][1]);
		if (std::abs(a[0][2]) > big) {
			p = 0;
			q = 2;
			big = std::abs(a[0][2]);
		}
		if (std::abs(a[1][2]) > big) {
			p = 1;
			q = 2;
			big = std::abs(a[1][2]);
		}
		if (big < 1e-9f)
			break;
		const float theta = .5f * std::atan2(2 * a[p][q], a[q][q] - a[p][p]), c = std::cos(theta), s = std::sin(theta);
		for (int k = 0; k < 3; ++k) {
			const float kp = a[k][p], kq = a[k][q];
			a[k][p] = c * kp - s * kq;
			a[k][q] = s * kp + c * kq;
		}
		for (int k = 0; k < 3; ++k) {
			const float pk = a[p][k], qk = a[q][k];
			a[p][k] = c * pk - s * qk;
			a[q][k] = s * pk + c * qk;
		}
		for (int k = 0; k < 3; ++k) {
			const float kp = v[k][p], kq = v[k][q];
			v[k][p] = c * kp - s * kq;
			v[k][q] = s * kp + c * kq;
		}
	}
	std::array<int, 3> order{0, 1, 2};
	std::sort(
	    order.begin(), order.end(), [&](int x, int y) { return a[size_t(x)][size_t(x)] > a[size_t(y)][size_t(y)]; });
	for (int i = 0; i < 3; ++i) {
		const int k = order[size_t(i)];
		values[size_t(i)] = a[size_t(k)][size_t(k)];
		vectors[size_t(i)] = Vec{v[0][size_t(k)], v[1][size_t(k)], v[2][size_t(k)]};
	}
}
// 長さの割合の境目: 一番長い向きが 2 番目の何倍より長ければカプセル、一番短い向きが一番長い向きの何倍より短ければ板
inline constexpr float SdfLongRatio = 2.2f, SdfFlatRatio = .42f;
// 物の箱 (埋まった箱の真ん中の点) から基本形を 1 つ作る。大きさは一番長い向きの半分が 1 になるようにそろえる (置くときに倍率をかける)。
// 中心は原点、向きは前 (w) が一番長い向き・上 (v) が一番短い向き
inline SdfPart SdfFitPoints(const std::vector<Vec>& points) {
	SdfPart s;
	if (points.size() < 4) {
		s.half = {1, 1, 1};
		return s;
	}
	Vec mean{};
	for (const auto& p : points)
		mean = mean + p;
	mean = mean * (1 / float(points.size()));
	std::array<std::array<float, 3>, 3> cov{};
	for (const auto& p : points) {
		const Vec d = p - mean;
		const float c[3] = {d.x, d.y, d.z};
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				cov[size_t(i)][size_t(j)] += c[i] * c[j];
	}
	std::array<float, 3> values{};
	std::array<Vec, 3> vectors{};
	SdfEigen(cov, values, vectors);
	// 向きごとの広がり: 中心からの一番遠い所 (外れた点に引っぱられないよう 97% の所)
	std::array<float, 3> extent{};
	for (int k = 0; k < 3; ++k) {
		std::vector<float> along;
		along.reserve(points.size());
		for (const auto& p : points) {
			const Vec d = p - mean;
			along.push_back(
			    std::abs(d.x * vectors[size_t(k)].x + d.y * vectors[size_t(k)].y + d.z * vectors[size_t(k)].z));
		}
		std::nth_element(along.begin(), along.begin() + std::ptrdiff_t(along.size() * 97 / 100), along.end());
		extent[size_t(k)] = std::max(1e-3f, along[along.size() * 97 / 100]);
	}
	const float a = extent[0], b = extent[1], c = extent[2];
	// 前 = 一番長い向き・右 = 2 番目・上 = 一番短い (物そのものの向きは使わず、置く所で向きを決める)
	s.axes = {Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}};
	if (a > SdfLongRatio * b) {
		s.kind = SdfKind::Capsule;
		s.half = {b / a, c / a, 1};
	} else if (c < SdfFlatRatio * a) {
		s.kind = SdfKind::RoundBox;
		s.half = {b / a, c / a, 1};
		s.round = .6f * c / a;
	} else {
		s.kind = SdfKind::Ellipsoid;
		s.half = {b / a, c / a, 1};
	}
	return s;
}
} // namespace Game::Chrono
