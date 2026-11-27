// WeaponShapeGrammar.h
// 変形武器の形を、武器の「性質」から組み立てる (F3。エンジンに依存しない)。
//  - 型 (体): 一番強い「飛び方」の性質で 1 つ決める。転がる → 車輪の玉・飛ぶ (追尾・ふわふわ・戻る) → 翼のある滑空体・
//    貫通 → 先のとがった槍・重い → どっしりした箱・どれも無い → 前に筒の付いた大砲
//  - 部品: 残りの性質から強い順に CraftShapeTraitParts 個まで、決まった付け所 (上・前・後ろ・横・下) に、性質ごとの部品を付ける。
//    例: 爆発 → 上に導火線と火花・回転 → 上に十字の羽根・分裂 → 上の左右に分身の玉・吸い寄せ → 前に U 字の角・壁 → 前に盾・
//    速い → 後ろに噴射口・跳ねる → 下にばねの足 (体が持ち上がる)。同じ性質が重なると部品が大きくなる
//  - 物ごとの違い: 本体の物の形 (長さ・平たさ) で体の比率を少し変える。部品の番号は、その性質を持ってきた物 (1〜3)
//  - 太さの下限: どの部品も minHalf (半分の太さ) 以上 (WeaponShapeSdf.h の SdfApplyFloor)
//  - 大きさは「設計の長さ」(体の高さ 1.6 くらい)。最後に体積をそろえる (呼ぶ側)
#pragma once
#include "WeaponShapeSdf.h"
#include "WeaponCraftRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace Game::Chrono {
enum class ShapeArchetype : uint8_t { Wheel, Glider, Lance, Brute, Cannon, Count };
inline const char* ShapeArchetypeName(ShapeArchetype a) {
	static constexpr const char* names[] = {"車輪", "滑空", "槍", "どっしり", "大砲"};
	return names[std::min(int(a), int(ShapeArchetype::Count) - 1)];
}
// 型を決める: 転がる > 飛ぶ (追尾・ふわふわ・戻る) > 貫通 > 重い > 大砲
inline ShapeArchetype ChooseArchetype(const FuseLevels& lv) {
	auto L = [&](FuseTrait t) { return lv[size_t(t)]; };
	if (L(FuseTrait::Roll))
		return ShapeArchetype::Wheel;
	if (L(FuseTrait::Homing) || L(FuseTrait::Float) || L(FuseTrait::Return))
		return ShapeArchetype::Glider;
	if (L(FuseTrait::Pierce))
		return ShapeArchetype::Lance;
	if (L(FuseTrait::Heavy))
		return ShapeArchetype::Brute;
	return ShapeArchetype::Cannon;
}
// 部品を付ける性質の順 (同じレベルならこの順で選ぶ)。重いは体の大きさだけ (部品にしない)
inline constexpr FuseTrait ShapeTraitOrder[] = {
    FuseTrait::Homing, FuseTrait::Explode, FuseTrait::Spin,  FuseTrait::Split, FuseTrait::Bounce, FuseTrait::Burn,
    FuseTrait::Magnet, FuseTrait::Wall,    FuseTrait::Stun,  FuseTrait::Float, FuseTrait::Return, FuseTrait::Speed,
    FuseTrait::Pierce, FuseTrait::Chain,   FuseTrait::Paint, FuseTrait::Weaken};
// 設計の大きさ: 部品の重なり・重い 1 レベルで体が大きくなる割合・ばねの足の長さ (体が持ち上がる)・付ける部品の数
// (ShapePartScale は性質の部品の大きさの倍率。体に比べて小さすぎると見えないので大きめに)
// (ShapeTopReach は体の上に部品が突き出してよい高さ。体の高さの半分 = 半分の大きさの倍率)
inline constexpr float ShapeOverlap = .22f, ShapeHeavyGrow = .18f, ShapeLegLift = .45f, ShapePartScale = 1.3f,
                       ShapeTopReach = .5f, ShapePairReach = .5f, ShapeBodyAspect = 1.4f, ShapeFrontReach = .6f;
inline constexpr int CraftShapeTraitParts = 3;
// 入力: 性質のレベル・性質ごとにそれを持ってきた物 (1〜3。0 は無し)・本体の物を置きかえた基本形 (一番長い向きの半分が 1)
// (partScale・overlap は調整用: 性質の部品の大きさの倍率と、部品の重なり。ふつうは ShapePartScale・ShapeOverlap)
struct ShapeDesignInput {
	FuseLevels lv{};
	std::array<int, FuseTraitCount> owner{};
	SdfPart body;
	float partScale = ShapePartScale, overlap = ShapeOverlap;
};

namespace ShapeGrammarDetail {
inline Vec Cross(Vec a, Vec b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec Norm(Vec v) {
	const float l = SdfLength(v);
	return l > 1e-6f ? v * (1 / l) : Vec{0, 0, 1};
}
// 前 (w) を forward に向ける向き。上の目安は up
inline std::array<Vec, 3> Facing(Vec forward, Vec up = {0, 1, 0}) {
	const Vec w = Norm(forward);
	Vec u = Cross(up, w);
	if (SdfLength(u) < 1e-4f)
		u = Cross(Vec{1, 0, 0}, w);
	u = Norm(u);
	return {u, Cross(w, u), w};
}
// y の軸のまわりに a 回した向き (前が +z から回る)
inline std::array<Vec, 3> TurnY(float a) {
	const float c = std::cos(a), s = std::sin(a);
	return {Vec{c, 0, -s}, Vec{0, 1, 0}, Vec{s, 0, c}};
}
inline const std::array<Vec, 3> kForward{Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}}, kUp = Facing({0, 1, 0}, {0, 0, -1});
} // namespace ShapeGrammarDetail

// 形の部品を組み立てる。chosen に選んだ型を返す
inline std::vector<SdfPart>
    DesignWeaponShape(const ShapeDesignInput& in, float minHalf, ShapeArchetype* chosen = nullptr) {
	using namespace ShapeGrammarDetail;
	auto L = [&](FuseTrait t) { return in.lv[size_t(t)]; };
	auto own = [&](FuseTrait t) { return std::max(1, in.owner[size_t(t)]); };
	const ShapeArchetype archetype = ChooseArchetype(in.lv);
	if (chosen)
		*chosen = archetype;
	std::vector<SdfPart> parts;
	auto make = [&](SdfKind kind, Vec half, const std::array<Vec, 3>& axes, int owner, bool mirror = false,
	                float round = .12f) {
		SdfPart s;
		s.kind = kind;
		s.half = half;
		s.axes = axes;
		s.part = owner;
		s.mirror = mirror;
		s.round = round;
		SdfApplyFloor(s, minHalf);
		return s;
	};
	// 性質の部品は ShapePartScale 倍 (体は make のまま)
	auto part = [&](SdfKind kind, Vec half, const std::array<Vec, 3>& axes, int owner, bool mirror = false,
	                float round = .12f) {
		return make(kind, half * in.partScale, axes, owner, mirror, round * in.partScale);
	};
	// ---- 体: 型ごとの基本の大きさに、本体の物の長さと平たさ、重さをかける ----
	const SdfPart& item = in.body;
	const float elong = item.half.z / std::max(.05f, (item.half.x + item.half.y) * .5f),
	            flat = item.half.y / std::max(.05f, item.half.x);
	const float stretch = std::clamp(1 + .12f * (elong - 1), 1.f, 1.35f),
	            squash = std::clamp(.8f + .2f * flat, .8f, 1.f),
	            heavy = 1 + ShapeHeavyGrow * float(L(FuseTrait::Heavy));
	SdfPart body;
	switch (archetype) {
	case ShapeArchetype::Wheel:
		body = make(SdfKind::Ellipsoid, Vec{.78f, .78f * squash, .78f * stretch} * heavy, kForward, 1);
		break;
	// (滑空体と槍の体は、前後の長さを幅の ShapeBodyAspect 倍までにする。細長い胴は、上から見ると形として良くない並びになりやすい)
	case ShapeArchetype::Glider:
		body = make(
		    SdfKind::Ellipsoid, Vec{.55f, .52f * squash, std::min(.7f * stretch, .55f * ShapeBodyAspect)} * heavy,
		    kForward, 1);
		break;
	case ShapeArchetype::Lance:
		body = make(
		    SdfKind::Capsule, Vec{.5f, .5f * squash, std::min(.62f * stretch, .5f * ShapeBodyAspect)} * heavy, kForward,
		    1);
		break;
	case ShapeArchetype::Brute:
		body = make(SdfKind::RoundBox, Vec{.85f, .72f * squash, .7f * stretch} * heavy, kForward, 1, false, .3f);
		break;
	default:
		// 大砲: 体は本体の物の形 (カプセルは立てた筒・板は立てた箱・ほかは立てた卵)
		// (立てた体は細長くしない。細い縦の筒は、正面から見ると柱になるので、太く短い筒にする)
		if (item.kind == SdfKind::Capsule)
			body = make(SdfKind::Capsule, Vec{.66f, .66f, .72f * std::min(stretch, 1.15f)} * heavy, kUp, 1);
		else if (item.kind == SdfKind::RoundBox)
			body = make(
			    SdfKind::RoundBox, Vec{.66f, .5f * squash + .1f, .72f * std::min(stretch, 1.15f)} * heavy, kUp, 1,
			    false, .25f);
		else
			body = make(SdfKind::Ellipsoid, Vec{.66f, .6f * squash, .75f * std::min(stretch, 1.15f)} * heavy, kUp, 1);
		break;
	}
	const float lift = L(FuseTrait::Bounce) > 0 ? ShapeLegLift : 0.f;
	const Vec be = SdfExtent(body);
	body.center = {0, be.y + lift, 0};
	parts.push_back(body);
	const float cy = body.center.y, bx = be.x, by = be.y, bz = be.z;
	float front = bz;
	// 付け所へ置く: anchor から dir の向きへ、部品の大きさの (1 - 2 x 重なり) だけ出す
	auto place = [&](SdfPart s, Vec anchor, Vec dir) {
		const Vec e = SdfExtent(s);
		s.center = anchor + Vec{dir.x * e.x, dir.y * e.y, dir.z * e.z} * (1 - 2 * in.overlap);
		parts.push_back(s);
		return parts.back();
	};
	// ---- 型ごとの決まった部品 ----
	// 大砲: 前に太く短い四角い砲口 (丸い先の細長い筒にはしない)
	// (貫通があれば砲口は付けない: ドリルの先が前の先になる。前に部品を 2 つ重ねると、前へ長く伸びた形になるため)
	if (archetype == ShapeArchetype::Cannon && !L(FuseTrait::Pierce)) {
		const auto& b = place(
		    make(
		        SdfKind::RoundBox, {std::max(.38f, bx * .6f), std::max(.34f, by * .5f), .28f}, kForward, 1, false,
		        .08f),
		    {0, cy + by * .1f, bz}, {0, 0, 1});
		parts.back().front = true;
		front = b.center.z + SdfExtent(b).z;
	}
	// 槍: 先は短く太い円すい (長く細い先にはしない)
	if (archetype == ShapeArchetype::Lance) {
		const float h = .3f * (1 + .2f * float(L(FuseTrait::Pierce) - 1));
		const auto& t =
		    place(part(SdfKind::Cone, {.42f, .1f, h}, kForward, own(FuseTrait::Pierce)), {0, cy, bz}, {0, 0, 1});
		parts.back().front = true;
		front = t.center.z + SdfExtent(t).z;
	}
	// どっしり: 左右に太いこぶし (体の横の下の方、少し前)
	// (どっしりのこぶしは大きく、体の横へはっきり出す。性質の少ない物でも、ただの塊に見えないように)
	if (archetype == ShapeArchetype::Brute)
		place(
		    part(SdfKind::Ellipsoid, Vec{.42f, .38f, .4f} * heavy, kForward, own(FuseTrait::Heavy), true),
		    {bx * 1.1f, cy - by * .3f, bz * .2f}, {1, 0, 0});
	// 大砲: 左右の下に台車の車輪 (横向きの平たい円盤)。性質の無い物だけでも、大砲の形が読めるように
	if (archetype == ShapeArchetype::Cannon)
		place(
		    part(SdfKind::RoundBox, {.1f, .42f, .42f}, kForward, 1, true, .36f),
		    {bx * 1.05f, .42f * in.partScale, -bz * .1f}, {1, 0, 0});
	if (archetype == ShapeArchetype::Glider) {
		const FuseTrait wingBy = L(FuseTrait::Homing)  ? FuseTrait::Homing
		                         : L(FuseTrait::Float) ? FuseTrait::Float
		                                               : FuseTrait::Return;
		const float s = 1 + .25f * float(std::max(0, L(wingBy) - 1));
		place(
		    part(SdfKind::RoundBox, {.6f * s, .09f, .32f}, TurnY(-.35f), own(wingBy), true),
		    {bx, cy + by * .1f, -bz * .1f}, {1, 0, 0});
		parts.back().motion = SdfMotion::Flap;
	}
	// ---- 性質の部品: 型で使った性質と重いを除き、強い順に CraftShapeTraitParts 個まで ----
	// (槍の先は貫通、滑空体の翼は翼にした性質で、もう付けてある)
	const FuseTrait wingTrait = L(FuseTrait::Homing)  ? FuseTrait::Homing
	                            : L(FuseTrait::Float) ? FuseTrait::Float
	                                                  : FuseTrait::Return;
	std::vector<FuseTrait> chosenTraits;
	for (int level = 3; level >= 1; --level)
		for (FuseTrait t : ShapeTraitOrder) {
			if (int(chosenTraits.size()) >= CraftShapeTraitParts)
				break;
			if (L(t) != level)
				continue;
			if (archetype == ShapeArchetype::Lance && t == FuseTrait::Pierce)
				continue;
			if (archetype == ShapeArchetype::Glider && t == wingTrait)
				continue;
			chosenTraits.push_back(t);
		}
	// 上の付け所は真ん中・後ろ・前の順に使う
	int topUsed = 0;
	auto topSlot = [&] {
		const float z = topUsed == 0 ? 0.f : topUsed == 1 ? -bz * .45f : bz * .4f;
		++topUsed;
		return Vec{0, cy + by, z};
	};
	for (FuseTrait t : chosenTraits) {
		const int o = own(t);
		const float s = 1 + .25f * float(L(t) - 1);
		switch (t) {
		case FuseTrait::Homing:
			place(
			    part(SdfKind::RoundBox, {.55f * s, .09f, .3f}, TurnY(-.35f), o, true), {bx, cy + by * .15f, -bz * .1f},
			    {1, 0, 0});
			parts.back().motion = SdfMotion::Flap;
			break;
		// 貫通: 太く短いドリルの先 (根もとは体の幅の 7 割)
		case FuseTrait::Pierce: {
			const auto& c = place(
			    part(SdfKind::Cone, {std::max(.3f, bx * .7f / in.partScale), .1f, .22f * s}, kForward, o),
			    {0, cy, front}, {0, 0, 1});
			parts.back().front = true;
			front = c.center.z + SdfExtent(c).z;
			break;
		}
		// 爆発: 爆弾の口金 (平たい円盤) と、後ろへ倒れた短い火花。まっすぐ上に立つ柱にはしない (先の丸い柱は形として良くないため)
		case FuseTrait::Explode: {
			const Vec at = topSlot();
			const auto& cap = place(
			    part(SdfKind::RoundBox, Vec{.3f, .08f, .3f} * s, kForward, o, false, .06f), at + Vec{0, 0, -bz * .2f},
			    {0, 1, 0});
			const Vec ce = SdfExtent(cap);
			place(
			    part(SdfKind::Cone, {.18f, .05f, .2f}, Facing({0, .6f, -1}), o),
			    cap.center + Vec{0, ce.y * .5f, -ce.z * .6f}, {0, .5f, -1});
			break;
		}
		case FuseTrait::Spin: {
			const Vec at = topSlot();
			place(part(SdfKind::Ellipsoid, {.15f, .15f, .15f}, kForward, o), at, {0, 1, 0});
			parts.back().motion = SdfMotion::Rotor;
			const Vec hub = parts.back().center;
			for (float a : {.785f, -.785f}) {
				place(part(SdfKind::RoundBox, {.75f * s, .06f, .13f}, TurnY(a), o), hub, {0, 0, 0});
				parts.back().motion = SdfMotion::Rotor;
			}
			break;
		}
		case FuseTrait::Split:
			place(
			    part(SdfKind::Ellipsoid, Vec{.24f, .24f, .24f} * s, kForward, o, true), {bx * .55f, cy + by * .8f, 0},
			    {.3f, .6f, 0});
			parts.back().motion = SdfMotion::Bob;
			if (L(t) >= 2) {
				place(
				    part(SdfKind::Ellipsoid, Vec{.22f, .22f, .22f}, kForward, o, true),
				    {bx * .75f, cy + by * .2f, -bz * .3f}, {.6f, .2f, 0});
				parts.back().motion = SdfMotion::Bob;
			}
			break;
		case FuseTrait::Bounce:
			place(
			    part(SdfKind::Capsule, {.16f, .16f, .3f + .08f * float(L(t) - 1)}, kUp, o, true), {bx * .5f, 0, 0},
			    {0, 1, 0});
			parts.back().motion = SdfMotion::Leg;
			break;
		// 熱い: 背中の左右に、外の後ろへなびく 2 つの炎 (真ん中に 1 本立てない)
		case FuseTrait::Burn:
			place(
			    part(SdfKind::Cone, {.22f * s, .05f, .24f * s}, Facing({.6f, .8f, -.8f}), o, true),
			    {bx * .45f, cy + by * .8f, -bz * .2f}, {.4f, .5f, -.3f});
			parts.back().motion = SdfMotion::Flicker;
			break;
		// 吸い寄せ: 体の前の左右に、外へ開いた角ばった 2 本の角 (先の丸い棒を前に並べない)
		case FuseTrait::Magnet:
			place(
			    part(SdfKind::RoundBox, {.1f, .14f, .24f * s}, TurnY(.45f), o, true, .03f),
			    {bx * .6f, cy - by * .1f, bz * .45f}, {.5f, 0, .5f});
			break;
		case FuseTrait::Wall:
			place(
			    part(SdfKind::RoundBox, {std::max(bx * 1.05f, .6f) * s, by * .9f, .07f}, kForward, o, false, .05f),
			    {0, cy, bz * .85f}, {0, 0, 1});
			break;
		// 止める: 外へ開いた短い 2 本の角 (高く立てない)
		case FuseTrait::Stun:
			place(
			    part(SdfKind::Cone, {.13f, .04f, .15f * s}, Facing({.8f, 1, 0}, {0, 0, 1}), o, true),
			    {bx * .65f, cy + by * .75f, bz * .25f}, {.6f, .6f, 0});
			break;
		// ふわふわ: 背中に乗った平たい雲 (縦に長い玉にはしない)
		case FuseTrait::Float:
			place(
			    part(SdfKind::Ellipsoid, Vec{.42f, .26f, .36f} * s, kForward, o), topSlot() + Vec{0, 0, -bz * .15f},
			    {0, 1, 0});
			break;
		case FuseTrait::Return:
			place(
			    part(SdfKind::RoundBox, {.55f * s, .08f, .18f}, TurnY(.8f), o, true), {bx, cy + by * .25f, -bz * .45f},
			    {1, 0, 0});
			parts.back().motion = SdfMotion::Flap;
			break;
		// 速い: 左右の後ろに平たいひれ (後ろの真ん中に 1 つ突き出す物は付けない)
		case FuseTrait::Speed:
			place(
			    part(SdfKind::RoundBox, {.32f * s, .06f, .26f}, TurnY(.5f), o, true, .05f),
			    {bx * .8f, cy + by * .1f, -bz * .55f}, {1, 0, 0});
			break;
		// 連鎖: 体の左右の後ろに、体に沿って並んだ平たい輪 (鎖の環)
		case FuseTrait::Chain:
			place(
			    part(SdfKind::RoundBox, Vec{.08f, .2f, .2f} * s, kForward, o, true, .1f), {bx, cy, -bz * .35f},
			    {1, 0, 0});
			break;
		// 塗り: 後ろの足もとに広がった平たい水たまり (体より幅の広い、低いだ円)
		case FuseTrait::Paint:
			place(
			    part(SdfKind::Ellipsoid, Vec{std::max(.45f, bx * .9f / in.partScale), .1f, .3f} * s, kForward, o),
			    {0, .05f, -bz * .7f}, {0, 0, -1});
			parts.back().flat = true;
			break;
		case FuseTrait::Weaken:
			place(
			    part(SdfKind::RoundBox, Vec{.16f, .14f, .24f} * s, kForward, o, true, .03f), {bx, cy - by * .1f, 0},
			    {1, 0, 0});
			break;
		default:
			break;
		}
	}
	// 形の決まり (形として良くない並びを作らないため。tests/WeaponShapeMetricsTests.cpp で全部の組み合わせを確かめる):
	//  1. 真ん中の線上の部品 (左右の組でない) は、体の前後と左右の外へ出さない (出てよいのは太く短い前の先と、低く平たい水たまりだけ)
	//  2. 左右の組の丸い部品 (だ円体・カプセル) は、体の前後の端ではなく真ん中寄り (前後 ShapePairReach x 体の長さの半分まで) に置く
	for (size_t i = 1; i < parts.size(); ++i) {
		auto& s = parts[i];
		const Vec e = SdfExtent(s);
		if (!s.mirror && !s.front && !s.flat) {
			s.center.z = e.z >= bz ? 0.f : std::clamp(s.center.z, -bz + e.z, bz - e.z);
			s.center.x = e.x >= bx ? 0.f : std::clamp(s.center.x, -bx + e.x, bx - e.x);
		}
		if (s.mirror && (s.kind == SdfKind::Ellipsoid || s.kind == SdfKind::Capsule))
			s.center.z = std::clamp(s.center.z, -bz * ShapePairReach, bz * ShapePairReach);
	}
	//  3. 前の先 (ドリル・砲口) が体の前から出る長さは、体の半分の大きさの ShapeFrontReach 倍まで (長く伸びた前の部分を作らない)
	{
		float tip = bz;
		for (const auto& s : parts)
			if (s.front)
				tip = std::max(tip, s.center.z + SdfExtent(s).z);
		const float over = tip - (bz + ShapeFrontReach * std::max(bx, bz));
		if (over > 0)
			for (auto& s : parts)
				if (s.front)
					s.center.z -= over;
	}
	//  4. 上への突き出しの上限: 体の上から体の高さの半分 (半分の大きさ by の 1 倍) まで。それより高い部品は体の方へ沈める
	// (細い部品が上に長く立つと、柱のような形になる。形として良くないので作らない。WeaponShapeMetrics の「柱」の判定でも確かめる)
	const float top = cy + by * (1 + ShapeTopReach);
	for (size_t i = 1; i < parts.size(); ++i) {
		auto& s = parts[i];
		const float h = SdfExtent(s).y;
		if (s.center.y + h > top)
			s.center.y = std::max(top - h, cy);
	}
	// 動く部品の支点 (右側の部品の座標。左は鏡に映して使う): 翼は体の横の付け根・羽根は真ん中の軸 (上下)・足は床・炎と分身の玉は部品の真ん中
	for (size_t i = 1; i < parts.size(); ++i) {
		auto& s = parts[i];
		switch (s.motion) {
		case SdfMotion::Flap:
			s.pivot = {bx * .9f, s.center.y, s.center.z};
			break;
		case SdfMotion::Rotor:
			s.pivot = {0, s.center.y, s.center.z};
			break;
		case SdfMotion::Leg:
			s.pivot = {s.center.x, 0, s.center.z};
			break;
		default:
			s.pivot = s.center;
			break;
		}
	}
	return parts;
}
} // namespace Game::Chrono
