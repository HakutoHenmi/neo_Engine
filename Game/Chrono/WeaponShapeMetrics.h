// WeaponShapeMetrics.h
// 変形武器の形の見やすさを測る (F1。エンジンに依存しない)。形は細かい箱 (ボクセル) の並びで受け取る。
//  - 細すぎ: 太さが minThick より細い所の体積の割合 (液体の表面でならされて消える部分)。
//    半径 minThick/2 の球で削ってから戻す (モルフォロジーのオープニング) と消える部分を数える
//  - 塊の数: 半径 massRadius の球で削って戻したあとに残る、つながった塊の数 (遠くから見て読める大きなかたまり)。小さすぎる塊は数えない
//  - 離れた部品: 元の形の中で、つながっていない塊の数 (1 が正しい。2 以上は宙に浮いた部品)
//  - 部品の割合: 箱ごとにどの部品 (本体・材料・材料) が作ったかを持っていれば、部品ごとの体積の割合。小さすぎる部品は見えない
//  - 影絵: 横・正面・上から見た影絵の、囲む四角に対する面積の割合と、輪郭の込み具合 (周の長さの 2 乗 / (4π 面積)。円で 1) と、
//    凸包 (影絵を輪ゴムで囲んだ形) に対する面積の割合 (へこみの少なさ。1 に近いほど、どこから見ても 1 つの塊)。
//    「塊が 1 つだけ」は、塊が 1 つで、しかもどの向きの影絵にもへこみが少ないとき (太くつながった部品でも、影絵のへこみで読めれば良い)
//  - 判定: 上の数を基準と比べて、問題ごとに印を付ける (WeaponShapeIssue)。点数は 100 点から問題ごとに引く
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace Game::Chrono {
// 基準 (形の座標の長さ。体の形は高さ 6 くらい)
struct WeaponShapeLimits {
	float minThick = .8f;         // これより細い所は消える (呼ぶ側が流体の升と粒の間隔から決める)
	float massRadius = .9f;       // 塊を数えるときに削る半径
	float massShare = .04f;       // 全体のこれより小さい塊は数えない
	float thinMax = .15f;         // 細すぎの割合の上限
	int massMin = 2, massMax = 5; // 読める塊の数の範囲
	float partMin = .1f;          // 部品の体積の割合の下限 (材料の部品がこれより小さいと見えない)
	float baseMax = .85f;         // 本体の割合の上限 (これより大きいと、ほかの材料がほとんど見えない)
	float pillarWidth = .45f;     // 柱: 端の部分の幅が、一番広い所の幅のこの割合より狭く
	float pillarHeight = .25f;    //      その細い部分が、その向きの長さのこの割合より長く、
	float pillarAspect = 1.6f;    //      しかも細い部分の長さが、その幅のこの倍より長いと「柱」
	float solidMax = .88f;        // 影絵の凸包に対する割合の上限 (どの向きもこれより大きいと、ただの塊)
};
// (Pillar は、影絵の上に細い柱が長く突き出している形。形として良くないので、必ず直す (大きく減点))
enum class WeaponShapeIssue : uint8_t { Thin, Blob, Busy, Floating, TinyPart, BaseOnly, Pillar, Count };
inline const char* WeaponShapeIssueName(WeaponShapeIssue i) {
	static constexpr const char* names[] = {"細すぎ",         "塊が1つだけ", "塊が多すぎ",  "離れた部品",
	                                        "小さすぎる部品", "本体ばかり",  "柱の突き出し"};
	return names[std::min(int(i), int(WeaponShapeIssue::Count) - 1)];
}
struct WeaponShapeReport {
	int voxels = 0, masses = 0, pieces = 0;
	float thinShare = 0;
	std::array<float, 3> partShare{};
	std::array<float, 3> fill{}, outline{}, solid{};
	float pillar = 0;
	int pillarFrom = -1; // (pillarFrom: どの影絵のどの端か。影絵 x 4 + 端 (0 上・1 下・2 後ろ・3 前))
	std::array<bool, size_t(WeaponShapeIssue::Count)> issues{};
	int score = 100;
	bool Has(WeaponShapeIssue i) const {
		return issues[size_t(i)];
	}
};
// 箱の並び: nx ny nz・箱の大きさ cell・埋まっているか (並びは (z*ny+y)*nx+x)・どの部品か (0 は無し、1〜3。空でもよい)
struct WeaponShapeVoxels {
	int nx = 0, ny = 0, nz = 0;
	float cell = 1;
	const std::vector<uint8_t>* fill = nullptr;
	const std::vector<uint8_t>* part = nullptr;
};

namespace ShapeMetricsDetail {
inline size_t Index(const WeaponShapeVoxels& v, int x, int y, int z) {
	return size_t((z * v.ny + y) * v.nx + x);
}
// 距離 (箱の数): seed の箱からの近似のユークリッド距離 (26 近傍の 2 パスのチャンファー)。seed でない箱へ広げる
inline std::vector<float> Distance(const WeaponShapeVoxels& v, const std::vector<uint8_t>& seed) {
	const float big = 1e9f;
	std::vector<float> d(seed.size(), big);
	for (size_t i = 0; i < seed.size(); ++i)
		if (seed[i])
			d[i] = 0;
	const float a = 1, b = std::sqrt(2.f), c = std::sqrt(3.f);
	auto pass = [&](int dir) {
		const int z0 = dir > 0 ? 0 : v.nz - 1, z1 = dir > 0 ? v.nz : -1, y0 = dir > 0 ? 0 : v.ny - 1,
		          y1 = dir > 0 ? v.ny : -1, x0 = dir > 0 ? 0 : v.nx - 1, x1 = dir > 0 ? v.nx : -1;
		for (int z = z0; z != z1; z += dir)
			for (int y = y0; y != y1; y += dir)
				for (int x = x0; x != x1; x += dir) {
					float& here = d[Index(v, x, y, z)];
					if (here == 0)
						continue;
					for (int dz = -1; dz <= 0; ++dz)
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx) {
								// 前にたどった側 (dir の向きで先に来る箱) だけを見る
								const int key = dz * 9 + dy * 3 + dx;
								if (key >= 0)
									continue;
								const int X = x + dx * dir, Y = y + dy * dir, Z = z + dz * dir;
								if (X < 0 || Y < 0 || Z < 0 || X >= v.nx || Y >= v.ny || Z >= v.nz)
									continue;
								const int n = std::abs(dx) + std::abs(dy) + std::abs(dz);
								const float step = n == 1 ? a : n == 2 ? b : c;
								here = std::min(here, d[Index(v, X, Y, Z)] + step);
							}
				}
	};
	pass(1);
	pass(-1);
	return d;
}
// 半径 r (箱の数) のオープニング: 中から r より深い箱を残し、そこから r 以内に戻す (元の形の中だけ)
inline std::vector<uint8_t> Open(const WeaponShapeVoxels& v, float r) {
	const auto& fill = *v.fill;
	std::vector<uint8_t> empty(fill.size());
	for (size_t i = 0; i < fill.size(); ++i)
		empty[i] = fill[i] ? 0 : 1;
	// (並びの外は空とみなすので、外枠の箱も「空からの距離 1」で数える)
	auto depth = Distance(v, empty);
	for (int z = 0; z < v.nz; ++z)
		for (int y = 0; y < v.ny; ++y)
			for (int x = 0; x < v.nx; ++x)
				if (x == 0 || y == 0 || z == 0 || x == v.nx - 1 || y == v.ny - 1 || z == v.nz - 1) {
					float& d = depth[Index(v, x, y, z)];
					if (fill[Index(v, x, y, z)])
						d = std::min(d, 1.f);
				}
	std::vector<uint8_t> core(fill.size());
	for (size_t i = 0; i < fill.size(); ++i)
		core[i] = fill[i] && depth[i] > r ? 1 : 0;
	auto back = Distance(v, core);
	std::vector<uint8_t> out(fill.size());
	for (size_t i = 0; i < fill.size(); ++i)
		out[i] = fill[i] && back[i] <= r + .5f ? 1 : 0;
	return out;
}
// つながった塊 (6 近傍か 26 近傍) の大きさの一覧
inline std::vector<int> Components(const WeaponShapeVoxels& v, const std::vector<uint8_t>& set, bool corners) {
	std::vector<int> sizes;
	std::vector<uint8_t> seen(set.size(), 0);
	std::vector<int> stack;
	for (int z = 0; z < v.nz; ++z)
		for (int y = 0; y < v.ny; ++y)
			for (int x = 0; x < v.nx; ++x) {
				const size_t start = Index(v, x, y, z);
				if (!set[start] || seen[start])
					continue;
				int size = 0;
				seen[start] = 1;
				stack.assign(1, int(start));
				while (!stack.empty()) {
					const int at = stack.back();
					stack.pop_back();
					++size;
					const int cx = at % v.nx, cy = (at / v.nx) % v.ny, cz = at / (v.nx * v.ny);
					for (int dz = -1; dz <= 1; ++dz)
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx) {
								const int n = std::abs(dx) + std::abs(dy) + std::abs(dz);
								if (n == 0 || (!corners && n > 1))
									continue;
								const int X = cx + dx, Y = cy + dy, Z = cz + dz;
								if (X < 0 || Y < 0 || Z < 0 || X >= v.nx || Y >= v.ny || Z >= v.nz)
									continue;
								const size_t i = Index(v, X, Y, Z);
								if (set[i] && !seen[i]) {
									seen[i] = 1;
									stack.push_back(int(i));
								}
							}
				}
				sizes.push_back(size);
			}
	return sizes;
}
// axis 0: 横 (z, y)、1: 正面 (x, y)、2: 上 (x, z)。輪郭と柱の評価で同じ影絵を使う。
struct SilhouetteImage {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> pixels;

	bool Filled(int column, int row) const {
		return column >= 0 && row >= 0 && column < width && row < height && pixels[size_t(row * width + column)] != 0;
	}
};

inline SilhouetteImage ProjectSilhouette(const WeaponShapeVoxels& voxels, int axis) {
	SilhouetteImage image;
	image.width = axis == 0 ? voxels.nz : voxels.nx;
	image.height = axis == 2 ? voxels.nz : voxels.ny;
	image.pixels.assign(size_t(image.width * image.height), 0);
	for (int z = 0; z < voxels.nz; ++z) {
		for (int y = 0; y < voxels.ny; ++y) {
			for (int x = 0; x < voxels.nx; ++x) {
				if (!(*voxels.fill)[Index(voxels, x, y, z)]) {
					continue;
				}
				const int column = axis == 0 ? z : x;
				const int row = axis == 2 ? z : y;
				image.pixels[size_t(row * image.width + column)] = 1;
			}
		}
	}
	return image;
}

// 埋まった升の四隅を囲む凸包の面積 (単調連鎖法)。
inline float SilhouetteHullArea(const SilhouetteImage& image) {
	std::vector<std::array<int, 2>> points;
	for (int row = 0; row < image.height; ++row) {
		for (int column = 0; column < image.width; ++column) {
			if (!image.Filled(column, row)) {
				continue;
			}
			points.push_back({column, row});
			points.push_back({column, row + 1});
			points.push_back({column + 1, row});
			points.push_back({column + 1, row + 1});
		}
	}
	std::sort(points.begin(), points.end());
	points.erase(std::unique(points.begin(), points.end()), points.end());
	if (points.empty()) {
		return 0;
	}
	auto cross = [](std::array<int, 2> origin, std::array<int, 2> a, std::array<int, 2> b) {
		return static_cast<long long>(a[0] - origin[0]) * (b[1] - origin[1]) -
		       static_cast<long long>(a[1] - origin[1]) * (b[0] - origin[0]);
	};
	std::vector<std::array<int, 2>> hull(points.size() * 2);
	size_t count = 0;
	for (const auto& point : points) {
		while (count >= 2 && cross(hull[count - 2], hull[count - 1], point) <= 0) {
			--count;
		}
		hull[count++] = point;
	}
	for (size_t i = points.size() - 1, lower = count + 1; i-- > 0;) {
		while (count >= lower && cross(hull[count - 2], hull[count - 1], points[i]) <= 0) {
			--count;
		}
		hull[count++] = points[i];
	}
	hull.resize(count > 0 ? count - 1 : 0);
	long long twiceArea = 0;
	for (size_t i = 0; i < hull.size(); ++i) {
		const auto& a = hull[i];
		const auto& b = hull[(i + 1) % hull.size()];
		twiceArea += static_cast<long long>(a[0]) * b[1] - static_cast<long long>(b[0]) * a[1];
	}
	return float(std::llabs(twiceArea)) * .5f;
}

inline void Silhouette(const SilhouetteImage& image, float& fill, float& outline, float& solid) {
	int area = 0, perimeter = 0;
	int firstColumn = image.width, lastColumn = -1;
	int firstRow = image.height, lastRow = -1;
	for (int row = 0; row < image.height; ++row) {
		for (int column = 0; column < image.width; ++column) {
			if (!image.Filled(column, row)) {
				continue;
			}
			++area;
			firstColumn = std::min(firstColumn, column);
			lastColumn = std::max(lastColumn, column);
			firstRow = std::min(firstRow, row);
			lastRow = std::max(lastRow, row);
			perimeter += !image.Filled(column - 1, row) + !image.Filled(column + 1, row) +
			             !image.Filled(column, row - 1) + !image.Filled(column, row + 1);
		}
	}
	if (area == 0) {
		fill = outline = solid = 0;
		return;
	}
	fill = float(area) / float((lastColumn - firstColumn + 1) * (lastRow - firstRow + 1));
	outline = float(perimeter) * float(perimeter) / (4 * 3.1415927f * float(area));
	const float hullArea = SilhouetteHullArea(image);
	solid = hullArea != 0 ? std::min(1.f, float(area) / hullArea) : 1.f;
}

// transpose が false のときは縦の段、true のときは横の段ごとの幅を返す。
inline std::vector<int> SilhouetteRowWidths(const SilhouetteImage& image, bool transpose) {
	const int rows = transpose ? image.width : image.height;
	const int columns = transpose ? image.height : image.width;
	std::vector<int> widths(size_t(rows), 0);
	for (int row = 0; row < rows; ++row) {
		int first = columns, last = -1;
		for (int column = 0; column < columns; ++column) {
			const bool filled = transpose ? image.Filled(row, column) : image.Filled(column, row);
			if (filled) {
				first = std::min(first, column);
				last = std::max(last, column);
			}
		}
		widths[size_t(row)] = last >= first ? last - first + 1 : 0;
	}
	return widths;
}

// 端から続く細い部分を評価する。幅に対して短い頭や肩は柱とみなさない。
inline float
    PillarShare(const std::vector<int>& widths, int first, int last, bool fromLast, const WeaponShapeLimits& limits) {
	const int widest = *std::max_element(widths.begin(), widths.end());
	const float widthLimit = limits.pillarWidth * float(widest);
	int run = 0, widthSum = 0;
	const int step = fromLast ? -1 : 1;
	for (int row = fromLast ? last : first; row >= first && row <= last && float(widths[size_t(row)]) < widthLimit;
	     row += step) {
		++run;
		widthSum += widths[size_t(row)];
	}
	if (run <= 0 || float(run) < limits.pillarAspect * (float(widthSum) / float(run))) {
		return 0;
	}
	return float(run) / float(last - first + 1);
}

inline void
    MeasurePillars(const SilhouetteImage& image, int axis, const WeaponShapeLimits& limits, WeaponShapeReport& report) {
	auto take = [&](float share, int end) {
		if (share > report.pillar) {
			report.pillar = share;
			report.pillarFrom = axis * 4 + end;
		}
	};
	for (int direction = 0; direction < 2; ++direction) {
		if (direction == 1 && axis != 0) {
			continue;
		}
		const auto widths = SilhouetteRowWidths(image, direction == 1);
		int first = int(widths.size()), last = -1;
		for (int row = 0; row < int(widths.size()); ++row) {
			if (widths[size_t(row)] > 0) {
				first = std::min(first, row);
				last = std::max(last, row);
			}
		}
		if (last < 0) {
			continue;
		}
		// 上の端と前後を調べる。床に接する下端と、左右対称の部品は柱に数えない。
		take(PillarShare(widths, first, last, true, limits), direction == 0 && axis != 2 ? 0 : 3);
		if ((axis == 0 && direction == 1) || axis == 2) {
			take(PillarShare(widths, first, last, false, limits), 2);
		}
	}
}

inline void MeasurePartShares(const WeaponShapeVoxels& voxels, WeaponShapeReport& report) {
	if (!voxels.part || voxels.part->size() != voxels.fill->size()) {
		return;
	}
	std::array<int, 3> count{};
	for (size_t i = 0; i < voxels.fill->size(); ++i) {
		if (!(*voxels.fill)[i]) {
			continue;
		}
		const int part = (*voxels.part)[i];
		if (part >= 1 && part <= 3) {
			++count[size_t(part - 1)];
		}
	}
	for (size_t part = 0; part < count.size(); ++part) {
		report.partShare[part] = float(count[part]) / float(report.voxels);
	}
}

inline void
    ApplyShapeIssues(const WeaponShapeVoxels& voxels, const WeaponShapeLimits& limits, WeaponShapeReport& report) {
	auto flag = [&](WeaponShapeIssue issue, bool on, int cost) {
		report.issues[size_t(issue)] = on;
		if (on) {
			report.score -= cost;
		}
	};
	flag(WeaponShapeIssue::Thin, report.thinShare > limits.thinMax, 25);
	flag(
	    WeaponShapeIssue::Blob,
	    report.masses < limits.massMin &&
	        std::min({report.solid[0], report.solid[1], report.solid[2]}) > limits.solidMax,
	    25);
	flag(WeaponShapeIssue::Busy, report.masses > limits.massMax, 10);
	flag(WeaponShapeIssue::Floating, report.pieces > 1, 20);
	const bool tinyPart =
	    voxels.part && std::any_of(report.partShare.begin() + 1, report.partShare.end(), [&](float share) {
		    return share > 0 && share < limits.partMin;
	    });
	flag(WeaponShapeIssue::TinyPart, tinyPart, 10);
	flag(WeaponShapeIssue::BaseOnly, voxels.part && report.partShare[0] > limits.baseMax, 10);
	flag(WeaponShapeIssue::Pillar, report.pillar > limits.pillarHeight, 60);
	report.score = std::max(0, report.score);
}
} // namespace ShapeMetricsDetail

inline WeaponShapeReport MeasureWeaponShape(const WeaponShapeVoxels& voxels, const WeaponShapeLimits& limits = {}) {
	using namespace ShapeMetricsDetail;
	WeaponShapeReport report;
	if (!voxels.fill || voxels.nx <= 0) {
		return report;
	}
	const auto& fill = *voxels.fill;
	report.voxels = int(std::count_if(fill.begin(), fill.end(), [](uint8_t value) { return value != 0; }));
	if (report.voxels == 0) {
		return report;
	}
	// 細すぎ: 半径 minThick/2 のオープニングで消える割合。
	const auto openedThin = Open(voxels, limits.minThick * .5f / voxels.cell);
	const int kept = int(std::count_if(openedThin.begin(), openedThin.end(), [](uint8_t value) { return value != 0; }));
	report.thinShare = 1 - float(kept) / float(report.voxels);
	// 塊の数 (小さい塊は数えない) と、元の形にある離れた部品。
	const auto openedMasses = Open(voxels, limits.massRadius / voxels.cell);
	for (int size : Components(voxels, openedMasses, false)) {
		if (float(size) >= limits.massShare * float(report.voxels)) {
			++report.masses;
		}
	}
	for (int size : Components(voxels, fill, true)) {
		if (float(size) >= .01f * float(report.voxels)) {
			++report.pieces;
		}
	}
	MeasurePartShares(voxels, report);
	for (int axis = 0; axis < 3; ++axis) {
		const auto image = ProjectSilhouette(voxels, axis);
		Silhouette(image, report.fill[size_t(axis)], report.outline[size_t(axis)], report.solid[size_t(axis)]);
		MeasurePillars(image, axis, limits, report);
	}
	ApplyShapeIssues(voxels, limits, report);
	return report;
}
} // namespace Game::Chrono
