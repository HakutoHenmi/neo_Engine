// 変形武器の形の見やすさの測り方 (Game/Chrono/WeaponShapeMetrics.h) を、作った形で確かめる
#include "../Game/Chrono/WeaponShapeMetrics.h"
#include "../Game/Chrono/WeaponShapeSdf.h"
#include "../Game/Chrono/WeaponShapeGrammar.h"
#include "../Game/Chrono/DevourGimmickRules.h"
#include "../Game/Chrono/ItemProfileRules.h"
// Production headers above use the selected configuration; test checks stay active.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <functional>
#include <algorithm>
#include <iostream>
using namespace Game::Chrono;
// 40 x 40 x 40 の箱 (1 箱 0.25) に、inside の形を詰める。part は部品の番号
struct Grid {
	int n = 40;
	float cell = .25f;
	std::vector<uint8_t> fill, part;
	Grid() {
		fill.assign(size_t(n * n * n), 0);
		part.assign(fill.size(), 0);
	}
	void Add(const std::function<bool(float, float, float)>& inside, uint8_t id) {
		for (int z = 0; z < n; ++z)
			for (int y = 0; y < n; ++y)
				for (int x = 0; x < n; ++x) {
					const float px = (float(x) + .5f) * cell - 5, py = (float(y) + .5f) * cell,
					            pz = (float(z) + .5f) * cell - 5;
					const size_t i = size_t((z * n + y) * n + x);
					if (!fill[i] && inside(px, py, pz)) {
						fill[i] = 1;
						part[i] = id;
					}
				}
	}
	WeaponShapeVoxels View() const {
		return {n, n, n, cell, &fill, &part};
	}
};
auto Ball(float cx, float cy, float cz, float r) {
	return [=](float x, float y, float z) {
		return (x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz) <= r * r;
	};
}
void TestRectangularSilhouettes() {
	const int nx = 3, ny = 4, nz = 5;
	std::vector<uint8_t> fill(size_t(nx * ny * nz), 0);
	for (int z = 1; z < nz; ++z) {
		for (int y = 1; y < ny; ++y) {
			for (int x = 1; x < nx; ++x) {
				fill[size_t((z * ny + y) * nx + x)] = 1;
			}
		}
	}
	const WeaponShapeVoxels voxels{nx, ny, nz, 1, &fill, nullptr};
	const auto report = MeasureWeaponShape(voxels);
	assert(report.voxels == 24 && report.pieces == 1);
	const std::array<float, 3> areas{12, 6, 8};
	const std::array<float, 3> perimeters{14, 10, 12};
	for (size_t axis = 0; axis < areas.size(); ++axis) {
		assert(report.fill[axis] == 1 && report.solid[axis] == 1);
		const float expectedOutline = perimeters[axis] * perimeters[axis] / (4 * 3.1415927f * areas[axis]);
		assert(std::abs(report.outline[axis] - expectedOutline) < 1e-5f);
	}
	std::fill(fill.begin(), fill.end(), uint8_t(0));
	const auto empty = MeasureWeaponShape(voxels);
	assert(empty.voxels == 0 && empty.pillarFrom == -1 && empty.score == 100);
	std::cout << "PASS: non-cubic grids project the correct area and perimeter on all axes\n";
}

void TestWeaponDescriptions() {
	auto with = [](std::initializer_list<std::pair<FuseTrait, int>> traits) {
		FuseLevels levels{};
		for (const auto& [trait, level] : traits)
			levels[size_t(trait)] = level;
		return FuseWeaponDescriptionPhrases(levels);
	};
	auto has = [](const std::vector<std::string>& phrases, const std::string& phrase) {
		return std::find(phrases.begin(), phrases.end(), phrase) != phrases.end();
	};
	// 1 番目は飛び方 (撃ち方が一発のとき): 放物線・床を転がる・宙を飛ぶ
	assert(with({}) == std::vector<std::string>{"放物線で飛び"});
	assert(with({{FuseTrait::Roll, 1}}).front() == "床を転がり");
	assert(with({{FuseTrait::Homing, 1}}).front() == "宙を飛び");
	assert(with({{FuseTrait::Float, 1}}).front() == "宙を飛び");
	// 撃ち方が決まる組み合わせは、撃ち方の説明が先
	assert(with({{FuseTrait::Bounce, 2}}).front() == "【全方位】体のまわり全部へ撃つ");
	// レベルで数が変わる
	assert(has(with({{FuseTrait::Pierce, 1}}), "4個まで突き抜け"));
	assert(has(with({{FuseTrait::Bounce, 2}}), "6回跳ね"));
	// 揺らす・ひび・熱いは、積み木に何が起きるかを書く (相手を止める・弱らせるような言葉は無い)
	assert(has(with({{FuseTrait::Stun, 1}}), "当たった所のまわりを揺さぶって崩し"));
	assert(has(with({{FuseTrait::Weaken, 1}}), "当てた所にひびを入れて次の当たりを 2 倍にし"));
	assert(has(with({{FuseTrait::Burn, 1}}), "当てた積み木を焦がして崩し"));
	FuseLevels all{};
	all.fill(1);
	for (const auto& phrase : FuseWeaponDescriptionPhrases(all))
		assert(phrase.find("相手を") == std::string::npos && phrase.find("弱らせ") == std::string::npos);
	std::cout << "PASS: weapon descriptions show movement first, pattern phrases, level counts and block effects\n";
}

int main() {
	TestRectangularSilhouettes();
	TestWeaponDescriptions();
	WeaponShapeLimits limits;
	limits.minThick = .8f;
	limits.massRadius = .6f;
	{ // 大きな玉 1 つ: 細くないが、塊は 1 つだけで、どこから見ても詰まっている
		Grid g;
		g.Add(Ball(0, 3, 0, 2.5f), 1);
		const auto r = MeasureWeaponShape(g.View(), limits);
		assert(
		    r.thinShare < .05f && r.masses == 1 && r.pieces == 1 && r.Has(WeaponShapeIssue::Blob) &&
		    !r.Has(WeaponShapeIssue::Thin));
		std::cout << "PASS: one ball is a blob, not thin\n";
	}
	{ // 細い棒 (太さ 0.25): ほとんど全部が細すぎ
		Grid g;
		g.Add(
		    [](float x, float y, float z) { return std::abs(x) < .13f && std::abs(y - 3) < .13f && std::abs(z) < 4; },
		    1);
		const auto r = MeasureWeaponShape(g.View(), limits);
		assert(r.thinShare > .9f && r.Has(WeaponShapeIssue::Thin));
		std::cout << "PASS: thin rod is flagged thin\n";
	}
	{ // 玉 3 つを太い首でつないだ形: 塊が 3 つ読め、細すぎではない。部品の割合も数える
		Grid g;
		g.Add(Ball(0, 2.5f, 0, 1.8f), 1);
		g.Add(Ball(0, 2.5f, 3.6f, 1.2f), 2);
		g.Add(Ball(0, 2.5f, -3.6f, 1.2f), 3);
		g.Add(
		    [](float x, float y, float z) { return x * x + (y - 2.5f) * (y - 2.5f) < .5f * .5f && std::abs(z) < 3.6f; },
		    1);
		const auto r = MeasureWeaponShape(g.View(), limits);
		assert(
		    r.masses == 3 && !r.Has(WeaponShapeIssue::Thin) && r.pieces == 1 && r.partShare[1] > .1f &&
		    r.partShare[2] > .1f && r.score >= 75);
		std::cout << "PASS: three linked balls read as three masses (score " << r.score << ")\n";
	}
	{ // 離れた玉 2 つ: 離れた部品
		Grid g;
		g.Add(Ball(-2.5f, 2, 0, 1.5f), 1);
		g.Add(Ball(2.5f, 2, 0, 1.5f), 2);
		const auto r = MeasureWeaponShape(g.View(), limits);
		assert(r.pieces == 2 && r.Has(WeaponShapeIssue::Floating));
		std::cout << "PASS: two separate balls are floating parts\n";
	}
	{ // 大きな本体と、とても小さな部品: 本体ばかり・小さすぎる部品
		Grid g;
		g.Add(Ball(0, 3, 0, 2.6f), 1);
		g.Add(Ball(0, 3, 2.8f, .5f), 2);
		g.Add(Ball(0, 5.8f, 0, .5f), 3);
		const auto r = MeasureWeaponShape(g.View(), limits);
		assert(r.Has(WeaponShapeIssue::TinyPart) && r.Has(WeaponShapeIssue::BaseOnly));
		std::cout << "PASS: tiny parts on a big body are flagged\n";
	}
	{ // 物の形を基本形に置きかえる: 長い棒はカプセル、平たい板は角の丸い板、玉はだ円体。どれも一番長い向きの半分が 1
		auto cloud = [](const std::function<bool(float, float, float)>& in) {
			std::vector<Vec> pts;
			for (int z = -20; z <= 20; ++z)
				for (int y = -20; y <= 20; ++y)
					for (int x = -20; x <= 20; ++x) {
						const float px = x * .1f, py = y * .1f, pz = z * .1f;
						if (in(px, py, pz))
							pts.push_back(Vec{px, py, pz});
					}
			return pts;
		};
		const SdfPart rod = SdfFitPoints(
		    cloud([](float x, float y, float z) { return x * x + z * z < .2f * .2f && std::abs(y) < 1.8f; }));
		const SdfPart plate = SdfFitPoints(cloud(
		    [](float x, float y, float z) { return std::abs(x) < 1.5f && std::abs(z) < 1.2f && std::abs(y) < .25f; }));
		const SdfPart ball =
		    SdfFitPoints(cloud([](float x, float y, float z) { return x * x + y * y + z * z < 1.5f * 1.5f; }));
		assert(rod.kind == SdfKind::Capsule && plate.kind == SdfKind::RoundBox && ball.kind == SdfKind::Ellipsoid);
		assert(
		    std::abs(rod.half.z - 1) < 1e-4f && rod.half.x < .3f && plate.half.y < .3f && plate.half.x > .6f &&
		    ball.half.x > .85f && ball.half.y > .85f);
		std::cout << "PASS: rod/plate/ball fit to capsule/round box/ellipsoid\n";
	}
	{ // なめらかなつなぎ: 2 つの球のつなぎ目は、ただ重ねたときより太い。中は負、外は正
		SdfPart a;
		a.kind = SdfKind::Ellipsoid;
		a.half = {1, 1, 1};
		a.center = {-1, 0, 0};
		SdfPart b = a;
		b.center = {1.2f, 0, 0};
		b.part = 2;
		const std::vector<SdfPart> parts{a, b};
		int owner = 0;
		assert(
		    SdfShapeDistance(parts, Vec{-1, 0, 0}, 0, &owner) < 0 && owner == 1 &&
		    SdfShapeDistance(parts, Vec{0, 3, 0}, 0) > 0);
		assert(SdfShapeDistance(parts, Vec{.1f, .55f, 0}, .4f) < SdfShapeDistance(parts, Vec{.1f, .55f, 0}, 0));
		std::cout << "PASS: smooth union thickens the joint\n";
	}
	{ // 性質から型を決める: 転がる > 飛ぶ > 貫通 > 重い > 大砲。ボウリング (転がる・重い) + 鉛筆 (貫通・速い) + 紙飛行機 (追尾) は車輪
		const auto lv = FuseLevelsOf({BattleAction::Roll, BattleAction::Pierce, BattleAction::Homing}, 3);
		assert(ChooseArchetype(lv) == ShapeArchetype::Wheel);
		assert(
		    ChooseArchetype(FuseLevelsOf({BattleAction::Pierce, BattleAction::Homing, BattleAction::Throw}, 3)) ==
		    ShapeArchetype::Glider);
		assert(
		    ChooseArchetype(FuseLevelsOf({BattleAction::Pierce, BattleAction::Smash, BattleAction::Throw}, 3)) ==
		    ShapeArchetype::Lance);
		assert(
		    ChooseArchetype(FuseLevelsOf({BattleAction::Smash, BattleAction::Throw, BattleAction::Throw}, 3)) ==
		    ShapeArchetype::Brute);
		assert(
		    ChooseArchetype(FuseLevelsOf({BattleAction::Throw, BattleAction::Tea, BattleAction::Throw}, 3)) ==
		    ShapeArchetype::Cannon);
		// 部品: 体が最初、性質の部品は 3 つまで。追尾は左右の翼 (鏡)、部品の番号はその性質を持ってきた物
		ShapeDesignInput in;
		in.lv = lv;
		in.owner[size_t(FuseTrait::Roll)] = 1;
		in.owner[size_t(FuseTrait::Heavy)] = 1;
		in.owner[size_t(FuseTrait::Pierce)] = 2;
		in.owner[size_t(FuseTrait::Speed)] = 2;
		in.owner[size_t(FuseTrait::Homing)] = 3;
		in.body.kind = SdfKind::Ellipsoid;
		in.body.half = {1, 1, 1};
		ShapeArchetype chosen = ShapeArchetype::Count;
		const auto parts = DesignWeaponShape(in, .2f, &chosen);
		assert(chosen == ShapeArchetype::Wheel && parts.front().part == 1 && parts.size() >= 4);
		bool wing = false, nose = false;
		for (const auto& p : parts) {
			if (p.part == 3 && p.mirror && p.kind == SdfKind::RoundBox)
				wing = true;
			if (p.part == 2 && p.kind == SdfKind::Cone)
				nose = true;
			assert(p.Thickness() >= .4f - 1e-4f || p.kind == SdfKind::Cone);
		}
		assert(wing && nose);
		std::cout << "PASS: traits choose the archetype and add owned parts (wings, nose)\n";
	}
	{ // 円すい: 根もとは太く、先は細い。軸の上は中
		SdfPart c;
		c.kind = SdfKind::Cone;
		c.half = {.5f, .1f, 1};
		assert(
		    SdfPartDistance(c, {0, 0, 0}) < 0 && SdfPartDistance(c, {.4f, 0, -.9f}) < 0 &&
		    SdfPartDistance(c, {.4f, 0, .9f}) > 0);
		std::cout << "PASS: cone is thick at the base and thin at the tip\n";
	}
	{ // 柱の突き出し: 横に広い体の上に、細い柱が高く立つ形は「柱」。同じ体に低く平たい部品なら柱ではない
		Grid g;
		g.Add(
		    [](float x, float y, float z) {
			    return (x * x) / (2.4f * 2.4f) + ((y - 1.2f) * (y - 1.2f)) / (1.1f * 1.1f) + (z * z) / (1.6f * 1.6f) <=
			           1;
		    },
		    1);
		g.Add([](float x, float y, float z) { return x * x + z * z < .45f * .45f && y > 1.5f && y < 6.5f; }, 2);
		const auto r = MeasureWeaponShape(g.View(), limits);
		assert(r.Has(WeaponShapeIssue::Pillar) && r.score < 50);
		Grid h;
		h.Add(
		    [](float x, float y, float z) {
			    return (x * x) / (2.4f * 2.4f) + ((y - 1.2f) * (y - 1.2f)) / (1.1f * 1.1f) + (z * z) / (1.6f * 1.6f) <=
			           1;
		    },
		    1);
		h.Add(
		    [](float x, float y, float z) {
			    return (x * x) / (1.2f * 1.2f) + ((y - 2.4f) * (y - 2.4f)) / (.35f * .35f) + (z * z) / (1.f) <= 1;
		    },
		    2);
		assert(!MeasureWeaponShape(h.View(), limits).Has(WeaponShapeIssue::Pillar));
		std::cout << "PASS: a tall thin pillar on a wide body is flagged, a low flat part is not\n";
	}
	{ // 形の決まりを、全部の物の技の組み合わせ (44 x 44 x 44) と、本体の物の形 3 種・太さの下限 2 種で確かめる:
		//  真ん中の線上の部品は体の外へ出ない・前の先は太く短い (出てよいのは太く短い前の先と、低く平たい水たまり)・左右の組の丸い部品は真ん中寄り・上への突き出しは体の高さの半分まで
		int checked = 0;
		const int n = int(BattleAction::Count);
		for (int kind = 0; kind < 3; ++kind)
			for (float minHalf : {0.f, .35f})
				for (int a = 0; a < n; ++a)
					for (int b = 0; b < n; ++b)
						for (int c = 0; c < n; ++c) {
							ShapeDesignInput in;
							in.lv = FuseLevelsOf({BattleAction(a), BattleAction(b), BattleAction(c)}, 3);
							for (int i = 3; i-- > 0;)
								for (FuseTrait t : FuseTraitsOf(BattleAction(i == 0 ? a : i == 1 ? b : c)))
									if (t != FuseTrait::Count)
										in.owner[size_t(t)] = i + 1;
							in.body.kind = SdfKind(kind);
							in.body.half = kind == 1   ? Vec{.3f, .3f, 1}
							               : kind == 2 ? Vec{.8f, .2f, 1}
							                           : Vec{.8f, .7f, 1};
							const auto parts = DesignWeaponShape(in, minHalf);
							const auto& body = parts.front();
							const Vec be = SdfExtent(body);
							const float eps = 1e-3f;
							assert(be.z <= ShapeBodyAspect * be.x + eps); // (体は前後に細長くしない)
							for (size_t i = 1; i < parts.size(); ++i) {
								const auto& p = parts[i];
								const Vec e = SdfExtent(p);
								if (!p.mirror && !p.front && !p.flat && (e.z < be.z || std::abs(p.center.z) > eps) &&
								    (std::abs(p.center.z) + e.z > be.z + eps && e.z < be.z)) {
									std::cout << "FAIL center part outside body " << a << "," << b << "," << c
									          << std::endl;
									assert(false);
								}
								if (!p.mirror && !p.front && !p.flat && e.x < be.x &&
								    std::abs(p.center.x) + e.x > be.x + eps) {
									std::cout << "FAIL center part outside body width " << a << "," << b << "," << c
									          << std::endl;
									assert(false);
								}
								if (p.front)
									assert(p.half.x >= e.z - eps);
								if (p.front)
									assert(
									    p.center.z + e.z <=
									    be.z + ShapeFrontReach * std::max(be.x, be.z) +
									        eps); // (前の先は前へ長く伸ばさない) // (前の先は太く短い: 根もとの半分の幅が、長さの半分以上)
								if (p.mirror && (p.kind == SdfKind::Ellipsoid || p.kind == SdfKind::Capsule))
									assert(std::abs(p.center.z) <= .5f * be.z + eps);
								assert(p.center.y + e.y <= body.center.y + be.y * 1.5f + eps);
							}
							++checked;
						}
		std::cout << "PASS: shape rules hold for all " << checked << " combinations\n";
	}
	{ // 変形武器の形の姿勢: 何も動かさなければそのまま。羽根は支点のまわりに回り、翼は左右が鏡のようにはばたき、大きさは床を中心に変わる
		auto& grids = WeaponShapeGrids();
		WeaponShapeGrid g;
		g.nx = g.ny = g.nz = 10;
		g.cell = 1;
		g.lo = {-5, 0, -5};
		g.fill.assign(1000, 1);
		g.piece.assign(1000, 1);
		g.pieces = {{uint8_t(SdfMotion::Rotor), Vec{0, 5, 0}}};
		grids.push_back(g);
		const int rotor = WeaponShapeFirst + int(grids.size()) - 1;
		g.pieces = {{uint8_t(SdfMotion::Flap), Vec{1, 5, 0}}};
		grids.push_back(g);
		const int wing = WeaponShapeFirst + int(grids.size()) - 1;
		GimmickPose rest;
		const Vec p{3, 5, 0};
		const Vec same = GimmickPosePoint(rotor, p, rest);
		assert(SdfLength(same - p) < 1e-5f);
		GimmickPose spun;
		spun.spin = 1.5707963f;
		const Vec r = GimmickPosePoint(rotor, p, spun);
		assert(std::abs(r.y - 5) < 1e-4f && std::abs(SdfLength(Vec{r.x, 0, r.z}) - 3) < 1e-4f && std::abs(r.x) < 1e-4f);
		GimmickPose flap;
		flap.roll = .4f;
		const Vec right = GimmickPosePoint(wing, {3, 5, 0}, flap), left = GimmickPosePoint(wing, {-3, 5, 0}, flap);
		assert(right.y > 5 && std::abs(right.y - left.y) < 1e-4f && std::abs(right.x + left.x) < 1e-4f);
		GimmickPose small;
		small.scale = .5f;
		const Vec q = GimmickPosePoint(rotor, {0, 0, 0}, small);
		assert(SdfLength(q) < 1e-5f);
		std::cout << "PASS: weapon pose spins rotors, flaps wings symmetrically and scales from the floor\n";
	}
	// 物のプロフィール (H1): 箱は 1 つの箱・離れた 2 つの箱は 2 つのパーツ・細長い棒は「長い」・板は「平たい」・
	// 入れ物 (底と 4 つの壁) は中が埋まって 1 つの塊・板の下に長い棒 (傘) は持ち手
	{
		auto profile = [](const std::vector<std::array<Vec, 2>>& boxes) {
			std::vector<Vec> pts;
			std::vector<uint32_t> idx;
			for (const auto& b : boxes)
				ItemBoxMesh(pts, idx, b[0], b[1]);
			return BuildItemProfile("test", pts, idx);
		};
		const auto box = profile({{Vec{0, 1, 0}, Vec{1, 1, 1}}});
		assert(box.parts.size() == 1 && box.shape == ItemShapeClass::Box && box.fit > .8f);
		const auto two = profile({{Vec{-3, 1, 0}, Vec{1, 1, 1}}, {Vec{3, 1, 0}, Vec{1, 1, 1}}});
		assert(two.parts.size() == 2);
		const auto rod = profile({{Vec{0, .2f, 0}, Vec{.2f, .2f, 2}}});
		assert(rod.shape == ItemShapeClass::Long);
		const auto plate = profile({{Vec{0, .1f, 0}, Vec{1.5f, .1f, 1.2f}}});
		assert(plate.shape == ItemShapeClass::Flat);
		const auto cup = profile(
		    {{Vec{0, .1f, 0}, Vec{1, .1f, 1}},
		     {Vec{-.95f, 1, 0}, Vec{.05f, 1, 1}},
		     {Vec{.95f, 1, 0}, Vec{.05f, 1, 1}},
		     {Vec{0, 1, -.95f}, Vec{1, 1, .05f}},
		     {Vec{0, 1, .95f}, Vec{1, 1, .05f}}});
		assert(cup.fit > .6f && cup.parts.size() <= 2);
		const auto umbrella =
		    profile({{Vec{0, 3.6f, 0}, Vec{1.4f, .35f, 1.4f}}, {Vec{0, 1.6f, 0}, Vec{.06f, 1.7f, .06f}}});
		assert(umbrella.Has(ItemHasHandle) && umbrella.parts.size() >= 2);
		std::cout << "PASS: item profiles split boxes, find long/flat shapes, fill containers and find handles\n";
	}
	// 物の形そのもの (H1 の作り直し): 立方体は太さの下限をかけても形が変わらない・L 字は基本形より面影が残る・
	// 傘の細い柄は、太さの下限をかけると柄の所だけ太くなる (傘の上の板は太らない)
	{
		auto build = [](const std::vector<std::array<Vec, 2>>& boxes, ItemProfileDetail::Grid& g) {
			std::vector<Vec> pts;
			std::vector<uint32_t> idx;
			for (const auto& b : boxes)
				ItemBoxMesh(pts, idx, b[0], b[1]);
			return BuildItemProfile("test", pts, idx, &g);
		};
		ItemProfileDetail::Grid g;
		const auto cube = build({{Vec{0, 1, 0}, Vec{1, 1, 1}}}, g);
		assert(ItemProfileDetail::Resemblance(g, {cube.look}, .77f) > .95f);
		const auto ell = build({{Vec{0, .25f, 0}, Vec{1, .25f, .25f}}, {Vec{-.75f, 1, 0}, Vec{.25f, 1, .25f}}}, g);
		assert(ItemProfileDetail::Resemblance(g, {ell.look}, .2f) > ItemProfileDetail::Resemblance(g, ell.parts, .2f));
		const auto umbrella =
		    build({{Vec{0, 3.6f, 0}, Vec{1.4f, .35f, 1.4f}}, {Vec{0, 1.6f, 0}, Vec{.06f, 1.7f, .06f}}}, g);
		SdfPart look = umbrella.look;
		// (物の座標: 一番長い辺 (高さ) が 2。柄は x = 0 の細い棒で、傘の上の端は y = 2)
		const Vec shaftSide{.07f, .6f, 0}, canopyAbove{0, 2.06f, 0};
		const bool shaftBefore = SdfPartDistance(look, shaftSide) < 0,
		           canopyBefore = SdfPartDistance(look, canopyAbove) < 0;
		SdfApplyFloor(look, .2f);
		assert(
		    !shaftBefore && SdfPartDistance(look, shaftSide) < 0 &&
		    canopyBefore == (SdfPartDistance(look, canopyAbove) < 0));
		std::cout << "PASS: item look keeps boxes and L shapes and thickens only the thin shaft\n";
	}
	std::cout << "ALL PASS\n";
}
