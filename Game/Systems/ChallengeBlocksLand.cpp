// ChallengeBlocksLand.cpp
// 積み木クラッシャーの専用ステージ「積み木の街」の見た目と、体の止め方 (ChronoSystem の一部。形と配置は Chrono/BlocksLandRules.h)。
//  - 見た目は形の決まり (BlocksLand) と同じ数で作る: 地面のでこぼこ (丘・すり鉢・こぶ) は 1 unit の升の三角形、塔は多角形の柱、
//    橋・坂・道は帯の断面を順につないだ板 (前後の区切りで同じ断面を使うので、曲がってもすき間ができない)。手すりと柱も同じ帯から作る
//  - 帯・塔・丘は区切りごとに別の物にして、カメラと体の間に入ったら薄くする (下の段にいるとき、上を通る橋やらせんの坂が体を隠さないように)
//  - 体は「足元から段より高く、体の高さより低い物」がある所へは進めない (崖・塔の壁・手すり・低い橋の下)。壁には沿って滑る。橋の上から外へ出ると落ちる
//  - 体は足元の 9 か所から求めた傾いた面に乗り、流体の床にも同じ傾きを渡す (坂でも体が浮いたり埋まったりしない)
//  - 弾・投げた物は動いた道すじを細かく調べて、橋・坂・塔・手すりに当たる (BlocksSweep)
//  - 確認用 (--blocks-land-check): 積み木が平らな所に置けているか・場所ごとに歩いて行けるかを tests/out/blocks-land.txt に書く
#include "ChronoSystemInternal.h"
#include "CityBuilder.h"
#include "../Data/GameData.h"
#include <fstream>
#include <unordered_set>

namespace Game {
namespace {
using Builder = CityParts::CityBuilder;
// 色 (積み木のおもちゃの色が目立つよう、少しくすませる)
constexpr Vec kFloor{.82f, .79f, .72f}, kFence{.60f, .44f, .31f}, kBridge{.68f, .52f, .38f}, kSpiral{.90f, .80f, .58f},
    kRoad{.62f, .78f, .56f}, kHill{.60f, .78f, .52f}, kBowl{.84f, .76f, .60f}, kBump{.70f, .84f, .62f};
constexpr Vec kTowers[] = {{.72f, .79f, .89f}, {.88f, .74f, .72f}};
constexpr Vec kBoxes[] = {{.84f, .74f, .84f}, {.92f, .82f, .60f}, {.66f, .76f, .88f}, {.88f, .72f, .66f}, {.88f, .72f, .66f}};
constexpr Vec kUp{0, 1, 0}, kDown{0, -1, 0};

// unit の位置から世界の位置へ
struct LandSpace {
	Vec c;
	float u;
	Vec Pos(float x, float y, float z) const {
		return {c.x + x * u, c.y + y * u, c.z + z * u};
	}
};
// 面を 1 枚足す: hint の向きを表にする (法線は面の形から求める)
void Face(Builder& b, std::initializer_list<Vec> pts, Vec hint) {
	const Vec* p = pts.begin();
	Vec n = SwarmCross(p[1] - p[0], p[2] - p[0]);
	if (pts.size() >= 4 && Length(n) < 1e-10f)
		n = SwarmCross(p[2] - p[0], p[3] - p[0]);
	if (Length(n) < 1e-10f)
		return;
	if (Dot(n, hint) < 0)
		n = n * -1.f;
	b.Polygon(CityParts::CityMetal, pts, Unit(n));
}
// 多角形の柱 (x,z が真ん中・半径 r・高さ y0 から y1)。top なら上のふたも作る
void Cylinder(Builder& b, const LandSpace& s, float x, float z, float r, float y0, float y1, int count, bool top) {
	for (int i = 0; i < count; ++i) {
		const float a0 = 6.2831853f * float(i) / float(count), a1 = 6.2831853f * float(i + 1) / float(count);
		const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
		Face(b,
		     {s.Pos(x + c0 * r, y0, z + s0 * r), s.Pos(x + c1 * r, y0, z + s1 * r), s.Pos(x + c1 * r, y1, z + s1 * r),
		      s.Pos(x + c0 * r, y1, z + s0 * r)},
		     Vec{(c0 + c1), 0, (s0 + s1)});
		if (top)
			Face(b, {s.Pos(x, y1, z), s.Pos(x + c0 * r, y1, z + s0 * r), s.Pos(x + c1 * r, y1, z + s1 * r)}, kUp);
	}
}
} // namespace

// チャレンジの場を見せる: 積み木の街の配置なら街を、ほかは平らな床を見せる
void ChronoSystem::ShowChallengeStage(entt::registry& r) {
	ShowBlocksStage(r, BlocksStageActive());
}

// 積み木の街を作る (unit か場の真ん中が変わったら作り直す) と、見せる・隠す。見せる間は戦う場所の床を隠す (同じ高さで重なるので)
void ChronoSystem::ShowBlocksStage(entt::registry& r, bool show) {
	const float u = battle_.unit;
	const V c = battle_.arenaAt;
	bool built = !blocksStage_.empty() && std::abs(blocksStageUnit_ - u) < 1e-4f && Length(blocksStageAt_ - c) < 1e-3f;
	for (auto e : blocksStage_)
		if (!r.valid(e))
			built = false;
	if (show && !built) {
		for (auto e : blocksStage_)
			if (r.valid(e))
				r.destroy(e);
		blocksStage_.clear();
		blocksStageUnit_ = u;
		blocksStageAt_ = c;
		const auto& land = Land();
		const LandSpace s{c, u};
		auto make = [&](const char* name, Builder& builder, Vec color, bool occluder, V lo, V hi) {
			auto& g = builder.geometry[CityParts::CityMetal];
			if (g.indices.empty())
				return;
			auto e = r.create();
			r.emplace<NameComponent>(e, name);
			r.emplace<TransformComponent>(e).scale = {1, 1, 1};
			auto& mr = r.emplace<MeshRendererComponent>(e);
			mr.modelHandle = Engine::Renderer::GetInstance()->CreateDynamicMesh(g.vertices, g.indices);
			mr.textureHandle = white_;
			mr.extraTextureHandles = lowMaps_;
			mr.shaderName = "LowpolySurface";
			mr.useCubemap = false;
			mr.color = {color.x, color.y, color.z, 1};
			// カメラと体の間に入ったら薄くする (上に乗っているときは消さない)
			if (occluder)
				r.emplace<CameraOccluder>(e, (lo + hi) * .5f, hi - lo, 1.f);
			blocksStage_.push_back(e);
		};
		// 地面のでこぼこ: 1 unit の升を 2 枚の三角形にする。keepFlat でなければ高さが 0 の升は作らない (床の板が見える)
		auto heightfield = [&](Builder& b, float x0, float z0, float x1, float z1, bool keepFlat, auto&& include) {
			const float lift = keepFlat ? 0.f : .002f;
			for (float x = x0; x < x1 - 1e-3f; x += 1)
				for (float z = z0; z < z1 - 1e-3f; z += 1) {
					if (!include(x + .5f, z + .5f))
						continue;
					const float h00 = land.Base(x, z), h10 = land.Base(x + 1, z), h11 = land.Base(x + 1, z + 1),
					            h01 = land.Base(x, z + 1);
					if (!keepFlat && std::max({std::abs(h00), std::abs(h10), std::abs(h11), std::abs(h01)}) < 1e-3f)
						continue;
					Face(b, {s.Pos(x, h00 + lift, z), s.Pos(x + 1, h10 + lift, z), s.Pos(x + 1, h11 + lift, z + 1)}, kUp);
					Face(b, {s.Pos(x, h00 + lift, z), s.Pos(x + 1, h11 + lift, z + 1), s.Pos(x, h01 + lift, z + 1)}, kUp);
				}
		};
		auto all = [](float, float) { return true; };
		// 床 (すり鉢の四角は穴にして、すり鉢の升で埋める) と縁の柵
		{
			const float e = ChallengeBlocksHalf + 1;
			Builder floor;
			auto slab = [&](float x0, float z0, float x1, float z1) {
				if (x1 > x0 && z1 > z0)
					floor.Box(CityParts::CityMetal, V{c.x + x0 * u, c.y - 4, c.z + z0 * u}, s.Pos(x1, 0, z1));
			};
			if (land.bowls.empty())
				slab(-e, -e, e, e);
			else {
				const auto& w = land.bowls.front();
				const float bx0 = w.x - w.r, bx1 = w.x + w.r, bz0 = w.z - w.r, bz1 = w.z + w.r;
				slab(-e, -e, e, bz0);
				slab(-e, bz1, e, e);
				slab(-e, bz0, bx0, bz1);
				slab(bx1, bz0, e, bz1);
			}
			make("Chrono Blocks floor", floor, kFloor, false, {}, {});
			Builder bowl;
			for (const auto& w : land.bowls)
				heightfield(bowl, w.x - w.r, w.z - w.r, w.x + w.r, w.z + w.r, true, all);
			make("Chrono Blocks bowl", bowl, kBowl, false, {}, {});
			Builder bumps;
			for (const auto& b : land.bumps)
				heightfield(bumps, std::floor(b.x - b.r), std::floor(b.z - b.r), std::ceil(b.x + b.r),
				            std::ceil(b.z + b.r), false, all);
			make("Chrono Blocks bumps", bumps, kBump, false, {}, {});
			const float f = ChallengeBlocksFence, h = ChallengeBlocksHalf;
			Builder fence;
			fence.Box(CityParts::CityMetal, s.Pos(-h - 1, 0, h), s.Pos(h + 1, f, h + 1));
			fence.Box(CityParts::CityMetal, s.Pos(-h - 1, 0, -h - 1), s.Pos(h + 1, f, -h));
			fence.Box(CityParts::CityMetal, s.Pos(-h - 1, 0, -h), s.Pos(-h, f, h));
			fence.Box(CityParts::CityMetal, s.Pos(h, 0, -h), s.Pos(h + 1, f, h));
			make("Chrono Blocks fence", fence, kFence, false, {}, {});
		}
		// 丸い丘 (トンネルの通る所は、屋根の上と下・左右の壁・入口の面を別に作る)
		for (const auto& d : land.domes) {
			Builder hill;
			const bool tunnel = d.tunnelHalf > 0;
			auto outside = [&](float x, float) { return !tunnel || std::abs(x - d.x) >= d.tunnelHalf; };
			heightfield(hill, d.x - d.r, d.z - d.r, d.x + d.r, d.z + d.r, false, outside);
			if (tunnel) {
				const float tx0 = d.x - d.tunnelHalf, tx1 = d.x + d.tunnelHalf, top = d.tunnelTop;
				for (float z = d.z - d.r; z < d.z + d.r - 1e-3f; z += 1) {
					const bool covered = LandTunnelCovered(d, z + .5f);
					// 左右の壁 (屋根があれば屋根の下まで、無ければ丘の面まで)
					for (int side = 0; side < 2; ++side) {
						const float x = side == 0 ? tx0 : tx1;
						const float h0 = covered ? top : LandDomeHeight(d, x, z),
						            h1 = covered ? top : LandDomeHeight(d, x, z + 1);
						if (h0 > 1e-3f || h1 > 1e-3f)
							Face(hill, {s.Pos(x, 0, z), s.Pos(x, 0, z + 1), s.Pos(x, h1, z + 1), s.Pos(x, h0, z)},
							     V{side == 0 ? 1.f : -1.f, 0, 0});
					}
					if (covered)
						for (float x = tx0; x < tx1 - 1e-3f; x += 1) {
							const float h00 = LandDomeHeight(d, x, z), h10 = LandDomeHeight(d, x + 1, z),
							            h11 = LandDomeHeight(d, x + 1, z + 1), h01 = LandDomeHeight(d, x, z + 1);
							Face(hill, {s.Pos(x, h00, z), s.Pos(x + 1, h10, z), s.Pos(x + 1, h11, z + 1)}, kUp);
							Face(hill, {s.Pos(x, h00, z), s.Pos(x + 1, h11, z + 1), s.Pos(x, h01, z + 1)}, kUp);
							Face(hill, {s.Pos(x, top, z), s.Pos(x + 1, top, z), s.Pos(x + 1, top, z + 1), s.Pos(x, top, z + 1)},
							     kDown);
						}
					// 入口の面 (屋根の下の面から丘の面まで)
					const bool before = LandTunnelCovered(d, z - .5f);
					if (covered != before)
						for (float x = tx0; x < tx1 - 1e-3f; x += 1)
							Face(hill,
							     {s.Pos(x, top, z), s.Pos(x + 1, top, z), s.Pos(x + 1, LandDomeHeight(d, x + 1, z), z),
							      s.Pos(x, LandDomeHeight(d, x, z), z)},
							     V{0, 0, covered ? -1.f : 1.f});
				}
			}
			make("Chrono Blocks hill", hill, kHill, d.h >= 6, s.Pos(d.x - d.r, 0, d.z - d.r), s.Pos(d.x + d.r, d.h, d.z + d.r));
		}
		// 四角い塔・坂・階段
		for (size_t i = 0; i < land.boxes.size(); ++i) {
			const auto& p = land.boxes[i];
			Builder builder;
			const float top = std::max(p.h0, p.h1);
			if (p.rise == BlocksRise::Flat) {
				builder.Box(CityParts::CityMetal, s.Pos(p.x0, 0, p.z0), s.Pos(p.x1, p.h0, p.z1));
			} else if (p.steps > 0) {
				const bool alongX = p.rise == BlocksRise::PlusX || p.rise == BlocksRise::MinusX;
				const bool up = p.rise == BlocksRise::PlusX || p.rise == BlocksRise::PlusZ;
				const float lo = alongX ? p.x0 : p.z0, len = (alongX ? p.x1 : p.z1) - lo, n = float(p.steps);
				for (int k = 0; k < p.steps; ++k) {
					const float a = lo + len * (up ? float(k) : n - 1 - float(k)) / n, b = a + len / n,
					            y = p.h0 + (p.h1 - p.h0) * float(k + 1) / n;
					if (alongX)
						builder.Box(CityParts::CityMetal, s.Pos(a, 0, p.z0), s.Pos(b, y, p.z1));
					else
						builder.Box(CityParts::CityMetal, s.Pos(p.x0, 0, a), s.Pos(p.x1, y, b));
				}
			} else {
				// 坂: 四隅 (x0z0・x1z0・x1z1・x0z1) の高さで上の斜面を張り、横の面は床から立ち上げる
				const float hs[4] = {
				    BlocksTerrainHeight(p, p.x0, p.z0), BlocksTerrainHeight(p, p.x1, p.z0),
				    BlocksTerrainHeight(p, p.x1, p.z1), BlocksTerrainHeight(p, p.x0, p.z1)};
				const float xs[4] = {p.x0, p.x1, p.x1, p.x0}, zs[4] = {p.z0, p.z0, p.z1, p.z1};
				Face(builder,
				     {s.Pos(xs[0], hs[0], zs[0]), s.Pos(xs[1], hs[1], zs[1]), s.Pos(xs[2], hs[2], zs[2]),
				      s.Pos(xs[3], hs[3], zs[3])},
				     kUp);
				const V outs[4] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
				for (int k = 0; k < 4; ++k) {
					const int j = (k + 1) % 4;
					if (hs[k] <= 1e-3f && hs[j] <= 1e-3f)
						continue;
					Face(builder,
					     {s.Pos(xs[k], 0, zs[k]), s.Pos(xs[j], 0, zs[j]), s.Pos(xs[j], hs[j], zs[j]), s.Pos(xs[k], hs[k], zs[k])},
					     outs[k]);
				}
			}
			make(p.rise == BlocksRise::Flat ? "Chrono Blocks tower" : "Chrono Blocks ramp", builder,
			     kBoxes[i % std::size(kBoxes)], p.rise == BlocksRise::Flat, s.Pos(p.x0, 0, p.z0), s.Pos(p.x1, top, p.z1));
		}
		// 丸い塔
		for (size_t i = 0; i < land.towers.size(); ++i) {
			const auto& t = land.towers[i];
			Builder builder;
			Cylinder(builder, s, t.x, t.z, t.r, 0, t.h, 48, true);
			make("Chrono Blocks round tower", builder, kTowers[i % std::size(kTowers)], true,
			     s.Pos(t.x - t.r, 0, t.z - t.r), s.Pos(t.x + t.r, t.h, t.z + t.r));
		}
		// 帯 (らせんの坂・橋・高架の道): 16 区切りごとに 1 つの物。断面は前後の区切りで共有する
		for (const auto& rb : land.ribbons) {
			const size_t n = rb.at.size();
			const float hl = rb.half, in = hl - LandRailWidth, T = LandThick, R = LandRailHeight;
			for (size_t i0 = 0; i0 + 1 < n; i0 += 16) {
				const size_t i1 = std::min(n - 1, i0 + 16);
				Builder b;
				V lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
				auto P = [&](size_t k, float side, float dy) {
					const V& p = rb.at[k];
					const V& l = rb.left[k];
					const V w = s.Pos(p.x + l.x * side, p.y + dy, p.z + l.z * side);
					lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
					hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
					return w;
				};
				for (size_t a = i0; a < i1; ++a) {
					const size_t z = a + 1;
					Face(b, {P(a, hl, 0), P(z, hl, 0), P(z, -hl, 0), P(a, -hl, 0)}, kUp);
					Face(b, {P(a, hl, -T), P(z, hl, -T), P(z, -hl, -T), P(a, -hl, -T)}, kDown);
					Face(b, {P(a, hl, 0), P(z, hl, 0), P(z, hl, -T), P(a, hl, -T)}, rb.left[a]);
					Face(b, {P(a, -hl, 0), P(z, -hl, 0), P(z, -hl, -T), P(a, -hl, -T)}, rb.left[a] * -1.f);
					for (int side = 0; side < 2; ++side) {
						if (!(side == 0 ? rb.railLeft : rb.railRight))
							continue;
						const float sg = side == 0 ? 1.f : -1.f;
						Face(b, {P(a, sg * hl, R), P(z, sg * hl, R), P(z, sg * in, R), P(a, sg * in, R)}, kUp);
						Face(b, {P(a, sg * in, 0), P(z, sg * in, 0), P(z, sg * in, R), P(a, sg * in, R)},
						     rb.left[a] * -sg);
						Face(b, {P(a, sg * hl, 0), P(z, sg * hl, 0), P(z, sg * hl, R), P(a, sg * hl, R)}, rb.left[a] * sg);
					}
				}
				// 帯の両端のふた
				for (size_t k : {size_t(0), n - 1}) {
					if (k < i0 || k > i1)
						continue;
					const V out = rb.Tangent(k) * (k == 0 ? -1.f : 1.f);
					Face(b, {P(k, hl, 0), P(k, -hl, 0), P(k, -hl, -T), P(k, hl, -T)}, out);
					for (int side = 0; side < 2; ++side)
						if (side == 0 ? rb.railLeft : rb.railRight) {
							const float sg = side == 0 ? 1.f : -1.f;
							Face(b, {P(k, sg * hl, R), P(k, sg * in, R), P(k, sg * in, 0), P(k, sg * hl, 0)}, out);
						}
				}
				// 高架の柱
				for (int k : rb.pillars)
					if (size_t(k) >= i0 && size_t(k) < i1) {
						const V& p = rb.at[size_t(k)];
						Cylinder(b, s, p.x, p.z, LandPillarRadius, land.Base(p.x, p.z) - .2f, p.y - T, 10, false);
					}
				make(rb.kind == 1   ? "Chrono Blocks spiral"
				     : rb.kind == 2 ? "Chrono Blocks road"
				                    : "Chrono Blocks bridge",
				     b, rb.kind == 1 ? kSpiral : rb.kind == 2 ? kRoad : kBridge, true, lo, hi);
			}
		}
	}
	for (auto e : blocksStage_)
		if (r.valid(e))
			if (auto* mr = r.try_get<MeshRendererComponent>(e))
				mr->enabled = show;
	if (r.valid(battleArenaEntity_))
		if (auto* mr = r.try_get<MeshRendererComponent>(battleArenaEntity_))
			mr->enabled = !show;
}

// 積み木の街で体を止める: 縁の柵の内側に入れ、足元から段より高く体の高さより低い物 (崖・塔の壁・手すり・低い橋の下) に体の縁が入らないようにする。
// 体の縁 (真ん中から体の半径の BlocksBodyEdge 倍の 8 か所) と真ん中で調べる。壁に当たったら、壁の向き (入った所の反対) に沿って滑らせる
// (丸い塔やらせんの坂の曲がった壁でも、x と z を別々に止めるとガクガク止まったり進んだりするので、壁に沿う向きへ動かす)。
// 体がもう壁に入っている (落ちた所が壁の際だった) ときは、少しずつ外へ押し出す (from は前のフレームの体の真ん中)
void ChronoSystem::BlockBlocksStageBody(V& at, V from, float radius) const {
	const float u = battle_.unit;
	const V c = battle_.arenaAt;
	const float h = ChallengeBlocksHalf * u - radius * .6f;
	at.x = std::clamp(at.x, c.x - h, c.x + h);
	at.z = std::clamp(at.z, c.z - h, c.z + h);
	const auto& land = Land();
	const float feet = (from.y - ChallengeBodyGround - c.y) / u, height = BlocksBodyHeight * radius / u,
	            edge = radius * BlocksBodyEdge, edgeStep = ChallengeBlocksStep + edge / u * BlocksEdgeRise;
	// 入っている所の数と、外へ向かう向き (入っている所の反対の向きの和)
	auto probe = [&](float x, float z, V& out) {
		int count = 0;
		out = {};
		if (land.Blocked((x - c.x) / u, (z - c.z) / u, feet, ChallengeBlocksStep, height))
			++count;
		for (int k = 0; k < 8; ++k) {
			const float a = .78539816f * float(k), dx = std::cos(a), dz = std::sin(a);
			if (land.Blocked((x + dx * edge - c.x) / u, (z + dz * edge - c.z) / u, feet, edgeStep, height)) {
				++count;
				out = out - V{dx, 0, dz};
			}
		}
		return count;
	};
	V away;
	const int now = probe(from.x, from.z, away);
	V wall;
	const int next = probe(at.x, at.z, wall);
	// 何にも入らない・入っている所が減る (壁から出ていく) なら、そのまま進む
	if (next == 0 || (now > 0 && next < now))
		return;
	const V move{at.x - from.x, 0, at.z - from.z};
	// 壁に沿って滑らせる: 進む向きを壁から離れる側へ 15 度ずつ回し、入らない一番小さい回し方で進む (長さは回した分だけ短く)。
	// 曲がった壁 (丸い塔・らせんの坂) でも、壁の向きを細かく求めずに沿える。壁の向きは、どちらへ回すかを決めるのにだけ使う
	// (真正面から当たったときは、左右どちらへも回さずに止まる)
	const V n = Length(wall) > 1e-4f ? Unit(wall) : V{};
	const float len = Length(move);
	if (len > 1e-6f) {
		const V f = move * (1 / len), side{-f.z, 0, f.x};
		const float turn = Dot(side, n);
		if (std::abs(turn) > .05f) {
			const float sign = turn > 0 ? 1.f : -1.f;
			for (int k = 1; k <= 6; ++k) {
				const float a = .26179939f * float(k), cs = std::cos(a), sn = std::sin(a) * sign;
				const V slide = (f * cs + side * sn) * (len * cs);
				V dummy;
				// (もう壁に触れていれば、触れたまま (深く入らずに) 滑ってよい)
				const int count = probe(from.x + slide.x, from.z + slide.z, dummy);
				if (count == 0 || count <= now) {
					at.x = from.x + slide.x;
					at.z = from.z + slide.z;
					return;
				}
			}
		}
	}
	// 進めない: 前の位置に残る。もう壁に入っていれば、外へ少し押し出す
	at.x = from.x;
	at.z = from.z;
	if (now > 0 && Length(away) > 1e-4f) {
		const V push = Unit(away) * (radius * .08f);
		V dummy;
		if (probe(from.x + push.x, from.z + push.z, dummy) <= now) {
			at.x += push.x;
			at.z += push.z;
		}
	}
}

// 体を乗せる面: 体の下の 9 か所 (真ん中と、縁の 8 か所) で、足元 (前のフレームの体の真ん中から求める) から段以内で乗れる面の高さを調べ、
// 傾き (x・z へ 1 進んだときの高さの変わり方。縁の 8 か所から最小二乗) を求めて、全部の所の上に乗る傾いた面にする
// (縁の所が少し高い段に乗っていれば、そちらに乗る)。段の際・橋の端のように高さが急に変わる所は、傾きに入れない (平らとして扱う)。
// 返すのは体の真ん中での面の高さ (世界の高さ)。傾きは体の見た目 (SlimeLife.cpp の形の点・流体の床) にも使う
float ChronoSystem::BlocksFootprint(V at, float fromY, float radius, V& slope) const {
	const float u = battle_.unit;
	const V c = battle_.arenaAt;
	const auto& land = Land();
	const float feet = (fromY - ChallengeBodyGround - c.y) / u, x = (at.x - c.x) / u, z = (at.z - c.z) / u,
	            d = std::max(.2f, radius * BlocksBodyEdge / u);
	auto H = [&](float ox, float oz) { return land.Stand(x + ox, z + oz, feet, ChallengeBlocksStep); };
	const float h0 = H(0, 0), e = d * .7071f;
	// 縁の 8 か所 (前後・左右・斜め)。傾きは 8 か所から最小二乗で求める (丘のふもとや肩のように傾きが変わる所でもなめらか)
	const float ox[8] = {d, -d, 0, 0, e, e, -e, -e}, oz[8] = {0, 0, d, -d, e, -e, e, -e};
	float hs[8];
	for (int k = 0; k < 8; ++k)
		hs[k] = H(ox[k], oz[k]);
	const float sharp = d * 1.2f;
	float sx = 0, sz = 0, sxx = 0, szz = 0;
	for (int k = 0; k < 8; ++k) {
		const float dh = std::abs(hs[k] - h0) <= sharp ? hs[k] - h0 : 0.f;
		sx += ox[k] * dh;
		sz += oz[k] * dh;
		sxx += ox[k] * ox[k];
		szz += oz[k] * oz[k];
	}
	const float gx = std::clamp(sx / sxx, -1.f, 1.f), gz = std::clamp(sz / szz, -1.f, 1.f);
	float top = h0;
	for (int k = 0; k < 8; ++k)
		top = std::max(top, hs[k] - (gx * ox[k] + gz * oz[k]));
	slope = {gx, 0, gz};
	return c.y + top * u;
}

// 積み木の街で体の高さを決める (ゲームと確認用の試しで同じ): 乗る面 (BlocksFootprint) の上に乗る。段より高い所から下りたら落ちる。
// 面の高さが傾きから見込める分より急に変わったとき (縁の所が段の際をまたいだときなど) は、その差だけ少しずつ合わせる (ガクガクしないように)。
// 坂を上り下りする分は傾きで見込むので遅れない。面より下には BlocksSinkMax (unit) までしか下がらない
float ChronoSystem::BlocksSettle(V at, V from, float radius, float dt, float& fallSpeed, V& slope) const {
	const float u = battle_.unit;
	const float target = BlocksFootprint(at, from.y, radius, slope) + ChallengeBodyGround;
	if (fallSpeed > 0 || target < from.y - ChallengeBlocksStep * u) {
		fallSpeed += u * 30 * dt;
		const float y = std::max(target, from.y - fallSpeed * dt);
		if (y <= target + 1e-4f)
			fallSpeed = 0;
		return y;
	}
	const float predicted = from.y + slope.x * (at.x - from.x) + slope.z * (at.z - from.z);
	const float y = predicted + (target - predicted) * (1 - std::exp(-BlocksSettleRate * dt));
	return std::clamp(y, target - BlocksSinkMax * u, target + ChallengeBlocksStep * u);
}

// 積み木の街: 体の形の点を、乗っている面の傾き (lifeSlope_[0]: 体の右・前へ 1 進むごとの高さ) に合わせて回す。
// 上の向きを面の法線に重ねる回転 (ずらす (せん断) と、坂が急なほど形が坂に沿って平たくなるので、回して形をそのまま傾ける)。
// 体の中に浮かべる物にも同じ回し方を使い、体の形と一緒に傾ける
ChronoSystem::V ChronoSystem::BodyTiltApply(V v) const {
	if (!BlocksStageActive())
		return v;
	const V t = lifeSlope_[0];
	const V n = Unit(V{-t.x, 1, -t.z});
	const V axis{n.z, 0, -n.x};
	const float s = Length(axis);
	if (s < 1e-4f)
		return v;
	const V a = axis * (1 / s);
	const float c = n.y;
	return v * c + SwarmCross(a, v) * s + a * (Dot(a, v) * (1 - c));
}

// 積み木の街: from から to へ動いた物 (弾・投げた物) が街の物 (橋・坂・塔・丘・手すり) に当たったか。
// 細かく区切って調べるので、速い弾でも薄い橋をすり抜けない。物の中に入った所が上の面から lift より浅ければ (または上から落ちてきたなら)
// 面に乗った (wall = false、hit は上の面の上)。深ければ壁に当たった (wall = true、hit は当たる手前)。
// normal は当たった面の外向き (坂・丘では傾いた向き。跳ねる弾はどちらでもこの向きで跳ね返る)
bool ChronoSystem::BlocksSweep(V from, V to, float lift, V& hit, V& normal, bool& wall) const {
	if (!BlocksStageActive())
		return false;
	const float u = battle_.unit;
	const V c = battle_.arenaAt;
	const auto& land = Land();
	// その所で、高さ y (unit) を含む物の上の面 (無ければとても低い値)
	auto inside = [&](float x, float z, float y) {
		LandSolids q;
		land.Solids(x, z, q);
		float top = -1e9f;
		for (int i = 0; i < q.n; ++i)
			if (y >= q.s[i].bottom && y < q.s[i].top)
				top = std::max(top, q.s[i].top);
		return top;
	};
	const V d = to - from;
	const int n = std::clamp(int(std::ceil(Length(d) / (u * .25f))), 1, 64);
	V prev = from;
	for (int k = 1; k <= n; ++k) {
		const V p = from + d * (float(k) / float(n));
		const float x = (p.x - c.x) / u, z = (p.z - c.z) / u, y = (p.y - c.y) / u;
		const float top = inside(x, z, y);
		if (top > -1e8f) {
			const float prevY = (prev.y - c.y) / u;
			const bool onTop = top - y <= lift / u || prevY >= top - .05f;
			// 面の向き: まわりの面の高さの傾きから求める (丘・坂・すり鉢は傾いた向き)。まわりの高さが急に変わる所
			// (塔の壁・橋の端) は、上に乗ったなら上向き、横から当たったなら、まわりのどちら側が物の中かで横向き
			const float e = .5f;
			const float hx1 = land.Support(x + e, z, top + .3f), hx0 = land.Support(x - e, z, top + .3f),
			            hz1 = land.Support(x, z + e, top + .3f), hz0 = land.Support(x, z - e, top + .3f);
			const float steep = 2.5f * e;
			if (std::max({std::abs(hx1 - top), std::abs(hx0 - top), std::abs(hz1 - top), std::abs(hz0 - top)}) <= steep)
				normal = Unit(V{-(hx1 - hx0) / (2 * e), 1, -(hz1 - hz0) / (2 * e)});
			else if (onTop)
				normal = {0, 1, 0};
			else {
				const float nx = (inside(x - e, z, y) > -1e8f ? 1.f : 0.f) - (inside(x + e, z, y) > -1e8f ? 1.f : 0.f);
				const float nz = (inside(x, z - e, y) > -1e8f ? 1.f : 0.f) - (inside(x, z + e, y) > -1e8f ? 1.f : 0.f);
				V w{nx, 0, nz};
				if (Length(w) < 1e-4f)
					w = V{-d.x, 0, -d.z};
				normal = Length(w) > 1e-4f ? Unit(w) : V{0, 0, 1};
			}
			if (onTop) {
				hit = {p.x, c.y + top * u, p.z};
				wall = false;
			} else {
				hit = prev;
				wall = true;
			}
			return true;
		}
		prev = p;
	}
	return false;
}

// 確認用 (--blocks-land-check): 積み木が平らな所に置けているか・ほかの物に埋まっていないか・場所ごとに歩いて行けるか
void ChronoSystem::WriteBlocksLandCheck() const {
	const auto& land = Land();
	const auto spots = BlocksLandLayout();
	std::filesystem::create_directories("tests/out");
	std::ofstream out("tests/out/blocks-land.txt");
	int counts[3] = {};
	int uneven = 0, buried = 0;
	for (const auto& sp : spots) {
		++counts[int(sp.type)];
		if (sp.level != 0)
			continue;
		const float base = land.Support(sp.x, sp.z, sp.y);
		float lo = base, hi = base;
		for (float dx : {-.45f, .45f})
			for (float dz : {-.45f, .45f}) {
				const float h = land.Support(sp.x + dx, sp.z + dz, sp.y);
				lo = std::min(lo, h);
				hi = std::max(hi, h);
			}
		if (hi - lo > .05f) {
			++uneven;
			out << "uneven x=" << sp.x << " z=" << sp.z << " base=" << base << " spread=" << hi - lo << '\n';
		}
		if (land.Blocked(sp.x, sp.z, base, .05f, 1)) {
			++buried;
			out << "buried x=" << sp.x << " z=" << sp.z << " base=" << base << '\n';
		}
	}
	// 歩いて行ける所: 0.25 unit の升 (1 フレームに進む長さに近い)と足元の高さ (0.25 刻み) で広げる。体の半径は 1 unit とする (積み木は壊せるので数えない)
	const float step = .25f, H = ChallengeBlocksHalf - .6f;
	auto key = [](int ix, int iz, int iy) {
		return (uint64_t(uint32_t(ix + 4096)) << 40) | (uint64_t(uint32_t(iz + 4096)) << 20) | uint64_t(uint32_t(iy + 4096));
	};
	// (体の縁の 8 か所と真ん中。ゲームの体の止め方と同じ)
	auto blocked = [&](float x, float z, float feet) {
		if (land.Blocked(x, z, feet, ChallengeBlocksStep, BlocksBodyHeight))
			return true;
		for (int k = 0; k < 8; ++k) {
			const float a = .78539816f * float(k);
			if (land.Blocked(x + std::cos(a) * BlocksBodyEdge, z + std::sin(a) * BlocksBodyEdge, feet,
			                 ChallengeBlocksStep + BlocksBodyEdge * BlocksEdgeRise, BlocksBodyHeight))
				return true;
		}
		return false;
	};
	std::unordered_set<uint64_t> seen;
	std::vector<std::array<float, 3>> queue;
	std::vector<std::array<float, 3>> reached;
	{
		const float x = 0, z = ChallengeBlocksStartZ, f = land.Stand(x, z, 0, ChallengeBlocksStep);
		queue.push_back({x, z, f});
		seen.insert(key(0, int(std::round(z / step)), int(std::round(f * 4))));
	}
	for (size_t qi = 0; qi < queue.size() && queue.size() < 4000000; ++qi) {
		const auto cur = queue[qi];
		reached.push_back(cur);
		const float dirs[4][2] = {{step, 0}, {-step, 0}, {0, step}, {0, -step}};
		for (const auto& d : dirs) {
			const float nx = cur[0] + d[0], nz = cur[1] + d[1];
			if (std::abs(nx) > H || std::abs(nz) > H || blocked(nx, nz, cur[2]))
				continue;
			const float nf = land.Stand(nx, nz, cur[2], ChallengeBlocksStep);
			const uint64_t k = key(int(std::round(nx / step)), int(std::round(nz / step)), int(std::round(nf * 4)));
			if (seen.insert(k).second)
				queue.push_back({nx, nz, nf});
		}
	}
	// 行きたい所 (名前・x,z・足元の高さ)
	struct Goal {
		std::string name;
		float x, z, y;
	};
	std::vector<Goal> goals;
	for (size_t i = 0; i < land.boxes.size(); ++i) {
		const auto& b = land.boxes[i];
		if (b.rise == BlocksRise::Flat)
			goals.push_back({"box" + std::to_string(i) + "_top", (b.x0 + b.x1) * .5f, (b.z0 + b.z1) * .5f, b.h0});
	}
	for (size_t i = 0; i < land.towers.size(); ++i) {
		const auto& t = land.towers[i];
		goals.push_back({"round" + std::to_string(i) + "_top", t.x, t.z, t.h});
	}
	for (size_t i = 0; i < land.domes.size(); ++i) {
		const auto& d = land.domes[i];
		goals.push_back({"hill" + std::to_string(i) + "_top", d.x + d.r * .2f, d.z, d.h});
		if (d.tunnelHalf > 0)
			goals.push_back({"hill" + std::to_string(i) + "_tunnel", d.x, d.z, 0});
	}
	for (const auto& w : land.bowls)
		goals.push_back({"bowl_bottom", w.x, w.z, -w.depth});
	for (size_t i = 0; i < land.ribbons.size(); ++i) {
		const auto& rb = land.ribbons[i];
		const auto& m = rb.at[rb.at.size() / 2];
		goals.push_back({"ribbon" + std::to_string(i) + "_middle", m.x, m.z, m.y});
	}
	int missing = 0;
	for (const auto& g : goals) {
		bool ok = false;
		for (const auto& p : reached)
			if (std::abs(p[0] - g.x) < 1.6f && std::abs(p[1] - g.z) < 1.6f && std::abs(p[2] - g.y) < .4f) {
				ok = true;
				break;
			}
		if (!ok)
			++missing;
		out << "goal " << g.name << " x=" << g.x << " z=" << g.z << " y=" << g.y << (ok ? " ok" : " UNREACHED") << '\n';
	}
	// 体の動きの試し (ゲームと同じ止め方・乗り方): 高さのがたつき (1 フレームの高さの変わり方が、前のフレームとどれだけ違うか。unit)・
	// 止まったフレームの数・壁に入っている体の縁の数の最大
	const float u = battle_.unit, R = u, speed = BattleMoveSpeed(u, R), dt = 1.f / 60;
	const V c = battle_.arenaAt;
	auto edgeIn = [&](V at) {
		int count = 0;
		const float feet = (at.y - ChallengeBodyGround - c.y) / u;
		for (int k = 0; k < 8; ++k) {
			const float a = .78539816f * float(k);
			if (land.Blocked((at.x - c.x) / u + std::cos(a) * BlocksBodyEdge, (at.z - c.z) / u + std::sin(a) * BlocksBodyEdge,
			                 feet, ChallengeBlocksStep + BlocksBodyEdge * BlocksEdgeRise, BlocksBodyHeight))
				++count;
		}
		return count;
	};
	auto world = [&](float x, float y, float z) { return V{c.x + x * u, c.y + y * u, c.z + z * u}; };
	auto simulate = [&](const char* name, V start, auto&& wish, int frames) {
		V at = start, first;
		at.y += ChallengeBodyGround;
		at.y = BlocksFootprint(at, at.y, R, first) + ChallengeBodyGround;
		float fall = 0, prevDy = 0, jerk = 0, floatMax = 0, sinkMax = 0;
		int stalls = 0, worstIn = 0;
		for (int f = 0; f < frames; ++f) {
			V w = wish(at);
			w.y = 0;
			w = Length(w) > 1e-5f ? Unit(w) : V{0, 0, 1};
			V next = at + w * (speed * dt);
			next.y = at.y;
			BlockBlocksStageBody(next, at, R);
			V slope;
			next.y = BlocksSettle(next, at, R, dt, fall, slope);
			const float dy = next.y - at.y;
			if (f > 2 && fall <= 0)
				jerk = std::max(jerk, std::abs(dy - prevDy) / u);
			prevDy = dy;
			if (std::hypot(next.x - at.x, next.z - at.z) < speed * dt * .2f)
				++stalls;
			at = next;
			worstIn = std::max(worstIn, edgeIn(at));
			// 体の見た目の床 (傾いた面) と本当の地面の差: 体のまわり (体の半径の 0.5・1.0・1.5 倍、8 方向) で、
			// 面が地面より上 (浮く) と下 (埋まる) の一番大きい所 (unit)。段の際 (段より高い・低い所) は数えない
			if (fall <= 0) {
				const float feet = (at.y - ChallengeBodyGround - c.y) / u;
				for (float rr : {.5f, 1.f, 1.5f})
					for (int k = 0; k < 8; ++k) {
						const float a = .78539816f * float(k), ox = std::cos(a) * rr * R / u, oz = std::sin(a) * rr * R / u;
						const float ground = land.Stand((at.x - c.x) / u + ox, (at.z - c.z) / u + oz, feet, 2.f);
						const float plane = feet + slope.x * ox + slope.z * oz, gap = plane - ground;
						if (std::abs(gap) < 2)
							(gap > 0 ? floatMax : sinkMax) = std::max(gap > 0 ? floatMax : sinkMax, std::abs(gap));
					}
			}
		}
		out << "move " << name << " jerk=" << jerk << " stalls=" << stalls << "/" << frames
		    << " edge_in_max=" << worstIn << " float_max=" << floatMax << " sink_max=" << sinkMax << " end=("
		    << (at.x - c.x) / u << "," << (at.y - c.y) / u << "," << (at.z - c.z) / u << ")" << '\n';
		return at;
	};
	// らせんの坂を上る (一番近い坂の区切りの向きへ進む)
	{
		const auto& sp = land.ribbons[0];
		auto along = [&](V at) {
			size_t best = 0;
			float bd = 1e9f;
			for (size_t i = 0; i < sp.at.size(); ++i) {
				const V w = world(sp.at[i].x, sp.at[i].y, sp.at[i].z);
				const float dd = std::hypot(w.x - at.x, w.z - at.z) + std::abs(w.y - (at.y - ChallengeBodyGround)) * 2;
				if (dd < bd) {
					bd = dd;
					best = i;
				}
			}
			return sp.Tangent(best);
		};
		const auto& p = sp.at[4];
		simulate("spiral_up", world(p.x, p.y + .5f, p.z), along, 900);
	}
	// 四角い塔の壁へ斜めに押し付けて歩く (壁に沿って滑る)
	simulate("slide_box_wall", world(-17, .5f, 49), [](V) { return V{1, 0, .35f}; }, 240);
	// らせんの坂を、内側の塔の壁へ押し付けながら上る (曲がった壁に沿う)
	{
		const auto& sp = land.ribbons[1];
		auto hug = [&](V at) {
			size_t best = 0;
			float bd = 1e9f;
			for (size_t i = 0; i < sp.at.size(); ++i) {
				const V w = world(sp.at[i].x, sp.at[i].y, sp.at[i].z);
				const float dd = std::hypot(w.x - at.x, w.z - at.z) + std::abs(w.y - (at.y - ChallengeBodyGround)) * 2;
				if (dd < bd) {
					bd = dd;
					best = i;
				}
			}
			return sp.Tangent(best) + sp.left[best] * .5f;
		};
		const auto& p = sp.at[6];
		simulate("spiral_hug_wall", world(p.x, p.y + .5f, p.z), hug, 600);
	}
	// 丘を越える (トンネルの横)・すり鉢を横切る
	simulate("hill_climb", world(10, .5f, -62), [](V) { return V{0, 0, 1}; }, 420);
	// 丘のふもと (傾きが変わる所) を回る
	{
		const auto& d = land.domes[1];
		auto around = [&](V at) {
			const float x = (at.x - c.x) / u - d.x, z = (at.z - c.z) / u - d.z, l = std::max(.01f, std::hypot(x, z));
			const float pull = (l - d.r * .8f) * .3f;
			return V{-z / l - x / l * pull, 0, x / l - z / l * pull};
		};
		simulate("hill_toe_around", world(d.x + d.r * .8f, 1, d.z), around, 600);
	}
	simulate("bowl_cross", world(-60, .5f, -97), [](V) { return V{0, 0, 1}; }, 380);
	// 橋の上から外へ出て下へ落ちる・らせんの坂の外の縁のすぐ外へ落ちる (落ちたあと壁に入っていないか)
	{
		const auto& br = land.ribbons[2];
		const auto& m = br.at[br.at.size() / 2];
		const V side = br.left[br.at.size() / 2];
		simulate("fall_off_bridge", world(m.x, m.y + .1f, m.z), [&](V) { return side; }, 240);
		const auto& t = land.towers[0];
		const float a = 1.f, rr = t.r + 5.2f + .3f;
		simulate("drop_beside_spiral", world(t.x + std::cos(a) * rr, 30, t.z + std::sin(a) * rr),
		         [&](V) { return V{-std::cos(a), 0, -std::sin(a)}; }, 240);
	}
	// 弾の試し: 速い弾が橋を上から下へ 1 フレームで抜ける・塔の壁へ横から・らせんの坂の横の面へ横から
	{
		auto shot = [&](const char* name, V from, V to) {
			V hit, n;
			bool wall = false;
			const bool ok = BlocksSweep(from, to, u * ChallengeBlocksStep, hit, n, wall);
			out << "shot " << name << " hit=" << ok << " wall=" << wall << " at=(" << (hit.x - c.x) / u << ","
			    << (hit.y - c.y) / u << "," << (hit.z - c.z) / u << ") normal=(" << n.x << "," << n.y << "," << n.z
			    << ")" << '\n';
		};
		const auto& br = land.ribbons[2];
		const auto& m = br.at[br.at.size() / 2];
		shot("through_bridge", world(m.x, m.y + 1.5f, m.z), world(m.x, m.y - 2.5f, m.z));
		shot("into_tower_wall", world(24, 2, 15), world(30, 2, 15));
		const auto& t = land.towers[0];
		// (らせんの坂の 1 周目の 1/4 の所。上の面の高さは 3 なので、高さ 2.5 は坂の横の面)
		const float a = -.2f + 1.5707963f;
		shot("into_spiral_side", world(t.x + std::cos(a) * 18, 2.5f, t.z + std::sin(a) * 18),
		     world(t.x + std::cos(a) * 11, 2.5f, t.z + std::sin(a) * 11));
		shot("along_ground", world(0, .5f, -100), world(0, .5f, -95));
		// (丘の坂へ上から・すり鉢の坂へ横から: 面の向きが坂に沿って傾くか)
		shot("onto_hill_slope", world(12, 10, -30), world(12, 7, -30));
		shot("into_bowl_side", world(-60, -3, -68), world(-60, -3, -61));
	}
	out << "summary blocks=" << spots.size() << " normal=" << counts[0] << " hard=" << counts[1]
	    << " gold=" << counts[2] << " uneven=" << uneven << " buried=" << buried << " reached=" << reached.size()
	    << " goals=" << goals.size() - size_t(missing) << "/" << goals.size() << '\n';
}

// ---- 積み木の街のまわりの景色 (遊んでいる間) ----
// 地面: 結果の画面と同じ地球 (野原の絵。場の板の下の面の高さ)。近く (StageSceneryNear〜Mid): Blender で作ったローポリの木と家、
// 積み木で建てた家 (箱を並べる)。遠く (Mid〜Far): 木と家の絵を貼った板 (ビルボード。カメラへ向ける) で軽くする。
// 物の大きさは場の積み木に合わせたおもちゃの大きさで、遠い板ほど少し大きくして地平線まで埋める
namespace {
uint32_t SceneryHash(uint32_t v) {
	v ^= v >> 16;
	v *= 0x7feb352du;
	v ^= v >> 15;
	v *= 0x846ca68bu;
	return v ^ (v >> 16);
}
float SceneryRand(uint32_t& s) {
	s = SceneryHash(s + 0x9e3779b9u);
	return float(s >> 8) / 16777216.f;
}
} // namespace

void ChronoSystem::BuildStageScenery() {
	auto& sc = stageScenery_;
	const float u = battle_.unit;
	const V center = battle_.arenaAt;
	sc = {};
	sc.unit = u;
	sc.center = center;
	sc.groundY = center.y - u * BlocksPileGroundDrop;
	auto* render = Engine::Renderer::GetInstance();
	if (lowPalette_ == 0) {
		lowPalette_ = render->LoadTexture2D("Resources/Textures/Lowpoly/palette.png");
		lowMaps_ = {render->LoadTexture2D("Resources/Textures/PBR/NeutralNormal.png", false), white_, white_};
	}
	// 板の絵と、升ごとの板の形 (足元が原点。上向きの法線で、日の向きによらず同じ明るさ。両面)
	if (boardTex_ == 0) {
		boardTex_ = render->LoadTexture2D("Resources/Textures/Lowpoly/billboards.png");
		const auto& data = Data::Load("billboards.json");
		const int cols = data.value("columns", 4), rows = data.value("rows", 2);
		int index = 0;
		for (const auto& cell : data.at("cells")) {
			const float w = cell.value("width", 1.f), h = cell.value("height", 1.f);
			const int c = index % cols, r = index / cols;
			const float u0 = (float(c) + (1 - w) * .5f) / float(cols), u1 = (float(c) + (1 + w) * .5f) / float(cols),
			            v1 = float(r + 1) / float(rows), v0 = (float(r + 1) - h) / float(rows);
			std::vector<Engine::VertexData> vertices(4);
			const float xs[] = {-w * .5f, w * .5f, w * .5f, -w * .5f}, ys[] = {0, 0, h, h}, us[] = {u0, u1, u1, u0},
			            vs[] = {v1, v1, v0, v0};
			for (int k = 0; k < 4; ++k) {
				vertices[size_t(k)].position = {xs[k], ys[k], 0, 1};
				vertices[size_t(k)].normal = {0, 1, 0};
				vertices[size_t(k)].texcoord = {us[k], vs[k]};
			}
			boardMeshes_.push_back(render->CreateDynamicMesh(vertices, {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2}));
			boardShapes_.push_back({w, h, cell.value("id", std::string()).rfind("house", 0) == 0 ? 1.f : 0.f});
			++index;
		}
	}
	uint32_t seed = 4242;
	const float board = (ChallengeBlocksHalf + 3) * u;
	auto ground = [&](V at) {
		at.y = sc.groundY;
		return at;
	};
	auto outside = [&](V at, float radius) {
		return std::max(std::abs(at.x - center.x), std::abs(at.z - center.z)) > board + radius;
	};
	auto ring = [&](float from, float to) {
		const float a = SceneryRand(seed) * 6.2831853f,
		            r = std::sqrt(from * from + (to * to - from * from) * SceneryRand(seed)) * u;
		return center + V{std::cos(a) * r, 0, std::sin(a) * r};
	};
	auto pick = [&](std::initializer_list<const char*> ids) { return ids.begin()[SceneryHash(seed++) % ids.size()]; };
	// ローポリのモデルを高さ height で置く
	auto model = [&](const char* id, V at, float height) {
		const auto& m = LowModelOf(id);
		const float s = height / std::max(m.hi.y - m.lo.y, 1e-4f);
		BlocksPileDecor d;
		d.mesh = m.mesh;
		d.half = V{s, s, s};
		d.yaw = SceneryRand(seed) * 6.2831853f;
		d.at = ground(at);
		d.at.y -= m.lo.y * s;
		d.size = std::max({m.hi.x - m.lo.x, m.hi.y - m.lo.y, m.hi.z - m.lo.z}) * s * .5f;
		sc.models.push_back(d);
	};
	// 近く: 木の林・ローポリの家・積み木の家
	for (int n = 0; n < StageSceneryGroves; ++n) {
		const V at = ring(StageSceneryNear, StageSceneryMid);
		if (!outside(at, 12 * u))
			continue;
		const float roll = SceneryRand(seed);
		if (roll < .5f) {
			const int trees = 2 + int(SceneryRand(seed) * 4);
			for (int k = 0; k < trees; ++k) {
				const float a = SceneryRand(seed) * 6.2831853f, d = SceneryRand(seed) * 10 * u;
				const V spot = at + V{std::cos(a) * d, 0, std::sin(a) * d};
				if (outside(spot, 6 * u))
					model(pick({"ki_maru", "sugi", "sakura", "matsu", "kaki_no_ki", "ki_maru"}), spot, (16 + SceneryRand(seed) * 16) * u);
			}
		} else if (roll < .7f) {
			model(pick({"house_hiraya", "house_kawara", "my_house"}), at, (12 + SceneryRand(seed) * 6) * u);
		} else {
			// 積み木の家: 壁 (一辺 3 個・高さ 2 段の外周。前の真ん中の下は戸口) と、段々の屋根 (2 段) と煙突。積み木は場の積み木の色
			const float b = (2.2f + SceneryRand(seed) * .8f) * u, yaw = SceneryRand(seed) * 6.2831853f;
			const V fx{std::cos(yaw), 0, std::sin(yaw)}, fz{-std::sin(yaw), 0, std::cos(yaw)};
			static constexpr Engine::Vector4 walls[] = {{.98f, .95f, .88f, 1}, {.98f, .78f, .3f, 1}, {.4f, .72f, .95f, 1}, {.95f, .6f, .85f, 1}};
			static constexpr Engine::Vector4 roofs[] = {{.95f, .42f, .38f, 1}, {.4f, .72f, .95f, 1}, {.5f, .85f, .45f, 1}};
			const auto wall = walls[SceneryHash(seed++) % 4], roof = roofs[SceneryHash(seed++) % 3];
			auto cube = [&](float i, float j, float level, Engine::Vector4 color, float scale = 1) {
				BlocksPileDecor d;
				d.at = ground(at + fx * (i * b) + fz * (j * b));
				d.at.y += (level + .5f) * b;
				d.half = V{b * .49f * scale, b * .49f * scale, b * .49f * scale};
				d.size = b * .5f;
				d.color = color;
				sc.cubes.push_back(d);
			};
			for (int level = 0; level < 2; ++level)
				for (int i = -1; i <= 1; ++i)
					for (int j = -1; j <= 1; ++j)
						if ((i != 0 || j != 0) && !(level == 0 && i == 0 && j == 1))
							cube(float(i), float(j), float(level), wall);
			for (int i = -1; i <= 1; ++i)
				for (int j = -1; j <= 1; ++j)
					cube(float(i), float(j), 2, roof);
			cube(0, 0, 3, roof);
			cube(1, -1, 3, {.55f, .5f, .48f, 1}, .6f);
		}
	}
	// 遠く: 木と家の絵の板 (家は 3 割)。遠いほど少し大きい
	for (int n = 0; n < StageSceneryBoards; ++n) {
		const V at = ring(StageSceneryMid, StageSceneryFar);
		const float away = std::clamp((Length(at - center) / u - StageSceneryMid) / (StageSceneryFar - StageSceneryMid), 0.f, 1.f);
		const bool house = SceneryRand(seed) < .3f;
		int cell = 0;
		for (int tries = 0; tries < 16; ++tries) {
			cell = int(SceneryHash(seed++) % uint32_t(boardShapes_.size()));
			if ((boardShapes_[size_t(cell)][2] > .5f) == house)
				break;
		}
		const float height = (house ? 14 + SceneryRand(seed) * 6 : 20 + SceneryRand(seed) * 18) * u * (1 + 1.5f * away);
		sc.boards.push_back({ground(at), height / std::max(boardShapes_[size_t(cell)][1], .05f), cell});
	}
}

void ChronoSystem::DrawStageScenery(GameContext& ctx, bool ground) {
	auto& sc = stageScenery_;
	if (sc.unit != battle_.unit || Length(sc.center - battle_.arenaAt) > 1e-3f)
		BuildStageScenery();
	const auto eye = ctx.camera->GetPosition();
	const V camera{eye.x, eye.y, eye.z};
	// 地面 (地球の絵。結果の画面と同じ物。結果の画面では、そちらが描く)
	PreparePileWorld(false);
	if (ground && earthMesh_ != 0) {
		const float meter = battle_.unit * ChallengeBlockSize * BlocksPileScale / (ChallengePileBlockCm / 100),
		            radius = 6371000.f * meter;
		Engine::Transform t;
		t.translate = EV(V{sc.center.x, sc.groundY, sc.center.z});
		t.scale = {radius, radius, radius};
		ctx.renderer->DrawMeshInstanced(
		    earthMesh_, earthTex_ ? earthTex_ : white_, t,
		    earthTex_ ? Engine::Vector4{1, 1, 1, 1} : Engine::Vector4{.16f, .37f, .07f, 1});
	}
	// 近くの物 (画面で 2 点より小さく見える物は描かない)
	const float scale = ctx.viewportSize.y / (2 * std::tan(.6f));
	for (const auto& d : sc.models) {
		const float dist = Length(d.at - camera);
		if (d.size * 2 * scale < dist * 2)
			continue;
		Engine::Transform t;
		t.translate = EV(d.at);
		t.scale = EV(d.half);
		t.rotate = {0, d.yaw, 0};
		ctx.renderer->DrawMeshInstanced(d.mesh, lowPalette_, t, {1, 1, 1, 1}, "LowpolySurface", lowMaps_);
	}
	for (const auto& d : sc.cubes) {
		if (d.size * 2 * scale < Length(d.at - camera) * 2)
			continue;
		Engine::Transform t;
		t.translate = EV(d.at);
		t.scale = EV(d.half);
		const auto c = d.color;
		ctx.renderer->DrawMeshInstanced(
		    cube_, white_, t, Engine::Vector4{std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f), 1});
	}
	// 遠くの板: カメラへ向ける (縦の軸だけ回す)
	for (const auto& bd : sc.boards) {
		if (bd.cell < 0 || bd.cell >= int(boardMeshes_.size()))
			continue;
		V to = camera - bd.at;
		to.y = 0;
		Engine::Transform t;
		t.translate = EV(bd.at);
		t.scale = {bd.size, bd.size, bd.size};
		t.rotate = {0, std::atan2(to.x, to.z), 0};
		// (近くのモデルと同じ描き方で、同じかすみがかかる)
		ctx.renderer->DrawMeshInstanced(boardMeshes_[size_t(bd.cell)], boardTex_, t, {1, 1, 1, 1}, "LowpolySurface", lowMaps_);
	}
}

} // namespace Game
