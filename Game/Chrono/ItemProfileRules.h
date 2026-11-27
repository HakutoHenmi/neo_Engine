// ItemProfileRules.h
// 物のプロフィール (H1。エンジンに依存しない)。186 万通りの組み合わせを自動で「意味のある姿」にするための、物ごとの材料表。
//  - 形のパーツ: 物のモデル (三角形) を細かい箱に分け、つながった部分ごとに、なめらかな基本形 (WeaponShapeSdf.h) 1〜4 個で近似する。
//    物を基本形 1 個にすると個性が消えるので、だるま = 大きな球と小さな球、傘 = ドームと柄、のように部分を残す。
//    小さすぎる部分は近くの大きな部分にまとめ、1 つの部分で形が合わないとき (重なりの割合 fit が低い) は、主軸の向きに 2 つに割る
//  - 形の種類: 丸・長い・平たい・輪・箱 (物全体の主軸の長さと、詰まり具合と、穴で決める)
//  - 形の特徴: 穴がある・持ち手がある (細長い小さな部分)・立っている (一番長い向きが上下)
//  - 意味のタグ: Resources/Data/item_profiles.json に手で書く (分類と、その物でできること)。「配慮」は神仏・お墓に関わる物 (武器の部品にしない)
// 座標: 物の一番長い辺を 2 (半分が 1) にそろえ、左右前後の真ん中を 0、一番下を y=0 にする。向きはモデルのまま (y が上)
#pragma once
#include "WeaponShapeSdf.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace Game::Chrono {
enum class ItemShapeClass : uint8_t { Round, Long, Flat, Ring, Box, Count };
inline const char* ItemShapeClassName(ItemShapeClass c) {
	static constexpr const char* names[] = {"丸", "長い", "平たい", "輪", "箱"};
	return names[std::min(int(c), int(ItemShapeClass::Count) - 1)];
}
// 形の特徴 (ビット)
enum ItemFeature : uint32_t { ItemHasHole = 1, ItemHasHandle = 2, ItemUpright = 4 };
// 意味のタグに使ってよい言葉 (item_profiles.json の確かめに使う)
inline const std::vector<std::string>& ItemTagWords() {
	static const std::vector<std::string> words = {// 分類
	                                               "生き物", "乗り物", "道具", "食べ物", "文房具", "おもちゃ", "暮らし",
	                                               "自然", "和もの", "街の物", "スポーツ", "入れ物", "建物", "配慮",
	                                               // その物でできること
	                                               "乗る", "持つ", "かぶる", "投げる", "入れる", "回る", "飛ぶ",
	                                               "転がる", "着る", "座る", "積む", "振る", "撃つ", "光る", "鳴る"};
	return words;
}
inline bool ItemTagKnown(const std::string& tag) {
	const auto& w = ItemTagWords();
	return std::find(w.begin(), w.end(), tag) != w.end();
}

// 箱の大きさ (一番長い辺を何個に分けるか)・パーツの数の上限・まとめる小さな部分の大きさ (全体の体積に対する割合)・割るかどうかの重なりの割合
inline constexpr int ItemProfileCells = 40, ItemPartMax = 4;
// 物の形の距離の場: まわりに足す空きの箱の数・ならす回数 (3x3x3 の平均。1 回で角が 1 箱ぶんほど丸くなる)・外へ太さを伝える回数
inline constexpr int ItemFieldPad = 16, ItemFieldSmooth = 2, ItemFieldSpread = 16;
inline constexpr float ItemFieldSmoothThick = 4;
inline constexpr float ItemPartMinShare = .06f, ItemSplitFit = .68f, ItemSplitGain = .04f;
// 輪の穴の大きさ (影絵の面積に対する割合) と、穴の真ん中が影絵の真ん中からずれてよい割合・持ち手の大きさの上限 (一番大きなパーツに対する体積の割合)・
// 細い骨組みを除いたあとに残す割合の下限 (これより減るなら除かない)
inline constexpr float ItemRingHole = .25f, ItemRingCenter = .25f, ItemHandleShare = .35f, ItemHandleLength = .4f,
                       ItemOpenKeep = .5f;
// 細い部分を細長いパーツとして戻す長さの下限 (半分の長さ。物の一番長い辺が 2)
inline constexpr float ItemRodLength = .45f;

struct ItemProfile {
	std::string id;
	ItemShapeClass shape = ItemShapeClass::Round;
	uint32_t features = 0;
	std::vector<SdfPart> parts;
	float fit = 0;
	Vec size{1, 1, 1};
	std::vector<std::string> tags;
	// 物の形そのもの (H1 の作り直し): 距離の場の番号 (SdfFields)・その場を物の座標に置いた部品 (見た目に使う。parts は役の判定に使う)
	int field = -1;
	SdfPart look;
	// モデルの元の大きさ (範囲の対角線。物どうしの大きさの比べに使う)
	float realSize = 1;
	bool Has(ItemFeature f) const {
		return (features & f) != 0;
	}
};

namespace ItemProfileDetail {
// 箱の並び: lo・cell・nx ny nz・つながった部分の番号 (0 は空、1 から)
struct Grid {
	Vec lo{};
	float cell = 1;
	int nx = 0, ny = 0, nz = 0;
	std::vector<uint16_t> label;
	size_t At(int x, int y, int z) const {
		return size_t((z * ny + y) * nx + x);
	}
	Vec Center(int x, int y, int z) const {
		return lo + Vec{float(x) + .5f, float(y) + .5f, float(z) + .5f} * cell;
	}
};
// 三角形を箱に分ける: つながった部分ごとに x の向きの線で偶奇を数えて中を埋め、部分の番号を付ける (先に埋めた部分が勝つ)
inline Grid Voxelize(const std::vector<Vec>& pts, const std::vector<uint32_t>& idx, int cells, int& parts) {
	Grid g;
	Vec lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	for (const auto& p : pts) {
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	}
	const Vec size = hi - lo;
	const float longest = std::max({size.x, size.y, size.z, 1e-4f});
	g.cell = longest / float(cells);
	g.lo = lo - Vec{g.cell, g.cell, g.cell};
	g.nx = int(std::ceil(size.x / g.cell)) + 3;
	g.ny = int(std::ceil(size.y / g.cell)) + 3;
	g.nz = int(std::ceil(size.z / g.cell)) + 3;
	g.label.assign(size_t(g.nx * g.ny * g.nz), 0);
	// つながった部分 (同じ位置の頂点と三角形の頂点どうし)
	const size_t n = pts.size();
	std::vector<uint32_t> parent(n);
	for (size_t i = 0; i < n; ++i)
		parent[i] = uint32_t(i);
	auto find = [&](uint32_t a) {
		while (parent[a] != a)
			a = parent[a] = parent[parent[a]];
		return a;
	};
	{
		std::map<std::array<long, 3>, uint32_t> same;
		const float q = 1000 / longest;
		for (size_t i = 0; i < n; ++i) {
			const std::array<long, 3> key{
			    std::lround(pts[i].x * q), std::lround(pts[i].y * q), std::lround(pts[i].z * q)};
			auto it = same.emplace(key, uint32_t(i));
			if (!it.second)
				parent[find(uint32_t(i))] = find(it.first->second);
		}
	}
	for (size_t k = 0; k + 2 < idx.size(); k += 3) {
		parent[find(idx[k])] = find(idx[k + 1]);
		parent[find(idx[k + 1])] = find(idx[k + 2]);
	}
	std::map<uint32_t, int> partOf;
	std::vector<int> triPart;
	for (size_t k = 0; k + 2 < idx.size(); k += 3) {
		auto it = partOf.emplace(find(idx[k]), int(partOf.size()));
		triPart.push_back(it.first->second);
	}
	parts = int(partOf.size());
	std::vector<std::vector<float>> hits(static_cast<size_t>(parts));
	std::vector<int> partFill(static_cast<size_t>(parts), 0);
	std::vector<std::array<Vec, 2>> box(static_cast<size_t>(parts), {Vec{1e9f, 1e9f, 1e9f}, Vec{-1e9f, -1e9f, -1e9f}});
	for (size_t k = 0; k + 2 < idx.size(); k += 3) {
		auto& b = box[size_t(triPart[k / 3])];
		for (int c = 0; c < 3; ++c) {
			const Vec p = pts[idx[k + size_t(c)]];
			b[0] = {std::min(b[0].x, p.x), std::min(b[0].y, p.y), std::min(b[0].z, p.z)};
			b[1] = {std::max(b[1].x, p.x), std::max(b[1].y, p.y), std::max(b[1].z, p.z)};
		}
	}
	for (int z = 0; z < g.nz; ++z)
		for (int y = 0; y < g.ny; ++y) {
			const float py = g.lo.y + (float(y) + .5f) * g.cell + g.cell * .0137f,
			            pz = g.lo.z + (float(z) + .5f) * g.cell + g.cell * .0071f;
			for (auto& h : hits)
				h.clear();
			for (size_t k = 0; k + 2 < idx.size(); k += 3) {
				const Vec a = pts[idx[k]], b = pts[idx[k + 1]], c = pts[idx[k + 2]];
				const float d = (b.y - a.y) * (c.z - a.z) - (c.y - a.y) * (b.z - a.z);
				if (std::abs(d) < 1e-12f)
					continue;
				const float u = ((py - a.y) * (c.z - a.z) - (c.y - a.y) * (pz - a.z)) / d,
				            w = ((b.y - a.y) * (pz - a.z) - (py - a.y) * (b.z - a.z)) / d;
				if (u < 0 || w < 0 || u + w > 1)
					continue;
				hits[size_t(triPart[k / 3])].push_back(a.x + (b.x - a.x) * u + (c.x - a.x) * w);
			}
			for (int part = 0; part < parts; ++part) {
				auto& h = hits[size_t(part)];
				if (h.size() < 2)
					continue;
				std::sort(h.begin(), h.end());
				for (size_t i = 0; i + 1 < h.size(); i += 2) {
					const int x0 = int(std::floor((h[i] - g.lo.x) / g.cell + .5f)),
					          x1 = int(std::floor((h[i + 1] - g.lo.x) / g.cell - .5f));
					for (int x = std::max(0, x0); x <= std::min(g.nx - 1, x1); ++x) {
						auto& l = g.label[g.At(x, y, z)];
						if (!l) {
							l = uint16_t(part + 1);
							++partFill[size_t(part)];
						}
					}
				}
			}
		}
	// 閉じていない (ほとんど埋まらない) 部分は、範囲の箱を埋める (板 1 枚の物など)
	for (int part = 0; part < parts; ++part) {
		const auto& b = box[size_t(part)];
		const int x0 = int((b[0].x - g.lo.x) / g.cell), x1 = int((b[1].x - g.lo.x) / g.cell),
		          y0 = int((b[0].y - g.lo.y) / g.cell), y1 = int((b[1].y - g.lo.y) / g.cell),
		          z0 = int((b[0].z - g.lo.z) / g.cell), z1 = int((b[1].z - g.lo.z) / g.cell);
		const int all = (x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1);
		if (partFill[size_t(part)] >= std::max(1, all / 30))
			continue;
		for (int z = z0; z <= z1; ++z)
			for (int y = y0; y <= y1; ++y)
				for (int x = x0; x <= x1; ++x)
					if (x >= 0 && y >= 0 && z >= 0 && x < g.nx && y < g.ny && z < g.nz) {
						auto& l = g.label[g.At(x, y, z)];
						if (!l)
							l = uint16_t(part + 1);
					}
	}
	return g;
}
// 入れ物の中を埋める: 横の断面で外とつながらない空き (壁に囲まれた所) のうち、同じ縦の列の下に底がある所 (湯のみ・バケツの中)。
// 下に底の無い穴 (タイヤの穴) は埋めない
inline void FillContainers(Grid& g) {
	std::vector<uint8_t> outside(size_t(g.nx * g.nz));
	std::vector<int> stack;
	for (int y = 0; y < g.ny; ++y) {
		std::fill(outside.begin(), outside.end(), uint8_t(0));
		stack.clear();
		auto seed = [&](int x, int z) {
			const size_t i = size_t(z * g.nx + x);
			if (!g.label[g.At(x, y, z)] && !outside[i]) {
				outside[i] = 1;
				stack.push_back(int(i));
			}
		};
		for (int x = 0; x < g.nx; ++x) {
			seed(x, 0);
			seed(x, g.nz - 1);
		}
		for (int z = 0; z < g.nz; ++z) {
			seed(0, z);
			seed(g.nx - 1, z);
		}
		while (!stack.empty()) {
			const int at = stack.back();
			stack.pop_back();
			const int x = at % g.nx, z = at / g.nx;
			if (x > 0)
				seed(x - 1, z);
			if (x + 1 < g.nx)
				seed(x + 1, z);
			if (z > 0)
				seed(x, z - 1);
			if (z + 1 < g.nz)
				seed(x, z + 1);
		}
		for (int z = 0; z < g.nz; ++z)
			for (int x = 0; x < g.nx; ++x) {
				if (outside[size_t(z * g.nx + x)] || g.label[g.At(x, y, z)])
					continue;
				for (int below = y - 1; below >= 0; --below)
					if (const uint16_t l = g.label[g.At(x, below, z)]) {
						g.label[g.At(x, y, z)] = l;
						break;
					}
			}
	}
}
// 細すぎる骨組みを除く: 1 箱ぶん削って戻す。除いた結果が元の ItemOpenKeep より少なければ除かない (鉛筆・剣のような細い物)
inline void OpenThin(Grid& g) {
	// (削るのも戻すのも 3x3x3 の立方体で数える。四角い物の角や辺は削れずに残る)
	auto any = [&](const std::vector<uint16_t>& l, int x, int y, int z, bool all) {
		for (int dz = -1; dz <= 1; ++dz)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) {
					const int X = x + dx, Y = y + dy, Z = z + dz;
					const bool on =
					    X >= 0 && Y >= 0 && Z >= 0 && X < g.nx && Y < g.ny && Z < g.nz && l[g.At(X, Y, Z)] != 0;
					if (all && !on)
						return false;
					if (!all && on)
						return true;
				}
		return all;
	};
	std::vector<uint16_t> core(g.label.size(), 0);
	for (int z = 0; z < g.nz; ++z)
		for (int y = 0; y < g.ny; ++y)
			for (int x = 0; x < g.nx; ++x)
				if (g.label[g.At(x, y, z)] && any(g.label, x, y, z, true))
					core[g.At(x, y, z)] = g.label[g.At(x, y, z)];
	std::vector<uint16_t> opened(g.label.size(), 0);
	int before = 0, after = 0;
	for (int z = 0; z < g.nz; ++z)
		for (int y = 0; y < g.ny; ++y)
			for (int x = 0; x < g.nx; ++x) {
				const size_t i = g.At(x, y, z);
				before += g.label[i] != 0;
				if (g.label[i] && any(core, x, y, z, false)) {
					opened[i] = g.label[i];
					++after;
				}
			}
	if (float(after) >= ItemOpenKeep * float(before))
		g.label = std::move(opened);
}
// 箱の集まりの、表面までの距離 (箱の数。26 近傍の 2 パスのチャンファー)。seed の箱が 0
// (cube なら、斜めも 1 と数える (立方体の距離))
inline std::vector<float> Chamfer(int nx, int ny, int nz, const std::vector<uint8_t>& seed, bool cube = false) {
	std::vector<float> d(seed.size(), 1e9f);
	for (size_t i = 0; i < seed.size(); ++i)
		if (seed[i])
			d[i] = 0;
	auto at = [&](int x, int y, int z) { return size_t((z * ny + y) * nx + x); };
	const float steps[4] = {0, 1, cube ? 1.f : std::sqrt(2.f), cube ? 1.f : std::sqrt(3.f)};
	for (int dir : {1, -1}) {
		const int z0 = dir > 0 ? 0 : nz - 1, y0 = dir > 0 ? 0 : ny - 1, x0 = dir > 0 ? 0 : nx - 1;
		for (int z = z0; z >= 0 && z < nz; z += dir)
			for (int y = y0; y >= 0 && y < ny; y += dir)
				for (int x = x0; x >= 0 && x < nx; x += dir) {
					float& here = d[at(x, y, z)];
					for (int dz = -1; dz <= 0; ++dz)
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx) {
								if (dz * 9 + dy * 3 + dx >= 0)
									continue;
								const int X = x + dx * dir, Y = y + dy * dir, Z = z + dz * dir;
								if (X < 0 || Y < 0 || Z < 0 || X >= nx || Y >= ny || Z >= nz)
									continue;
								here =
								    std::min(here, d[at(X, Y, Z)] + steps[std::abs(dx) + std::abs(dy) + std::abs(dz)]);
							}
				}
	}
	return d;
}
// 物の形の距離の場を作る: まわりに空きを足し、外と中の距離から表面までの距離を作ってならす。太さは、中に入る一番大きな球の直径を
// その球の中に配り、外へも近くの物の太さを伝える。座標は物の範囲の真ん中が 0 (center に、物の座標での真ん中を返す)
inline SdfField BuildField(const Grid& g, Vec& center) {
	const int P = ItemFieldPad, nx = g.nx + 2 * P, ny = g.ny + 2 * P, nz = g.nz + 2 * P;
	auto at = [&](int x, int y, int z) { return size_t((z * ny + y) * nx + x); };
	std::vector<uint8_t> solid(size_t(nx * ny * nz), 0), empty(solid.size(), 0);
	Vec lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	for (int z = 0; z < g.nz; ++z)
		for (int y = 0; y < g.ny; ++y)
			for (int x = 0; x < g.nx; ++x)
				if (g.label[g.At(x, y, z)]) {
					solid[at(x + P, y + P, z + P)] = 1;
					const Vec c = g.Center(x, y, z);
					lo = {std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z)};
					hi = {std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z)};
				}
	for (size_t i = 0; i < solid.size(); ++i)
		empty[i] = !solid[i];
	const auto inside = Chamfer(nx, ny, nz, empty), outside = Chamfer(nx, ny, nz, solid);
	SdfField f;
	f.nx = nx;
	f.ny = ny;
	f.nz = nz;
	f.cell = g.cell;
	f.dist.assign(solid.size(), 0);
	for (size_t i = 0; i < solid.size(); ++i)
		f.dist[i] = (solid[i] ? -(inside[i] - .5f) : outside[i] - .5f) * g.cell;
	// 太さ: 立方体で k 回削って k 回戻したときに残る所は、太さ 2k - 1 箱以上 (立方体の距離)。四角い物の角は削れずに残るので太く、
	// 柄や棒だけが細くなる。戻すのは 1 方向ずつの範囲の最大 (重くならないように)
	f.thick.assign(solid.size(), 0);
	{
		const auto depth = Chamfer(nx, ny, nz, empty, true);
		int deepest = 0;
		for (size_t i = 0; i < depth.size(); ++i)
			if (solid[i])
				deepest = std::max(deepest, int(depth[i]));
		std::vector<uint8_t> mask(solid.size()), tmp(solid.size());
		for (int k = 1; k <= deepest; ++k) {
			for (size_t i = 0; i < solid.size(); ++i)
				mask[i] = solid[i] && depth[i] >= float(k);
			// 半径 k - 1 の立方体で広げる (x・y・z の順に、範囲の中に 1 つでもあれば 1)
			const int r = k - 1;
			for (int axis = 0; axis < 3 && r > 0; ++axis) {
				const int len = axis == 0 ? nx : axis == 1 ? ny : nz;
				for (int a2 = 0; a2 < (axis == 2 ? ny : nz); ++a2)
					for (int b2 = 0; b2 < (axis == 0 ? ny : nx); ++b2) {
						auto idx = [&](int t) {
							return axis == 0 ? at(t, b2, a2) : axis == 1 ? at(b2, t, a2) : at(b2, a2, t);
						};
						int count = 0;
						for (int t = 0; t < std::min(r, len); ++t)
							count += mask[idx(t)];
						for (int t = 0; t < len; ++t) {
							if (t + r < len)
								count += mask[idx(t + r)];
							if (t - r - 1 >= 0)
								count -= mask[idx(t - r - 1)];
							tmp[idx(t)] = count > 0;
						}
					}
				std::swap(mask, tmp);
			}
			for (size_t i = 0; i < solid.size(); ++i)
				if (mask[i] && solid[i])
					f.thick[i] = float(2 * k - 1) * g.cell;
		}
	}
	// 外へも、近くの物の太さを伝える (細い所のまわりで、太らせる量が決まるように)
	for (int pass = 0; pass < ItemFieldSpread; ++pass) {
		std::vector<float> next = f.thick;
		for (int z = 0; z < nz; ++z)
			for (int y = 0; y < ny; ++y)
				for (int x = 0; x < nx; ++x) {
					if (solid[at(x, y, z)])
						continue;
					float best = next[at(x, y, z)];
					for (int dz = -1; dz <= 1; ++dz)
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx) {
								const int X = x + dx, Y = y + dy, Z = z + dz;
								if (X >= 0 && Y >= 0 && Z >= 0 && X < nx && Y < ny && Z < nz)
									best = std::max(best, f.thick[at(X, Y, Z)]);
							}
					next[at(x, y, z)] = best;
				}
		f.thick = std::move(next);
	}
	// ならす (角が丸くなる。スライムの表面のように)。ただし太さに余裕のある所 (ItemFieldSmoothThick 箱以上) だけ。
	// 細い棒 (鉛筆・電柱) までならすと、消えてしまうため
	for (int pass = 0; pass < ItemFieldSmooth; ++pass) {
		std::vector<float> next(f.dist.size());
		for (int z = 0; z < nz; ++z)
			for (int y = 0; y < ny; ++y)
				for (int x = 0; x < nx; ++x) {
					float sum = 0;
					int n = 0;
					for (int dz = -1; dz <= 1; ++dz)
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx) {
								const int X = x + dx, Y = y + dy, Z = z + dz;
								if (X < 0 || Y < 0 || Z < 0 || X >= nx || Y >= ny || Z >= nz)
									continue;
								sum += f.dist[at(X, Y, Z)];
								++n;
							}
					next[at(x, y, z)] =
					    f.thick[at(x, y, z)] >= ItemFieldSmoothThick * g.cell ? sum / float(n) : f.dist[at(x, y, z)];
				}
		f.dist = std::move(next);
	}
	f.maxThick = std::max(g.cell, *std::max_element(f.thick.begin(), f.thick.end()));
	center = (lo + hi) * .5f;
	f.half = (hi - lo) * .5f + Vec{g.cell, g.cell, g.cell} * .5f;
	f.lo = g.lo + Vec{g.cell, g.cell, g.cell} * .5f - Vec{float(P), float(P), float(P)} * g.cell - center;
	return f;
}
// 面影: 物の元の形 (箱) と、部品の並び (太さの下限 minHalf をかけたあと) を、正面・横・上の影絵で比べた重なりの割合の平均
inline float Resemblance(const Grid& g, std::vector<SdfPart> parts, float minHalf) {
	for (auto& s : parts)
		SdfApplyFloor(s, minHalf);
	const int P = 4, nx = g.nx + 2 * P, ny = g.ny + 2 * P, nz = g.nz + 2 * P;
	std::vector<uint8_t> orig(size_t(nx * ny * nz), 0), made(orig.size(), 0);
	auto at = [&](int x, int y, int z) { return size_t((z * ny + y) * nx + x); };
	for (int z = 0; z < nz; ++z)
		for (int y = 0; y < ny; ++y)
			for (int x = 0; x < nx; ++x) {
				const int gx = x - P, gy = y - P, gz = z - P;
				if (gx >= 0 && gy >= 0 && gz >= 0 && gx < g.nx && gy < g.ny && gz < g.nz)
					orig[at(x, y, z)] = g.label[g.At(gx, gy, gz)] != 0;
				made[at(x, y, z)] = SdfShapeDistance(parts, g.Center(gx, gy, gz), 0) < 0;
			}
	float total = 0;
	for (int axis = 0; axis < 3; ++axis) {
		// (0: 正面 (z を潰す)・1: 上 (y を潰す)・2: 横 (x を潰す))
		const int w = axis == 2 ? nz : nx, h = axis == 1 ? nz : ny, deep = axis == 0 ? nz : axis == 1 ? ny : nx;
		int both = 0, either = 0;
		for (int t = 0; t < h; ++t)
			for (int u = 0; u < w; ++u) {
				bool a = false, b = false;
				for (int k = 0; k < deep; ++k) {
					const int x = axis == 2 ? k : u, y = axis == 1 ? k : t, z = axis == 0 ? k : axis == 1 ? t : u;
					a = a || orig[at(x, y, z)];
					b = b || made[at(x, y, z)];
				}
				both += a && b;
				either += a || b;
			}
		total += either ? float(both) / float(either) : 0.f;
	}
	return total / 3;
}
// 点の集まりに、向きを持った基本形を 1 つ合わせる (中心・主軸・広がり)。詰まり具合で箱・カプセル・板・だ円体を選ぶ
inline SdfPart FitOriented(const std::vector<Vec>& pts, float cell) {
	SdfPart s;
	if (pts.empty())
		return s;
	Vec mean{};
	for (const auto& p : pts)
		mean = mean + p;
	mean = mean * (1 / float(pts.size()));
	std::array<std::array<float, 3>, 3> cov{};
	for (const auto& p : pts) {
		const Vec d = p - mean;
		const float c[3] = {d.x, d.y, d.z};
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				cov[size_t(i)][size_t(j)] += c[i] * c[j];
	}
	std::array<float, 3> values{};
	std::array<Vec, 3> v{};
	SdfEigen(cov, values, v);
	std::array<float, 3> ext{};
	for (int k = 0; k < 3; ++k) {
		std::vector<float> along;
		along.reserve(pts.size());
		for (const auto& p : pts) {
			const Vec d = p - mean;
			along.push_back(std::abs(d.x * v[size_t(k)].x + d.y * v[size_t(k)].y + d.z * v[size_t(k)].z));
		}
		const size_t at = std::min(along.size() - 1, along.size() * 97 / 100);
		std::nth_element(along.begin(), along.begin() + std::ptrdiff_t(at), along.end());
		ext[size_t(k)] = std::max(cell * .5f, along[at] + cell * .5f);
	}
	const float a = ext[0], b = ext[1], c = ext[2];
	// 向き: 前 (w) = 一番長い向き・右 (u) = 2 番目・上 (v) = w x u
	const Vec w = v[0], u = v[1];
	const Vec up{w.y * u.z - w.z * u.y, w.z * u.x - w.x * u.z, w.x * u.y - w.y * u.x};
	s.axes = {u, up, w};
	s.center = mean;
	const float fill = float(pts.size()) * cell * cell * cell / std::max(1e-6f, 8 * a * b * c);
	if (fill > .78f) {
		s.kind = SdfKind::RoundBox;
		s.half = {b, c, a};
		s.round = .2f * c;
	} else if (a > 2.2f * b) {
		s.kind = SdfKind::Capsule;
		s.half = {b, c, a};
	} else if (c < .42f * a) {
		s.kind = SdfKind::RoundBox;
		s.half = {b, c, a};
		s.round = .6f * c;
	} else {
		s.kind = SdfKind::Ellipsoid;
		s.half = {b, c, a};
	}
	return s;
}
// 基本形の並びが、箱の集まりにどれだけ重なるか (共通部分 / 合わせた部分)
inline float Fit(const Grid& g, const std::vector<SdfPart>& parts) {
	int both = 0, either = 0;
	for (int z = 0; z < g.nz; ++z)
		for (int y = 0; y < g.ny; ++y)
			for (int x = 0; x < g.nx; ++x) {
				const bool in = g.label[g.At(x, y, z)] != 0;
				const bool shape = SdfShapeDistance(parts, g.Center(x, y, z), 0) < 0;
				both += in && shape;
				either += in || shape;
			}
	return either ? float(both) / float(either) : 0.f;
}
// 点の集まりを主軸の向きに 2 つに割る (重心より前と後ろ。2 回の平均の寄せ直し)
inline std::array<std::vector<Vec>, 2> Split(const std::vector<Vec>& pts) {
	Vec mean{};
	for (const auto& p : pts)
		mean = mean + p;
	mean = mean * (1 / float(std::max<size_t>(1, pts.size())));
	std::array<std::array<float, 3>, 3> cov{};
	for (const auto& p : pts) {
		const Vec d = p - mean;
		const float c[3] = {d.x, d.y, d.z};
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				cov[size_t(i)][size_t(j)] += c[i] * c[j];
	}
	std::array<float, 3> values{};
	std::array<Vec, 3> v{};
	SdfEigen(cov, values, v);
	const Vec axis = v[0];
	float split = 0;
	std::array<std::vector<Vec>, 2> out;
	for (int pass = 0; pass < 3; ++pass) {
		out[0].clear();
		out[1].clear();
		for (const auto& p : pts) {
			const Vec d = p - mean;
			(d.x * axis.x + d.y * axis.y + d.z * axis.z < split ? out[0] : out[1]).push_back(p);
		}
		if (out[0].empty() || out[1].empty())
			break;
		auto meanOf = [&](const std::vector<Vec>& s) {
			float t = 0;
			for (const auto& p : s) {
				const Vec d = p - mean;
				t += d.x * axis.x + d.y * axis.y + d.z * axis.z;
			}
			return t / float(s.size());
		};
		split = (meanOf(out[0]) + meanOf(out[1])) * .5f;
	}
	return out;
}
} // namespace ItemProfileDetail

// 手直し (item_profiles.json の overrides): 形の種類 (名前。空なら変えない) と特徴 (書いた物だけにする。hasFeatures が false なら変えない)
inline void ApplyItemOverride(
    ItemProfile& prof, const std::string& shape, const std::vector<std::string>& features, bool hasFeatures) {
	for (int c = 0; c < int(ItemShapeClass::Count); ++c)
		if (shape == ItemShapeClassName(ItemShapeClass(c)))
			prof.shape = ItemShapeClass(c);
	if (!hasFeatures)
		return;
	prof.features = 0;
	for (const auto& f : features)
		prof.features |= f == "穴" ? ItemHasHole : f == "持ち手" ? ItemHasHandle : f == "立つ" ? ItemUpright : 0u;
}
// 箱の三角形 (中心 center・半分の大きさ half) を足す。モデルの無い物の代わり (トランプは平たいカード) と、確かめのテストに使う
inline void ItemBoxMesh(std::vector<Vec>& pts, std::vector<uint32_t>& idx, Vec center, Vec half) {
	const uint32_t base = uint32_t(pts.size());
	for (int i = 0; i < 8; ++i)
		pts.push_back(center + Vec{i & 1 ? half.x : -half.x, i & 2 ? half.y : -half.y, i & 4 ? half.z : -half.z});
	static constexpr uint32_t faces[36] = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
	                                       2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
	for (uint32_t f : faces)
		idx.push_back(base + f);
}
// 物のモデル (頂点と三角形) からプロフィールの形の部分を作る (意味のタグは別に入れる)
// (gridOut に、そろえた大きさの箱の並びを返す。確かめの絵に使う)
inline ItemProfile BuildItemProfile(
    const std::string& id, const std::vector<Vec>& pts, const std::vector<uint32_t>& idx,
    ItemProfileDetail::Grid* gridOut = nullptr) {
	using namespace ItemProfileDetail;
	ItemProfile prof;
	prof.id = id;
	if (pts.empty() || idx.size() < 3)
		return prof;
	// 大きさをそろえる: 一番長い辺を 2、左右前後の真ん中を 0、一番下を 0
	Vec lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	for (const auto& p : pts) {
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	}
	const float k = 2 / std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-4f});
	const Vec mid{(lo.x + hi.x) * .5f, lo.y, (lo.z + hi.z) * .5f};
	std::vector<Vec> norm;
	norm.reserve(pts.size());
	for (const auto& p : pts)
		norm.push_back((p - mid) * k);
	prof.size = (hi - lo) * (k * .5f);
	prof.realSize = SdfLength(hi - lo);
	int partCount = 0;
	Grid g = Voxelize(norm, idx, ItemProfileCells, partCount);
	// スライムで作れない所を整える: 入れ物の中は埋め (湯のみ・かご・バケツ)、細すぎる骨組みは除く (ベンチの脚など。除くと半分以上消える物はそのまま)
	FillContainers(g);
	const std::vector<uint16_t> full = g.label;
	// 物の形そのものの距離の場 (細い骨組みを除く前の形から)。見た目の部品 look は、この場を物の座標の真ん中に置いたもの
	{
		Vec center{};
		SdfFields().push_back(BuildField(g, center));
		prof.field = int(SdfFields().size()) - 1;
		prof.look.kind = SdfKind::Item;
		prof.look.field = prof.field;
		prof.look.center = center;
		prof.look.half = SdfFields().back().half;
	}
	OpenThin(g);
	// つながった部分ごとの箱
	std::vector<std::vector<Vec>> groups(static_cast<size_t>(partCount));
	int total = 0;
	for (int z = 0; z < g.nz; ++z)
		for (int y = 0; y < g.ny; ++y)
			for (int x = 0; x < g.nx; ++x) {
				const int l = g.label[g.At(x, y, z)];
				if (l) {
					groups[size_t(l - 1)].push_back(g.Center(x, y, z));
					++total;
				}
			}
	groups.erase(
	    std::remove_if(groups.begin(), groups.end(), [](const std::vector<Vec>& s) { return s.empty(); }),
	    groups.end());
	auto centroid = [](const std::vector<Vec>& s) {
		Vec m{};
		for (const auto& p : s)
			m = m + p;
		return m * (1 / float(std::max<size_t>(1, s.size())));
	};
	// 小さな部分と、多すぎる部分は、近くの大きな部分にまとめる
	auto mergeSmallest = [&](bool onlySmall) {
		std::sort(groups.begin(), groups.end(), [](const std::vector<Vec>& a, const std::vector<Vec>& b) {
			return a.size() > b.size();
		});
		if (groups.size() < 2)
			return false;
		auto& smallest = groups.back();
		if (onlySmall && float(smallest.size()) >= ItemPartMinShare * float(total))
			return false;
		const Vec c = centroid(smallest);
		size_t best = 0;
		float bestD = 1e9f;
		for (size_t i = 0; i + 1 < groups.size(); ++i) {
			const float d = SdfLength(centroid(groups[i]) - c);
			if (d < bestD) {
				bestD = d;
				best = i;
			}
		}
		groups[best].insert(groups[best].end(), smallest.begin(), smallest.end());
		groups.pop_back();
		return true;
	};
	while (mergeSmallest(true)) {
	}
	while (int(groups.size()) > ItemPartMax)
		mergeSmallest(false);
	// 部分ごとに基本形を合わせ、合いが悪ければ一番大きな部分を割る
	auto fitAll = [&] {
		std::vector<SdfPart> parts;
		for (size_t i = 0; i < groups.size(); ++i) {
			SdfPart s = FitOriented(groups[i], g.cell);
			s.part = int(i) + 1;
			parts.push_back(s);
		}
		return parts;
	};
	prof.parts = fitAll();
	prof.fit = Fit(g, prof.parts);
	while (prof.fit < ItemSplitFit && int(groups.size()) < ItemPartMax) {
		std::sort(groups.begin(), groups.end(), [](const std::vector<Vec>& a, const std::vector<Vec>& b) {
			return a.size() > b.size();
		});
		auto halves = Split(groups[0]);
		if (halves[0].size() < 4 || halves[1].size() < 4)
			break;
		auto trial = groups;
		trial[0] = halves[0];
		trial.push_back(halves[1]);
		std::swap(groups, trial);
		const auto parts = fitAll();
		const float fit = Fit(g, parts);
		if (fit < prof.fit + ItemSplitGain) {
			std::swap(groups, trial);
			break;
		}
		prof.parts = parts;
		prof.fit = fit;
	}
	// 除いた細い部分のうち長いもの (傘の柄・ほうきの柄・電柱) は、細長いパーツとして戻す (物らしさを決める所なので)。
	// 短いもの (ベンチの脚・飾り) は戻さない。パーツの数の上限まで、長い順に
	{
		std::vector<uint8_t> thin(full.size(), 0);
		for (size_t i = 0; i < full.size(); ++i)
			thin[i] = full[i] && !g.label[i];
		std::vector<std::vector<Vec>> pieces;
		std::vector<int> stack;
		for (int z = 0; z < g.nz; ++z)
			for (int y = 0; y < g.ny; ++y)
				for (int x = 0; x < g.nx; ++x) {
					if (!thin[g.At(x, y, z)])
						continue;
					std::vector<Vec> piece;
					thin[g.At(x, y, z)] = 0;
					stack.assign(1, int(g.At(x, y, z)));
					while (!stack.empty()) {
						const int at = stack.back();
						stack.pop_back();
						const int cx = at % g.nx, cy = (at / g.nx) % g.ny, cz = at / (g.nx * g.ny);
						piece.push_back(g.Center(cx, cy, cz));
						for (int dz = -1; dz <= 1; ++dz)
							for (int dy = -1; dy <= 1; ++dy)
								for (int dx = -1; dx <= 1; ++dx) {
									const int X = cx + dx, Y = cy + dy, Z = cz + dz;
									if (X < 0 || Y < 0 || Z < 0 || X >= g.nx || Y >= g.ny || Z >= g.nz)
										continue;
									const size_t i = g.At(X, Y, Z);
									if (thin[i]) {
										thin[i] = 0;
										stack.push_back(int(i));
									}
								}
					}
					pieces.push_back(std::move(piece));
				}
		std::vector<SdfPart> rods;
		for (const auto& piece : pieces) {
			if (piece.size() < 4)
				continue;
			SdfPart rod = FitOriented(piece, g.cell);
			// (本当に棒の形のものだけ: 長くて、長さが太さの 2.5 倍以上。球の表面から出る薄い殻は戻さない)
			if (rod.half.z < ItemRodLength || rod.half.z < 2.5f * std::max(rod.half.x, rod.half.y))
				continue;
			// (体の表面に沿った薄い殻ではなく、体の外へ出ている棒だけ: 両端と真ん中のうち 2 つ以上が、ほかのパーツの外にある)
			int outside = 0;
			for (float t : {-.8f, 0.f, .8f})
				outside += SdfShapeDistance(prof.parts, rod.center + rod.axes[2] * (rod.half.z * t), 0) > g.cell * 1.5f;
			if (outside < 2)
				continue;
			rod.kind = SdfKind::Capsule;
			rods.push_back(rod);
		}
		std::sort(rods.begin(), rods.end(), [](const SdfPart& a, const SdfPart& b) { return a.half.z > b.half.z; });
		for (auto& rod : rods) {
			if (int(prof.parts.size()) >= ItemPartMax)
				break;
			rod.part = int(prof.parts.size()) + 1;
			prof.parts.push_back(rod);
		}
		// (重なりの割合と絵は、元の形 (細い部分も入れた形) で数える)
		g.label = full;
		if (!rods.empty())
			prof.fit = Fit(g, prof.parts);
	}
	// 形の種類と特徴 (物全体の主軸と詰まり具合・穴・持ち手・立っているか)
	std::vector<Vec> all;
	for (const auto& s : groups)
		all.insert(all.end(), s.begin(), s.end());
	const SdfPart whole = FitOriented(all, g.cell);
	const float a = whole.half.z, b = whole.half.x, c = whole.half.y;
	const float fill = float(all.size()) * g.cell * g.cell * g.cell / std::max(1e-6f, 8 * a * b * c);
	// 穴: 一番短い向きから見た影絵に、外とつながらない空きがある
	bool hole = false;
	{
		const Vec n = whole.axes[1];
		const int axis = std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z) ? 0
		                 : std::abs(n.y) >= std::abs(n.z)                                 ? 1
		                                                                                  : 2;
		const int w = axis == 0 ? g.nz : g.nx, h = axis == 1 ? g.nz : g.ny;
		std::vector<uint8_t> img(size_t(w * h), 0);
		for (int z = 0; z < g.nz; ++z)
			for (int y = 0; y < g.ny; ++y)
				for (int x = 0; x < g.nx; ++x)
					if (g.label[g.At(x, y, z)]) {
						const int u = axis == 0 ? z : x, t = axis == 1 ? z : y;
						img[size_t(t * w + u)] = 1;
					}
		std::vector<uint8_t> outside(img.size(), 0);
		std::vector<int> stack;
		for (int u = 0; u < w; ++u)
			for (int t : {0, h - 1})
				if (!img[size_t(t * w + u)]) {
					stack.push_back(t * w + u);
					outside[size_t(t * w + u)] = 1;
				}
		for (int t = 0; t < h; ++t)
			for (int u : {0, w - 1})
				if (!img[size_t(t * w + u)] && !outside[size_t(t * w + u)]) {
					stack.push_back(t * w + u);
					outside[size_t(t * w + u)] = 1;
				}
		while (!stack.empty()) {
			const int at = stack.back();
			stack.pop_back();
			const int u = at % w, t = at / w;
			const int nb[4][2] = {{u - 1, t}, {u + 1, t}, {u, t - 1}, {u, t + 1}};
			for (const auto& q : nb) {
				if (q[0] < 0 || q[1] < 0 || q[0] >= w || q[1] >= h)
					continue;
				const size_t i = size_t(q[1] * w + q[0]);
				if (!img[i] && !outside[i]) {
					outside[i] = 1;
					stack.push_back(int(i));
				}
			}
		}
		// (輪は、真ん中に大きな穴がある物だけ。脚や骨組みのすき間の小さな穴は数えない)
		int area = 0, enclosed = 0;
		float au = 0, at = 0, eu = 0, et = 0;
		for (int t = 0; t < h; ++t)
			for (int u = 0; u < w; ++u) {
				const size_t i = size_t(t * w + u);
				if (img[i]) {
					++area;
					au += float(u);
					at += float(t);
				} else if (!outside[i]) {
					++enclosed;
					eu += float(u);
					et += float(t);
				}
			}
		if (area > 0 && float(enclosed) > ItemRingHole * float(area)) {
			const float du = eu / float(enclosed) - au / float(area), dt = et / float(enclosed) - at / float(area);
			hole = std::sqrt(du * du + dt * dt) < ItemRingCenter * float(std::max(w, h));
		}
	}
	if (hole)
		prof.features |= ItemHasHole;
	if (std::abs(whole.axes[2].y) > .8f && a > 1.3f * b)
		prof.features |= ItemUpright;
	// 持ち手: 断面の 2 つの向きがどちらも細い (長さの 1 / 2.5 より細い)、物の大きさに比べて長い (半分の長さが ItemHandleLength 以上)、
	// 一番大きなパーツに比べて小さいパーツ (平たい板・車のバンパーのような短い飾りは持ち手にしない)
	{
		float biggest = 0;
		for (const auto& s : prof.parts)
			biggest = std::max(biggest, s.half.x * s.half.y * s.half.z);
		for (const auto& s : prof.parts) {
			const float thick = std::max(s.half.x, s.half.y), volume = s.half.x * s.half.y * s.half.z;
			if (prof.parts.size() > 1 && s.kind == SdfKind::Capsule && s.half.z > 2.5f * thick &&
			    s.half.z > ItemHandleLength && volume < ItemHandleShare * biggest)
				prof.features |= ItemHasHandle;
		}
	}
	prof.shape = hole           ? ItemShapeClass::Ring
	             : a > 2.2f * b ? ItemShapeClass::Long
	             : c < .42f * a ? ItemShapeClass::Flat
	             : fill > .75f  ? ItemShapeClass::Box
	                            : ItemShapeClass::Round;
	if (gridOut)
		*gridOut = g;
	return prof;
}
} // namespace Game::Chrono
