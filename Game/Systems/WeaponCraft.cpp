// WeaponCraft.cpp
// 変形武器づくり (ChronoSystem の一部。規則は Chrono/WeaponCraftRules.h)。
//  - 準備フェーズが終わると、チャレンジの始まりの前に「作る画面」を開く。集めた物を全部並べ、3 つを混ぜて武器を 1 本作る
//    (3 つの物の性質 (追尾・貫通・重い・爆発…) が全部まざった弾を撃つ。枠が埋まったあとに入れた物は、選んでいる強化 (弾数・威力・連射) の点になる)
//  - 材料を 3 つ選んで「決定」すると、残りの物は全部 cm (大きさ) になる (演出)。その cm を弾数・威力・連射に − ＋ で割り振り、
//    レベル (Lv0〜10) が上がると強くなる。「戻る」で材料選びへ戻ると、割り振りは全部戻る
//  - 武器の形: 材料 3 つの物のモデル (ローポリ) を細かい箱に分けて、本体・前の部品・左右の部品として組み合わせる。
//    スライムの体 (流体の粒子) は撃つ間この形に変形する。材料が 1 つでも違えば、並べる順が違っても違う形になる
//  - 回数の制限はない。押している間、連射の間隔ごとに撃つ。撃ち方 (扇・連なり・全方位・衛星・大砲・降り注ぎ・槍・置き罠・挟み撃ち) は
//    性質の組み合わせで決まり (FusePatternOf)、性質でスライムの体も変わる (速さ・体当たり・狙いの補正・歩いた跡の塗り)
// 操作 (作る画面): 材料: クリック / Enter / A で入れる・右クリック / Backspace / B で 1 つ戻す・R / Y でおまかせ・Space / START で決定。
//   割り振り: − ＋ / ←→ で 1 つずつ (長押しで速く)・1〜3 / ↑↓ で行・右クリック / B で戻る・Space / START で挑戦
#include "ChronoSystemInternal.h"
#include "../Data/Collection.h"
#include "../Chrono/WeaponShapeSdf.h"
#include "../Chrono/WeaponShapeGrammar.h"
#include "../Chrono/MotifShape.h"
#include "../Chrono/WeaponShapeMetrics.h"
#include <filesystem>
#include <tuple>
#include <fstream>

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
std::string ImageOf(const std::string& id) {
	const int index = Collection::IndexOf(id);
	return index < 0 ? std::string() : Collection::ImagePath(Collection::Entries()[size_t(index)]);
}

// ---- 物のモデルを細かい箱に分ける ----
// 箱の並び: 一番下の角 lo・箱の大きさ cell・数 nx ny nz・埋まっているか。範囲 (lo..hi) はモデルの座標
struct ItemVoxels {
	Vec lo{}, hi{};
	float cell = 1;
	int nx = 0, ny = 0, nz = 0;
	std::vector<uint8_t> fill;
	bool At(int x, int y, int z) const {
		return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz && fill[size_t((z * ny + y) * nx + x)] != 0;
	}
	bool Inside(Vec p) const {
		return At(
		    int(std::floor((p.x - lo.x) / cell)), int(std::floor((p.y - lo.y) / cell)),
		    int(std::floor((p.z - lo.z) / cell)));
	}
};
// 三角形の並び (頂点の位置) から、つながった部分ごとに中を埋める。
// 部分ごとに x の向きの線を通して、面を横切る数の偶奇で中かどうかを決める (重なった部分どうしが打ち消し合わないように)。
// 閉じていない部分 (板 1 枚など) は、その部分の範囲の箱を埋める。最後に 1 箱ぶん太らせる (薄い所が切れないように)
ItemVoxels VoxelizeTriangles(const std::vector<Vec>& points, const std::vector<uint32_t>& indices) {
	ItemVoxels v;
	v.lo = {1e9f, 1e9f, 1e9f};
	v.hi = {-1e9f, -1e9f, -1e9f};
	for (const auto& p : points) {
		v.lo = {std::min(v.lo.x, p.x), std::min(v.lo.y, p.y), std::min(v.lo.z, p.z)};
		v.hi = {std::max(v.hi.x, p.x), std::max(v.hi.y, p.y), std::max(v.hi.z, p.z)};
	}
	const Vec size = v.hi - v.lo;
	const float longest = std::max({size.x, size.y, size.z, 1e-3f});
	v.cell = longest / float(CraftItemCells);
	v.lo = v.lo - Vec{v.cell, v.cell, v.cell};
	v.nx = int(std::ceil(size.x / v.cell)) + 3;
	v.ny = int(std::ceil(size.y / v.cell)) + 3;
	v.nz = int(std::ceil(size.z / v.cell)) + 3;
	v.fill.assign(size_t(v.nx * v.ny * v.nz), 0);
	// つながった部分: 同じ位置の頂点と、三角形の頂点どうしをまとめる
	const size_t n = points.size();
	std::vector<uint32_t> parent(n);
	for (size_t i = 0; i < n; ++i)
		parent[i] = uint32_t(i);
	auto find = [&](uint32_t a) {
		while (parent[a] != a)
			a = parent[a] = parent[parent[a]];
		return a;
	};
	auto unite = [&](uint32_t a, uint32_t b) { parent[find(a)] = find(b); };
	{
		std::map<std::tuple<int, int, int>, uint32_t> same;
		const float q = 1000 / longest;
		for (size_t i = 0; i < n; ++i) {
			auto key = std::make_tuple(
			    int(std::lround(points[i].x * q)), int(std::lround(points[i].y * q)),
			    int(std::lround(points[i].z * q)));
			auto [match, added] = same.emplace(key, uint32_t(i));
			if (!added)
				unite(uint32_t(i), match->second);
		}
	}
	for (size_t k = 0; k + 2 < indices.size(); k += 3) {
		unite(indices[k], indices[k + 1]);
		unite(indices[k], indices[k + 2]);
	}
	std::map<uint32_t, int> partOf;
	for (size_t k = 0; k + 2 < indices.size(); k += 3)
		partOf.emplace(find(indices[k]), int(partOf.size()));
	const int parts = int(partOf.size());
	std::vector<std::array<Vec, 2>> partBox(size_t(parts), {Vec{1e9f, 1e9f, 1e9f}, Vec{-1e9f, -1e9f, -1e9f}});
	std::vector<int> triPart;
	for (size_t k = 0; k + 2 < indices.size(); k += 3) {
		const int part = partOf[find(indices[k])];
		triPart.push_back(part);
		auto& box = partBox[size_t(part)];
		for (int c = 0; c < 3; ++c) {
			const Vec p = points[indices[k + size_t(c)]];
			box[0] = {std::min(box[0].x, p.x), std::min(box[0].y, p.y), std::min(box[0].z, p.z)};
			box[1] = {std::max(box[1].x, p.x), std::max(box[1].y, p.y), std::max(box[1].z, p.z)};
		}
	}
	std::vector<int> partFill(size_t(parts), 0);
	std::vector<uint8_t> owner(v.fill.size(), 0);
	std::vector<std::vector<float>> rowHits(static_cast<size_t>(parts));
	for (int z = 0; z < v.nz; ++z)
		for (int y = 0; y < v.ny; ++y) {
			// (線は箱の真ん中から少しずらす。面の境目をちょうど通ると数え間違えるため)
			const float py = v.lo.y + (float(y) + .5f) * v.cell + v.cell * .0137f,
			            pz = v.lo.z + (float(z) + .5f) * v.cell + v.cell * .0071f;
			for (auto& h : rowHits)
				h.clear();
			for (size_t k = 0; k + 2 < indices.size(); k += 3) {
				const Vec a = points[indices[k]], b = points[indices[k + 1]], c = points[indices[k + 2]];
				// yz の面に映した三角形に点が入るか (重心の座標)
				const float d = (b.y - a.y) * (c.z - a.z) - (c.y - a.y) * (b.z - a.z);
				if (std::abs(d) < 1e-9f)
					continue;
				const float u = ((py - a.y) * (c.z - a.z) - (c.y - a.y) * (pz - a.z)) / d,
				            w = ((b.y - a.y) * (pz - a.z) - (py - a.y) * (b.z - a.z)) / d;
				if (u < 0 || w < 0 || u + w > 1)
					continue;
				rowHits[size_t(triPart[k / 3])].push_back(a.x + (b.x - a.x) * u + (c.x - a.x) * w);
			}
			for (int part = 0; part < parts; ++part) {
				auto& h = rowHits[size_t(part)];
				if (h.size() < 2)
					continue;
				std::sort(h.begin(), h.end());
				for (size_t i = 0; i + 1 < h.size(); i += 2) {
					const int x0 = int(std::floor((h[i] - v.lo.x) / v.cell + .5f)),
					          x1 = int(std::floor((h[i + 1] - v.lo.x) / v.cell - .5f));
					for (int x = std::max(0, x0); x <= std::min(v.nx - 1, x1); ++x) {
						const size_t at = size_t((z * v.ny + y) * v.nx + x);
						if (!v.fill[at]) {
							v.fill[at] = 1;
							++partFill[size_t(part)];
						}
					}
				}
			}
		}
	// 閉じていない (ほとんど埋まらなかった) 部分は、範囲の箱を埋める
	for (int part = 0; part < parts; ++part) {
		const auto& box = partBox[size_t(part)];
		const int x0 = int((box[0].x - v.lo.x) / v.cell), x1 = int((box[1].x - v.lo.x) / v.cell),
		          y0 = int((box[0].y - v.lo.y) / v.cell), y1 = int((box[1].y - v.lo.y) / v.cell),
		          z0 = int((box[0].z - v.lo.z) / v.cell), z1 = int((box[1].z - v.lo.z) / v.cell);
		const int cells = (x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1);
		if (partFill[size_t(part)] >= std::max(1, cells / 30))
			continue;
		for (int z = z0; z <= z1; ++z)
			for (int y = y0; y <= y1; ++y)
				for (int x = x0; x <= x1; ++x)
					if (x >= 0 && y >= 0 && z >= 0 && x < v.nx && y < v.ny && z < v.nz)
						v.fill[size_t((z * v.ny + y) * v.nx + x)] = 1;
	}
	// 1 箱ぶん太らせる
	std::vector<uint8_t> grown = v.fill;
	for (int z = 0; z < v.nz; ++z)
		for (int y = 0; y < v.ny; ++y)
			for (int x = 0; x < v.nx; ++x)
				if (!v.fill[size_t((z * v.ny + y) * v.nx + x)])
					if (v.At(x - 1, y, z) || v.At(x + 1, y, z) || v.At(x, y - 1, z) || v.At(x, y + 1, z) ||
					    v.At(x, y, z - 1) || v.At(x, y, z + 1))
						grown[size_t((z * v.ny + y) * v.nx + x)] = 1;
	v.fill = std::move(grown);
	return v;
}
// 範囲 (埋まった所) の下の角と上の角
struct ItemShape {
	ItemVoxels voxels;
	Vec lo{}, hi{};
};
ItemShape SphereShape() {
	// モデルの無い物は丸い玉
	std::vector<Vec> pts;
	std::vector<uint32_t> idx;
	const int rings = 8, segs = 12;
	for (int r = 0; r <= rings; ++r)
		for (int s = 0; s < segs; ++s) {
			const float a = 3.1415927f * float(r) / rings, b = 6.2831853f * float(s) / segs;
			pts.push_back(Vec{std::sin(a) * std::cos(b), std::cos(a), std::sin(a) * std::sin(b)});
		}
	for (int r = 0; r < rings; ++r)
		for (int s = 0; s < segs; ++s) {
			const uint32_t a = uint32_t(r * segs + s), b = uint32_t(r * segs + (s + 1) % segs), c = a + uint32_t(segs),
			               d = b + uint32_t(segs);
			idx.insert(idx.end(), {a, c, b, b, c, d});
		}
	ItemShape out;
	out.voxels = VoxelizeTriangles(pts, idx);
	out.lo = {-1, -1, -1};
	out.hi = {1, 1, 1};
	return out;
}
} // namespace

// 物 (図鑑の id) の形: ローポリのモデルを箱に分けたもの (一度作ったら使い回す)
static const ItemShape& CraftItemShape(const std::string& id) {
	static std::map<std::string, ItemShape> cache;
	auto found = cache.find(id);
	if (found != cache.end())
		return found->second;
	const std::string path = "Resources/Models/Lowpoly/" + id + ".obj";
	ItemShape shape;
	bool made = false;
	if (std::filesystem::exists(path)) {
		auto* render = Engine::Renderer::GetInstance();
		const uint32_t mesh = render->LoadObjMesh(path);
		if (auto* model = render->GetModel(mesh)) {
			const auto& data = model->GetData();
			std::vector<Vec> points;
			points.reserve(data.vertices.size());
			for (const auto& v : data.vertices)
				points.push_back(Vec{v.position.x, v.position.y, v.position.z});
			std::vector<uint32_t> indices(data.indices.begin(), data.indices.end());
			if (indices.empty())
				for (uint32_t i = 0; i < uint32_t(points.size()); ++i)
					indices.push_back(i);
			if (!points.empty()) {
				shape.voxels = VoxelizeTriangles(points, indices);
				made = true;
			}
		}
	}
	if (!made)
		shape = SphereShape();
	// 埋まった範囲
	const auto& v = shape.voxels;
	Vec lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	for (int z = 0; z < v.nz; ++z)
		for (int y = 0; y < v.ny; ++y)
			for (int x = 0; x < v.nx; ++x)
				if (v.At(x, y, z)) {
					const Vec a = v.lo + Vec{float(x), float(y), float(z)} * v.cell;
					lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
					hi = {std::max(hi.x, a.x + v.cell), std::max(hi.y, a.y + v.cell), std::max(hi.z, a.z + v.cell)};
				}
	if (lo.x > hi.x) {
		lo = v.lo;
		hi = v.lo + Vec{1, 1, 1};
	}
	shape.lo = lo;
	shape.hi = hi;
	return cache.emplace(id, std::move(shape)).first->second;
}

// (比べる用: F1 までの作り方。物のモデルの箱をそのまま本体・前・左右に重ねる。--craft-shape-legacy のときだけ使う)
static WeaponShapeGrid BuildVoxelUnionShape(const std::array<std::string, CraftParts>& ids, int filled) {
	struct Place {
		const ItemShape* shape = nullptr;
		Vec center{};
		float scale = 1;
		bool side = false;
	};
	std::vector<Place> places;
	auto longest = [](const ItemShape& s) {
		const Vec d = s.hi - s.lo;
		return std::max({d.x, d.y, d.z, 1e-3f});
	};
	const ItemShape& base = CraftItemShape(ids[0]);
	const float baseScale = CraftBaseSize / longest(base);
	const Vec bd = (base.hi - base.lo) * baseScale;
	places.push_back({&base, Vec{0, bd.y * .5f, 0}, baseScale, false});
	if (filled >= 2) {
		const ItemShape& s = CraftItemShape(ids[1]);
		const float k = CraftFrontScale * CraftBaseSize / longest(s);
		const Vec d = (s.hi - s.lo) * k;
		places.push_back(
		    {&s, Vec{0, std::max(d.y * .5f, bd.y * CraftFrontHeight), bd.z * .5f + d.z * .5f * (1 - 2 * CraftOverlap)},
		     k, false});
	}
	if (filled >= 3) {
		const ItemShape& s = CraftItemShape(ids[2]);
		const float k = CraftSideScale * CraftBaseSize / longest(s);
		const Vec d = (s.hi - s.lo) * k;
		places.push_back(
		    {&s, Vec{bd.x * .5f + d.x * .5f * (1 - 2 * CraftOverlap), std::max(d.y * .5f, bd.y * CraftSideHeight), 0},
		     k, true});
	}
	Vec lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	for (const auto& p : places) {
		const Vec half = (p.shape->hi - p.shape->lo) * (p.scale * .5f);
		for (float mirror : {1.f, -1.f}) {
			if (mirror < 0 && !p.side)
				continue;
			const Vec c{p.center.x * mirror, p.center.y, p.center.z};
			lo = {std::min(lo.x, c.x - half.x), std::min(lo.y, c.y - half.y), std::min(lo.z, c.z - half.z)};
			hi = {std::max(hi.x, c.x + half.x), std::max(hi.y, c.y + half.y), std::max(hi.z, c.z + half.z)};
		}
	}
	lo.y = std::min(lo.y, 0.f);
	const Vec size = hi - lo;
	WeaponShapeGrid g;
	g.cell = std::max({size.x, size.y, size.z}) / float(CraftShapeCells);
	g.lo = lo;
	g.nx = int(std::ceil(size.x / g.cell)) + 1;
	g.ny = int(std::ceil(size.y / g.cell)) + 1;
	g.nz = int(std::ceil(size.z / g.cell)) + 1;
	g.fill.assign(size_t(g.nx * g.ny * g.nz), 0);
	g.part.assign(g.fill.size(), 0);
	int count = 0;
	for (int z = 0; z < g.nz; ++z)
		for (int y = 0; y < g.ny; ++y)
			for (int x = 0; x < g.nx; ++x) {
				const Vec p = g.lo + Vec{float(x) + .5f, float(y) + .5f, float(z) + .5f} * g.cell;
				int owner = 0;
				for (size_t pi = 0; pi < places.size() && !owner; ++pi) {
					const auto& pl = places[pi];
					const Vec mid = (pl.shape->lo + pl.shape->hi) * .5f;
					for (float mirror : {1.f, -1.f}) {
						if (mirror < 0 && !pl.side)
							continue;
						const Vec q{p.x * mirror, p.y, p.z};
						if (pl.shape->voxels.Inside(mid + (q - pl.center) * (1 / pl.scale))) {
							owner = int(pi) + 1;
							break;
						}
					}
				}
				if (owner) {
					const size_t i = size_t((z * g.ny + y) * g.nx + x);
					g.fill[i] = 1;
					g.part[i] = uint8_t(owner);
					++count;
				}
			}
	const float volume = std::max(1e-4f, float(count) * g.cell * g.cell * g.cell);
	const float k =
	    std::min(std::cbrt(CraftShapeVolume / volume), CraftShapeLongest / std::max({size.x, size.y, size.z, 1e-3f}));
	g.lo = g.lo * k;
	g.cell *= k;
	const Vec glo = lo * k, ghi = hi * k;
	g.bounds = {std::max(std::abs(glo.x), std::abs(ghi.x)), ghi.y, std::max(std::abs(glo.z), std::abs(ghi.z))};
	g.core = {
	    Vec{0, bd.y * .5f * k, 0}, std::clamp(std::min({bd.x * .5f, bd.y * .5f, bd.z * .5f}) * k * .6f, .35f, 1.6f)};
	return g;
}

// なめらかな部品の並びを箱に分ける (床より下は作らない)。count に埋まった箱の数
WeaponShapeGrid RasterizeParts(const std::vector<SdfPart>& parts, const std::string& key, int& count) {
	Vec lo{1e9f, 0, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	for (const auto& s : parts) {
		const Vec e = SdfExtent(s) + Vec{CraftBlend, CraftBlend, CraftBlend};
		for (float m : {1.f, -1.f}) {
			if (m < 0 && !s.mirror)
				continue;
			lo = {
			    std::min(lo.x, s.center.x * m - e.x), std::min(lo.y, s.center.y - e.y),
			    std::min(lo.z, s.center.z - e.z)};
			hi = {
			    std::max(hi.x, s.center.x * m + e.x), std::max(hi.y, s.center.y + e.y),
			    std::max(hi.z, s.center.z + e.z)};
		}
	}
	lo.y = std::max(lo.y, 0.f);
	const Vec size = hi - lo;
	WeaponShapeGrid g;
	g.key = key;
	g.cell = std::max({size.x, size.y, size.z}) / float(CraftShapeCells);
	g.lo = lo;
	g.nx = int(std::ceil(size.x / g.cell)) + 1;
	g.ny = int(std::ceil(size.y / g.cell)) + 1;
	g.nz = int(std::ceil(size.z / g.cell)) + 1;
	g.fill.assign(size_t(g.nx * g.ny * g.nz), 0);
	g.part.assign(g.fill.size(), 0);
	g.piece.assign(g.fill.size(), 0);
	count = 0;
	for (int z = 0; z < g.nz; ++z)
		for (int y = 0; y < g.ny; ++y)
			for (int x = 0; x < g.nx; ++x) {
				const Vec p = g.lo + Vec{float(x) + .5f, float(y) + .5f, float(z) + .5f} * g.cell;
				int owner = 0, index = 0;
				if (SdfShapeDistance(parts, p, CraftBlend, &owner, &index) < 0) {
					const size_t i = size_t((z * g.ny + y) * g.nx + x);
					g.fill[i] = 1;
					g.part[i] = uint8_t(owner);
					g.piece[i] = uint8_t(std::min(index + 1, 255));
					++count;
				}
			}
	return g;
}

// 物の形を基本形 1 つに置きかえたもの (一度作ったら使い回す)。一番長い向きの半分が 1
static const SdfPart& CraftItemPart(const std::string& id) {
	static std::map<std::string, SdfPart> cache;
	auto found = cache.find(id);
	if (found != cache.end())
		return found->second;
	const auto& v = CraftItemShape(id).voxels;
	std::vector<Vec> points;
	for (int z = 0; z < v.nz; ++z)
		for (int y = 0; y < v.ny; ++y)
			for (int x = 0; x < v.nx; ++x)
				if (v.At(x, y, z))
					points.push_back(v.lo + Vec{float(x) + .5f, float(y) + .5f, float(z) + .5f} * v.cell);
	return cache.emplace(id, SdfFitPoints(points)).first->second;
}

// 武器の形を作る (同じ材料の並びなら前に作った形を使う)。形の番号を返す
// 3 つの物をそれぞれなめらかな基本形 1 つに置きかえ、本体・前・左右 (鏡で 2 つ) に置いて、なめらかにつなぐ。
// 体積をそろえたあとで細すぎる部品は太らせ、置き直してもう一度作る (太さの下限 CraftMinThick)
int ChronoSystem::BuildWeaponShape(const BattleWeapon& w) {
	if (w.filled <= 0)
		return 0;
	std::string key = "sdf|";
	for (int i = 0; i < w.filled; ++i)
		key += w.ids[size_t(i)] + "|";
	// (比べる用: --craft-shape-legacy なら F1 までの作り方、--craft-shape-f2 なら F2 の作り方 (物の形をそのまま本体・前・左右に))
	static const bool legacy = wcsstr(GetCommandLineW(), L"--craft-shape-legacy") != nullptr,
	                  f2 = wcsstr(GetCommandLineW(), L"--craft-shape-f2") != nullptr,
	                  f3 = wcsstr(GetCommandLineW(), L"--craft-shape-f3") != nullptr,
	                  h3 = wcsstr(GetCommandLineW(), L"--craft-shape-h3") != nullptr;
	if (legacy)
		key = "voxel|" + key.substr(4);
	else if (!f2)
		key = (f3 ? "grammar|" : h3 ? "motif|" : "socket|") + key.substr(4);
	auto& grids = WeaponShapeGrids();
	for (size_t i = 0; i < grids.size(); ++i)
		if (grids[i].key == key)
			return WeaponShapeFirst + int(i);
	if (legacy) {
		WeaponShapeGrid g = BuildVoxelUnionShape(w.ids, w.filled);
		g.key = key;
		grids.push_back(std::move(g));
		return WeaponShapeFirst + int(grids.size()) - 1;
	}
	// (F3) 性質から型と部品を組み立てる。体積をそろえた倍率で太さの下限を決め直し、落ち着くまで作り直す
	if (!f2) {
		ShapeDesignInput in;
		in.lv = FuseLevelsOf(w.actions, w.filled);
		in.body = CraftItemPart(w.ids[0]);
		// (調整用: --shape-part-scale= と --shape-overlap= で部品の大きさと重なりを試せる)
		{
			static const float partScale = [] {
				const wchar_t* at = wcsstr(GetCommandLineW(), L"--shape-part-scale=");
				return at ? float(_wtof(at + 19)) : ShapePartScale;
			}();
			static const float overlap = [] {
				const wchar_t* at = wcsstr(GetCommandLineW(), L"--shape-overlap=");
				return at ? float(_wtof(at + 16)) : ShapeOverlap;
			}();
			in.partScale = partScale;
			in.overlap = overlap;
		}
		for (int i = w.filled; i-- > 0;)
			for (FuseTrait t : FuseTraitsOf(w.actions[size_t(i)]))
				if (t != FuseTrait::Count)
					in.owner[size_t(t)] = i + 1;
		// (H3) 3 つの物を姿の型紙に当てはめる。当てはまる型紙が無ければ (と --craft-shape-f3 のとき)、性質から型と部品を組み立てる (F3)
		const Motif* motif = nullptr;
		MotifChoice choice;
		std::array<const ItemProfile*, 3> profiles{};
		// (作る画面で 1 つ目・2 つ目まで入れたときも、同じ型紙の作り方で形を作る。そろっていない物は nullptr)
		if (!f3 && w.filled >= 1) {
			std::array<MotifItem, 3> items;
			bool ok = true;
			uint32_t seed = 0;
			for (int i = 0; i < w.filled && ok; ++i) {
				profiles[size_t(i)] = ItemProfileOf(w.ids[size_t(i)]);
				ok = profiles[size_t(i)] != nullptr;
				if (ok)
					items[size_t(i)] =
					    PrepareMotifItem(Motifs(), *profiles[size_t(i)], FuseTraitsOf(w.actions[size_t(i)]));
				for (char ch : w.ids[size_t(i)])
					seed = seed * 31 + uint8_t(ch);
			}
			if (ok) {
				choice = ChooseMotif(
				    Motifs(), {&items[0], w.filled > 1 ? &items[1] : nullptr, w.filled > 2 ? &items[2] : nullptr},
				    MotifHash(seed, 7));
				if (choice.motif >= 0)
					motif = &Motifs()[size_t(choice.motif)];
			}
		}
		float minHalf = 0, k = 1;
		std::vector<SdfPart> parts;
		WeaponShapeGrid g;
		std::array<const ItemProfile*, 3> use = profiles;
		// (J2) 土台に物を並べる作り方 (--craft-shape-h3 なら H3 の、物を枠に置く作り方)。配慮の物は置かない
		std::array<const ItemProfile*, 3> socketItems = profiles;
		for (int i = 0; i < 3; ++i)
			if (socketItems[size_t(i)] &&
			    std::find(socketItems[size_t(i)]->tags.begin(), socketItems[size_t(i)]->tags.end(), "配慮") !=
			        socketItems[size_t(i)]->tags.end())
				socketItems[size_t(i)] = nullptr;
		if (socketItems[0] == nullptr)
			socketItems[0] = profiles[0];
		std::vector<MotifModel> models;
		auto build = [&] {
			minHalf = 0;
			k = 1;
			for (int pass = 0; pass < 4; ++pass) {
				parts = !motif ? DesignWeaponShape(in, minHalf)
				        : h3   ? BuildMotifParts(*motif, choice, use, minHalf)
				               : BuildSocketParts(*motif, socketItems, minHalf, &models);
				int count = 0;
				g = RasterizeParts(parts, key, count);
				const float longest = float(std::max({g.nx, g.ny, g.nz})) * g.cell;
				const float volume = std::max(1e-4f, float(count) * g.cell * g.cell * g.cell);
				k = std::min(std::cbrt(CraftShapeVolume / volume), CraftShapeLongest / std::max(longest, 1e-3f));
				const float want = CraftMinThick * .5f / k;
				if (std::abs(want - minHalf) < .01f * want)
					break;
				minHalf = want;
			}
		};
		build();
		// (H3) 形の決まりの確かめ: 物の形そのものには細い所 (急須の注ぎ口・竹とんぼの軸) があるので、作ったあとで測り、
		// 細い柱が突き出していたら、後ろの物から順に、その枠をスライムだけの形にして作り直す。それでも残れば型紙を使わない (F3 の作り方)
		if (motif) {
			auto pillar = [&] {
				WeaponShapeVoxels v{g.nx, g.ny, g.nz, g.cell * k, &g.fill, nullptr};
				return MeasureWeaponShape(v).Has(WeaponShapeIssue::Pillar);
			};
			int drops = 0;
			// (J2 の土台は決まった形なので、柱は tests の全部の土台の確かめで防ぐ。並べる物はモデルで、面に沿って寝かせる)
			for (int i = 3; h3 && i-- > 0 && pillar();) {
				if (!use[size_t(i)] || choice.slotOf[size_t(i)] < 0)
					continue;
				use[size_t(i)] = nullptr;
				++drops;
				build();
			}
			if (pillar()) {
				motif = nullptr;
				build();
			} else {
				g.motif = motif->name;
				g.motifDrops = drops;
			}
			// (確認用: --motif-debug で部品の並びを tests/out/motifs/parts.txt に足す)
			if (wcsstr(GetCommandLineW(), L"--motif-debug")) {
				std::ofstream dbg("tests/out/motifs/parts.txt", std::ios::app);
				dbg << key << " motif=" << (motif ? motif->name : std::string("-")) << " k=" << k
				    << " minHalf=" << minHalf << " slots=" << choice.slotOf[0] << "," << choice.slotOf[1] << ","
				    << choice.slotOf[2] << '\n';
				for (const auto& s : parts)
					dbg << "  kind=" << int(s.kind) << " part=" << s.part << " field=" << s.field
					    << " scale=" << s.scale << " c=" << s.center.x << "," << s.center.y << "," << s.center.z
					    << " half=" << s.half.x << "," << s.half.y << "," << s.half.z << " stretch=" << s.stretch.x
					    << "," << s.stretch.y << "," << s.stretch.z << " mirror=" << s.mirror << '\n';
			}
		}
		g.lo = g.lo * k;
		g.cell *= k;
		// (J2) 並べる本物のモデル (形の座標に直す)
		g.models.clear();
		if (motif && !h3)
			for (const auto& m : models)
				g.models.push_back({uint8_t(m.item), m.pos * k, m.axes, m.scale * k, m.mirrored, m.emblem});
		const Vec glo = g.lo, ghi = g.lo + Vec{float(g.nx), float(g.ny), float(g.nz)} * g.cell;
		// (F5) 部品ごとの動きと支点 (形の座標に直す)
		g.pieces.clear();
		for (const auto& s : parts)
			g.pieces.push_back({uint8_t(s.motion), s.pivot * k});
		g.bounds = {std::max(std::abs(glo.x), std::abs(ghi.x)), ghi.y, std::max(std::abs(glo.z), std::abs(ghi.z))};
		const auto& b = parts[0];
		const Vec e = SdfExtent(b);
		g.core = {Vec{0, b.center.y * k, 0}, std::clamp(std::min({e.x, e.y, e.z}) * k * .9f, .35f, 1.6f)};
		// (F4) 材料の物を浮かべる所: 本体の物は体の真ん中。ほかの物は、その物が持ってきた性質の部品 (左右に 2 つある部品は右) の真ん中。
		// 部品を持たない物 (性質の無い物) は、体の中の上の前・下の後ろに小さく
		const float bodyFit = std::min({e.x, e.y, e.z});
		g.anchors = w.filled;
		for (int i = 0; i < w.filled; ++i) {
			const int owner = i + 1;
			const SdfPart* own = nullptr;
			// (型紙のときは、その物がなった部品 (どの枠でも)。枠に入らなかった物は体の中に小さく)
			if (motif) {
				for (const auto& s : parts)
					if (s.part == owner) {
						own = &s;
						break;
					}
				const Vec off = i == 1 ? Vec{0, e.y * .4f, e.z * .35f} : Vec{0, -e.y * .35f, -e.z * .35f};
				const Vec oe = own ? SdfExtent(*own) : Vec{};
				g.anchor[size_t(i)] = own ? own->center * k : (b.center + (i == 0 ? Vec{} : off)) * k;
				g.anchorFit[size_t(i)] = own ? std::min({oe.x, oe.y, oe.z}) * k * .8f : bodyFit * k * .4f;
				continue;
			}
			if (i > 0)
				for (size_t p = 1; p < parts.size(); ++p)
					if (parts[p].part == owner) {
						own = &parts[p];
						break;
					}
			if (i == 0) {
				g.anchor[0] = b.center * k;
				g.anchorFit[0] = bodyFit * k * .7f;
			} else if (own) {
				const Vec oe = SdfExtent(*own);
				g.anchor[size_t(i)] = own->center * k;
				g.anchorFit[size_t(i)] = std::min({oe.x, oe.y, oe.z}) * k * .85f;
			} else {
				const Vec off = i == 1 ? Vec{0, e.y * .4f, e.z * .35f} : Vec{0, -e.y * .35f, -e.z * .35f};
				g.anchor[size_t(i)] = (b.center + off) * k;
				g.anchorFit[size_t(i)] = bodyFit * k * .4f;
			}
		}
		grids.push_back(std::move(g));
		return WeaponShapeFirst + int(grids.size()) - 1;
	}
	// 部品の大きさ (一番長い向きの半分): 本体 0.5・前 0.5 x CraftFrontScale・左右 0.5 x CraftSideScale
	std::vector<SdfPart> parts;
	for (int i = 0; i < w.filled; ++i) {
		SdfPart s = CraftItemPart(w.ids[size_t(i)]);
		const float size = .5f * CraftBaseSize * (i == 0 ? 1.f : i == 1 ? CraftFrontScale : CraftSideScale);
		s.half = s.half * size;
		s.round *= size;
		s.part = i + 1;
		s.mirror = i == 2;
		// 向き: 本体と左右の部品は立てる (一番長い向きを上、2 番目を左右、一番短い向きを前後)。ゲームのカメラは後ろの少し上から見るので、
		// 寝かせると平たい丸にしか見えない。前の部品は一番長い向きを前に向ける (鼻先・砲身)
		if (i != 1)
			s.axes = {Vec{1, 0, 0}, Vec{0, 0, 1}, Vec{0, 1, 0}};
		parts.push_back(s);
	}
	// 部品の、ワールドの向きごとの半分の大きさ (向きは軸の入れかえだけなので、そのまま足せる)
	auto extent = [](const SdfPart& s) {
		Vec e{};
		for (int k = 0; k < 3; ++k) {
			const Vec a = s.axes[size_t(k)];
			const float h = k == 0 ? s.half.x : k == 1 ? s.half.y : s.half.z;
			e = e + Vec{std::abs(a.x) * h, std::abs(a.y) * h, std::abs(a.z) * h};
		}
		return e;
	};
	// 置き方: 本体は真ん中で床に乗せる。前の部品は本体の前に少し重ね、本体の真ん中より上。左右の部品は本体の横に少し重ね、少し後ろ
	auto layout = [&] {
		const Vec b = extent(parts[0]);
		parts[0].center = {0, b.y, 0};
		if (parts.size() > 1) {
			auto& f = parts[1];
			const Vec e = extent(f);
			f.center = {0, std::max(e.y, b.y * (1 + CraftFrontLift)), b.z + e.z * (1 - 2 * CraftOverlap)};
		}
		if (parts.size() > 2) {
			auto& s = parts[2];
			const Vec e = extent(s);
			s.center = {
			    b.x + e.x * (1 - 2 * CraftOverlap), std::max(e.y, b.y * CraftSideHeight * 2), -b.z * CraftSideBack};
		}
	};
	layout();
	WeaponShapeGrid g;
	float k = 1;
	for (int pass = 0; pass < 4; ++pass) {
		// 範囲 (鏡の部品も入れる)
		Vec lo{1e9f, 0, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
		for (const auto& s : parts) {
			const float r = std::max({s.half.x, s.half.y, s.half.z}) + CraftBlend;
			for (float m : {1.f, -1.f}) {
				if (m < 0 && !s.mirror)
					continue;
				lo = {
				    std::min(lo.x, s.center.x * m - r), std::min(lo.y, s.center.y - r), std::min(lo.z, s.center.z - r)};
				hi = {
				    std::max(hi.x, s.center.x * m + r), std::max(hi.y, s.center.y + r), std::max(hi.z, s.center.z + r)};
			}
		}
		lo.y = std::max(lo.y, 0.f);
		const Vec size = hi - lo;
		g = WeaponShapeGrid{};
		g.key = key;
		g.cell = std::max({size.x, size.y, size.z}) / float(CraftShapeCells);
		g.lo = lo;
		g.nx = int(std::ceil(size.x / g.cell)) + 1;
		g.ny = int(std::ceil(size.y / g.cell)) + 1;
		g.nz = int(std::ceil(size.z / g.cell)) + 1;
		g.fill.assign(size_t(g.nx * g.ny * g.nz), 0);
		g.part.assign(g.fill.size(), 0);
		int count = 0;
		for (int z = 0; z < g.nz; ++z)
			for (int y = 0; y < g.ny; ++y)
				for (int x = 0; x < g.nx; ++x) {
					const Vec p = g.lo + Vec{float(x) + .5f, float(y) + .5f, float(z) + .5f} * g.cell;
					int owner = 0;
					if (SdfShapeDistance(parts, p, CraftBlend, &owner) < 0) {
						const size_t i = size_t((z * g.ny + y) * g.nx + x);
						g.fill[i] = 1;
						g.part[i] = uint8_t(owner);
						++count;
					}
				}
		// 体積をそろえる倍率 (長い形は長さ 9.5 まで)
		const float volume = std::max(1e-4f, float(count) * g.cell * g.cell * g.cell);
		k = std::min(
		    std::cbrt(CraftShapeVolume / volume), CraftShapeLongest / std::max({size.x, size.y, size.z, 1e-3f}));
		// 細すぎる部品を太らせて、もう一度 (太らせる所がなければ終わり)
		const float floor = CraftMinThick * .5f / k;
		bool grew = false;
		for (auto& s : parts) {
			if (s.half.x < floor) {
				s.half.x = floor;
				grew = true;
			}
			if (s.half.y < floor) {
				s.half.y = floor;
				grew = true;
			}
			if (s.half.z < floor) {
				s.half.z = floor;
				grew = true;
			}
			s.round = std::max(s.round, std::min(s.half.y, s.half.x) * .6f);
		}
		if (!grew)
			break;
		layout();
	}
	g.lo = g.lo * k;
	g.cell *= k;
	const Vec glo = g.lo, ghi = g.lo + Vec{float(g.nx), float(g.ny), float(g.nz)} * g.cell;
	g.bounds = {std::max(std::abs(glo.x), std::abs(ghi.x)), ghi.y, std::max(std::abs(glo.z), std::abs(ghi.z))};
	const auto& b = parts[0];
	g.core = {Vec{0, b.center.y * k, 0}, std::clamp(std::min({b.half.x, b.half.y, b.half.z}) * k * .9f, .35f, 1.6f)};
	grids.push_back(std::move(g));
	return WeaponShapeFirst + int(grids.size()) - 1;
}

// ---- 作る画面 ----
namespace {
// 材料の升の並び (設計座標)
struct CraftGrid {
	int cols = 8;
	float cell = 84, x = 56, y = 176;
};
CraftGrid CraftGridFor(int count) {
	CraftGrid g;
	g.cols = count <= 24 ? 6 : count <= 40 ? 8 : 10;
	const int rows = std::max(1, (count + g.cols - 1) / g.cols);
	g.cell = std::min({84.f, 668.f / float(g.cols), 360.f / float(rows)});
	return g;
}
UI::Rect CraftCellRect(const CraftGrid& g, int i) {
	return {g.x + float(i % g.cols) * g.cell + 3, g.y + float(i / g.cols) * g.cell + 3, g.cell - 6, g.cell - 6};
}
// 右の武器の札 (1 枚)・枠・形の見本・強化の行・ボタン
constexpr UI::Rect kCard{770, 130, 470, 500};
UI::Rect CraftSocket(int part) {
	return {kCard.x + 16 + float(part) * 76, kCard.y + 50, 64, 64};
}
// (形の見本は、実際のスライムを映す窓。札と背景はこの窓を開けて描く。札の右側を強化の行の上まで使う)
constexpr UI::Rect kPreview{kCard.x + 244, kCard.y + 40, 216, 302};
// 見本の回る速さ (ラジアン/秒。1 周 約 16 秒)・手で回したあと自動で回り始めるまでの秒・ドラッグの回る量 (ラジアン/UI の 1 点)
constexpr float kPreviewSpin = .4f, kPreviewSpinWait = 2.f, kPreviewDragTurn = .012f;
// 見本の撃ち方: 見本の窓に収まるよう、弾の動き (速さ・届く距離・重力。どれも戦闘の大きさの基準 unit に比べた量) を
// この倍率に縮める。見える大きさ (体の半径に対する倍率)・まっすぐ飛ぶ弾の速さの上限 (体の半径/秒)・弾が残る秒の上限
constexpr float kDemoScale = .1f, kDemoLook = .7f, kDemoSpeed = 2.5f, kDemoLife = 3;
// 撃つ時計: 長押しの撃ち方は kDemoPeriod 秒ごとに kDemoHold 秒撃ち続ける。連打の撃ち方は kDemoTapPeriod 秒ごとに 1 回
constexpr float kDemoPeriod = 2.6f, kDemoHold = 1.f, kDemoTapPeriod = 1.4f;
// rect から hole を除いた所を塗る (hole は rect の中。上・下・左・右の 4 つの帯)
void FillAround(UI::Canvas& ui, UI::Rect rect, UI::Rect hole, Engine::Vector4 color) {
	const float x0 = std::max(rect.x, hole.x), x1 = std::min(rect.x + rect.w, hole.x + hole.w),
	            y0 = std::max(rect.y, hole.y), y1 = std::min(rect.y + rect.h, hole.y + hole.h);
	if (x0 >= x1 || y0 >= y1) {
		ui.Fill(rect, color);
		return;
	}
	ui.Fill({rect.x, rect.y, rect.w, y0 - rect.y}, color);
	ui.Fill({rect.x, y1, rect.w, rect.y + rect.h - y1}, color);
	ui.Fill({rect.x, y0, x0 - rect.x, y1 - y0}, color);
	ui.Fill({x1, y0, rect.x + rect.w - x1, y1 - y0}, color);
}
UI::Rect CraftBoostRow(int boost) {
	return {kCard.x + 14, kCard.y + 352 + float(boost) * 46, 442, 40};
}
// 強化の行の − と ＋ (− は名前の右、＋ は行の右の端)
UI::Rect CraftMinusButton(int boost) {
	const UI::Rect row = CraftBoostRow(boost);
	return {row.x + 52, row.y + 6, 30, 28};
}
UI::Rect CraftPlusButton(int boost) {
	const UI::Rect row = CraftBoostRow(boost);
	return {row.x + row.w - 38, row.y + 6, 30, 28};
}
// ＋− の長押し: 速く入れ始めるまでの秒・速さ (1 秒に入れる数) の始めと最後・最後の速さになるまでの秒
constexpr float kHoldDelay = .35f, kHoldRateMin = 8, kHoldRateMax = 60, kHoldRamp = 1.6f;
// cm に変える演出の秒
constexpr float kConvertTime = 1.9f;
// 左下の札の下の帯: cm の巻尺 (左の端・右の端・上・高さ)
constexpr float kTapeLeft = 43, kTapeRight = 747, kTapeTop = 664, kTapeHeight = 33;
// 升 i の光の玉の進み (0 はまだ、1 は器に着いた)。升ごとに少しずつ遅らせる
float ConvertProgress(float age, int i, int n) {
	const float gap = std::min(.035f, .8f / float(std::max(1, n)));
	return std::clamp((age - .15f - gap * float(i)) / .55f, 0.f, 1.f);
}
constexpr UI::Rect kAutoButton{770, 648, 180, 52}, kGoButton{966, 648, 274, 52};
// 性質の色 (札の性質の印)
Engine::Vector4 TraitColor(int t) {
	static constexpr Engine::Vector4 c[] = {
	    {.3f, .6f, .95f, 1},   {.45f, .75f, .4f, 1}, {.95f, .4f, .35f, 1}, {.55f, .4f, .85f, 1}, {.45f, .42f, .4f, 1},
	    {.95f, .6f, .2f, 1},   {.9f, .25f, .2f, 1},  {.85f, .65f, .3f, 1}, {.95f, .5f, .75f, 1}, {.5f, .5f, .62f, 1},
	    {.95f, .35f, .15f, 1}, {.35f, .7f, .75f, 1}, {.2f, .75f, .95f, 1}, {.3f, .55f, .95f, 1}, {.85f, .7f, .35f, 1},
	    {.75f, .85f, .95f, 1}, {.6f, .7f, 1, 1},     {.95f, .8f, .25f, 1}};
	return c[std::clamp(t, 0, FuseTraitCount - 1)];
}
} // namespace

// 材料 1 つの点 (袋の物の大きさの平均)
template<class Bag> static int CraftBagValue(const Bag& bag, float unit) {
	float r = 0;
	for (const auto& it : bag.items)
		r += it.radius;
	return CraftValue(bag.items.empty() ? 0 : r / float(bag.items.size()), unit);
}

void ChronoSystem::OpenWeaponCraft() {
	auto& b = battle_;
	auto& c = b.craft;
	c = {};
	for (const auto& bag : b.startBag)
		if (!bag.items.empty()) {
			c.bags.push_back(bag);
			auto& copy = c.bags.back();
			std::stable_sort(copy.items.begin(), copy.items.end(), [](const BattleItem& x, const BattleItem& y) {
				return x.radius > y.radius;
			});
		}
	// (多く集めた物を前に)
	std::stable_sort(c.bags.begin(), c.bags.end(), [](const BattleBag& x, const BattleBag& y) {
		return x.items.size() > y.items.size();
	});
	c.used.assign(c.bags.size(), 0);
	// 確認用 (--craft-census): 袋ごとの数と 1 つの点、全部の点を tests/out/craft-census-<ステージ>.txt に書く
	if (HasArg(L"--craft-census")) {
		std::filesystem::create_directories("tests/out");
		std::ofstream out("tests/out/craft-census-" + CurrentDevourStage().id + ".txt");
		int items = 0, points = 0;
		for (const auto& bag : c.bags) {
			const int value = CraftBagValue(bag, b.unit);
			items += int(bag.items.size());
			points += value * int(bag.items.size());
			out << bag.id << ' ' << bag.items.size() << ' ' << value << '\n';
		}
		out << "kinds=" << c.bags.size() << " items=" << items << " points=" << points << " unit=" << b.unit << '\n';
	}
	b.phase = BattleStep::Craft;
	CraftScreenOpen() = true;
	// 確認用: 自動で遊ぶときは、おまかせで埋めて --craft-hold= 秒だけ見せてから決める (無ければすぐ決める)。
	// --craft-items=id,id,id で材料の 3 つを決め、--craft-boost=弾数,威力,連射 で強化の点を足せる
	if (HasArg(L"--devour-battle-auto") || b.matrix.active || b.impactCheck) {
		AutoWeaponCraft(true);
		c.hold = ArgFloat(L"--craft-hold=", 0);
		if (c.hold <= 0)
			FinishWeaponCraft();
	}
}

// 入れた材料から武器を作る (形はまだ作らない)
ChronoSystem::BattleWeapon ChronoSystem::CraftedWeapon() const {
	const auto& b = battle_;
	const auto& c = b.craft;
	BattleWeapon w;
	for (const auto& pick : c.picks) {
		if (pick.bag < 0 || pick.bag >= int(c.bags.size()) || w.filled >= CraftParts)
			continue;
		const auto& bag = c.bags[size_t(pick.bag)];
		const size_t i = size_t(w.filled++);
		w.ids[i] = bag.id;
		w.actions[i] = bag.action;
		w.items[i] = bag.items.front();
	}
	// 強化の点 = 割り振った cm
	w.points = c.alloc;
	// 確認用: --craft-boost=弾数,威力,連射 の点を足す
	if (const wchar_t* at = wcsstr(GetCommandLineW(), L"--craft-boost=")) {
		at += 14;
		for (int k = 0; k < CraftBoostCount && *at; ++k) {
			w.points[size_t(k)] += _wtoi(at);
			while (*at && *at != L',' && *at != L' ')
				++at;
			if (*at == L',')
				++at;
		}
	}
	w.caps = CurrentDevourStage().craftMax;
	w.lv = FuseLevelsOf(w.actions, w.filled);
	w.name = w.filled > 0 ? FuseWeaponName(w.lv, NameOf(w.ids[0])) : std::string();
	return w;
}

// cm の量: 材料にしなかった残りの物の点の合計 (1 点 = 1cm)
int ChronoSystem::CraftEnergy() const {
	const auto& c = battle_.craft;
	int energy = 0;
	for (size_t i = 0; i < c.bags.size(); ++i)
		energy += (int(c.bags[i].items.size()) - c.used[i]) * CraftBagValue(c.bags[i], battle_.unit);
	return energy;
}

// おまかせ: 多く集めた物を本体にして、性質の違う物を 2 つ混ぜる。boosts なら (自動で遊ぶ確認用) 残りを cm にして 3 つの強化へ均等に配る
void ChronoSystem::AutoWeaponCraft(bool boosts) {
	auto& c = battle_.craft;
	c.picks.clear();
	std::fill(c.used.begin(), c.used.end(), 0);
	c.alloc = {};
	const int n = int(c.bags.size());
	if (n == 0)
		return;
	auto take = [&](int bag, int boost) {
		if (bag < 0 || bag >= n || c.used[size_t(bag)] >= int(c.bags[size_t(bag)].items.size()))
			return false;
		++c.used[size_t(bag)];
		c.picks.push_back({bag, boost});
		return true;
	};
	// 確認用: --craft-items=id,id,id
	if (const wchar_t* at = wcsstr(GetCommandLineW(), L"--craft-items=")) {
		at += 14;
		std::string list;
		while (*at && *at != L' ' && *at != L'"')
			list += char(*at++);
		size_t from = 0;
		while (from <= list.size()) {
			const size_t comma = list.find(',', from);
			const std::string id = list.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
			for (int i = 0; i < n; ++i)
				if (c.bags[size_t(i)].id == id) {
					take(i, -1);
					break;
				}
			if (comma == std::string::npos)
				break;
			from = comma + 1;
		}
	} else {
		std::vector<BattleAction> seen;
		for (int i = 0; i < n && int(c.picks.size()) < CraftParts; ++i) {
			const auto traits = FuseTraitsOf(c.bags[size_t(i)].action);
			const bool plain = traits[0] == FuseTrait::Count;
			if ((plain && !c.picks.empty()) ||
			    std::find(seen.begin(), seen.end(), c.bags[size_t(i)].action) != seen.end())
				continue;
			seen.push_back(c.bags[size_t(i)].action);
			take(i, -1);
		}
		for (int i = 0; i < n && int(c.picks.size()) < CraftParts; ++i)
			take(i, -1);
	}
	if (!boosts)
		return;
	const CraftCaps caps = CurrentDevourStage().craftMax;
	c.energy = CraftEnergy();
	c.step = 2;
	for (int k = 0; k < CraftBoostCount; ++k)
		c.alloc[size_t(k)] = std::min(caps[size_t(k)], c.energy / CraftBoostCount);
}

// 決める: 武器と形を作り、始まりの演出へ (入れなかった残りは強化に回さない: 強化は自分で入れた分だけ)
void ChronoSystem::FinishWeaponCraft() {
	auto& b = battle_;
	auto& c = b.craft;
	// 本体が空なら、一番多く集めた物を入れる
	bool hasBase = false;
	for (const auto& p : c.picks)
		if (p.boost < 0)
			hasBase = true;
	if (!hasBase)
		for (int bag = 0; bag < int(c.bags.size()); ++bag)
			if (c.used[size_t(bag)] < int(c.bags[size_t(bag)].items.size())) {
				++c.used[size_t(bag)];
				c.picks.insert(c.picks.begin(), {bag, -1});
				break;
			}
	// 見本の撃ち方を片付ける (弾・光の輪。武器は作り直すので、撃つ待ちと連なり撃ちの残りも消える)
	c.demoShots.clear();
	c.demoNewShots.clear();
	b.fx.clear();
	b.weaponKick = 0;
	b.weapon = CraftedWeapon();
	b.weapon.shape = BuildWeaponShape(b.weapon);
	// 確認用 (--craft-shape-dump): 武器の形を横・正面・上から見た影絵にして tests/out/craft-shape.ppm に書く
	if (HasArg(L"--craft-shape-dump"))
		if (const auto* g = WeaponShapeOf(b.weapon.shape)) {
			std::filesystem::create_directories("tests/out");
			const int px = 5, W = (g->nz + g->nx + g->nx + 4) * px, H = std::max(g->ny, g->nz) * px + 2 * px;
			std::vector<uint8_t> img(size_t(W * H * 3), 255);
			auto dot = [&](int x, int y, uint8_t shade) {
				for (int a = 0; a < px; ++a)
					for (int k = 0; k < px; ++k) {
						const int X = x * px + a, Y = y * px + k;
						if (X < 0 || Y < 0 || X >= W || Y >= H)
							continue;
						uint8_t* q = &img[size_t((Y * W + X) * 3)];
						q[0] = std::min(q[0], shade);
						q[1] = std::min(q[1], uint8_t(shade / 2 + 60));
						q[2] = std::min(q[2], uint8_t(shade / 3 + 40));
					}
			};
			for (int z = 0; z < g->nz; ++z)
				for (int y = 0; y < g->ny; ++y)
					for (int x = 0; x < g->nx; ++x)
						if (g->fill[size_t((z * g->ny + y) * g->nx + x)]) {
							dot(z, g->ny - 1 - y, uint8_t(60 + 150 * x / std::max(1, g->nx)));
							dot(g->nz + 1 + x, g->ny - 1 - y, uint8_t(60 + 150 * z / std::max(1, g->nz)));
							dot(g->nz + g->nx + 3 + x, g->nz - 1 - z, uint8_t(60 + 150 * y / std::max(1, g->ny)));
						}
			std::ofstream out("tests/out/craft-shape.ppm", std::ios::binary);
			out << "P6" << '\n' << W << " " << H << '\n' << "255" << '\n';
			out.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
		}
	b.crafted = true;
	b.phase = BattleStep::Intro;
	b.intro = 0;
	CraftScreenOpen() = false;
	PlayDevourSound(devourSounds_.rankUp, .6f);
}

void ChronoSystem::UpdateWeaponCraft(GameContext& ctx, float dt) {
	auto& b = battle_;
	auto& c = b.craft;
	c.age += dt;
	// 確認用 (--craft-shape-sheet): 流体が落ち着いたら形を測って書き、終わる
	{
		static bool written = false;
		if (!written && c.age > 1.5f && HasArg(L"--craft-shape-sheet")) {
			written = true;
			WriteWeaponShapeSheet();
			PostQuitMessage(0);
		}
	}
	// 確認用 (--motif-stats): 186 万通りの当てはめを数えて書き、終わる。(--motif-shape-check): 型紙から作った形を測って書き、終わる
	{
		static bool written = false;
		if (!written && c.age > .5f &&
		    (HasArg(L"--motif-stats") || HasArg(L"--motif-shape-check") || HasArg(L"--motif-base-sheet"))) {
			written = true;
			if (HasArg(L"--motif-stats"))
				WriteMotifStats();
			else if (HasArg(L"--motif-base-sheet"))
				WriteMotifBaseSheet();
			else
				WriteMotifShapeCheck();
			PostQuitMessage(0);
		}
	}
	// 確認用 (--item-profile-sheet): 図鑑の物を全部プロフィールにして書き、終わる
	{
		static bool written = false;
		if (!written && c.age > .5f && HasArg(L"--item-profile-sheet")) {
			written = true;
			WriteItemProfileSheet();
			PostQuitMessage(0);
		}
	}
	if (c.hold >= 0) {
		if (c.age >= c.hold)
			FinishWeaponCraft();
		return;
	}
	UI::Canvas ui(ctx.renderer, ctx.viewportSize.x, ctx.viewportSize.y);
	const int n = int(c.bags.size());
	const CraftGrid grid = CraftGridFor(n);
	// 形の見本の窓をドラッグすると、スライムを回して見られる (横に動かすと横に、縦に動かすと見下ろす角度が変わる)
	if (auto* input = Engine::Input::GetInstance(); input && !UI::UsingPad()) {
		const auto mouse = ui.Pointer();
		if (input->IsMouseTrigger(0) && ui.Hover(kPreview))
			c.previewDrag = true;
		if (!input->IsMouseDown(0))
			c.previewDrag = false;
		if (c.previewDrag) {
			c.previewYaw -= (mouse[0] - c.previewMouse[0]) * kPreviewDragTurn;
			c.previewPitch = std::clamp(c.previewPitch + (mouse[1] - c.previewMouse[1]) * kPreviewDragTurn, .05f, 1.2f);
			c.previewIdle = 0;
		}
		c.previewMouse = mouse;
	}
	auto remove = [&](size_t at) {
		if (at >= c.picks.size())
			return;
		--c.used[size_t(c.picks[at].bag)];
		c.picks.erase(c.picks.begin() + std::ptrdiff_t(at));
		PlayDevourSound(devourSounds_.pounce, .35f, .8f);
	};
	auto notice = [&](const std::string& text) {
		c.notice = text;
		c.noticeAge = 0;
		PlayDevourSound(devourSounds_.hit, .3f, .6f);
	};
	auto* input = Engine::Input::GetInstance();
	c.noticeAge += dt;
	c.stepAge += dt;
	for (auto& flash : c.levelFlash)
		flash += dt;
	const bool confirm = UI::Pressed(DIK_SPACE) || UI::PadPressed(XINPUT_GAMEPAD_START) || ui.MouseClick(kGoButton);
	const bool back = UI::Pressed(DIK_BACK) || UI::PadPressed(XINPUT_GAMEPAD_B) ||
	                  (input && !UI::UsingPad() && input->IsMouseTrigger(1));
	// ---- 1. 材料を 3 つ選ぶ ----
	if (c.step == 0) {
		auto add = [&](int bag) {
			if (bag < 0 || bag >= n)
				return;
			if (int(c.picks.size()) >= CraftParts) {
				notice("材料は 3 つまで。「決定」で残りを cm に変えよう");
				return;
			}
			if (c.used[size_t(bag)] >= int(c.bags[size_t(bag)].items.size())) {
				notice("もう残っていない");
				return;
			}
			++c.used[size_t(bag)];
			c.picks.push_back({bag, -1});
			PlayDevourSound(devourSounds_.note, .5f, 1.f + .2f * float(c.picks.size() - 1));
		};
		// 升を動かす (矢印・十字キー)。マウスを乗せた升も選ぶ
		if (n > 0) {
			const int dx =
			    (UI::Pressed(DIK_RIGHT) || UI::PadPressed(XINPUT_GAMEPAD_DPAD_RIGHT | Engine::Input::kPadLsRight) ? 1
			                                                                                                      : 0) -
			    (UI::Pressed(DIK_LEFT) || UI::PadPressed(XINPUT_GAMEPAD_DPAD_LEFT | Engine::Input::kPadLsLeft) ? 1 : 0);
			const int dy =
			    (UI::Pressed(DIK_DOWN) || UI::PadPressed(XINPUT_GAMEPAD_DPAD_DOWN | Engine::Input::kPadLsDown) ? 1
			                                                                                                   : 0) -
			    (UI::Pressed(DIK_UP) || UI::PadPressed(XINPUT_GAMEPAD_DPAD_UP | Engine::Input::kPadLsUp) ? 1 : 0);
			if (dx || dy)
				c.cursor = std::clamp(c.cursor + dx + dy * grid.cols, 0, n - 1);
			for (int i = 0; i < n; ++i) {
				const auto r = CraftCellRect(grid, i);
				if (!UI::UsingPad() && ui.Hover(r))
					c.cursor = i;
				if (ui.MouseClick(r))
					add(i);
			}
			if (UI::Pressed(DIK_RETURN) || UI::PadPressed(XINPUT_GAMEPAD_A))
				add(c.cursor);
		}
		// 枠をクリックするとその材料を外す。右クリック・B で最後の 1 つを外す
		for (int part = 0; part < CraftParts; ++part)
			if (ui.MouseClick(CraftSocket(part)) && part < int(c.picks.size()))
				remove(size_t(part));
		if (back && !c.picks.empty())
			remove(c.picks.size() - 1);
		if (UI::Pressed(DIK_R) || UI::PadPressed(XINPUT_GAMEPAD_Y) || ui.MouseClick(kAutoButton)) {
			AutoWeaponCraft(false);
			PlayDevourSound(devourSounds_.rankUp, .4f, 1.3f);
		}
		// 決定: 材料がそろったら (物が 3 つより少なければ、入れられるだけ入れたら)、残りの物を全部 cm に変える
		if (confirm) {
			int left = 0;
			for (int i = 0; i < n; ++i)
				left += int(c.bags[size_t(i)].items.size()) - c.used[size_t(i)];
			if (c.picks.empty() || (int(c.picks.size()) < CraftParts && left > 0))
				notice("先に材料を 3 つ選んでね");
			else {
				c.step = 1;
				c.stepAge = 0;
				c.energy = CraftEnergy();
				c.alloc = {};
				c.holdRow = -1;
				PlayDevourSound(devourSounds_.rankUp, .55f, .9f);
			}
		}
		return;
	}
	// ---- 2. 残りの物が cm になる演出 (クリック・Enter・A で飛ばせる) ----
	if (c.step == 1) {
		// (決定のボタンの押し続け・連打で、割り振らずに挑戦まで進まないよう、飛ばすのはクリック・Enter・A だけ)
		const bool skip = UI::Pressed(DIK_RETURN) || UI::PadPressed(XINPUT_GAMEPAD_A) ||
		                  (input && !UI::UsingPad() && input->IsMouseTrigger(0));
		// (物が飛び込むたびに小さく鳴らす)
		const int before = int((c.stepAge - dt) / .09f), now = int(c.stepAge / .09f);
		if (now != before && c.stepAge < kConvertTime - .3f)
			PlayDevourSound(devourSounds_.note, .25f, 1.2f + .04f * float(now % 12));
		if (skip || c.stepAge >= kConvertTime) {
			c.step = 2;
			c.stepAge = 0;
			PlayDevourSound(devourSounds_.rankUp, .5f, 1.4f);
		}
		return;
	}
	// ---- 3. cm を割り振る ----
	const BattleWeapon weapon = CraftedWeapon();
	int spent = 0;
	for (int v : c.alloc)
		spent += v;
	// 1cm 入れる / 戻す。入れられなければ false (最大・cm 切れ・0)
	auto nudge = [&](int row, int dir) {
		int& v = c.alloc[size_t(row)];
		const int cap = weapon.caps[size_t(row)];
		if (dir > 0 && (spent >= c.energy || CraftBoostFull(v, cap)))
			return false;
		if (dir < 0 && v <= 0)
			return false;
		const int levelBefore = CraftLevelOf(v, cap);
		c.warned = false;
		v += dir;
		spent += dir;
		const int levelAfter = CraftLevelOf(v, cap);
		if (levelAfter > levelBefore) {
			c.levelFlash[size_t(row)] = 0;
			PlayDevourSound(devourSounds_.rankUp, .45f, 1.f + .06f * float(levelAfter));
		} else if (levelAfter < levelBefore)
			PlayDevourSound(devourSounds_.pounce, .35f, .8f);
		return true;
	};
	// 行を選ぶ (↑↓・1〜3・行のクリック)
	{
		const int dy =
		    (UI::Pressed(DIK_DOWN) || UI::PadPressed(XINPUT_GAMEPAD_DPAD_DOWN | Engine::Input::kPadLsDown) ? 1 : 0) -
		    (UI::Pressed(DIK_UP) || UI::PadPressed(XINPUT_GAMEPAD_DPAD_UP | Engine::Input::kPadLsUp) ? 1 : 0);
		if (dy)
			c.boost = (c.boost + dy + CraftBoostCount) % CraftBoostCount;
		const uint8_t numbers[] = {DIK_1, DIK_2, DIK_3};
		for (int k = 0; k < CraftBoostCount; ++k)
			if (UI::Pressed(numbers[k]) || ui.MouseClick(CraftBoostRow(k)))
				c.boost = k;
	}
	// ＋− を押している行と向き (マウスはボタンの上で押している間、キーとパッドは ←→ を押している間)
	int row = -1, dir = 0;
	if (input && !UI::UsingPad() && input->IsMouseDown(0))
		for (int k = 0; k < CraftBoostCount; ++k) {
			if (ui.Hover(CraftMinusButton(k)))
				row = k, dir = -1;
			else if (ui.Hover(CraftPlusButton(k)))
				row = k, dir = 1;
		}
	if (dir == 0 && input) {
		const bool left = input->Down(DIK_LEFT) || input->PadDown(XINPUT_GAMEPAD_DPAD_LEFT | Engine::Input::kPadLsLeft),
		           right =
		               input->Down(DIK_RIGHT) || input->PadDown(XINPUT_GAMEPAD_DPAD_RIGHT | Engine::Input::kPadLsRight);
		if (left != right)
			row = c.boost, dir = right ? 1 : -1;
	}
	// 押し始めは 1 つ。押し続けると kHoldDelay 秒のあとから、だんだん速く (毎秒 kHoldRateMin → kHoldRateMax)
	if (dir == 0) {
		c.holdRow = -1;
		c.holdDir = 0;
	} else if (row != c.holdRow || dir != c.holdDir) {
		c.holdRow = row;
		c.holdDir = dir;
		c.holdTime = 0;
		c.holdCarry = 0;
		c.boost = row;
		if (!nudge(row, dir))
			PlayDevourSound(devourSounds_.hit, .3f, .6f);
		else if (dir > 0)
			PlayDevourSound(devourSounds_.note, .35f, 1.3f + .1f * float(row));
	} else {
		c.holdTime += dt;
		if (c.holdTime > kHoldDelay) {
			const float speed = kHoldRateMin + (kHoldRateMax - kHoldRateMin) *
			                                       std::clamp((c.holdTime - kHoldDelay) / kHoldRamp, 0.f, 1.f);
			c.holdCarry += speed * dt;
			int moved = 0;
			while (c.holdCarry >= 1) {
				c.holdCarry -= 1;
				if (!nudge(row, dir))
					break;
				++moved;
			}
			if (moved > 0 && std::fmod(c.holdTime, .08f) < dt)
				PlayDevourSound(devourSounds_.note, .2f, 1.3f + .1f * float(row));
		}
	}
	// 戻る: 材料選びへ (割り振った cm は全部戻る)
	if (back || UI::PadPressed(XINPUT_GAMEPAD_Y) || ui.MouseClick(kAutoButton)) {
		c.step = 0;
		c.stepAge = 0;
		c.alloc = {};
		c.energy = 0;
		c.holdRow = -1;
		c.warned = false;
		c.noticeAge = 99;
		PlayDevourSound(devourSounds_.pounce, .4f, .7f);
		return;
	}
	// 挑戦: 開いてすぐは受けない (決定・演出を飛ばす押し続けで始まらないように)。cm が残っていれば、1 回目は知らせるだけ
	if (confirm && c.stepAge > .4f) {
		const int left = c.energy - spent;
		if (left > 0 && !(c.warned && c.noticeAge < 2)) {
			notice("まだ " + std::to_string(left) + "cm 残っている！もう一度押すと挑戦");
			c.warned = true;
		} else
			FinishWeaponCraft();
	}
}

namespace {
const std::vector<uint8_t>& CraftPreviewSilhouette(int shape, const WeaponShapeGrid& grid) {
	static std::map<int, std::vector<uint8_t>> sides;
	auto& side = sides[shape];
	if (side.empty()) {
		side.assign(size_t(grid.nz * grid.ny), 0);
		for (int z = 0; z < grid.nz; ++z)
			for (int y = 0; y < grid.ny; ++y)
				for (int x = 0; x < grid.nx; ++x)
					if (grid.fill[size_t((z * grid.ny + y) * grid.nx + x)]) {
						side[size_t(z * grid.ny + y)] = 1;
						break;
					}
	}
	return side;
}
} // namespace

void ChronoSystem::DrawWeaponCraftInventory(UI::Canvas& ui) const {
	using namespace UI::Scale;
	const auto& b = battle_;
	const auto& c = b.craft;
	const int n = int(c.bags.size());
	const CraftGrid grid = CraftGridFor(n);
	const float t = c.age;
	// ---- 左: 集めた物 (cm に変えたあとは薄く) ----
	Plate(ui, {40, 130, 710, 436}, Cream);
	ui.Text(c.step == 0 ? "集めた物" : "集めた物 → cm", 56, 136, 22, Black, Jp);
	for (int i = 0; i < n; ++i) {
		const auto& bag = c.bags[size_t(i)];
		const auto r = CraftCellRect(grid, i);
		const int left = int(bag.items.size()) - c.used[size_t(i)];
		const float pop = Overshoot((t - float(i) * .015f) / .25f);
		if (pop <= 0)
			continue;
		// (変える演出: 升ごとに少しずつ遅れて、絵が縮んで消える。残りの無い升はそのまま)
		const float gone = c.step == 0 || left <= 0 ? 0.f
		                   : c.step == 1            ? std::clamp(ConvertProgress(c.stepAge, i, n) * 1.6f, 0.f, 1.f)
		                                            : 1.f;
		const float shrink = pop * (1 - .7f * gone);
		const UI::Rect art{
		    r.x + r.w * .5f * (1 - shrink) + 4, r.y + r.h * .5f * (1 - shrink) + 4, r.w * shrink - 8, r.h * shrink - 8};
		const bool lit = left > 0 && gone < 1;
		ui.Rounded(r, lit ? White : UI::WithAlpha(Black, .12f), 10);
		const std::string image = ImageOf(bag.id);
		if (!image.empty())
			ui.Photo(image, art, Engine::Vector4{1, 1, 1, lit ? 1 - .65f * gone : .3f});
		// 点 (大きさ) と残りの数 (変えたあとは 0)
		const int value = CraftBagValue(bag, b.unit);
		for (int k = 0; k < value; ++k)
			ui.Dot(r.x + 9 + float(k) * 11, r.y + 9, 4.5f, lit ? Red : UI::WithAlpha(Red, .35f));
		const std::string count = "x" + std::to_string(gone >= 1 ? 0 : left);
		const float cw = ui.Width(count, 18, Num) + 8;
		ui.Fill({r.x + r.w - cw - 2, r.y + r.h - 22, cw, 20}, lit ? Black : UI::WithAlpha(Black, .4f));
		ui.Text(count, r.x + r.w - cw + 2, r.y + r.h - 25, 18, Yellow, Num);
		if (i == c.cursor && c.step == 0) {
			ui.RoundedRing({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, Red, 12);
			ui.RoundedRing({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, Red, 11);
		}
	}
	// 割り振る間: 変えたことが分かるように、升の上に一言
	if (c.step == 2) {
		const std::string text = "材料の 3 つ以外は、全部で " + std::to_string(c.energy) + "cm になった！";
		const float w = ui.Width(text, 22, Jp) + 36;
		ui.Rounded({395 - w * .5f, 520, w, 40}, UI::WithAlpha(Black, .78f), 10);
		ui.Text(text, 395 - w * .5f + 18, 526, 22, Yellow, Jp);
	}
}

// 左下の cm の巻尺 (成長の HUD と同じ巻尺: 黄色の帯・上の黒い線・3 つの高さの目盛り・Anton の数字・赤い読み取りの線)。
// テープは左の端から引き出してあり、出ている長さが「のこりの cm」。変える演出では伸び、割り振ると巻き戻る。
// 演出では、升の物の絵がテープの先へ飛び込む
void ChronoSystem::DrawWeaponCraftEnergy(UI::Canvas& ui) const {
	using namespace UI::Scale;
	const auto& b = battle_;
	const auto& c = b.craft;
	const int n = int(c.bags.size());
	const CraftGrid grid = CraftGridFor(n);
	int spent = 0;
	for (int v : c.alloc)
		spent += v;
	// 演出の間は、飛び込んだ分だけ数え上がる
	float shown = float(c.energy - spent);
	if (c.step == 1) {
		float arrived = 0, total = 0;
		for (int i = 0; i < n; ++i) {
			const auto& bag = c.bags[size_t(i)];
			const float worth = float((int(bag.items.size()) - c.used[size_t(i)]) * CraftBagValue(bag, b.unit));
			total += worth;
			arrived += worth * (ConvertProgress(c.stepAge, i, n) >= 1 ? 1.f : 0.f);
		}
		shown = total > 0 ? float(c.energy) * arrived / total : float(c.energy);
	}
	// ---- 数: 「のこり」と大きい数字・cm・全部の cm (下の端を揃える) ----
	{
		const float x = kTapeLeft + 14, base = 650;
		ui.Text("のこり", x, 590, 18, Black, Jp);
		const std::string number = std::to_string(int(std::lround(shown)));
		ui.Text(number, x, base - 52 * .8f, 52, Black, Num);
		const float unitX = x + ui.Width(number, 52, Num) + 4;
		ui.Text("cm", unitX, base - 26 * .8f, 26, Black, Num);
		if (c.step == 2)
			ui.Text(
			    "/ " + std::to_string(c.energy) + "cm", unitX + ui.Width("cm", 26, Num) + 10, base - 18 * .8f, 18,
			    UI::WithAlpha(Black, .5f), Num);
	}
	// ---- 巻尺 ----
	// 目盛りの間隔: 全部の cm が帯の長さに収まる 1cm の長さ。数字は 90 以上離れる切りのいい間隔で
	const float total = float(std::max(1, c.energy));
	const float pxPerCm = (kTapeRight - kTapeLeft - 24) / total;
	float labelStep = 1;
	for (float step : {1.f, 2.f, 5.f, 10.f, 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f})
		if (step * pxPerCm >= 90) {
			labelStep = step;
			break;
		}
	const float tickStep = labelStep / 10, tip = kTapeLeft + shown * pxPerCm;
	ui.Fill({kTapeLeft, kTapeTop, kTapeRight - kTapeLeft, kTapeHeight}, UI::WithAlpha(Black, .06f));
	ui.Fill({kTapeLeft, kTapeTop, std::max(0.f, tip - kTapeLeft), kTapeHeight}, Yellow);
	ui.Fill({kTapeLeft, kTapeTop, std::max(0.f, tip - kTapeLeft), 2}, Black);
	for (int i = 0;; ++i) {
		const float cm = tickStep * float(i), x = kTapeLeft + cm * pxPerCm;
		if (x > tip)
			break;
		const bool big = i % 10 == 0, mid = i % 5 == 0;
		ui.Fill({x, kTapeTop, big ? 2.6f : 1.4f, kTapeHeight * (big ? .8f : mid ? .55f : .32f)}, Black);
		// (数字は cm のまま: 100 を超えても m にしない。強化に使う単位なので)
		const std::string label = std::to_string(int(std::lround(cm))) + "cm";
		if (big && i > 0 && x + 6 + ui.Width(label, 18, Num) < tip)
			ui.Text(label, x + 5, kTapeTop + kTapeHeight * .32f, 18, Black, Num);
	}
	// 先の金具と、読み取りの赤い線
	if (shown > 0) {
		ui.Fill({tip - 3, kTapeTop - 4, 6, kTapeHeight + 8}, Black);
		ui.Fill({tip - 1.5f, kTapeTop - 10, 3, kTapeHeight + 14}, Red);
	}
	// 物の絵: 升の真ん中から、弧を描いてテープの先へ (cm の多い物ほど大きい)
	if (c.step == 1)
		for (int i = 0; i < n; ++i) {
			const auto& bag = c.bags[size_t(i)];
			if (int(bag.items.size()) - c.used[size_t(i)] <= 0)
				continue;
			const float p = ConvertProgress(c.stepAge, i, n);
			if (p <= 0 || p >= 1)
				continue;
			const auto r = CraftCellRect(grid, i);
			const float e = EaseIn(p), sx = r.x + r.w * .5f, sy = r.y + r.h * .5f, tx = std::max(tip, kTapeLeft + 20),
			            ty = kTapeTop + kTapeHeight * .5f;
			const float x = sx + (tx - sx) * e, y = sy + (ty - sy) * e - 80 * std::sin(3.1415927f * p);
			// (物の絵そのものが、小さくなりながらテープへ飛び込む: 物が cm に変わる)
			const std::string image = ImageOf(bag.id);
			const float size = (30 + 6 * float(CraftBagValue(bag, b.unit))) * (1 - .65f * e);
			if (!image.empty())
				ui.Photo(image, {x - size * .5f, y - size * .5f, size, size});
		}
}

void ChronoSystem::DrawWeaponCraftSelection(UI::Canvas& ui, int filled) const {
	using namespace UI::Scale;
	const auto& b = battle_;
	const auto& c = b.craft;
	const int n = int(c.bags.size());
	// ---- 左下: 材料を選ぶ間は、選んでいる物の性質と操作。決定のあとは、cm の巻尺と割り振りの操作 ----
	Plate(ui, {40, 584, 710, 116}, Cream);
	if (c.step > 0) {
		DrawWeaponCraftEnergy(ui);
		const bool pad = UI::UsingPad();
		const std::string top = c.noticeAge < 2 ? c.notice
		                        : c.step == 1   ? std::string("集めた物を cm に変えています…")
		                                        : std::string("レベルが上がると強くなる");
		ui.Text(top, 330, 590, 17, c.noticeAge < 2 ? UI::WithAlpha(Black, .6f) : Red, Jp);
		const std::string line1 = c.step == 1 ? std::string("クリックで飛ばす")
		                          : pad       ? std::string("← → 割り振る (長押しで速く)   ↑↓ 選ぶ")
		                                      : std::string("− ＋ で割り振る (長押しで速く)   1〜3・↑↓ 選ぶ");
		const std::string line2 = c.step == 1 ? std::string("")
		                          : pad       ? std::string("B 材料選びに戻る   START 挑戦")
		                                      : std::string("右クリック 材料選びに戻る   Space 挑戦");
		ui.Text(line1, 330, 614, 15, Black, Jp);
		ui.Text(line2, 330, 636, 15, Black, Jp);
		return;
	}
	if (n > 0) {
		const auto& bag = c.bags[size_t(std::clamp(c.cursor, 0, n - 1))];
		const auto traits = FuseTraitsOf(bag.action);
		std::string text;
		for (FuseTrait tr : traits)
			if (tr != FuseTrait::Count)
				text += (text.empty() ? "" : "・") + std::string(FuseTraitName(tr));
		if (text.empty())
			text = "なし (大きさだけ)";
		ui.Text(NameOf(bag.id), 56, 590, 28, Black, Jp);
		ui.Text("性質: " + text, 56 + ui.Width(NameOf(bag.id), 28, Jp) + 18, 600, 20, Black, Jp);
		// (材料にしなければ、1 つで何 cm になるか)
		const std::string worth = "1 つ " + std::to_string(CraftBagValue(bag, b.unit)) + "cm";
		ui.Text(worth, 734 - ui.Width(worth, 18, Jp), 600, 18, UI::WithAlpha(Black, .6f), Jp);
		const std::string where = c.noticeAge < 2 ? c.notice
		                          : filled < CraftParts
		                              ? std::string("→ 「") + CraftPartRole(filled) + "」として混ぜる"
		                              : std::string("材料がそろった！「決定」で残りの物を全部 cm に変える");
		ui.Text(where, 56, 630, 20, c.noticeAge < 2 ? UI::WithAlpha(Black, .6f) : Red, Jp);
	}
	const std::string help = UI::UsingPad()
	                             ? "A 入れる   B 戻す   Y おまかせ   START 決定"
	                             : "クリック・Enter 入れる   右クリック・Backspace 戻す   R おまかせ   Space 決定";
	ui.Text(help, 56, 664, 17, Black, Jp);
}

void ChronoSystem::DrawWeaponCraftSockets(UI::Canvas& ui, const BattleWeapon& weapon) const {
	using namespace UI::Scale;
	const int filled = weapon.filled;
	// ---- 右: 武器の札 ----
	// (札は形の見本の窓を開けて描く: 窓に実際のスライムが映る)
	{
		const float line = 4, drop = 7;
		FillAround(
		    ui, {kCard.x - line + drop, kCard.y - line + drop, kCard.w + 2 * line, kCard.h + 2 * line}, kPreview,
		    Black);
		FillAround(ui, {kCard.x - line, kCard.y - line, kCard.w + 2 * line, kCard.h + 2 * line}, kPreview, Black);
		FillAround(ui, kCard, kPreview, White);
	}
	ui.Fill({kCard.x, kCard.y, 8, kCard.h}, Red);
	ui.Text(
	    filled > 0 ? weapon.name : std::string("（材料を 3 つ入れてね）"), kCard.x + 18, kCard.y + 10,
	    filled > 0 && ui.Width(weapon.name, 22, Jp) > 430 ? 18.f : 22.f, Black, Jp);
	for (int part = 0; part < CraftParts; ++part) {
		const auto s = CraftSocket(part);
		const bool has = part < filled;
		ui.Rounded(s, has ? White : UI::WithAlpha(Black, .1f), 10);
		ui.RoundedRing(s, part == 0 ? Red : Black, 10);
		if (has) {
			const std::string image = ImageOf(weapon.ids[size_t(part)]);
			if (!image.empty())
				ui.Photo(image, {s.x + 5, s.y + 5, s.w - 10, s.h - 10});
		} else
			ui.Text("?", s.x + s.w * .5f - 8, s.y + 10, 32, UI::WithAlpha(Black, .3f), Num);
		ui.Text(CraftPartRole(part), s.x + 4, s.y + s.h + 2, 14, Black, Jp);
		if (part + 1 < CraftParts)
			ui.Text("+", s.x + s.w + 2, s.y + 18, 20, Black, Num);
	}
}

void ChronoSystem::DrawWeaponCraftPreview(UI::Canvas& ui, const BattleWeapon& weapon) {
	using namespace UI::Scale;
	const int filled = weapon.filled;
	// 形の見本: 実際のスライムが今の材料の形になって、この窓に映る (TickCraftPreview・CraftPreviewCamera)。窓は枠と名前だけ描く
	ui.RoundedRing(kPreview, Black, 6);
	ui.Fill({kPreview.x, kPreview.y, 76, 22}, Black);
	ui.Text("変形の形", kPreview.x + 6, kPreview.y + 2, 15, White, Jp);
	if (filled <= 0)
		ui.Text("材料を入れると変形", kPreview.x + 8, kPreview.y + kPreview.h - 24, 14, White, Jp);
	else if (!UI::UsingPad())
		ui.Text("ドラッグで回す", kPreview.x + kPreview.w - 104, kPreview.y + kPreview.h - 22, 14, White, Jp);
	// (確認用: --craft-preview-silhouette なら、前の影絵の見本も重ねる)
	static const bool silhouette = wcsstr(GetCommandLineW(), L"--craft-preview-silhouette") != nullptr;
	const int shape = filled > 0 && silhouette ? BuildWeaponShape(weapon) : 0;
	if (const auto* g = WeaponShapeOf(shape)) {
		const auto& side = CraftPreviewSilhouette(shape, *g);
		const float cell = std::min((kPreview.w - 16) / float(g->nz), (kPreview.h - 28) / float(g->ny)),
		            ox = kPreview.x + (kPreview.w - cell * float(g->nz)) * .5f, oy = kPreview.y + kPreview.h - 8;
		for (int z = 0; z < g->nz; ++z)
			for (int y = 0; y < g->ny; ++y)
				if (side[size_t(z * g->ny + y)])
					ui.Fill(
					    {ox + float(z) * cell, oy - float(y + 1) * cell, cell + .5f, cell + .5f},
					    Engine::Vector4{.25f, .55f, .85f, 1});
		// (F4) 材料の物が体の中のどこに浮かぶか: その所に物の絵を重ねる
		// (本体の物が隠れないよう、本体を最後に重ねる)
		for (int i = std::min(g->anchors, filled); i-- > 0;) {
			const std::string image = ImageOf(weapon.ids[size_t(i)]);
			if (image.empty())
				continue;
			const float size = std::clamp(g->anchorFit[size_t(i)] * 2 / g->cell * cell, 18.f, 52.f),
			            px = ox + (g->anchor[size_t(i)].z - g->lo.z) / g->cell * cell,
			            py = oy - (g->anchor[size_t(i)].y - g->lo.y) / g->cell * cell;
			ui.Photo(image, {px - size * .5f, py - size * .5f, size, size});
		}
	}
}

void ChronoSystem::DrawWeaponCraftTraits(UI::Canvas& ui, const BattleWeapon& weapon) const {
	using namespace UI::Scale;
	const int filled = weapon.filled;
	// 性質名と、レベルが2以上の場合の倍率を表示する。
	float x = kCard.x + 16, y = kCard.y + 140;
	ui.Text("性質", x, y, 17, Black, Jp);
	y += 26;
	bool any = false;
	for (int tr = 0; tr < FuseTraitCount; ++tr) {
		const int level = weapon.lv[size_t(tr)];
		if (level <= 0)
			continue;
		any = true;
		const std::string label =
		    std::string(FuseTraitName(FuseTrait(tr))) + (level > 1 ? " x" + std::to_string(level) : "");
		const float w = ui.Width(label, 15, Jp) + 14;
		const float right = y < kPreview.y + kPreview.h ? kPreview.x - 8 : kCard.x + kCard.w - 14;
		if (x + w > right) {
			x = kCard.x + 16;
			y += 30;
		}
		ui.Rounded({x, y, w, 25}, TraitColor(tr), 7);
		ui.Text(label, x + 7, y + 2, 15, White, Jp);
		x += w + 6;
	}
	if (!any)
		ui.Text(filled > 0 ? "性質なし" : "材料を入れると並ぶ", kCard.x + 16, y, 15, UI::WithAlpha(Black, .5f), Jp);
	// どう動くか (性質から作る説明)。見本の窓の左の幅で、言葉の切れ目で行を分ける (強化の行の上まで)
	if (filled > 0) {
		const auto words = FuseWeaponDescriptionPhrases(weapon.lv);
		const float width = kPreview.x - 8 - (kCard.x + 16), size = 15, step = 20, bottom = kPreview.y + kPreview.h;
		std::vector<std::string> lines(1);
		for (size_t i = 0; i < words.size(); ++i) {
			const std::string w = words[i] + (i + 1 < words.size() ? "、" : "");
			if (!lines.back().empty() && ui.Width(lines.back() + w, size, Jp) > width)
				lines.emplace_back();
			lines.back() += w;
		}
		float ly = y + 36;
		for (const auto& l : lines) {
			if (ly + step > bottom)
				break;
			ui.Text(l, kCard.x + 16, ly, size, Black, Jp);
			ly += step;
		}
	}
}

void ChronoSystem::DrawWeaponCraftBoosts(UI::Canvas& ui, const BattleWeapon& weapon) const {
	using namespace UI::Scale;
	const auto& c = battle_.craft;
	const bool open = c.step == 2;
	int spent = 0;
	for (int v : c.alloc)
		spent += v;
	// 強化 (弾数・威力・連射): − レベル (次のレベルまでの貯め) 今の値 ＋。決定するまでは押せない
	for (int k = 0; k < CraftBoostCount; ++k) {
		const auto row = CraftBoostRow(k);
		const int points = weapon.points[size_t(k)], cap = weapon.caps[size_t(k)];
		const int level = CraftLevelOf(points, cap);
		const bool full = level >= CraftLevels, active = open && k == c.boost;
		ui.Rounded(row, active ? UI::WithAlpha(Red, .16f) : UI::WithAlpha(Black, .06f), 8);
		if (active)
			ui.RoundedRing(row, Red, 8);
		const Engine::Vector4 ink = open ? Black : UI::WithAlpha(Black, .4f);
		ui.Text(CraftBoostName(k), row.x + 8, row.y + 8, 19, ink, Jp);
		// − と ＋ (押せないときは薄く)
		// (記号は文字ではなく線で描く: 小さくならないように)
		auto button = [&](UI::Rect r, bool plus, bool can) {
			const bool down = open && c.holdRow == k && c.holdDir == (plus ? 1 : -1);
			ui.Rounded(r, can ? (down ? Red : Black) : UI::WithAlpha(Black, .15f), 7);
			const Engine::Vector4 mark = can ? Yellow : UI::WithAlpha(White, .8f);
			const float cx = r.x + r.w * .5f, cy = r.y + r.h * .5f, half = 8, thick = 3;
			ui.Fill({cx - half, cy - thick * .5f, half * 2, thick}, mark);
			if (plus)
				ui.Fill({cx - thick * .5f, cy - half, thick, half * 2}, mark);
		};
		button(CraftMinusButton(k), false, open && points > 0);
		button(CraftPlusButton(k), true, open && !full && spent < c.energy);
		// レベル (上がった瞬間は大きく光る)
		const float flash = std::clamp(1 - c.levelFlash[size_t(k)] / .45f, 0.f, 1.f);
		const std::string lv = full ? "MAX" : "Lv" + std::to_string(level);
		ui.Text(lv, row.x + 92, row.y + 6 - 3 * flash, 22 + 6 * flash, full ? Red : flash > 0 ? Red : ink, Num);
		// 次のレベルまでの貯め (バー) と「あと何」
		const UI::Rect bar{row.x + 150, row.y + 10, 110, 10};
		const int from = CraftLevelNeed(level, cap), to = CraftLevelNeed(level + 1, cap);
		const float fill = full ? 1.f : float(points - from) / float(std::max(1, to - from));
		const Engine::Vector4 color = k == 0   ? Engine::Vector4{.24f, .55f, .86f, 1}
		                              : k == 1 ? Engine::Vector4{.94f, .47f, .16f, 1}
		                                       : Engine::Vector4{.35f, .71f, .31f, 1};
		ui.Fill(bar, UI::WithAlpha(Black, .18f));
		ui.Fill({bar.x, bar.y, bar.w * std::clamp(fill, 0.f, 1.f), bar.h}, open ? color : UI::WithAlpha(color, .4f));
		// (レベルの区切りの目盛り)
		for (int s = 1; s < CraftLevels; ++s)
			ui.Fill(
			    {bar.x + bar.w * float(s) / float(CraftLevels) - .5f, bar.y + bar.h + 2, 1, 3},
			    UI::WithAlpha(Black, .25f));
		ui.Fill({bar.x, bar.y + bar.h + 2, bar.w * float(level) / float(CraftLevels), 3}, color);
		ui.Text(
		    full ? std::string("最大") : "次の Lv まで " + std::to_string(to - points) + "cm", bar.x, row.y + 24, 12,
		    UI::WithAlpha(Black, open ? .65f : .35f), Jp);
		// 今の値
		char value[48];
		const char* unit = "";
		if (k == 0) {
			// (撃ち方の数に足した、1 回で撃つ数)
			snprintf(value, sizeof(value), "%d", FuseShotCount(FusePatternOf(weapon.lv), points, cap));
			unit = "発";
		} else if (k == 1) {
			snprintf(value, sizeof(value), "x%.1f", CraftPower(points, cap));
		} else {
			const float iv = CraftInterval(points, weapon.lv[size_t(FuseTrait::Heavy)], cap) *
			                 FusePatternInterval(FusePatternOf(weapon.lv));
			snprintf(value, sizeof(value), "%.1f", 1 / iv);
			unit = "回/秒";
		}
		ui.Text(value, row.x + 272, row.y + 3, 24, ink, Num);
		ui.Text(unit, row.x + 274 + ui.Width(value, 24, Num), row.y + 12, 14, ink, Jp);
	}
}

void ChronoSystem::DrawWeaponCraft(GameContext& ctx) {
	using namespace UI::Scale;
	UI::Canvas ui(ctx.renderer, ctx.viewportSize.x, ctx.viewportSize.y);
	const auto& c = battle_.craft;
	const float t = c.age;
	FillAround(ui, {0, 0, 1280, 720}, kPreview, UI::WithAlpha(Yellow, .95f));
	ui.Big("変形武器をつくろう", 40, 10, 64, Black, JpBig, JpBigBase);
	ui.Text(
	    c.step == 0 ? "① 混ぜる物を 3 つ選んで「決定」。性質が全部まざる。残りの物は全部 cm になる (大きい物ほど多い)"
	                : "② 集めた cm を 弾数・威力・連射 に割り振ろう。レベルが上がると強くなる (戻って選び直せる)",
	    44, 90, 19, Black, Jp);
	DrawWeaponCraftInventory(ui);
	const BattleWeapon weapon = CraftedWeapon();
	DrawWeaponCraftSockets(ui, weapon);
	DrawWeaponCraftPreview(ui, weapon);
	DrawWeaponCraftTraits(ui, weapon);
	DrawWeaponCraftBoosts(ui, weapon);
	// (左下は最後に描く: 光の玉が札の上を飛ぶように)
	DrawWeaponCraftSelection(ui, weapon.filled);
	// ---- 右下: 材料を選ぶ間は おまかせ・決定、割り振る間は 戻る・挑戦 (演出の間は出さない) ----
	if (c.step == 1)
		return;
	Plate(ui, kAutoButton, Cream);
	ui.Text(
	    c.step == 0 ? "おまかせ" : "戻る", kAutoButton.x + (c.step == 0 ? 40 : 62), kAutoButton.y + 10, 24, Black, Jp);
	const float pulse = .5f + .5f * std::sin(t * 5);
	const bool ready = c.step == 2 || weapon.filled >= CraftParts;
	// (材料がそろうまでは、薄い札に薄い文字)
	Plate(ui, kGoButton, ready ? Red : Cream, 3, ready ? 5 + 2 * pulse : 3);
	ui.Text(
	    c.step == 0 ? "決定！" : "これで挑戦！", kGoButton.x + (c.step == 0 ? 98 : 56), kGoButton.y + 9, 26,
	    ready ? White : UI::WithAlpha(Black, .3f), Jp);
}

// ---- チャレンジで使う ----
// 弾を 1 つ作る: 性質で速さ・大きさ・当たりが決まる (撃ち方ごとの置き方と飛び方は、呼んだ側で変える)
ChronoSystem::BattleShot ChronoSystem::MakeFusedShot(float yaw) const {
	const auto& b = battle_;
	const auto& w = b.weapon;
	const auto& body = life_.bodies[0];
	const float unit = b.unit, R = DevourBodyRadius(body.mass);
	auto L = [&](FuseTrait t) { return w.lv[size_t(t)]; };
	const float power = CraftPower(w.points[1], w.caps[1]);
	const bool roll = L(FuseTrait::Roll) > 0,
	           flying = !roll && (L(FuseTrait::Homing) > 0 || L(FuseTrait::Float) > 0 || L(FuseTrait::Return) > 0);
	const float speed = unit * FuseSpeed * (1 + FuseSpeedStep * float(L(FuseTrait::Speed))) /
	                    (1 + FuseHeavySlow * float(L(FuseTrait::Heavy))) *
	                    (L(FuseTrait::Float) > 0 ? FuseFloatSlow : 1.f);
	const float radius = unit * FuseRadius * BattleItemScale(unit, w.items[0].radius) * std::sqrt(power) *
	                     (1 + FuseHeavyGrow * float(L(FuseTrait::Heavy)));
	const V dir = Forward(yaw, 0);
	BattleShot s;
	s.fused = true;
	s.lv = w.lv;
	s.pattern = FusePatternOf(w.lv);
	s.mode = BattleAction::Pierce;
	s.by = BattleAction::Throw;
	s.visual = w.items[0].visual;
	s.radius = radius;
	s.size = radius * 2.2f;
	s.power = power;
	s.at = body.at + dir * (R * .9f);
	{
		// (撃ち出す所の地面は、体より少し上の面まで見る: 上り坂や段の前では、その面の上から撃ち出す (坂の中から出すと、すぐ当たって消える))
		const float ground = InkGround(V{s.at.x, body.at.y + unit * BlocksEdgeRise, s.at.z});
		s.at.y = std::max(body.at.y + R * .45f, ground + std::max(unit * FuseLaunchHeight, radius * .6f));
	}
	// (山なりの弾の上がり方: 一番高い所が FuseArcApex を越えない。速い弾が間の積み木を飛び越えないように)
	const float gravity = unit * FuseGravity * (L(FuseTrait::Float) > 0 ? .35f : 1.f);
	const float lift = std::min(speed * .28f, std::sqrt(2 * gravity * unit * FuseArcApex));
	s.velocity = dir * speed + V{0, roll || flying ? 0.f : lift, 0};
	if (roll) {
		s.rolling = true;
		s.at.y = InkGround(s.at) + radius;
	}
	s.damage = BattleThrowDamage * ChallengePowerNow() * power * (1 + FuseDamageHeavy * float(L(FuseTrait::Heavy)));
	s.hits = 1 + FusePierceHits * L(FuseTrait::Pierce) + L(FuseTrait::Heavy);
	s.bounces = FuseBounces * L(FuseTrait::Bounce);
	s.chain = FuseChainHops * L(FuseTrait::Chain);
	s.life = FuseLife + FuseFloatLife * float(L(FuseTrait::Float)) + FuseBounceLife * float(L(FuseTrait::Bounce)) +
	         FuseReturnLife * float(L(FuseTrait::Return)) + (roll ? 1.f : 0.f);
	s.push = 3 * (1 + float(L(FuseTrait::Heavy)));
	return s;
}

// 撃つ向き: スライムの体の向き (カメラは自由に回せる。撃つ向きはスライムが向いている方)
// 狙いの補正: 追尾の性質があれば、体の向きの前 FuseAimCone の中で一番近い相手へ向ける
float ChronoSystem::FusedAimYaw() const {
	const auto& b = battle_;
	const V facing = life_.bodies[0].forward;
	const float base = std::atan2(facing.x, facing.z);
	if (b.weapon.lv[size_t(FuseTrait::Homing)] <= 0)
		return base;
	const V from = life_.bodies[0].at;
	float best = 1e9f, aim = base;
	for (const auto& t : CollectBattleTargets()) {
		V to = t.at - from;
		to.y = 0;
		const float d = Length(to);
		if (d < .01f || d > b.unit * 25 || d >= best)
			continue;
		const float a = std::atan2(to.x, to.z);
		if (std::abs(std::remainder(a - base, 6.2831853f)) > FuseAimCone)
			continue;
		best = d;
		aim = a;
	}
	return aim;
}

float ChronoSystem::WeaponMoveScale() const {
	return WeaponsActive() && battle_.weapon.filled > 0 ? FuseMoveScale(battle_.weapon.lv) : 1.f;
}

// 撃つ: 性質の組み合わせで決まる撃ち方 (FusePatternOf) で、1 回の数だけ撃つ (回数の制限はない。連射の間隔ごと)。体は武器の形になる
void ChronoSystem::UseBattleWeapon(GameContext&) {
	auto& b = battle_;
	auto& w = b.weapon;
	if (w.filled <= 0 || w.cool > 0)
		return;
	auto& body = life_.bodies[0];
	const float unit = b.unit, R = DevourBodyRadius(body.mass);
	auto L = [&](FuseTrait t) { return w.lv[size_t(t)]; };
	const FusePattern pattern = FusePatternOf(w.lv);
	const int shots = FuseShotCount(pattern, w.points[0], w.caps[0]);
	const float interval = CraftInterval(w.points[2], L(FuseTrait::Heavy), w.caps[2]) * FusePatternInterval(pattern);
	w.cool = interval;
	const float aim = FusedAimYaw();
	auto room = [&] { return FuseShotLimit - int(b.shots.size()); };
	// 場に残る撃ち方 (衛星・罠) は数に上限があり、超えたら古い物から終わらせる
	// (古い物は静かに消す: 爆発・分裂・壁は本来の終わり方 (残る秒が切れた・当たった) のときだけ。
	//  ここで終わらせると、出すたびに入れ替わる分が爆発し続ける)
	auto makeRoom = [&](FusePattern p, int adding, int limit) {
		int over = adding - limit;
		for (const auto& s : b.shots)
			if (s.fused && s.pattern == p && s.age < 99)
				++over;
		for (auto& s : b.shots)
			if (over > 0 && s.fused && s.pattern == p && s.age < 99) {
				b.fx.push_back({s.at, s.radius * 1.2f, 0, .2f, {.8f, .95f, 1, .6f}});
				s.age = 99;
				--over;
			}
	};
	auto push = [&](const BattleShot& s) {
		if (room() > 0)
			b.shots.push_back(s);
	};
	const V front = Forward(aim, 0), right{front.z, 0, -front.x};
	switch (pattern) {
	case FusePattern::Single:
	case FusePattern::Fan: {
		// 一発は (強化で増えた分を) 狭く、扇は広く並べる
		const float step = pattern == FusePattern::Fan ? CraftSpreadStep * 2.2f : CraftSpreadStep * .6f;
		const float spread = std::min(CraftSpreadMax, step * float(shots - 1));
		for (int k = 0; k < shots; ++k)
			push(MakeFusedShot(aim + (shots > 1 ? -spread * .5f + spread * float(k) / float(shots - 1) : 0.f)));
		break;
	}
	case FusePattern::Burst:
		// 1 発目は今、残りは FuseBurstGap ごとに同じ向きへ (TickBattleWeapons)
		push(MakeFusedShot(aim));
		w.burstLeft = shots - 1;
		w.burstClock = FuseBurstGap;
		break;
	case FusePattern::Ring:
		for (int k = 0; k < shots; ++k) {
			BattleShot s = MakeFusedShot(aim + 6.2831853f * float(k) / float(shots));
			s.velocity.y *= .5f;
			push(s);
		}
		break;
	case FusePattern::Orbit: {
		// 体のまわりを回る衛星
		const int n = std::min(shots, FuseOrbitLimit);
		makeRoom(FusePattern::Orbit, n, FuseOrbitLimit);
		const float base = life_.age * FuseOrbitTurn;
		for (int k = 0; k < n; ++k) {
			BattleShot s = MakeFusedShot(aim);
			s.orbitAngle = base + 6.2831853f * float(k) / float(n);
			s.orbitRadius = R * FuseOrbitRadius * (1 + .25f * float(L(FuseTrait::Magnet)));
			s.life = FuseOrbitLife + FuseFloatLife * float(L(FuseTrait::Float));
			s.hits = 9999;
			s.rolling = false;
			push(s);
		}
		break;
	}
	case FusePattern::Mortar:
		// 狙った向きの遠くへ、山なりに落とす (大きく、落ちた所で爆発)
		for (int k = 0; k < shots; ++k) {
			BattleShot s = MakeFusedShot(aim);
			s.rolling = false;
			s.radius *= 1.3f;
			s.size *= 1.3f;
			s.power *= 1.3f;
			s.damage *= 1.2f;
			s.bounces = 0;
			const float side = shots > 1 ? (float(k) / float(shots - 1) - .5f) * unit * 4 : 0.f;
			const V to =
			    body.at + front * (unit * FuseMortarReach * (1 + .15f * float(L(FuseTrait::Heavy)))) + right * side;
			const float g = unit * FuseGravity * (L(FuseTrait::Float) > 0 ? .35f : 1.f), T = FuseMortarTime;
			const V d = to - s.at;
			// (着くのは弾の下の面が地面に触れたとき (真ん中が半径の半分の高さ)。真ん中を地面に合わせると、大きな弾ほど手前に落ちる)
			const float dy = InkGround(to) + s.radius * .5f - s.at.y;
			s.velocity = {d.x / T, (dy + .5f * g * T * T) / T, d.z / T};
			s.life = T + 2;
			push(s);
		}
		break;
	case FusePattern::Rain: {
		// 近い相手の上から真下へ。相手が足りなければ前の広い所へ散らす
		std::vector<uint32_t> used;
		for (int k = 0; k < shots; ++k) {
			BattleShot s = MakeFusedShot(aim);
			BattleTarget t;
			V at;
			if (NearestBattleTarget(body.at, unit * 22, t, &used)) {
				used.push_back(t.id);
				at = t.at;
			} else {
				const float a = aim + (float(k % 5) - 2) * .35f, dist = unit * (5 + 2.5f * float(k / 5 + (k % 2)));
				at = body.at + Forward(a, 0) * dist;
			}
			s.at = {at.x, InkGround(at) + unit * FuseRainHeight * (1 + .15f * float(k)), at.z};
			s.velocity = {0, -unit * 16, 0};
			s.rolling = false;
			s.bounces = 0;
			push(s);
		}
		break;
	}
	case FusePattern::Lance:
		// まっすぐ速く、長い間突き抜ける (重力なし)
		for (int k = 0; k < shots; ++k) {
			BattleShot s = MakeFusedShot(aim + (shots > 1 ? (float(k) / float(shots - 1) - .5f) * .12f : 0.f));
			s.velocity = V{s.velocity.x, 0, s.velocity.z} * 1.7f;
			s.rolling = false;
			s.hits += 3;
			s.life = 1.4f;
			s.size *= 1.25f;
			push(s);
		}
		break;
	case FusePattern::Mine:
		// 前の少し先の床に置く。相手が近づくと効く (止まった的にも届くように、前へ並べる)
		makeRoom(FusePattern::Mine, shots, FuseMineLimit);
		for (int k = 0; k < shots; ++k) {
			BattleShot s = MakeFusedShot(aim);
			const float a = aim + (shots > 1 ? (float(k) / float(shots - 1) - .5f) * 1.2f : 0.f);
			const V at = body.at + Forward(a, 0) * (R * 1.4f + unit * (2.5f + .8f * float(k % 3)));
			s.at = {at.x, InkGround(at) + s.radius, at.z};
			s.velocity = {};
			s.rolling = false;
			s.bounces = 0;
			s.life = FuseMineLife;
			push(s);
		}
		break;
	case FusePattern::Twin:
		// 左右へ開いて撃ち、内へ曲がって前で交わる (戻る性質で、そのあと帰ってくる)
		for (int k = 0; k < shots; ++k) {
			const float sideSign = k % 2 ? 1.f : -1.f, wide = .75f + .12f * float(k / 2);
			BattleShot s = MakeFusedShot(aim + sideSign * wide);
			s.velocity.y = 0;
			s.rolling = false;
			s.curve = -sideSign * FuseTwinCurve;
			push(s);
		}
		break;
	default:
		push(MakeFusedShot(aim));
		break;
	}
	// (撃ち続けている間は形を保つ: 次の弾までの間 + CraftFormHold。間に液体へ戻ると、そのたびに平たく広がって見える)
	b.weaponShape = w.shape;
	b.weaponForm = std::max(CraftFormTime, interval + CraftFormHold);
	// (体の向きは変えない: 撃つたびにカメラの向きへ向けると、横に動きながら撃つとき移動の向きと行き来してガクガクする)
	++b.spent;
	// (F5) 反動: 1 発ごとに前後に縮む。速く連射するほど 1 発の反動は小さい (震えすぎないように)
	b.weaponKick = std::max(
	    b.weaponKick, CraftKick * std::clamp(interval / .25f, .25f, 1.f) * (1 + .15f * float(L(FuseTrait::Heavy))));
	const float pitch = pattern == FusePattern::Mortar  ? .7f
	                    : pattern == FusePattern::Lance ? 1.7f
	                    : pattern == FusePattern::Mine  ? .9f
	                                                    : 1.3f;
	// (作る画面の見本は小さな音で。連射の見本で鳴りっぱなしにならないように)
	const float volume = b.phase == BattleStep::Craft ? .12f : std::min(.7f, .35f + .03f * float(shots));
	PlayDevourSound(
	    devourSounds_.pounce, volume, pitch / (1 + .2f * float(L(FuseTrait::Heavy))) * (interval < .12f ? 1.25f : 1.f));
}

// 体の特徴: 体当たり (回る・重い・熱い・止める) と、歩いた跡を塗る (塗り)
void ChronoSystem::TickWeaponBody(GameContext& ctx, float dt) {
	auto& b = battle_;
	auto& w = b.weapon;
	if (w.filled <= 0 || b.phase != BattleStep::Fight)
		return;
	const auto& body = life_.bodies[0];
	const float unit = b.unit, R = DevourBodyRadius(body.mass);
	auto L = [&](FuseTrait t) { return w.lv[size_t(t)]; };
	w.bodyHitClock -= dt;
	if (FuseBodyHits(w.lv) && w.bodyHitClock <= 0) {
		w.bodyHitClock = FuseBodyHitEvery;
		const float power = CraftPower(w.points[1], w.caps[1]);
		BattleImpact i;
		i.action = BattleAction::Throw;
		i.effect = ImpactEffect::Hit;
		i.shape = ImpactShape::Sphere;
		i.at = body.at;
		i.reach = R * (1.05f + .2f * float(L(FuseTrait::Spin)));
		i.damage = BattleThrowDamage * ChallengePowerNow() * power * FuseBodyHitDamage *
		           (1 + .5f * float(L(FuseTrait::Spin) + L(FuseTrait::Heavy)));
		i.push = 2 + 2.5f * float(L(FuseTrait::Heavy));
		i.radial = true;
		std::vector<BattleTarget> touched;
		if (EmitBattleImpact(ctx, i, &touched) > 0) {
			b.fx.push_back({body.at, i.reach, 0, .2f, {1, .8f, .4f, .5f}});
			for (const auto& t : touched) {
				auto extra = [&](ImpactEffect e, float seconds, float damage) {
					BattleImpact x;
					x.action = BattleAction::Throw;
					x.effect = e;
					x.at = t.at;
					x.reach = t.radius + unit;
					x.only = t.id;
					x.limit = 1;
					x.seconds = seconds;
					x.damage = damage;
					EmitBattleImpact(ctx, x);
				};
				if (L(FuseTrait::Stun) > 0)
					ShakeAround(ctx, t, i.damage * FuseShakeShare, L(FuseTrait::Stun));
				if (L(FuseTrait::Burn) > 0)
					extra(ImpactEffect::Burn, BattleBurnTime, BattleBurnDps * power * float(L(FuseTrait::Burn)));
			}
		}
	}
}

void ChronoSystem::TickBattleWeapons(GameContext& ctx, float dt) {
	auto& b = battle_;
	// 確認用 (--weapon-facing-log): チャレンジの間、毎フレームの体の向き (度)・カメラの向き (度)・撃った数を
	// tests/out/weapon-facing.txt に書く (横に動きながら撃ってガクガクしないかを、向きの 1 フレームの変わり方で確かめる)
	if (b.phase == BattleStep::Fight && HasArg(L"--weapon-facing-log")) {
		static std::ofstream log;
		if (!log.is_open()) {
			std::filesystem::create_directories("tests/out");
			log.open("tests/out/weapon-facing.txt");
		}
		const V f = life_.bodies[0].forward;
		// (後ろの 3 つ: 撃つボタンを押しているか・次に撃てるまでの秒・連打を覚えている残りの秒)
		log << b.clock << ' ' << std::atan2(f.x, f.z) * 57.29578f << ' ' << yaw_ * 57.29578f << ' ' << b.spent << ' '
		    << (Controls::MorphAttack() ? 1 : 0) << ' ' << b.weapon.cool << ' ' << b.weapon.tapBuffer << '\n';
	}
	b.weapon.cool = std::max(0.f, b.weapon.cool - dt);
	// 連なり撃ちの残り: 同じ向き (今の狙い) へ続けて撃つ (作る画面の見本でも: そのときは見本の弾の並びに足す)
	if (b.weapon.burstLeft > 0) {
		b.weapon.burstClock -= dt;
		while (b.weapon.burstLeft > 0 && b.weapon.burstClock <= 0) {
			b.weapon.burstClock += FuseBurstGap;
			--b.weapon.burstLeft;
			if (int(b.shots.size()) < FuseShotLimit)
				b.shots.push_back(MakeFusedShot(FusedAimYaw()));
		}
	}
	if (b.phase != BattleStep::Craft)
		TickWeaponBody(ctx, dt);
	b.weaponForm = std::max(0.f, b.weaponForm - dt);
	// 武器を作ったら、撃たなくてもチャレンジの間はずっと武器の形でいる (撃つたびに形になるのではなく、作った武器の姿で動き回る)
	if (b.weapon.shape > 0 && b.phase != BattleStep::Craft) {
		b.weaponShape = b.weapon.shape;
		b.weaponForm = std::max(b.weaponForm, 1.f);
	}
	if (b.weaponForm <= 0) {
		b.weaponShape = 0;
		b.weaponAge = 0;
	} else
		b.weaponAge += dt;
	// (F5) 姿勢: 翼ははばたき、羽根は回り、足と分身の玉は弾み、炎はゆらぐ。撃つたびに前後に縮む反動。
	// 形になるときは小さい所から少し大きくなりすぎて戻り (ぽんと出てくる)、形が終わるときは少し縮む
	{
		b.weaponPosePrevious = b.weaponPose;
		auto& pose = b.weaponPose;
		const float t = b.weaponAge;
		pose.roll = CraftFlap * std::sin(t * 8);
		pose.spin += dt * 20;
		pose.tip = std::sin(t * 7);
		pose.pitch = std::sin(t * 23) * .7f + std::sin(t * 13) * .3f;
		b.weaponKick *= std::exp(-14 * dt);
		pose.squash = b.weaponKick;
		const float in = std::clamp(t / CraftPopTime, 0.f, 1.f), out = std::clamp(b.weaponForm / .12f, 0.f, 1.f);
		pose.scale = (in < 1 ? .72f + .28f * in + .16f * std::sin(3.1415927f * in) : 1.f) * (.85f + .15f * out);
		if (b.weaponShape == 0)
			pose = {};
	}
}

// 爆発と分裂 (当たったとき・落ちたとき)。分裂は 1 回だけ (分かれた弾はもう分かれない)
void ChronoSystem::BurstFusedShot(GameContext& ctx, BattleShot& s, bool landed) {
	auto& b = battle_;
	const float unit = b.unit;
	auto L = [&](FuseTrait t) { return s.lv[size_t(t)]; };
	if (L(FuseTrait::Explode) > 0) {
		const float reach =
		    unit * (FuseExplodeReach + FuseExplodeStep * float(L(FuseTrait::Explode) - 1)) * std::sqrt(s.power);
		BattleImpact i;
		i.action = BattleAction::Throw;
		i.effect = ImpactEffect::Hit;
		i.shape = ImpactShape::Sphere;
		i.at = s.at;
		i.reach = reach;
		i.damage = s.damage * .8f;
		i.push = 3;
		i.radial = true;
		EmitBattleImpact(ctx, i);
		b.fx.push_back({s.at, reach, 0, .3f, {1, .6f, .25f, .6f}});
		// (作る画面の見本では、地面の揺れと粒は出さない: 光の輪だけ)
		if (b.phase != BattleStep::Craft)
			LifeImpact(ctx, ImpactStomp, s.at, {0, 1, 0}, std::min(1.f, .3f + reach / (unit * 6)));
	}
	if (L(FuseTrait::Split) > 0 && !s.split) {
		s.split = true;
		const int pieces = FuseSplitPieces + L(FuseTrait::Split) - 1;
		V flat{s.velocity.x, 0, s.velocity.z};
		const float speed = std::max(Length(flat), unit * 8);
		const float face = std::atan2(flat.x, flat.z);
		for (int k = 0; k < pieces; ++k) {
			if (int(b.shots.size() + b.newShots.size()) >= FuseShotLimit)
				break;
			BattleShot c = s;
			c.split = true;
			c.lv[size_t(FuseTrait::Split)] = 0;
			c.radius *= FuseSplitScale;
			c.size *= FuseSplitScale;
			c.damage *= .6f;
			c.age = 0;
			c.life = std::max(.8f, s.life * .5f);
			c.hits = std::max(1, s.hits);
			const float a = face + (float(k) / float(std::max(1, pieces - 1)) - .5f) * 2.2f;
			const V d = Forward(a, 0);
			c.velocity = d * speed + V{0, c.rolling ? 0.f : unit * (landed ? 5.f : 2.f), 0};
			c.at = s.at + d * (s.radius * .6f);
			b.newShots.push_back(c);
		}
	}
}

// 揺らす: 当たった積み木 (target) のまわりの積み木にも当てる (当たった積み木には当てない)。まわりの積み木が崩れると、上の積み木も落ちる
void ChronoSystem::ShakeAround(GameContext& ctx, const BattleTarget& target, float damage, int level) {
	const float unit = battle_.unit;
	BattleImpact i;
	i.action = BattleAction::Throw;
	i.effect = ImpactEffect::Hit;
	i.shape = ImpactShape::Sphere;
	i.at = target.at;
	i.reach = target.radius + unit * (FuseShakeReach + FuseShakeStep * float(level - 1));
	i.damage = damage;
	i.push = 1.5f;
	i.radial = true;
	const std::vector<uint32_t> skip{target.id};
	// (文字は当たった所に 1 つだけ。まわりの積み木ごとに出すと画面が文字だらけになる)
	if (EmitBattleImpact(ctx, i, nullptr, &skip) > 0) {
		battle_.fx.push_back({target.at, i.reach, 0, .25f, {.9f, .8f, .55f, .5f}});
		battle_.pops.push_back({target.at + V{0, target.radius, 0}, "ぐらっ！", 0, {.95f, .8f, .45f, 1}});
	}
}
// ひび: 当たった積み木とそのまわりの積み木にひびを入れる (ひびの間は、受ける当たりが ChallengeFragileTake 倍で、硬さを無視する)
void ChronoSystem::CrackAround(GameContext& ctx, const BattleTarget& target) {
	BattleImpact i;
	i.action = BattleAction::Throw;
	i.effect = ImpactEffect::Weaken;
	i.at = target.at;
	i.reach = target.radius + battle_.unit * FuseCrackReach;
	i.seconds = BattleWeakTime;
	if (EmitBattleImpact(ctx, i) > 0)
		battle_.pops.push_back({target.at + V{0, target.radius, 0}, "ピシッ", 0, {.85f, .9f, 1, 1}});
}

// 弾が終わる: 爆発・分裂・壁 (止まった所に積み木の塔。多すぎないように 6 つまで)
void ChronoSystem::EndFusedShot(GameContext& ctx, BattleShot& s) {
	auto& b = battle_;
	if (s.age >= 99)
		return;
	BurstFusedShot(ctx, s, true);
	// (作る画面の見本では壁を建てない: チャレンジの場に残ってしまう)
	if (s.lv[size_t(FuseTrait::Wall)] > 0 && b.phase != BattleStep::Craft) {
		int towers = 0;
		for (const auto& pl : b.placed)
			if (pl.kind == BattleAction::Tower)
				++towers;
		if (towers < 6) {
			const float R = DevourBodyRadius(life_.bodies[0].mass);
			BattlePlaced t;
			t.kind = BattleAction::Tower;
			t.visual = s.visual;
			t.itemRadius = s.radius;
			t.at = s.at;
			t.at.y = InkGround(t.at);
			V f{s.velocity.x, 0, s.velocity.z};
			t.dir = Length(f) > .01f ? Unit(f) : V{0, 0, 1};
			t.radius = R * BattleTowerHalf * .7f * float(s.lv[size_t(FuseTrait::Wall)]);
			t.size = std::max(R * .5f, b.unit * .45f);
			t.life = BattleTowerLife;
			t.power = s.power;
			b.placed.push_back(t);
			BattleImpact z;
			z.action = BattleAction::Tower;
			z.effect = ImpactEffect::Zone;
			z.at = t.at;
			z.reach = t.radius;
			EmitBattleImpact(ctx, z);
		}
	}
	s.age = 99;
}

// 変形武器の弾: 性質ごとに動き、当たる
void ChronoSystem::TickFusedShot(GameContext& ctx, BattleShot& s, float dt) {
	auto& b = battle_;
	const auto& body = life_.bodies[0];
	const float unit = b.unit, R = DevourBodyRadius(body.mass);
	auto L = [&](FuseTrait t) { return s.lv[size_t(t)]; };
	if (s.age >= 99)
		return;
	s.age += dt;
	s.hitCool = std::max(0.f, s.hitCool - dt);
	s.spin += dt * (10 + 12 * float(L(FuseTrait::Spin)));
	const FusePattern pattern = s.pattern;
	// 衛星: 体のまわりを回る (重力・床・縁は無い)。回りながら、当たった相手にも少しあとでまた当たれる
	if (pattern == FusePattern::Orbit) {
		s.orbitAngle += dt * FuseOrbitTurn * (1 + .25f * float(L(FuseTrait::Speed)));
		const V around{std::sin(s.orbitAngle), 0, std::cos(s.orbitAngle)};
		const V tangent{around.z, 0, -around.x};
		const float wobble = L(FuseTrait::Float) > 0 ? .3f * R * std::sin(s.age * 3 + s.orbitAngle) : 0.f;
		s.at = body.at + around * s.orbitRadius + V{0, R * .2f + wobble, 0};
		s.velocity = tangent * (-FuseOrbitTurn * s.orbitRadius);
		if (std::fmod(s.age, .6f) < dt)
			s.hit.clear();
	}
	// 置き罠: 止まって待ち、相手が近づくと効く
	bool triggered = false;
	if (pattern == FusePattern::Mine) {
		s.velocity = {};
		s.at.y = InkGround(s.at) + s.radius;
		BattleTarget close;
		triggered = s.age > .25f && NearestBattleTarget(s.at, unit * FuseMineReach, close);
		if (std::fmod(s.age, .5f) < dt)
			b.fx.push_back({s.at, unit * FuseMineReach * .7f, 0, .3f, {1, .45f, .35f, .35f}});
	}
	// 挟み撃ち: 開いた向きから内へ曲がる (前で交わったら、まっすぐに)
	if (s.curve != 0) {
		if (s.age > .8f)
			s.curve = 0;
		else {
			const float a = std::atan2(s.velocity.x, s.velocity.z) + s.curve * dt;
			const float sp = Length(V{s.velocity.x, 0, s.velocity.z});
			const V d = Forward(a, 0);
			s.velocity = {d.x * sp, s.velocity.y, d.z * sp};
		}
	}
	const bool stays = pattern == FusePattern::Orbit || pattern == FusePattern::Mine;
	V flat{s.velocity.x, 0, s.velocity.z};
	float speed = Length(flat);
	// (槍はまっすぐ宙を飛ぶ。大砲と降り注ぎは重力で落ちる)
	const bool falls = pattern == FusePattern::Mortar || pattern == FusePattern::Rain;
	// (挟み撃ちも宙を飛ぶ: 落ちると、曲がって前で交わる前に地面に着いて終わる)
	const bool flying = !stays && !falls &&
	                    (pattern == FusePattern::Lance || pattern == FusePattern::Twin ||
	                     (!s.rolling && L(FuseTrait::Roll) == 0 &&
	                      (L(FuseTrait::Homing) > 0 || L(FuseTrait::Float) > 0 || s.returning)));
	// 追いかける: 近い相手へ向きを曲げる (当てた相手は追わない)
	auto steer = [&](V to, float rate) {
		to.y = 0;
		if (speed < .01f || Length(to) < .01f)
			return;
		const float now = std::atan2(flat.x, flat.z), want = std::atan2(to.x, to.z);
		const float turn = std::clamp(std::remainder(want - now, 6.2831853f), -rate * dt, rate * dt);
		const V d = Forward(now + turn, 0);
		s.velocity = {d.x * speed, s.velocity.y, d.z * speed};
		flat = {s.velocity.x, 0, s.velocity.z};
	};
	if (L(FuseTrait::Homing) > 0 && !s.returning && !stays && !falls) {
		BattleTarget target;
		if (NearestBattleTarget(s.at, unit * 30, target, &s.hit))
			steer(target.at - s.at, FuseHomingTurn * float(L(FuseTrait::Homing)));
	}
	// 戻ってくる: 体から FuseReturnReach 離れたら (遅い弾は時間の 7 割を過ぎたら) 体へ戻る (戻ったら消える。行きに当てた相手にもまた当たれる)
	if (L(FuseTrait::Return) > 0 && !stays && !s.returning &&
	    (Length(V{s.at.x - body.at.x, 0, s.at.z - body.at.z}) > unit * FuseReturnReach || s.age > s.life * .7f)) {
		s.returning = true;
		s.hit.clear();
		s.hits = std::max(s.hits, 1);
	}
	if (s.returning) {
		V to = body.at - s.at;
		to.y = 0;
		if (Length(to) < R * 1.2f) {
			s.age = 99;
			return;
		}
		steer(to, 8);
	}
	// 上下: 転がる・浮く・放物線 (衛星と罠は動きを上で決めた)
	if (stays) {
	} else if (s.rolling) {
		s.at.y = InkGround(s.at) + s.radius;
		s.velocity.y = 0;
		if (L(FuseTrait::Roll) == 0) {
			const float k = std::exp(-ChallengeThrowRoll * dt);
			s.velocity.x *= k;
			s.velocity.z *= k;
			if (speed < unit * 1.2f) {
				EndFusedShot(ctx, s);
				return;
			}
		}
	} else if (flying) {
		// (ふわふわの弾も、地面の積み木 1 段に当たれる高さで揺れる)
		const float want =
		    InkGround(s.at) + R * .6f +
		    (L(FuseTrait::Float) > 0 ? unit * (FuseFloatHeight + FuseFloatWave * std::sin(s.age * 3)) : 0.f);
		s.at.y += (want - s.at.y) * std::min(1.f, dt * 4);
		s.velocity.y = 0;
	} else
		s.velocity.y -= unit * FuseGravity * (L(FuseTrait::Float) > 0 ? .35f : 1.f) * dt;
	if (!stays) {
		const V before = s.at;
		s.at = s.at + s.velocity * dt;
		// 積み木の街: 橋・坂・塔・手すりに当たる (速い弾でも薄い橋をすり抜けない)。上から落ちたら面の上に乗る (下の床の処理で跳ねる・転がる)。
		// 壁に当たったら、跳ねる・転がる・戻る弾は跳ね返り、ほかは終わる
		V hit, n;
		bool wall = false;
		if (BlocksSweep(before, s.at, unit * ChallengeBlocksStep, hit, n, wall)) {
			if (L(FuseTrait::Bounce) > 0 && s.bounces > 0 && !s.rolling && !flying) {
				// 跳ねる弾: 当たった面 (床・坂・壁) の向きで跳ね返る (坂でも壁でも同じ決まり)。床や坂では跳ねた数を数え、爆発する弾は爆発する
				const float into = Dot(s.velocity, n);
				if (into < 0) {
					s.velocity = s.velocity - n * (into * 1.75f);
					if (n.y > .3f && Dot(s.velocity, n) < unit * 5)
						s.velocity = s.velocity + n * (unit * 5 - Dot(s.velocity, n));
					s.hit.clear();
				}
				s.at = wall ? hit : hit + V{0, s.radius * .5f, 0};
				if (!wall) {
					--s.bounces;
					if (L(FuseTrait::Explode) > 0)
						BurstFusedShot(ctx, s, true);
				}
			} else if (!wall)
				s.at.y = std::max(s.at.y, hit.y);
			else if (L(FuseTrait::Bounce) > 0 || s.rolling || s.returning) {
				// 跳ね終わった弾・転がる弾・戻る弾: 壁の横の向きで跳ね返る
				s.at = hit;
				V side{n.x, 0, n.z};
				side = Length(side) > 1e-3f ? Unit(side) : V{0, 0, 1};
				const float into = Dot(s.velocity, side);
				if (into < 0) {
					s.velocity = s.velocity - side * (2 * into);
					s.hit.clear();
				}
			} else {
				s.at = hit;
				EndFusedShot(ctx, s);
				return;
			}
		}
	}
	// 積み木の街: 着地しない弾 (体のまわりを回る衛星・宙を飛ぶ弾・転がる弾) は、地面・坂・橋の中に入らない
	// (体の高さに合わせて回る衛星や、地面からの高さへ遅れて追いつく弾が、上り坂や丘の中を通らないように)
	if (BlocksStageActive() && (pattern == FusePattern::Orbit || flying || s.rolling)) {
		const float floor = InkGround(s.at) + s.radius * .5f;
		if (s.at.y < floor)
			s.at.y = floor;
	}
	// 床に落ちた: 跳ねる・転がり始める (当たれる数が残っていれば)・終わる。落ちた所に体液
	if (!s.rolling && !flying && !stays) {
		const float floor = InkGround(s.at) + s.radius * .5f;
		// (着くのは下りているときだけ。上がっている途中に床より下とすると、大きな弾が撃った瞬間に着いて終わる)
		if (s.at.y < floor && (s.velocity.y <= 0 || s.at.y < InkGround(s.at))) {
			s.at.y = floor;
			if (s.bounces > 0) {
				--s.bounces;
				s.velocity.y = std::max(std::abs(s.velocity.y) * .75f, unit * 5);
				if (L(FuseTrait::Explode) > 0)
					BurstFusedShot(ctx, s, true);
			} else if (s.hits > 0 && L(FuseTrait::Explode) == 0 && L(FuseTrait::Split) == 0) {
				s.rolling = true;
				s.velocity.y = 0;
			} else {
				EndFusedShot(ctx, s);
				return;
			}
		}
	}
	// 積み木の街・ステージの縁: 跳ねる・転がる弾は跳ね返る。ほかは終わる
	{
		const V c = b.arenaAt;
		bool out = false;
		V n{};
		// (積み木の街は四角い縁の柵。戦う円の縁で消すと、広い街の途中で弾が見えない壁に当たって消える)
		if (BlocksStageActive()) {
			const float half = ChallengeBlocksHalf * unit - s.radius;
			if (s.at.x - c.x > half) {
				n = {-1, 0, 0};
				out = true;
				s.at.x = c.x + half;
			} else if (c.x - s.at.x > half) {
				n = {1, 0, 0};
				out = true;
				s.at.x = c.x - half;
			}
			if (s.at.z - c.z > half) {
				n = {0, 0, -1};
				out = true;
				s.at.z = c.z + half;
			} else if (c.z - s.at.z > half) {
				n = {0, 0, 1};
				out = true;
				s.at.z = c.z - half;
			}
		} else if (b.arenaRadius > 0) {
			V o = s.at - c;
			o.y = 0;
			const float len = Length(o);
			if (len > b.arenaRadius - s.radius && len > .01f) {
				n = o * (-1 / len);
				out = true;
				s.at = c + o * ((b.arenaRadius - s.radius) / len) + V{0, s.at.y - c.y, 0};
			}
		}
		if (out && !stays) {
			if (L(FuseTrait::Bounce) > 0 || s.rolling || s.returning) {
				const float into = Dot(s.velocity, n);
				if (into < 0) {
					s.velocity = s.velocity - n * (2 * into);
					s.hit.clear();
				}
			} else {
				EndFusedShot(ctx, s);
				return;
			}
		}
	}
	// 吸い寄せる: まわりの相手を少しずつ引き寄せる
	if (L(FuseTrait::Magnet) > 0) {
		s.pulse -= dt;
		if (s.pulse <= 0) {
			s.pulse = .3f;
			BattleImpact i;
			i.action = BattleAction::Throw;
			i.effect = ImpactEffect::Pull;
			i.at = s.at;
			i.reach = unit * FuseMagnetReach * float(L(FuseTrait::Magnet));
			i.push = 1.5f;
			i.pullCap = unit * 1.5f;
			i.bigScale = .3f;
			EmitBattleImpact(ctx, i);
		}
	}
	// 飛んだ跡の粒 (細い物でも道すじが分かるように)
	if (pattern != FusePattern::Mine && std::fmod(s.age, .04f) < dt)
		b.fx.push_back({s.at, s.radius * .4f, 0, .22f, {.55f, .8f, 1, .8f}});
	// 当たり: 回る弾は当たりが広く、何体にも当たる
	if (s.hits > 0 && s.hitCool <= 0 && (pattern != FusePattern::Mine || triggered)) {
		const float reach = pattern == FusePattern::Mine ? unit * FuseMineReach * 1.3f * std::sqrt(s.power)
		                                                 : s.radius * (1 + FuseSpinGrow * float(L(FuseTrait::Spin)));
		BattleImpact i;
		i.action = BattleAction::Throw;
		i.effect = ImpactEffect::Hit;
		i.shape = ImpactShape::Sphere;
		i.at = s.at;
		i.reach = reach;
		i.flat = s.rolling;
		i.dir = flat;
		i.damage = s.damage;
		i.push = s.push;
		i.limit = L(FuseTrait::Spin) > 0 || pattern == FusePattern::Mine ? 0 : 1;
		if (pattern == FusePattern::Mine)
			i.radial = true;
		std::vector<BattleTarget> touched;
		if (EmitBattleImpact(ctx, i, &touched, &s.hit) > 0) {
			// (罠は 1 回効いたら終わる)
			if (pattern == FusePattern::Mine)
				s.hits = 1;
			for (const auto& t : touched) {
				s.hit.push_back(t.id);
				// 揺らす・ひび・熱い: 当てた積み木 (揺らすとひびは、そのまわりも) に、続けて効き目を出す
				auto extra = [&](ImpactEffect e, float seconds, float damage, const char* label) {
					BattleImpact x;
					x.action = BattleAction::Throw;
					x.effect = e;
					x.at = t.at;
					x.reach = t.radius + unit;
					x.only = t.id;
					x.limit = 1;
					x.seconds = seconds;
					x.damage = damage;
					x.label = label;
					EmitBattleImpact(ctx, x);
				};
				if (L(FuseTrait::Stun) > 0)
					ShakeAround(ctx, t, s.damage * FuseShakeShare, L(FuseTrait::Stun));
				if (L(FuseTrait::Weaken) > 0)
					CrackAround(ctx, t);
				if (L(FuseTrait::Burn) > 0)
					extra(
					    ImpactEffect::Burn, BattleBurnTime, BattleBurnDps * s.power * float(L(FuseTrait::Burn)),
					    "じりじり");
			}
			s.hits -= 1;
			b.fx.push_back({s.at, reach * 1.4f, 0, .18f, {1, .85f, .4f, .8f}});
			s.hitCool = .08f;
			BurstFusedShot(ctx, s, false);
			// 跳び移る: 次に近い相手へ (いれば当たれる数も戻る)。跳ねる: 当たった相手から離れる向きへ跳ねる
			BattleTarget next;
			if (s.chain > 0 && NearestBattleTarget(s.at, unit * 12, next, &s.hit)) {
				--s.chain;
				V to = next.at - s.at;
				to.y = 0;
				s.velocity = Unit(to) * std::max(speed, unit * 14) + V{0, s.velocity.y, 0};
				s.hits = std::max(s.hits, 1);
				PlayDevourSound(devourSounds_.note, .35f, 1.9f);
			} else if (L(FuseTrait::Bounce) > 0 && s.hits > 0 && !touched.empty()) {
				V away = s.at - touched.front().at;
				away.y = 0;
				away = Length(away) > .01f ? Unit(away) : V{0, 0, 1};
				s.velocity = V{away.x * speed, flying || s.rolling ? 0.f : unit * 6, away.z * speed};
			}
			if (s.hits <= 0) {
				EndFusedShot(ctx, s);
				return;
			}
		}
	}
	if (s.age > s.life)
		EndFusedShot(ctx, s);
}

// 下の真ん中: 武器のステッカー (本体の絵と、混ぜた物の小さな絵・連射の待ち) と名前
void ChronoSystem::DrawWeaponStickers(GameContext& ctx) {
	using namespace UI::Scale;
	UI::Canvas ui(ctx.renderer, ctx.viewportSize.x, ctx.viewportSize.y);
	const auto& b = battle_;
	const auto& w = b.weapon;
	if (w.filled <= 0)
		return;
	const float size = 84, cx = 640, cy = 640, rot = -4 * .0174533f;
	const UI::Rect outer{cx - size * .5f, cy - size * .5f, size, size},
	    inner{outer.x + 5, outer.y + 5, size - 10, size - 10};
	ui.ImageRot(
	    "Resources/Textures/UI/HUD/sticker.png", {outer.x + 3, outer.y + 6, outer.w, outer.h}, rot, {0, 0, 0, .45f});
	ui.ImageRot("Resources/Textures/UI/HUD/sticker.png", outer, rot, Black);
	ui.ImageRot("Resources/Textures/UI/HUD/sticker.png", inner, rot, Yellow);
	{
		const std::string image = ImageOf(w.ids[0]);
		if (!image.empty())
			ui.ImageRot(image, {inner.x + 4, inner.y + 4, inner.w - 8, inner.h - 8}, rot);
	}
	for (int part = 1; part < w.filled; ++part) {
		const std::string image = ImageOf(w.ids[size_t(part)]);
		if (image.empty())
			continue;
		const float s = 28;
		const UI::Rect r{outer.x + outer.w - s * .6f, outer.y - s * .35f + float(part - 1) * (s + 3), s, s};
		ui.Rounded({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, Black, 6);
		ui.Rounded(r, White, 5);
		ui.Photo(image, {r.x + 2, r.y + 2, r.w - 4, r.h - 4});
	}
	const FusePattern pattern = FusePatternOf(w.lv);
	const float interval =
	    CraftInterval(w.points[2], w.lv[size_t(FuseTrait::Heavy)], w.caps[2]) * FusePatternInterval(pattern);
	if (w.cool > 0 && interval > .15f) {
		const float share = std::clamp(w.cool / interval, 0.f, 1.f);
		ui.Fill({inner.x, inner.y + inner.h * (1 - share), inner.w, inner.h * share}, {0, 0, 0, .4f});
	}
	// 名前と、弾数・威力・連射
	char stats[96];
	snprintf(
	    stats, sizeof(stats), "%s%s  %d発  x%.1f  %.1f回/秒", FusePatternName(pattern),
	    FusePatternTap(pattern) ? " (連打)" : "", FuseShotCount(pattern, w.points[0], w.caps[0]),
	    CraftPower(w.points[1], w.caps[1]), 1 / interval);
	const float nw = std::max(ui.Width(w.name, 20, Jp), ui.Width(stats, 16, Jp)) + 24, nx = outer.x - 16 - nw;
	Plate(ui, {nx, cy - 30, nw, 60}, Cream, 3, 4);
	ui.Text(w.name, nx + 12, cy - 28, 20, Black, Jp);
	ui.Text(stats, nx + 12, cy + 2, 16, Black, Jp);
}
// ---- 作る画面の形の見本 (実際のスライム) ----
// 今の材料で作った形に、実際のスライムをする (形が変わったら、形になってからの秒を 0 に戻して、ぽんと出てくるところから)
void ChronoSystem::TickCraftPreview(GameContext& ctx, float dt) {
	auto& b = battle_;
	const BattleWeapon weapon = CraftedWeapon();
	const int shape = weapon.filled > 0 ? BuildWeaponShape(weapon) : 0;
	if (shape != b.craft.previewShape) {
		b.craft.previewShape = shape;
		b.weaponAge = 0;
	}
	b.weaponShape = shape;
	b.weaponForm = shape > 0 ? 1.f : 0.f;
	auto& c = b.craft;
	// ---- 見本の撃ち方: 見本の窓のスライムが、今の材料と強化で撃って見せる ----
	// (材料が変わったら、出ている弾を消して最初から)
	if (shape != c.demoShape) {
		c.demoShape = shape;
		c.demoClock = 0;
		c.demoShots.clear();
		c.demoNewShots.clear();
	}
	// 武器は今の材料と強化の物にする (撃つ待ちと、連なり撃ちの残りは引き継ぐ)
	{
		BattleWeapon w = weapon;
		w.shape = shape;
		w.cool = b.weapon.cool;
		w.burstLeft = b.weapon.burstLeft;
		w.burstClock = b.weapon.burstClock;
		b.weapon = w;
	}
	// 見本の弾を、チャレンジの弾の並びと入れ替えて進める (撃つ・進める処理をそのまま使う)。
	// 動きは kDemoScale に縮める: 戦闘の大きさの基準 unit を一時的に小さくする。何にも効かない (作る画面の間は衝撃を出さない)
	std::swap(b.shots, c.demoShots);
	std::swap(b.newShots, c.demoNewShots);
	const float unit = b.unit;
	const int spent = b.spent;
	b.unit = unit * kDemoScale;
	if (shape > 0) {
		TickBattleWeapons(ctx, dt);
		const FusePattern pattern = FusePatternOf(b.weapon.lv);
		const float before = c.demoClock;
		c.demoClock += dt;
		if (FusePatternTap(pattern)) {
			if (before == 0 || int(before / kDemoTapPeriod) != int(c.demoClock / kDemoTapPeriod))
				UseBattleWeapon(ctx);
		} else if (std::fmod(c.demoClock, kDemoPeriod) < kDemoHold)
			UseBattleWeapon(ctx);
		// 出たばかりの弾: 見える大きさは体の半径に比べて決まった大きさに (大きい物も細い物も同じくらいに見える)、
		// 残る秒を短く、衛星は窓に収まるよう回る半径を縮める。まっすぐ飛ぶ撃ち方は、窓の中を目で追える速さまでに抑える
		// (大砲・降り注ぎ・衛星・置き罠は、落ちる所や回り方が速さで決まるのでそのまま)
		const float R = DevourBodyRadius(life_.bodies[0].mass);
		const bool straight = pattern != FusePattern::Mortar && pattern != FusePattern::Rain &&
		                      pattern != FusePattern::Orbit && pattern != FusePattern::Mine;
		for (auto& s : b.shots)
			if (!s.demo) {
				s.demo = true;
				s.size = R * kDemoLook * (s.split ? FuseSplitScale : 1.f);
				s.life = std::min(s.life, kDemoLife);
				s.orbitRadius *= .6f;
				const V flat{s.velocity.x, 0, s.velocity.z};
				const float speed = Length(flat);
				if (straight && speed > R * kDemoSpeed)
					s.velocity = V{0, s.velocity.y, 0} + flat * (R * kDemoSpeed / speed);
			}
		for (auto& s : b.shots)
			TickFusedShot(ctx, s, dt);
		b.shots.insert(b.shots.end(), b.newShots.begin(), b.newShots.end());
		b.newShots.clear();
		b.shots.erase(
		    std::remove_if(
		        b.shots.begin(), b.shots.end(), [](const BattleShot& s) { return s.age >= 99 || s.age > s.life; }),
		    b.shots.end());
	} else
		b.shots.clear();
	b.unit = unit;
	b.spent = spent;
	std::swap(b.shots, c.demoShots);
	std::swap(b.newShots, c.demoNewShots);
	// 手で回していないときは、ゆっくり回して全部の向きを見せる (手で回したあとは少し待ってから)
	c.previewIdle += dt;
	if (!c.previewDrag && c.previewIdle > kPreviewSpinWait)
		c.previewYaw += dt * kPreviewSpin * std::clamp((c.previewIdle - kPreviewSpinWait) / .8f, 0.f, 1.f);
}
// カメラを、スライムが形の見本の窓 (kPreview。画面は 1280x720 とみる) の真ん中に、窓の幅か高さの短い方の 8 割ほどの大きさで
// 映るように置く。前の右の上から見下ろす向きから始めて、スライムのまわりを回る (previewYaw・previewPitch)。
// 窓は画面の端にあるので、スライムを画面の真ん中からずらして映す
void ChronoSystem::CraftPreviewCamera(GameContext& ctx) {
	const auto& body = life_.bodies[0];
	const auto* g = WeaponShapeOf(battle_.weaponShape);
	const float scale = LifeScale(body.mass);
	V f = body.forward;
	f.y = 0;
	f = Length(f) > .01f ? Unit(f) : V{0, 0, 1};
	const V side{f.z, 0, -f.x};
	// (形が無いときは液体の体の大きさ)
	const V bounds = g ? V{g->bounds.x, g->bounds.y, g->bounds.z} : V{2.55f, 3.6f, 2.55f};
	const V center = body.at + V{0, -1.25f + bounds.y * .5f * scale, 0};
	const float radius = std::max({bounds.x, bounds.y * .5f, bounds.z}) * scale;
	// (前の右から見る角度 0.64 ラジアンに、回した角度を足す)
	const float yaw = .64f + battle_.craft.previewYaw, pitch = battle_.craft.previewPitch;
	const V flat = f * std::cos(yaw) + side * std::sin(yaw);
	const V view = Unit((flat * std::cos(pitch) + V{0, std::sin(pitch), 0}) * -1.f);
	const float aspect = ctx.viewportSize.x / std::max(1.f, ctx.viewportSize.y), tanHalf = std::tan(fov_ * .5f);
	const float hx = (kPreview.x + kPreview.w * .5f) / 1280.f * 2 - 1,
	            hy = 1 - (kPreview.y + kPreview.h * .5f) / 720.f * 2, hh = std::min(kPreview.w, kPreview.h) / 720.f;
	const float distance = radius / (tanHalf * hh * .8f);
	const V camRight = Unit(V{view.z, 0, -view.x});
	const V camUp{
	    view.y * camRight.z - view.z * camRight.y, view.z * camRight.x - view.x * camRight.z,
	    view.x * camRight.y - view.y * camRight.x};
	const V target = center - camRight * (hx * distance * tanHalf * aspect) - camUp * (hy * distance * tanHalf);
	auto& c = battle_.craft;
	c.previewEye = target - view * distance;
	c.previewView = view;
	c.previewCenter = center;
	c.previewRadius = radius;
	c.previewFloor = body.at.y - 1.25f;
	ctx.camera->SetPosition(Write(target - view * distance));
	ctx.camera->SetRotation(-std::asin(std::clamp(view.y, -1.f, 1.f)), std::atan2(view.x, view.z), 0);
	ctx.camera->SetProjection(fov_, aspect, .5f, 2000 + distance * 4);
	ctx.camera->SetHandheld(0);
	ctx.camera->SetShakeStrength(0);
}
// 見本の窓の背景: カメラとスライムを無地の箱で囲む (箱の外のステージは見えない)。箱は横の向きをカメラに合わせ、
// カメラの少し後ろからスライムの少し奥まで、底はスライムの立つ床の高さ。スライムの下に丸い影も置く
void ChronoSystem::DrawCraftBackdrop(GameContext& ctx) {
	if (cube_ == 0 || battle_.phase != BattleStep::Craft)
		return;
	const auto& c = battle_.craft;
	V f{c.previewView.x, 0, c.previewView.z};
	f = Length(f) > .01f ? Unit(f) : V{0, 0, 1};
	const V side{f.z, 0, -f.x};
	const V eye = c.previewEye;
	const float reach = Dot(c.previewCenter - eye, f) + c.previewRadius * 2.2f, back = 4;
	const float aspect = ctx.viewportSize.x / std::max(1.f, ctx.viewportSize.y), tanHalf = std::tan(fov_ * .5f);
	const float halfWide = (reach + back) * tanHalf * aspect * 1.6f + c.previewRadius * 2;
	const float bottom = c.previewFloor + .02f, top = std::max(eye.y, c.previewCenter.y) + reach * tanHalf * 2 + 10;
	const V mid = eye + f * ((reach - back) * .5f);
	const V at{mid.x, (bottom + top) * .5f, mid.z};
	auto box = [&](V center, V axisX, V axisY, V axisZ, Engine::Vector4 color, const char* shader) {
		Engine::Matrix4x4 m{};
		const V axes[3] = {axisX, axisY, axisZ};
		for (int a = 0; a < 3; ++a) {
			m.m[a][0] = axes[a].x;
			m.m[a][1] = axes[a].y;
			m.m[a][2] = axes[a].z;
			m.m[a][3] = 0;
		}
		m.m[3][0] = center.x;
		m.m[3][1] = center.y;
		m.m[3][2] = center.z;
		m.m[3][3] = 1;
		ctx.renderer->DrawMeshInstanced(cube_, white_, m, color, shader);
	};
	// (色は直線の明るさで渡す: 薄い水色の壁と、少し暗い床)
	box(at, side * halfWide, V{0, (top - bottom) * .5f, 0}, f * ((reach + back) * .5f), {.62f, .78f, .86f, 1},
	    "CraftBackdrop");
	// 丸い影 (床の少し上。スライムの大きさに合わせる)
	if (disc_ != 0) {
		const float r = c.previewRadius * .75f;
		Engine::Matrix4x4 m{};
		m.m[0][0] = r;
		m.m[1][1] = 1;
		m.m[2][2] = r;
		m.m[3][0] = c.previewCenter.x;
		m.m[3][1] = bottom + .02f;
		m.m[3][2] = c.previewCenter.z;
		m.m[3][3] = 1;
		ctx.renderer->DrawMeshInstanced(disc_, white_, m, {.3f, .36f, .42f, 1}, "CraftBackdrop");
	}
}
// ---- 確認用 (--weapon-check): 全部の変形武器の撃ち方を、積み木の街の平らな所で実際に撃って確かめる ----
// 性質 1 つ (18) と 2 つの組み合わせ (153) の武器を 1 つずつ作り、3 回ずつ撃つ:
//  0: 的なし。弾の地面からの高さと進んだ距離を 0.5 秒ごとではなく毎フレーム見て、地面の積み木 1 段 (高さ 0〜1 個) に当たれる距離の帯を記録
//  1: 前 4 個の所に地面の積み木の横一列 (7 個)。2: 前 11 個の所に同じ列。当たった積み木の数を数える
//  3: 上り坂の手前から坂へ向かって撃つ (撃った弾がすぐ消えないか: 一番長く残った弾の秒と進んだ距離)
// --weapon-check-only=番号,番号 で、その性質を含む武器だけを試す (性質の番号は FuseTrait の順)
// 結果は tests/out/weapon-check.txt (1 行 1 武器。問題のある武器に印)
void ChronoSystem::TickWeaponCheck(GameContext& ctx, float dt, V& input) {
	auto& b = battle_;
	auto& c = weaponCheck_;
	auto& body = life_.bodies[0];
	const float u = b.unit;
	input = {};
	if (!c.started) {
		c.started = true;
		b.time = 1e9f;
		// 武器の一覧: 性質 1 つ (レベル 1)・2 つの組み合わせ (同じ性質を 2 つならレベル 2)
		for (int i = 0; i < FuseTraitCount; ++i)
			for (int j = i; j < FuseTraitCount; ++j) {
				FuseLevels lv{};
				++lv[size_t(i)];
				++lv[size_t(j)];
				c.list.push_back(lv);
			}
		for (int i = 0; i < FuseTraitCount; ++i) {
			FuseLevels lv{};
			lv[size_t(i)] = 1;
			c.list.insert(c.list.begin() + i, lv);
		}
		if (const wchar_t* only = wcsstr(GetCommandLineW(), L"--weapon-check-only=")) {
			std::vector<int> want;
			for (const wchar_t* p = only + 20; *p && *p != L' ';) {
				want.push_back(_wtoi(p));
				while (*p && *p != L',' && *p != L' ')
					++p;
				if (*p == L',')
					++p;
			}
			std::vector<FuseLevels> kept;
			for (const auto& lv : c.list)
				for (int t : want)
					if (t >= 0 && t < FuseTraitCount && lv[size_t(t)] > 0) {
						kept.push_back(lv);
						break;
					}
			c.list = kept;
		}
		// 撃つ所: 前 (+z) 22 個ぶんと左右 4 個ぶんが平らな所 (地面の高さが同じ) を探す
		const V center = b.arenaAt;
		float best = 1e9f;
		for (float x = -100; x <= 100; x += 4)
			for (float z = -100; z <= 80; z += 4) {
				const V at = center + V{x * u, 0, z * u};
				const float g = InkGround(at);
				bool flat = true;
				for (float f = -2; f <= 24 && flat; f += 1)
					for (float s = -4; s <= 4 && flat; s += 2)
						flat = std::abs(InkGround(at + V{s * u, 0, f * u}) - g) < .02f * u;
				if (flat && std::abs(x) + std::abs(z + 104) < best) {
					best = std::abs(x) + std::abs(z + 104);
					c.spot = at;
					c.spot.y = g;
				}
			}
		// 上り坂の試しの所: 前 3〜8 個の間で地面が 1 個以上上がる所 (橋やらせんの坂の上り口)
		{
			float bestRise = 0;
			for (float x = -100; x <= 100; x += 2)
				for (float z = -100; z <= 100; z += 2)
					for (int dir = 0; dir < 4; ++dir) {
						const V f = dir == 0   ? V{0, 0, 1}
						            : dir == 1 ? V{0, 0, -1}
						            : dir == 2 ? V{1, 0, 0}
						                       : V{-1, 0, 0};
						const V at = center + V{x * u, 0, z * u};
						const float g = InkGround(at);
						if (std::abs(InkGround(at + f * (2 * u)) - g) > .05f * u)
							continue;
						const float rise = InkGround(at + f * (8 * u) + V{0, 3 * u, 0}) - g;
						bool smooth = true;
						for (float k = 2; k <= 8 && smooth; k += .5f)
							smooth = InkGround(at + f * ((k + .5f) * u) + V{0, 3 * u, 0}) >=
							         InkGround(at + f * (k * u) + V{0, 3 * u, 0}) - .02f * u;
						if (smooth && rise > bestRise && rise < 3.5f * u) {
							bestRise = rise;
							c.slope = at;
							c.slope.y = g;
							c.slopeDir = f;
						}
					}
		}
		std::filesystem::create_directories("tests/out");
		c.log.open("tests/out/weapon-check.txt");
		c.log << "spot=(" << (c.spot.x - center.x) / u << "," << (c.spot.z - center.z) / u << ") unit=" << u
		      << " body_radius=" << DevourBodyRadius(body.mass) / u << " body_ground=" << ChallengeBodyGround / u
		      << '\n';
		c.log << "slope=(" << (c.slope.x - center.x) / u << "," << (c.slope.z - center.z) / u << ") dir=("
		      << c.slopeDir.x << "," << c.slopeDir.z << ")\n";
		c.log
		    << "traits\tpattern\tspawn_h\tlow_h\tmax_dist\tband(1..20)\tnear_hits\tfar_hits\tslope_life\tslope_dist\tflags\n";
		c.trial = 0;
		c.sub = -1;
	}
	if (c.trial >= int(c.list.size())) {
		if (!c.done) {
			c.done = true;
			c.log << "summary trials=" << c.list.size() << " flagged=" << c.flagged << '\n';
			c.log.close();
			PostQuitMessage(0);
		}
		return;
	}
	// (1 回の試しは、弾が全部終わるまで (遅い弾も遠くまで行けるように)。長くても 4.5 秒)
	auto& w = b.weapon;
	bool flying = w.burstLeft > 0;
	for (const auto& s : b.shots)
		flying = flying || (s.fused && s.age < 99);
	const float subTime = c.clock > .4f && !flying ? 0.f : 4.5f;
	// 次の試し: 体を置き直し、弾と的を消して、武器の性質を入れ替えて撃つ
	if (c.sub < 0 || c.clock >= subTime) {
		if (c.sub == 3) {
			// 1 つの武器の結果を書く
			std::string traits;
			for (int t = 0; t < FuseTraitCount; ++t)
				for (int k = 0; k < c.list[size_t(c.trial)][size_t(t)]; ++k)
					traits += std::string(traits.empty() ? "" : "+") + FuseTraitName(FuseTrait(t));
			const FusePattern pattern = FusePatternOf(c.list[size_t(c.trial)]);
			std::string band;
			for (int d = 1; d <= 20; ++d)
				band += c.band[size_t(d)] ? '#' : '.';
			std::string flags;
			const bool travels = pattern != FusePattern::Orbit && pattern != FusePattern::Mine;
			// (衛星は体のまわりだけ・罠は前に置くだけ・降り注ぎは上から落とすので、それぞれの決まりで見る)
			if (c.nearHits == 0 && pattern != FusePattern::Orbit)
				flags += " NEAR_MISS";
			if (c.farHits == 0 && travels)
				flags += " FAR_MISS";
			if (c.spawnLow > 1.1f && travels && pattern != FusePattern::Rain)
				flags += " SPAWN_HIGH";
			if (c.slopeLife < .2f && travels && pattern != FusePattern::Rain)
				flags += " SLOPE_DIES";
			if (c.maxDist < 4 && travels)
				flags += " SHORT";
			if (c.lowest > 1.1f && travels)
				flags += " NEVER_LOW";
			if (!flags.empty())
				++c.flagged;
			c.log << traits << '\t' << FusePatternName(pattern) << '\t' << c.spawnH << '\t' << c.lowest << '\t'
			      << c.maxDist << '\t' << band << '\t' << c.nearHits << "/7\t" << c.farHits << "/7\t" << c.slopeLife
			      << '\t' << c.slopeDist << '\t' << flags << '\n';
			c.log.flush();
			++c.trial;
			c.sub = -1;
			if (c.trial >= int(c.list.size()))
				return;
		}
		c.sub = (c.sub + 1) % 4;
		c.clock = 0;
		if (c.sub == 0) {
			c.band = {};
			c.spawnH = c.spawnLow = c.lowest = 1e9f;
			c.maxDist = 0;
			c.nearHits = c.farHits = 0;
			c.slopeLife = c.slopeDist = 0;
		}
		b.shots.clear();
		b.newShots.clear();
		b.placed.clear();
		b.impactMemo.clear();
		w.burstLeft = 0;
		w.cool = 0;
		w.lv = c.list[size_t(c.trial)];
		const float lift = body.at.y - InkGround(body.at);
		const V from = c.sub == 3 ? c.slope : c.spot, facing = c.sub == 3 ? c.slopeDir : V{0, 0, 1};
		body.at = from + V{0, lift > 0 ? lift : ChallengeBodyGround, 0};
		body.forward = body.liquidForward = facing;
		b.velocity = {};
		// 的: 地面の積み木の横一列 (硬さは十分にして、壊れずに当たった数を数える)
		b.blocks.clear();
		if (c.sub == 1 || c.sub == 2) {
			const float ahead = c.sub == 1 ? 4.f : 11.f;
			for (int k = -3; k <= 3; ++k) {
				ChallengeBlock blk;
				blk.id = ChallengeBlockIdBase + uint32_t(b.blocks.size());
				blk.size = u * ChallengeBlockSize;
				blk.at = c.spot + V{float(k) * u * 1.05f, blk.size * .5f, (ahead + 1.2f) * u};
				blk.hp = blk.maxHp = 1e7f;
				b.blocks.push_back(blk);
			}
		}
		UseBattleWeapon(ctx);
		c.fired = true;
	}
	c.clock += dt;
	// 弾を見る: 撃った所 (体の前) からの前の距離と、地面からの高さ (積み木の大きさの何倍か)
	for (const auto& s : b.shots) {
		if (!s.fused || s.age >= 99)
			continue;
		const float h = (s.at.y - InkGround(s.at)) / u,
		            reach = s.radius * (1 + FuseSpinGrow * float(s.lv[size_t(FuseTrait::Spin)])) / u;
		const float d = (s.at.z - c.spot.z) / u;
		if (c.sub == 0) {
			if (s.age < .05f) {
				c.spawnH = std::min(c.spawnH, h);
				c.spawnLow = std::min(c.spawnLow, h - reach);
			}
			c.lowest = std::min(c.lowest, h - reach);
			c.maxDist = std::max(c.maxDist, Length(V{s.at.x - c.spot.x, 0, s.at.z - c.spot.z}) / u);
			// (地面の積み木の真ん中 (高さ 0.5) と当たりの球が届くか。積み木の当たりの半径は大きさの 0.6 倍)
			if (std::abs(h - .5f) < reach + .6f && d >= 0.5f && d < 20.5f)
				c.band[size_t(std::clamp(int(std::lround(d)), 1, 20))] = true;
		}
	}
	if (c.sub == 3) {
		for (const auto& s : b.shots)
			if (s.fused && s.age < 99) {
				c.slopeLife = std::max(c.slopeLife, s.age);
				c.slopeDist = std::max(c.slopeDist, Length(V{s.at.x - c.slope.x, 0, s.at.z - c.slope.z}) / u);
			}
	}
	if (c.sub == 1 || c.sub == 2) {
		int hit = 0;
		for (const auto& blk : b.blocks)
			if (blk.hp < blk.maxHp || !blk.alive)
				++hit;
		(c.sub == 1 ? c.nearHits : c.farHits) = std::max(c.sub == 1 ? c.nearHits : c.farHits, hit);
	}
}

} // namespace Game
