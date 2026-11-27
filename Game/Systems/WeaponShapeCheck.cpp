// WeaponShapeCheck.cpp
// 変形武器の形の確かめ (F1。ChronoSystem の一部。測り方は Chrono/WeaponShapeMetrics.h)。
//  --craft-shape-sheet: 作る画面が開いたら、集めた物から材料 3 つの組み合わせをいくつも作って形を測り、
//  tests/out/craft-shapes/sheet.ppm (組み合わせごとに横・正面・上から見た影絵。色は部品ごと、上の帯は点数) と
//  report.tsv (組み合わせごとの数) と summary.txt (平均の点数と、問題ごとの数) を書いて終わる。
//  --craft-shape-count=N で組み合わせの数 (既定 60)。--craft-items=id,id,id の組み合わせは最初に入れる
//  細すぎの基準は、今の流体の升 (FluidVolume) と粒の間隔から、形の座標に直して決める
#include "ChronoSystemInternal.h"
#include "../Data/Collection.h"
#include "../Chrono/WeaponShapeMetrics.h"
#include "../Chrono/WeaponShapeGrammar.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Game {
namespace {
bool HasArg(const wchar_t* flag) {
	return wcsstr(GetCommandLineW(), flag) != nullptr;
}
float ArgFloat(const wchar_t* flag, float fallback) {
	const wchar_t* at = wcsstr(GetCommandLineW(), flag);
	return at ? float(_wtof(at + wcslen(flag))) : fallback;
}
std::string NameOf(const std::string& id) {
	const int index = Collection::IndexOf(id);
	return index < 0 ? id : Collection::Entries()[size_t(index)].name;
}
// 3x5 の数字 (影絵の番号)
constexpr uint16_t kDigits[10] = {0x7B6F, 0x2492, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> rgb;
	void Init(int W, int H) {
		w = W;
		h = H;
		rgb.assign(size_t(w * h * 3), 245);
	}
	void Put(int x, int y, std::array<uint8_t, 3> c) {
		if (x < 0 || y < 0 || x >= w || y >= h)
			return;
		uint8_t* p = &rgb[size_t((y * w + x) * 3)];
		p[0] = c[0];
		p[1] = c[1];
		p[2] = c[2];
	}
	void Rect(int x, int y, int W, int H, std::array<uint8_t, 3> c) {
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i)
				Put(x + i, y + j, c);
	}
	void Number(int x, int y, int value, int scale, std::array<uint8_t, 3> c) {
		const std::string s = std::to_string(value);
		for (size_t k = 0; k < s.size(); ++k) {
			const uint16_t bits = kDigits[s[k] - '0'];
			for (int r = 0; r < 5; ++r)
				for (int q = 0; q < 3; ++q)
					if (bits & (1 << (14 - (r * 3 + q))))
						Rect(x + int(k) * 4 * scale + q * scale, y + r * scale, scale, scale, c);
		}
	}
	void Save(const std::string& path) const {
		std::ofstream out(path, std::ios::binary);
		out << "P6" << '\n' << w << ' ' << h << '\n' << 255 << '\n';
		out.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size()));
	}
};
} // namespace

void ChronoSystem::WriteWeaponShapeSheet() {
	auto& c = battle_.craft;
	const int n = int(c.bags.size());
	if (n == 0)
		return;
	// 細すぎの基準: 流体の升の 2 つ分か、粒の間隔の 1.6 倍の太さ (ワールドの長さ) を、形の座標に直す
	const auto* render = Engine::Renderer::GetInstance();
	const float voxel = render ? render->volumePreviousVoxelSize_ : 0.f,
	            spacing = render ? render->fluidSamplingScale_ : 1.f;
	const float scale = LifeScale(life_.bodies[0].mass);
	WeaponShapeLimits limits;
	if (voxel > 0 && scale > 0)
		limits.minThick = std::max(2 * voxel, 1.6f * spacing) / scale;
	// 組み合わせ: --craft-items の物を最初に。あとは決まった並びで散らす (同じ物が重なる組み合わせも少し入る)
	std::vector<std::array<int, 3>> combos;
	if (const wchar_t* at = wcsstr(GetCommandLineW(), L"--craft-items=")) {
		at += 14;
		std::string list;
		while (*at && *at != L' ' && *at != L'"')
			list += char(*at++);
		std::array<int, 3> combo{-1, -1, -1};
		size_t from = 0;
		int k = 0;
		while (k < 3 && from <= list.size()) {
			const size_t comma = list.find(',', from);
			const std::string id = list.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
			for (int i = 0; i < n; ++i)
				if (c.bags[size_t(i)].id == id)
					combo[size_t(k)] = i;
			++k;
			if (comma == std::string::npos)
				break;
			from = comma + 1;
		}
		if (combo[0] >= 0 && combo[1] >= 0 && combo[2] >= 0)
			combos.push_back(combo);
	}
	const int want = std::max(1, int(std::lround(ArgFloat(L"--craft-shape-count=", 60))));
	for (int i = 0; int(combos.size()) < want; ++i)
		combos.push_back({(i * 7) % n, (i * 13 + 3) % n, (i * 29 + 5) % n});
	// 測る・描く
	const std::string dir = "tests/out/craft-shapes";
	std::filesystem::create_directories(dir);
	constexpr int view = 120, pad = 6, tileW = view * 3 + pad * 4, tileH = view + pad * 2 + 18, cols = 4;
	const int rows = (int(combos.size()) + cols - 1) / cols;
	Image sheet;
	sheet.Init(cols * tileW, rows * tileH);
	std::ofstream report(dir + "/report.tsv");
	report
	    << "#\tscore\tname\tbody\tpart2\tpart3\tthin%\tmasses\tpieces\tbase%\tpart2%\tpart3%\tsolid_side\tsolid_front\tsolid_top\toutline_side\tpillar\tissues\n";
	std::array<int, size_t(WeaponShapeIssue::Count)> issueCount{};
	int scoreSum = 0, passed = 0;
	for (size_t ci = 0; ci < combos.size(); ++ci) {
		const auto& combo = combos[ci];
		BattleWeapon w;
		w.filled = 3;
		for (int k = 0; k < 3; ++k) {
			const auto& bag = c.bags[size_t(combo[size_t(k)])];
			w.ids[size_t(k)] = bag.id;
			w.actions[size_t(k)] = bag.action;
			w.items[size_t(k)] = bag.items.front();
		}
		w.lv = FuseLevelsOf(w.actions, 3);
		w.name = std::string("[") + ShapeArchetypeName(ChooseArchetype(w.lv)) + "] " +
		         FuseWeaponName(w.lv, NameOf(w.ids[0]));
		const auto* g = WeaponShapeOf(BuildWeaponShape(w));
		if (!g)
			continue;
		WeaponShapeVoxels v{g->nx, g->ny, g->nz, g->cell, &g->fill, &g->part};
		const WeaponShapeReport r = MeasureWeaponShape(v, limits);
		scoreSum += r.score;
		if (r.score >= 75)
			++passed;
		std::string issues;
		for (int k = 0; k < int(WeaponShapeIssue::Count); ++k)
			if (r.issues[size_t(k)]) {
				++issueCount[size_t(k)];
				issues += (issues.empty() ? "" : "・") + std::string(WeaponShapeIssueName(WeaponShapeIssue(k)));
			}
		char row[512];
		snprintf(
		    row, sizeof(row),
		    "%d\t%d\t%s\t%s\t%s\t%s\t%.0f\t%d\t%d\t%.0f\t%.0f\t%.0f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f@%d\t%s\n", int(ci),
		    r.score, w.name.c_str(), w.ids[0].c_str(), w.ids[1].c_str(), w.ids[2].c_str(), r.thinShare * 100, r.masses,
		    r.pieces, r.partShare[0] * 100, r.partShare[1] * 100, r.partShare[2] * 100, r.solid[0], r.solid[1],
		    r.solid[2], r.outline[0], r.pillar, r.pillarFrom, issues.empty() ? "-" : issues.c_str());
		report << row;
		// 影絵: 横 (x を潰す)・正面 (z を潰す)・上 (y を潰す)。色は手前に一番近い箱の部品 (本体 青・前 だいだい・左右 緑)、濃さは奥行き
		const int tx = int(ci % cols) * tileW, ty = int(ci / cols) * tileH;
		const std::array<uint8_t, 3> band = r.score >= 75   ? std::array<uint8_t, 3>{70, 170, 80}
		                                    : r.score >= 50 ? std::array<uint8_t, 3>{230, 180, 40}
		                                                    : std::array<uint8_t, 3>{210, 60, 50};
		sheet.Rect(tx, ty, tileW, 18, band);
		sheet.Number(tx + 4, ty + 3, int(ci), 2, {255, 255, 255});
		sheet.Number(tx + tileW - 40, ty + 3, r.score, 2, {255, 255, 255});
		const int longest = std::max({g->nx, g->ny, g->nz});
		const float px = float(view) / float(longest);
		static constexpr std::array<std::array<uint8_t, 3>, 4> colors{
		    {{60, 60, 60}, {50, 110, 210}, {235, 130, 40}, {60, 160, 80}}};
		for (int axis = 0; axis < 3; ++axis) {
			const int ox = tx + pad + axis * (view + pad), oy = ty + 18 + pad;
			sheet.Rect(ox, oy, view, view, {225, 225, 225});
			const int w2 = axis == 0 ? g->nz : g->nx, h2 = axis == 2 ? g->nz : g->ny;
			for (int t = 0; t < h2; ++t)
				for (int u = 0; u < w2; ++u) {
					int owner = 0;
					float depth = 0;
					bool found = false;
					const int deep = axis == 0 ? g->nx : axis == 1 ? g->nz : g->ny;
					for (int k = 0; k < deep; ++k) {
						const int x = axis == 0 ? k : u, y = axis == 2 ? (g->ny - 1 - k) : t,
						          z = axis == 0   ? u
						              : axis == 1 ? k
						                          : t;
						const size_t i = size_t((z * g->ny + y) * g->nx + x);
						if (g->fill[i]) {
							owner = g->part[i];
							depth = float(k) / float(std::max(1, deep));
							found = true;
							break;
						}
					}
					if (!found)
						continue;
					auto col = colors[size_t(std::clamp(owner, 0, 3))];
					const float shade = 1 - .45f * depth;
					for (auto& ch : col)
						ch = uint8_t(float(ch) * shade);
					const int X0 = ox + int(float(u) * px), Y0 = oy + view - int(float(t + 1) * px),
					          X1 = ox + int(float(u + 1) * px), Y1 = oy + view - int(float(t) * px);
					sheet.Rect(X0, Y0, std::max(1, X1 - X0), std::max(1, Y1 - Y0), col);
				}
		}
	}
	sheet.Save(dir + "/sheet.ppm");
	std::ofstream summary(dir + "/summary.txt");
	summary << "combos=" << combos.size() << " average_score=" << (combos.empty() ? 0 : scoreSum / int(combos.size()))
	        << " passed(>=75)=" << passed << "\n";
	summary << "limits min_thick=" << limits.minThick << " mass_radius=" << limits.massRadius
	        << " (fluid voxel=" << voxel << " spacing=" << spacing << " body_scale=" << scale << ")\n";
	for (int k = 0; k < int(WeaponShapeIssue::Count); ++k)
		summary << WeaponShapeIssueName(WeaponShapeIssue(k)) << "=" << issueCount[size_t(k)] << "\n";
}
} // namespace Game
