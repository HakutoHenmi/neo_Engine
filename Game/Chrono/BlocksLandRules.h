// BlocksLandRules.h
// 積み木クラッシャーの専用ステージ「積み木の街」の形と積み木の配置。エンジンに依存しない。
// 位置は unit (チャレンジを始めたときの体の半径) を単位にした、場の真ん中からの x,z (+z が奥) と、床からの高さ y。
//  - 四角い塔・丸い塔 (まわりをらせんの坂が頂上まで巻く)・塔どうしをつなぐ曲がった橋 (下をくぐれる)・地面から塔の上へ上る S 字の高架の道・
//    丸い丘 (1 つはトンネルが通る)・すり鉢・こぶの並び・登れない縁の柵
//  - 高さは 1 つではない (橋の上と下、トンネルの上と中)。ある x,z には「下の面 bottom から上の面 top まで詰まった物」(LandSolid) がいくつか重なる
//    Support: その高さより下から始まる物の一番上 (物が置かれる面・弾が当たる面)・Stand: 足元から段 (ChallengeBlocksStep) 以内で乗れる一番高い面・
//    Blocked: 足元から段より高く、体の高さより低い所に物がある (そこへは進めない)
//  - 橋・坂・道は中心の線を細かく区切った帯 (LandRibbon)。見た目も当たりも同じ区切りの断面を使い、つなぎ目は前後で同じ断面を共有する (すき間ができない)。
//    らせんの坂の終わりから出る橋は、坂の最後の断面から始める。四角い塔へ渡る橋は、塔の面でちょうど終わる
#pragma once
#include "ChallengeRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Game::Chrono {

inline constexpr float ChallengeBlocksHalf = 115, ChallengeBlocksStartZ = -104, ChallengeBlocksStep = .6f,
                       ChallengeBlocksFence = 1.6f;
// 体の高さ (下をくぐれるか) は体の半径の BlocksBodyHeight 倍。壁に入らない・面に乗るのに調べる体の縁は、体の半径の BlocksBodyEdge 倍
// (体の縁は真ん中より少し高い所にも乗れる: 坂を上るとき、縁が先に高い所へ届くので、縁の距離 x BlocksEdgeRise だけ段を高く見る)
inline constexpr float BlocksBodyHeight = 1.6f, BlocksBodyEdge = .7f, BlocksEdgeRise = .5f;
// 乗る面の高さが傾きから見込める分より急に変わったときに合わせる速さ (1/秒)・面より下がってよい深さ (unit)
inline constexpr float BlocksSettleRate = 25, BlocksSinkMax = .15f;
// カメラを地面 (床・丘・すり鉢・こぶ) からこの高さ (unit) より上に保つ
inline constexpr float BlocksCameraLift = 1.5f;
// 結果の積み木の山 (ChallengeBlocks.cpp の BlocksPile): 場所 (広場。スタートの少し奥)・積み木の大きさ (場の積み木の倍率)・
// 落ちてくる高さ (積み木の数)・カメラ (山の裾の広さに対する離れ方と、山の高さに対する離れ方・見る向き (南から西へ)・上からの角度・
// 山を画面の右へ寄せる量・積み終わったあと回る速さ (ラジアン/秒))。カメラは山のてっぺんと一緒に上る
inline constexpr float BlocksPileX = 0, BlocksPileZ = -96, BlocksPileScale = 2.4f, BlocksPileFall = 5;
inline constexpr float BlocksPileNear = 2.3f, BlocksPileAway = 1.f, BlocksPileYaw = -.45f, BlocksPileRise = .34f,
                       BlocksPileShift = .2f, BlocksPileOrbit = .1f;
// 結果の景色の地面 (地球) の高さ: 場の床の板の下の面 (unit の何倍下か。すり鉢の底より上にならないように、板の下に隠す)
inline constexpr float BlocksPileGroundDrop = 4.05f;
// 殻を積む間のカメラ: 本当の山のときより (1 + BlocksPileShellAway) 倍離れる
inline constexpr float BlocksPileShellAway = 1.1f;
// 積み木の街のまわりの景色 (場の真ん中からの距離。unit の何倍): 近くのモデルを置く輪 (Near〜Mid)・遠くの板を立てる輪 (Mid〜Far)。
// 近くの林と家のかたまりの数と、遠くの板の数
inline constexpr float StageSceneryNear = 135, StageSceneryMid = 300, StageSceneryFar = 1300;
inline constexpr int StageSceneryGroves = 90, StageSceneryBoards = 420;
// 積み木の街のかすみの距離の倍率 (unit の何倍。街の大きさのかすみより遠くからかすみ、近くの木と家は色のまま、遠くの板は少しかすむ)
inline constexpr float StageSceneryFogScale = 2.2f;
// 帯の厚み・手すりの幅と高さ・高架の柱の半径
// (手すりは、体の縁が段より少し高い所に乗れる分 (BlocksEdgeRise) より高くする)
inline constexpr float LandThick = 1, LandRailWidth = .35f, LandRailHeight = 1.2f, LandPillarRadius = .7f;

// 四角い地形 (塔の箱・坂・階段): x0..x1・z0..z1 の四角。rise の向きへ高さが h0 から h1 へ上がる (Flat なら h0 の平ら)。steps があれば階段
enum class BlocksRise : uint8_t { Flat, PlusX, MinusX, PlusZ, MinusZ };
struct BlocksTerrain {
	float x0, z0, x1, z1, h0, h1;
	BlocksRise rise;
	int steps;
};
inline float BlocksTerrainHeight(const BlocksTerrain& p, float x, float z) {
	if (x < p.x0 || x > p.x1 || z < p.z0 || z > p.z1)
		return -1e9f;
	float t = 0;
	switch (p.rise) {
	case BlocksRise::PlusX:
		t = (x - p.x0) / (p.x1 - p.x0);
		break;
	case BlocksRise::MinusX:
		t = (p.x1 - x) / (p.x1 - p.x0);
		break;
	case BlocksRise::PlusZ:
		t = (z - p.z0) / (p.z1 - p.z0);
		break;
	case BlocksRise::MinusZ:
		t = (p.z1 - z) / (p.z1 - p.z0);
		break;
	default:
		return p.h0;
	}
	t = std::clamp(t, 0.f, 1.f);
	if (p.steps > 0)
		t = std::min(1.f, (std::floor(t * float(p.steps)) + 1) / float(p.steps));
	return p.h0 + (p.h1 - p.h0) * t;
}

// 丸い塔 (円柱)・丸い丘 (真ん中の flat の範囲は平ら。tunnelHalf があれば z の向きにトンネルが通る)・すり鉢 (底は平ら)・こぶ
struct LandTower {
	float x, z, r, h;
};
struct LandDome {
	float x, z, r, h, tunnelHalf, tunnelTop;
};
struct LandBowl {
	float x, z, r, depth;
};
struct LandBump {
	float x, z, r, h;
};
// 丘・すり鉢の形: d が flat までは 1、r で 0 になる、なめらかな山
inline float LandBell(float d, float flat, float r) {
	if (d <= flat)
		return 1;
	if (d >= r)
		return 0;
	return .5f * (1 + std::cos(3.14159265f * (d - flat) / (r - flat)));
}
inline constexpr float LandDomeFlat = .2f, LandBowlFlat = .3f;
inline float LandDomeHeight(const LandDome& d, float x, float z) {
	return d.h * LandBell(std::hypot(x - d.x, z - d.z), d.r * LandDomeFlat, d.r);
}
// トンネルの屋根があるか (1 unit ごとの区切りで決める。見た目の区切りと同じ)
inline bool LandTunnelCovered(const LandDome& d, float z) {
	const float zc = std::floor(z - d.z) + .5f;
	return d.h * LandBell(std::abs(zc), d.r * LandDomeFlat, d.r) > d.tunnelTop + 1;
}

// 帯 (橋・坂・道): 中心 (x, 高さ, z) と左向き (x,0,z の単位) を細かく並べる。左右の端は中心 ± 左向き x half
// kind: 0 橋・1 らせんの坂・2 高架の道 (色)。手すりは左右別に付ける。pillars は柱を立てる区切りの番号
struct LandRibbon {
	std::vector<Vec> at, left;
	float half = 2.6f;
	int kind = 0;
	bool railLeft = true, railRight = true;
	std::vector<int> pillars;
	float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
	void Bound() {
		for (const auto& p : at) {
			minX = std::min(minX, p.x - half - 1);
			maxX = std::max(maxX, p.x + half + 1);
			minZ = std::min(minZ, p.z - half - 1);
			maxZ = std::max(maxZ, p.z + half + 1);
		}
	}
	Vec Tangent(size_t i) const {
		return {left[i].z, 0, -left[i].x};
	}
};

// ある x,z に重なる物 (下の面 bottom から上の面 top まで)
struct LandSolid {
	float bottom, top;
};
struct LandSolids {
	LandSolid s[32];
	int n = 0;
	void Add(float bottom, float top) {
		if (n < 32)
			s[n++] = {bottom, top};
	}
};

struct BlocksLand {
	std::vector<BlocksTerrain> boxes;
	std::vector<LandTower> towers;
	std::vector<LandDome> domes;
	std::vector<LandBowl> bowls;
	std::vector<LandBump> bumps;
	std::vector<LandRibbon> ribbons;

	// 地面 (床・すり鉢・こぶ・丘) の高さ。トンネルの中は床で、屋根があれば roof に下と上の面を入れる
	float Base(float x, float z, LandSolid* roof = nullptr) const {
		float h = 0;
		if (roof)
			*roof = {1e9f, -1e9f};
		for (const auto& b : bowls)
			h -= b.depth * LandBell(std::hypot(x - b.x, z - b.z), b.r * LandBowlFlat, b.r);
		for (const auto& b : bumps)
			h += b.h * LandBell(std::hypot(x - b.x, z - b.z), 0, b.r);
		for (const auto& d : domes) {
			const float dh = LandDomeHeight(d, x, z);
			if (dh <= 0)
				continue;
			if (d.tunnelHalf > 0 && std::abs(x - d.x) < d.tunnelHalf) {
				if (roof && LandTunnelCovered(d, z))
					*roof = {d.tunnelTop, dh};
				continue;
			}
			h = std::max(h, dh);
		}
		return h;
	}
	void Solids(float x, float z, LandSolids& q) const {
		if (std::abs(x) > ChallengeBlocksHalf || std::abs(z) > ChallengeBlocksHalf) {
			q.Add(-1e9f, ChallengeBlocksFence);
			return;
		}
		LandSolid roof;
		q.Add(-1e9f, Base(x, z, &roof));
		if (roof.top > roof.bottom)
			q.Add(roof.bottom, roof.top);
		for (const auto& b : boxes) {
			const float h = BlocksTerrainHeight(b, x, z);
			if (h > -1e8f)
				q.Add(-1e9f, h);
		}
		for (const auto& t : towers)
			if (std::hypot(x - t.x, z - t.z) <= t.r)
				q.Add(-1e9f, t.h);
		for (const auto& rb : ribbons) {
			if (x < rb.minX || x > rb.maxX || z < rb.minZ || z > rb.maxZ)
				continue;
			const size_t n = rb.at.size();
			for (size_t i = 0; i + 1 < n; ++i) {
				const Vec& a = rb.at[i];
				const Vec& b = rb.at[i + 1];
				const float ex = b.x - a.x, ez = b.z - a.z, len2 = ex * ex + ez * ez;
				if (len2 < 1e-8f)
					continue;
				float t = ((x - a.x) * ex + (z - a.z) * ez) / len2;
				// 帯の両端より外は帯ではない (つなぎ目は前後どちらかの区切りに入る)
				if ((i == 0 && t < 0) || (i + 2 == n && t > 1))
					continue;
				if (t < -.5f || t > 1.5f)
					continue;
				t = std::clamp(t, 0.f, 1.f);
				const float cx = a.x + ex * t, cz = a.z + ez * t;
				const float lx = rb.left[i].x + (rb.left[i + 1].x - rb.left[i].x) * t,
				            lz = rb.left[i].z + (rb.left[i + 1].z - rb.left[i].z) * t;
				const float side = (x - cx) * lx + (z - cz) * lz;
				const float along = std::hypot(x - cx - lx * side, z - cz - lz * side);
				if (std::abs(side) > rb.half || along > .6f)
					continue;
				const float y = a.y + (b.y - a.y) * t;
				const bool rail = std::abs(side) > rb.half - LandRailWidth && (side > 0 ? rb.railLeft : rb.railRight);
				q.Add(y - LandThick, rail ? y + LandRailHeight : y);
			}
			for (int k : rb.pillars) {
				const Vec& p = rb.at[size_t(k)];
				if (std::hypot(x - p.x, z - p.z) <= LandPillarRadius)
					q.Add(-1e9f, p.y - LandThick);
			}
		}
	}
	// y より下から始まる物の一番上 (物が置かれる面・弾が当たる面)
	float Support(float x, float z, float y) const {
		LandSolids q;
		Solids(x, z, q);
		float best = -1e9f;
		for (int i = 0; i < q.n; ++i)
			if (q.s[i].bottom <= y)
				best = std::max(best, q.s[i].top);
		return best;
	}
	// 足元 feet から段 step 以内で乗れる一番高い面 (無ければ Support)
	float Stand(float x, float z, float feet, float step) const {
		LandSolids q;
		Solids(x, z, q);
		float best = -1e9f;
		for (int i = 0; i < q.n; ++i)
			if (q.s[i].top <= feet + step)
				best = std::max(best, q.s[i].top);
		return best > -1e8f ? best : Support(x, z, feet + step);
	}
	// 足元 feet・体の高さ height で、そこへは進めないか
	bool Blocked(float x, float z, float feet, float step, float height) const {
		LandSolids q;
		Solids(x, z, q);
		for (int i = 0; i < q.n; ++i)
			if (q.s[i].top > feet + step && q.s[i].bottom < feet + height)
				return true;
		return false;
	}
};

// ---- 形を作る ----
namespace LandBuild {
// らせんの坂: 丸い塔のまわりを、床から頂上まで laps 周で上る。end は頂上に着く角度 (x から z へ回る向き)。坂の内側は塔の壁に付ける
inline LandRibbon Spiral(const LandTower& t, float end, float laps, float half) {
	LandRibbon r;
	r.kind = 1;
	r.half = half;
	r.railLeft = false; // 左 (内側) は塔の壁
	const float inner = t.r - .05f, mid = inner + half;
	const int n = std::max(8, int(laps * 72));
	for (int i = 0; i <= n; ++i) {
		const float s = laps * float(i) / float(n), a = end - 6.2831853f * (laps - s);
		r.at.push_back({t.x + std::cos(a) * mid, t.h * s / laps, t.z + std::sin(a) * mid});
		r.left.push_back({-std::cos(a), 0, -std::sin(a)});
	}
	r.Bound();
	return r;
}
// 曲がった帯: from (x,高さ,z) を dir0 の向きに出て、to に dir1 の向きで着く (xz は 3 次ベジェ)。高さは両端が水平になるようになめらかに変え、arch だけ真ん中を持ち上げる
// pillarEvery があれば、その区切りごとに柱を立てる (高さが 3 より高い所だけ)
inline LandRibbon Curve(Vec from, Vec dir0, Vec to, Vec dir1, float arch, float half, int kind, int pillarEvery,
                        bool rails) {
	LandRibbon r;
	r.kind = kind;
	r.half = half;
	r.railLeft = r.railRight = rails;
	const float L = std::hypot(to.x - from.x, to.z - from.z);
	const float p0x = from.x, p0z = from.z, p1x = from.x + dir0.x * L / 3, p1z = from.z + dir0.z * L / 3,
	            p2x = to.x - dir1.x * L / 3, p2z = to.z - dir1.z * L / 3, p3x = to.x, p3z = to.z;
	const int n = std::max(16, int(L * 1.2f));
	for (int i = 0; i <= n; ++i) {
		const float t = float(i) / float(n), m = 1 - t;
		const float x = m * m * m * p0x + 3 * m * m * t * p1x + 3 * m * t * t * p2x + t * t * t * p3x;
		const float z = m * m * m * p0z + 3 * m * m * t * p1z + 3 * m * t * t * p2z + t * t * t * p3z;
		float dx = 3 * m * m * (p1x - p0x) + 6 * m * t * (p2x - p1x) + 3 * t * t * (p3x - p2x);
		float dz = 3 * m * m * (p1z - p0z) + 6 * m * t * (p2z - p1z) + 3 * t * t * (p3z - p2z);
		// 両端は向きをぴったり合わせる (前後の物の断面と同じにする)
		if (i == 0) {
			dx = dir0.x;
			dz = dir0.z;
		} else if (i == n) {
			dx = dir1.x;
			dz = dir1.z;
		}
		const float dl = std::max(1e-6f, std::hypot(dx, dz));
		dx /= dl;
		dz /= dl;
		const float smooth = t * t * (3 - 2 * t), bump = std::sin(3.14159265f * t);
		const float y = from.y + (to.y - from.y) * smooth + arch * bump * bump;
		r.at.push_back({x, y, z});
		r.left.push_back({-dz, 0, dx});
		if (pillarEvery > 0 && i > 0 && i < n && i % pillarEvery == 0 && y - LandThick > 3)
			r.pillars.push_back(i);
	}
	r.Bound();
	return r;
}
} // namespace LandBuild

// 積み木の街の形 (1 回だけ作る)
inline const BlocksLand& Land() {
	static const BlocksLand land = [] {
		BlocksLand L;
		const float half = 2.6f;
		// 四角い塔 (北の大きな塔・真ん中の塔・東の低い塔・西の低い塔) と、西の塔へ上る坂
		L.boxes = {
		    {-13, 47, 3, 63, 20, 20, BlocksRise::Flat, 0},
		    {28, 8, 42, 22, 14, 14, BlocksRise::Flat, 0},
		    {69, -26, 81, -14, 10, 10, BlocksRise::Flat, 0},
		    {-86, -36, -74, -24, 8, 8, BlocksRise::Flat, 0},
		    {-74, -34, -62, -26, 0, 8, BlocksRise::MinusX, 0},
		};
		// 丸い塔 (らせんの坂で上る)
		L.towers = {{-50, 30, 9, 24}, {55, 60, 7, 16}};
		// 丸い丘 (手前の大きな丘はトンネルが通る)・すり鉢・こぶ
		L.domes = {{0, -30, 24, 12, 3, 5}, {-75, 70, 18, 8, 0, 0}, {80, 85, 12, 6, 0, 0}};
		L.bowls = {{-60, -75, 18, 6}};
		for (const auto& b : {std::array<float, 2>{-30, -75}, {-20, -75}, {-10, -75}, {10, -75}, {-25, -63}, {-15, -63},
		                      {15, -63}, {70, 30}, {80, 30}, {90, 30}, {75, 40}, {85, 40}})
			L.bumps.push_back({b[0], b[1], 4, 1.5f});
		// らせんの坂 (西の丸い塔は 2 周、東は 1 周半)
		LandRibbon sp0 = LandBuild::Spiral(L.towers[0], -.2f, 2, half);
		LandRibbon sp1 = LandBuild::Spiral(L.towers[1], 1.5707963f, 1.5f, half);
		// 橋: らせんの坂の終わりから北の大きな塔へ (坂の最後の断面から続ける)。大きな塔から真ん中の塔へ、真ん中の塔から東の塔へ、西の塔から手前の丘の上へ
		const Vec east{1, 0, 0}, west{-1, 0, 0}, north{0, 0, 1}, south{0, 0, -1};
		LandRibbon b0 = LandBuild::Curve(sp0.at.back(), sp0.Tangent(sp0.at.size() - 1), {-13, 20, 55}, east, 2, half,
		                                 0, 0, true);
		LandRibbon b1 = LandBuild::Curve(sp1.at.back(), sp1.Tangent(sp1.at.size() - 1), {3, 20, 55}, west, 2, half, 0,
		                                 0, true);
		LandRibbon b2 = LandBuild::Curve({-5, 20, 47}, south, {35, 14, 22}, south, 2, half, 0, 0, true);
		LandRibbon b3 = LandBuild::Curve({42, 14, 15}, east, {75, 10, -14}, south, 1.5f, half, 0, 14, true);
		LandRibbon b4 = LandBuild::Curve({-80, 8, -24}, north, {-5, 12, -30}, east, 2, half, 0, 14, true);
		// 高架の道: 手前の広場から S 字に曲がって東の塔の上へ (柱で支える。手すりは無い)
		LandRibbon road = LandBuild::Curve({20, .15f, -96}, east, {69, 10, -20}, east, 0, 3, 2, 6, false);
		L.ribbons = {sp0, sp1, b0, b1, b2, b3, b4, road};
		return L;
	}();
	return land;
}

// ---- 積み木の配置 ----
// (BlockSpot の y は、その高さより下から始まる面に置く。ふつうは地面と塔の上 (1)。丘の上やトンネルの屋根の上は高くする)
inline std::vector<BlockSpot> BlocksLandLayout() {
	std::vector<BlockSpot> out;
	auto add = [&](float x, float z, int level, BlockType type, float y = 1) {
		BlockSpot s{x, z, level, type};
		s.y = y;
		out.push_back(s);
	};
	// nx x nz の levels 段 (x,z は左下の積み木の真ん中)。kind: 0 普通・1 硬い芯と金色のてっぺん・2 てっぺんが金色
	auto grid = [&](float x, float z, int nx, int nz, int levels, int kind, float y = 1) {
		for (int i = 0; i < nx; ++i)
			for (int j = 0; j < nz; ++j)
				for (int l = 0; l < levels; ++l) {
					const bool edge = i == 0 || j == 0 || i == nx - 1 || j == nz - 1;
					BlockType t = BlockType::Normal;
					if (kind == 1 && !edge)
						t = l == levels - 1 && (i + j) % 2 == 0 ? BlockType::Gold : BlockType::Hard;
					if (kind == 2 && l == levels - 1 && (i + j) % 3 == 0)
						t = BlockType::Gold;
					add(x + float(i), z + float(j), l, t, y);
				}
	};
	// ピラミッド (n は下の段の 1 辺。てっぺんは金色)
	auto pyramid = [&](float cx, float cz, int n, float y = 1) {
		for (int l = 0; n - 2 * l > 0; ++l) {
			const int m = n - 2 * l;
			for (int i = 0; i < m; ++i)
				for (int j = 0; j < m; ++j)
					add(cx + float(i) - float(m - 1) * .5f, cz + float(j) - float(m - 1) * .5f, l,
					    m <= 1 ? BlockType::Gold : BlockType::Normal, y);
		}
	};
	// 壁 (x の向きに n 個・levels 段。真ん中の 4 個は硬い)
	auto wall = [&](float x, float z, int n, int levels, bool hardMiddle) {
		for (int i = 0; i < n; ++i)
			for (int l = 0; l < levels; ++l)
				add(x + float(i), z, l,
				    hardMiddle && std::abs(float(i) - float(n - 1) * .5f) < 2 ? BlockType::Hard : BlockType::Normal);
	};
	// 輪 (半径 r に count 個・levels 段) と真ん中の金色
	auto ring = [&](float cx, float cz, float r, int count, int levels, int goldLevels, float y) {
		for (int i = 0; i < count; ++i) {
			const float a = 6.2831853f * float(i) / float(count);
			for (int l = 0; l < levels; ++l)
				add(cx + std::cos(a) * r, cz + std::sin(a) * r, l, BlockType::Normal, y);
		}
		for (int l = 0; l < goldLevels; ++l)
			add(cx, cz, l, l == goldLevels - 1 ? BlockType::Gold : BlockType::Hard, y);
	};
	// 手前の広場: ピラミッド 3 つと、真ん中が開いた低い壁
	pyramid(-30, -98, 5);
	pyramid(-52, -104, 5);
	pyramid(45, -106, 5);
	wall(-14, -86, 11, 3, false);
	wall(4, -86, 11, 3, false);
	// こぶの間の小さな塔
	grid(-35.5f, -69.5f, 2, 2, 3, 2);
	grid(-5.5f, -69.5f, 2, 2, 3, 2);
	grid(-.5f, -80.5f, 2, 2, 3, 2);
	// 手前の丘: トンネルの中に金色の列、丘のてっぺんに輪
	for (int k = 0; k < 5; ++k)
		add(0, -36 + float(k) * 3, 0, BlockType::Gold);
	ring(0, -30, 4, 12, 2, 2, 100);
	// すり鉢の底: 硬い芯の 4x4
	grid(-61.5f, -76.5f, 4, 4, 3, 1, 1);
	// 北の大きな塔の上: 硬い城 (8x8 の 2 段。芯は硬く、金色)
	grid(-8.5f, 51.5f, 8, 8, 2, 1, 100);
	// 真ん中の塔・東の塔・西の塔の上
	grid(30, 9, 4, 4, 3, 2, 100);
	pyramid(77, -22, 5, 100);
	grid(-85, -35, 4, 4, 2, 2, 100);
	// 丸い塔の上: 輪と金色
	ring(-50, 30, 5, 16, 2, 2, 100);
	ring(55, 60, 3.5f, 10, 1, 2, 100);
	// 橋の下の壁 (北の塔から真ん中の塔への橋・東の丸い塔からの橋・真ん中の塔から東の塔への橋)
	wall(5, 30, 21, 3, true);
	wall(5, 38, 21, 3, false);
	wall(12, 64, 19, 2, false);
	wall(50, 0, 16, 2, true);
	// 西の丸い塔のふもと・西の広場・真ん中の広場
	grid(-32, 22, 3, 8, 3, 0);
	grid(-34, 2, 6, 4, 3, 2);
	grid(12, 0, 6, 4, 2, 2);
	// 東のこぶの間の細い塔
	for (const auto& p : {std::array<float, 2>{75, 30}, {85, 30}, {80, 40}})
		grid(p[0], p[1], 1, 1, 4, 2);
	// 丘のてっぺんのピラミッド
	pyramid(-75, 70, 5, 100);
	pyramid(80, 85, 3, 100);
	// 北の長い壁 3 枚 (真ん中は硬く、奥の壁は金色が混じる)
	for (int w = 0; w < 3; ++w)
		for (int i = 0; i < 81; ++i)
			for (int l = 0; l < 3; ++l) {
				BlockType t = BlockType::Normal;
				if (w == 1 && std::abs(i - 40) < 6)
					t = l == 1 && i == 40 ? BlockType::Gold : BlockType::Hard;
				if (w == 2 && l == 2 && i % 10 == 5)
					t = BlockType::Gold;
				add(-40 + float(i), 95 + float(w) * 5, l, t);
			}
	// 西の柱の林 (1x1 の 5 段。下を壊すと上が落ちる)
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 11; ++j)
			for (int l = 0; l < 5; ++l)
				add(-100 + float(i) * 4, float(j) * 4, l,
				    l == 4 && (i + j) % 4 == 0 ? BlockType::Gold : BlockType::Normal);
	// 南東と南西の広場
	grid(85, -92, 6, 6, 2, 1);
	grid(-106, -110, 8, 8, 2, 2);
	pyramid(98, -108, 5);
	// 東の縁の塔の列 (2x2 の 4 段)
	for (int k = 0; k < 11; ++k)
		grid(100, -60 + float(k) * 12, 2, 2, 4, 2);
	return out;
}

} // namespace Game::Chrono
