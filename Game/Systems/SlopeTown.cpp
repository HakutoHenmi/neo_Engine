// SlopeTown.cpp
// 吸収モードの坂の町 (ステージ 2。ChronoSystem の一部)。南の川と線路から北の神社の山へ上がる斜面の町。
// 地面は 1m のマスごとの面で、隣と近い高さの角はならして坂をなめらかにし、差が大きい所は縦の面 (擁壁・石垣) にする。
// 高さは Game/Chrono/SlopeTownRules.h の SlopeGround と同じ形。地形は Resources/Data/slope_town_terrain.json
// (scripts/terrain/slope_town_design.py が作る)、置く物は Resources/Data/slope_town.json。物は実寸 (1m = 6 単位) のローポリのモデル
#include "CityBuilder.h"
#include "../Chrono/SlopeTownRules.h"
#include <map>

namespace Game {
using namespace CityParts;
namespace {
using Chrono::Vec;
// 縦の面を作る高さの差 (これより小さい差はすき間ができないので面を作らない。メートル)
constexpr float kWallMin = .05f;
// 町の外の地面: 格子の大きさ・端の高さから寄せていく先の高さ (メートル。北ほど少し高い)・寄せ終わる距離・端より少し下げる量
constexpr float kOutsideGrid = 8, kOutsideHeight = 14, kOutsideRise = .08f, kOutsideBlend = 220, kOutsideDrop = .05f;
// 石段の箱を地面の下へ伸ばす深さ・前後のすき間を埋める量 (メートル)
constexpr float kStairsDepth = 1.5f, kStairsSeam = .02f, kStairsTopDrop = .03f;
// 石段の横の地面の縁を下へ伸ばす深さ (メートル)
constexpr float kSkirt = 4;
// 段差の上の縁取り (笠石): この高さより大きい段差の上の側に、幅 kLipWidth の明るい帯を地面から少し浮かせて描く (メートル)。
// 坂を下るとき、上の段と下の段が同じ色で続いて、どこで段差が来るのか見えなかった
constexpr float kLipDrop = .3f, kLipWidth = .3f, kLipLift = .03f;
// 道の中央の破線: 長さ・間隔・幅・路面から浮かせる高さ・破線を引く道の最小の幅・道の高さと路面がこれ以上違う所は引かない (メートル)
constexpr float kDash = 3, kDashGap = 3, kDashWidth = .15f, kDashLift = .04f, kDashRoad = 6, kDashMismatch = .3f;
// 町の端の塀の目地の間隔・太さ・面から浮かせる量・笠木の高さと出 (メートル)
constexpr float kSeam = .6f, kSeamWidth = .06f, kSeamLift = .02f, kCapHeight = .12f, kCapOut = .05f;
// ばらまく物が避ける余白: 石段・擁壁のきわ・大きな物の周り。大きな物とみなす幅。町の端からの余白 (メートル)
constexpr float kStairsMargin = 1, kWallMargin = 1.2f, kBigMargin = .5f, kBigSize = 1.5f, kEdgeMargin = 1.5f;
// ばらまくときに 1 個あたり試す回数
constexpr int kScatterTries = 8;
// 組の 1 つ 1 つが避ける余白: 大きな物の周り・擁壁のきわ。組の起点と地面の高さがこれ以上違う所には置かない (メートル)
constexpr float kSetBigMargin = .15f, kSetWallMargin = .5f, kSetLevel = .6f;
// 正面のある物の向き: 正面のこの距離の先が、これ以上高い段か建物 (この高さより高い大きな物) なら後ろ向きにする (メートル)
constexpr float kFrontProbe = .5f, kFrontRise = .3f, kFrontDrop = .6f, kSolidHeight = 2.5f;
// 置く前の確かめ: 足元の地面の高さの差の上限 (小さな物・1.5m 以上の物)・足元の角を内へ寄せる量・ほかの物と重なってよい幅 (メートル)
constexpr float kPlaceLevel = .12f, kPlaceLevelBig = .3f, kPlaceInset = .04f, kPlaceTouch = .03f;
// その場を回って歩く物: 回る道の点の数・通る点の地面の高さの差の上限 (メートル)
constexpr int kWanderPoints = 10;
constexpr float kWanderLevel = .3f;
// おみこしとこまの輪: 平らか調べる半径と高さの差の上限・町の端と石段から離す幅・探す範囲 (1m ずつ外へ)・家を建てない半分の幅 (メートル)
constexpr float kRideLevelReach = 3, kRideLevel = .5f, kRideEdge = 4, kRideKeep = 5;
constexpr int kRideSearch = 10;
// 道を歩く物: 道の両端から離す長さ・通る点の間隔 (メートル)
constexpr float kWalkerEnd = 6, kWalkerStep = 2;
// 電柱などを道のマスから外へずらす 1 回の量 (メートル)
constexpr float kRoadsideStep = .6f;
// 家の敷地: 塀を立てる高さの差の上限・塀を敷地の前の端から内へずらす量・塀の左右の位置 (間口の端から)・庭の組を置く角からの距離・道を調べる所 (前の端の外。メートル)
constexpr float kFenceLevel = .6f, kFenceInset = .3f, kFenceSide = 2.2f, kYardInset = .8f, kStreetProbe = 1.2f;
// 目印の物の足元と重なるとみなす敷地の食い込み (メートル。少し重なるだけなら家を建てる)
constexpr float kLandmarkSlack = 1;
// 光: 町全体の明るさ
const Engine::Vector3 kAmbient{.4f, .4f, .45f};
// カメラとの間に入ると薄くする物の高さ (単位)
constexpr float kOccludeHeight = 14;
constexpr float kTau = 6.2831853f;
} // namespace
void ChronoSystem::BuildSlopeTown(entt::registry& r, const StageTools& tools) {
	using namespace Chrono;
	auto* render = Engine::Renderer::GetInstance();
	const auto& d = SlopeData();
	const float m = d.unit, cm = d.unit / 100;
	BeginLowProps(kOccludeHeight);
	lowFeet_ = true;
	render->SetAmbientColor(kAmbient);
	stageLots_.clear();
	gimmickSigns_.clear();
	devourBuildingParts_.clear();
	// ---- 地面: 色の名前ごとに 1 つのメッシュへまとめる ----
	std::map<std::string, CityBuilder> meshes;
	auto quad = [&](const std::string& color, Vec a, Vec b, Vec c, Vec e, Vec normal) {
		meshes[color].Polygon(CityMetal, {a, b, c, e}, normal);
	};
	auto box = [&](const std::string& color, Vec lo, Vec hi) { meshes[color].Box(CityMetal, lo, hi); };
	// 向きのある箱 (中心・水平の向き a・a の向きと横の半分の長さ・下と上の高さ)
	auto turned = [&](const std::string& color, Vec c, Vec a, float ha, float hb, float y0, float y1) {
		const Vec b{-a.z, 0, a.x};
		const Vec p[4] = {c - a * ha - b * hb, c + a * ha - b * hb, c + a * ha + b * hb, c - a * ha + b * hb};
		quad(color, {p[0].x, y1, p[0].z}, {p[1].x, y1, p[1].z}, {p[2].x, y1, p[2].z}, {p[3].x, y1, p[3].z}, {0, 1, 0});
		for (int k = 0; k < 4; ++k) {
			const Vec s = p[k], e = p[(k + 1) % 4], mid = (s + e) * .5f - c;
			quad(color, {s.x, y0, s.z}, {e.x, y0, e.z}, {e.x, y1, e.z}, {s.x, y1, s.z}, Unit(Vec{mid.x, 0, mid.z}));
		}
	};
	// マス: 上の面と、東と北の隣との間の縦の面 (高い方から低い方へ向ける)。
	// 縦の面は 2 枚の三角形で、2 つのマスの縁の線の間を埋める。片方の端では東が高く、もう片方では西が高いとき (線が交わるとき) も、
	// 四角形のようにねじれて斜めの大きな面や穴にならない。
	// 石段のマスは描かない: 段の奥行き (神社は約 0.3m) が 1m のマスより短く、斜めの段に 1 つのマスが何段もまたがるので、
	// マスの面や縦の面がどこかの段の上へはみ出す。石段は下の段の箱だけで描き、箱は石段のマスを全部覆う広さにする。
	// 石段のマスと隣り合う地面の縁は、下へ深く伸ばした縦の面 (kSkirt) にして、石段の横から地面の下が見えないようにする
	{
		const int n = d.n, hc = d.halfCells;
		const float c = d.cell, minWall = kWallMin * m;
		// x (または z) の位置 e の縦の線の上で、a0〜a1 (こちらのマス) と b0〜b1 (隣のマス) の縁の間を埋める。u0 と u1 は線の両端の位置
		auto seam = [&](const std::string& color, bool alongZ, float e, float u0, float u1, float a0, float a1,
		                float b0, float b1, Vec normal) {
			if (std::max(std::abs(a0 - b0), std::abs(a1 - b1)) <= minWall)
				return;
			auto at = [&](float u, float y) { return alongZ ? Vec{e, y, u} : Vec{u, y, e}; };
			meshes[color].Polygon(CityMetal, {at(u0, a0), at(u1, a1), at(u1, b1)}, normal);
			meshes[color].Polygon(CityMetal, {at(u0, a0), at(u1, b1), at(u0, b0)}, normal);
		};
		const float skirt = kSkirt * m;
		// マスの中の点 (u, v は 0〜1。高さは 4 つの角を補間して少し浮かせる)
		auto inCell = [&](int i, int j, float u, float v) {
			const auto& h = d.Corner(i, j);
			const float x0 = (float(i - hc) - .5f) * c, z0 = (float(j - hc) - .5f) * c;
			return Vec{
			    x0 + u * c, (h[0] * (1 - u) + h[1] * u) * (1 - v) + (h[3] * (1 - u) + h[2] * u) * v + kLipLift * m,
			    z0 + v * c};
		};
		// 縁取りの帯: マス (i, j) の dir の辺 (0 東・1 西・2 北・3 南) に沿う
		const float lipT = std::min(1.f, kLipWidth * m / c);
		auto lip = [&](int i, int j, int dir) {
			float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
			if (dir == 0)
				u0 = 1 - lipT;
			else if (dir == 1)
				u1 = lipT;
			else if (dir == 2)
				v0 = 1 - lipT;
			else
				v1 = lipT;
			quad(
			    "lip", inCell(i, j, u0, v0), inCell(i, j, u1, v0), inCell(i, j, u1, v1), inCell(i, j, u0, v1),
			    {0, 1, 0});
		};
		const float lipDrop = kLipDrop * m;
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const SlopeKind kind = d.K(i, j);
				const bool stairs = kind == SlopeKind::Stairs;
				const float x0 = (float(i - hc) - .5f) * c, z0 = (float(j - hc) - .5f) * c, x1 = x0 + c, z1 = z0 + c;
				const auto& h = d.Corner(i, j);
				if (!stairs)
					quad(
					    d.kindColor[size_t(kind)], {x0, h[0], z0}, {x1, h[1], z0}, {x1, h[2], z1}, {x0, h[3], z1},
					    {0, 1, 0});
				auto wallColor = [&](SlopeKind other) {
					return kind == SlopeKind::Road || other == SlopeKind::Road ? d.roadWall : d.wall;
				};
				// 東の隣・北の隣との間。片方だけが石段なら、地面の側の縁から下へ深く伸ばす
				if (i + 1 < n) {
					const auto& o = d.Corner(i + 1, j);
					const bool other = d.K(i + 1, j) == SlopeKind::Stairs;
					if (!stairs && !other)
						seam(
						    wallColor(d.K(i + 1, j)), true, x1, z0, z1, h[1], h[2], o[0], o[3],
						    {h[1] + h[2] > o[0] + o[3] ? 1.f : -1.f, 0, 0});
					else if (!stairs)
						seam(d.wall, true, x1, z0, z1, h[1], h[2], h[1] - skirt, h[2] - skirt, {1, 0, 0});
					else if (!other)
						seam(d.wall, true, x1, z0, z1, o[0] - skirt, o[3] - skirt, o[0], o[3], {-1, 0, 0});
					if (!stairs && !other) {
						const float diff = (h[1] + h[2] - o[0] - o[3]) * .5f;
						if (diff > lipDrop)
							lip(i, j, 0);
						else if (-diff > lipDrop)
							lip(i + 1, j, 1);
					}
				}
				if (j + 1 < n) {
					const auto& o = d.Corner(i, j + 1);
					const bool other = d.K(i, j + 1) == SlopeKind::Stairs;
					if (!stairs && !other)
						seam(
						    wallColor(d.K(i, j + 1)), false, z1, x0, x1, h[3], h[2], o[0], o[1],
						    {0, 0, h[3] + h[2] > o[0] + o[1] ? 1.f : -1.f});
					else if (!stairs)
						seam(d.wall, false, z1, x0, x1, h[3], h[2], h[3] - skirt, h[2] - skirt, {0, 0, 1});
					else if (!other)
						seam(d.wall, false, z1, x0, x1, o[0] - skirt, o[1] - skirt, o[0], o[1], {0, 0, -1});
					if (!stairs && !other) {
						const float diff = (h[3] + h[2] - o[0] - o[1]) * .5f;
						if (diff > lipDrop)
							lip(i, j, 2);
						else if (-diff > lipDrop)
							lip(i, j + 1, 3);
					}
				}
			}
	}
	// 町の外の地面: 端の高さから、離れるほど決まった高さへなだらかに寄せる粗い格子 (町の外が切れて見えないように)
	{
		const float inner = (float(d.halfCells) + .5f) * d.cell, g = kOutsideGrid * m;
		const int rings = int(std::ceil(d.outside / g));
		const float start = -inner - float(rings) * g;
		const int count = int(std::round(2 * inner / g)) + 2 * rings + 1;
		auto height = [&](float x, float z) {
			const float e =
			    SlopeGround({std::clamp(x, -inner, inner), 0, std::clamp(z, -inner, inner)}) - kOutsideDrop * m;
			const float dist = std::max({std::abs(x) - inner, std::abs(z) - inner, 0.f});
			float t = std::min(1.f, dist / (kOutsideBlend * m));
			t = t * t * (3 - 2 * t);
			return e + (kOutsideHeight * m + kOutsideRise * z - e) * t;
		};
		for (int gz = 0; gz < count; ++gz)
			for (int gx = 0; gx < count; ++gx) {
				const float x0 = start + float(gx) * g, z0 = start + float(gz) * g, x1 = x0 + g, z1 = z0 + g;
				if (x0 >= -inner - 1 && x1 <= inner + 1 && z0 >= -inner - 1 && z1 <= inner + 1)
					continue;
				quad(
				    "outside", {x0, height(x0, z0), z0}, {x1, height(x1, z0), z0}, {x1, height(x1, z1), z1},
				    {x0, height(x0, z1), z1}, {0, 1, 0});
			}
	}
	// 石段: 石段の向きに沿った段の箱。段ごとの左右の広がりと、一番下・一番上の段の伸ばし方は、読み込むときに石段のマスを覆うだけに決めてある
	// (SlopeStairs::side・back・ahead。石段の横を通る道の上へははみ出さない。地面の高さ SlopeGround も同じ広さで段の高さになる)
	for (const auto& s : d.stairs) {
		Vec axis{s.to.x - s.from.x, 0, s.to.z - s.from.z};
		const float length = Length(axis);
		if (length < 1e-3f)
			continue;
		axis = axis * (1 / length);
		const Vec across{-axis.z, 0, axis.x};
		const float run = length / float(s.steps), base = std::min(s.from.y, s.to.y) - kStairsDepth * m;
		// (一番上の段の箱は少し下げる: 上の段のマスは境内と同じ高さで、箱の上の面と重なってちらつかないように)
		for (int k = 0; k < s.steps; ++k) {
			const float top = s.from.y + (s.to.y - s.from.y) * float(k + 1) / float(s.steps) -
			                  (k + 1 == s.steps ? kStairsTopDrop * m : 0.f);
			const float back = k == 0 ? s.back : 0.f, ahead = k + 1 == s.steps ? s.ahead : 0.f,
			            left = s.side[size_t(k)][0], right = s.side[size_t(k)][1];
			turned(
			    d.kindColor[size_t(SlopeKind::Stairs)],
			    Vec{s.from.x, 0, s.from.z} + axis * (run * (float(k) + .5f) + (ahead - back) * .5f) +
			        across * ((right - left) * .5f),
			    axis, run * .5f + kStairsSeam * m + (ahead + back) * .5f, (left + right) * .5f, base, top);
		}
	}
	// 道の中央の破線 (広い道だけ。路面の高さに沿わせ、道の外や、道の高さと路面が合わない所は引かない)
	for (const auto& road : d.roads) {
		if (road.width < kDashRoad * m)
			continue;
		const float L = SlopeRoadLength(road), w = kDashWidth * m * .5f;
		for (float s = kDashGap * m; s + kDash * m < L; s += (kDash + kDashGap) * m) {
			Vec dir;
			const Vec a = SlopeRoadPoint(road, s, &dir), b = SlopeRoadPoint(road, s + kDash * m);
			const Vec side{-dir.z * w, 0, dir.x * w};
			Vec p[4] = {a - side, b - side, b + side, a + side};
			bool fits = true;
			for (int k = 0; k < 4; ++k) {
				const float expect = k == 1 || k == 2 ? b.y : a.y;
				Vec& q = p[k];
				q.y = SlopeGround(q);
				fits &= SlopeKindAt(q) == SlopeKind::Road && std::abs(q.y - expect) < kDashMismatch * m;
				q.y += kDashLift * m;
			}
			if (fits)
				quad("line", p[0], p[1], p[2], p[3], {0, 1, 0});
		}
	}
	// 町の端の塀: 歩ける範囲のすぐ外を囲む。区切りごとに真下の地面の高さから立て、上に笠木、内側に控え柱と横の目地を付ける。
	// 隣の区切りと地面の高さが違う所は、低い側の塀を高い側の地面まで伸ばしてすき間を作らない
	{
		const auto& b = d.border;
		const float ex = d.halfX, ez = d.halfZ, t = b.thick, capH = kCapHeight * m, capOut = kCapOut * m;
		auto side = [&](Vec a, Vec c, Vec out) {
			const Vec dir = Unit(c - a);
			const float length = Length(c - a);
			const int count = std::max(1, int(std::ceil(length / b.step)));
			const float piece = length / float(count);
			const Vec n{std::abs(out.x) * t, 0, std::abs(out.z) * t}, o = out * (t * .5f);
			float lastPost = -b.post;
			for (int k = 0; k < count; ++k) {
				const Vec p0 = a + dir * (piece * float(k)), p1 = p0 + dir * piece;
				const float g = SlopeGround((p0 + p1) * .5f), low = std::min({g, SlopeGround(p0), SlopeGround(p1)}),
				            top = g + b.height;
				const Vec lo{std::min(p0.x, p1.x) + o.x - n.x * .5f, low, std::min(p0.z, p1.z) + o.z - n.z * .5f},
				    hi{std::max(p0.x, p1.x) + o.x + n.x * .5f, top, std::max(p0.z, p1.z) + o.z + n.z * .5f};
				box(b.color, lo, hi);
				box(b.cap, {lo.x - capOut * std::abs(out.x), top, lo.z - capOut * std::abs(out.z)},
				    {hi.x + capOut * std::abs(out.x), top + capH, hi.z + capOut * std::abs(out.z)});
				for (float y = g + kSeam * m; y < top - .1f * m; y += kSeam * m) {
					const Vec u0 = p0 - out * (t * .5f + kSeamLift * m), u1 = p1 - out * (t * .5f + kSeamLift * m);
					quad(
					    b.color + "_seam", {u0.x, y, u0.z}, {u1.x, y, u1.z}, {u1.x, y + kSeamWidth * m, u1.z},
					    {u0.x, y + kSeamWidth * m, u0.z}, out * -1.f);
				}
				const float s = piece * float(k);
				if (s - lastPost >= b.post) {
					lastPost = s;
					const Vec at = p0 - out * (t * 1.5f);
					box(b.cap, {at.x - t * .5f, low, at.z - t * .5f}, {at.x + t * .5f, top, at.z + t * .5f});
				}
			}
		};
		side({-ex, 0, -ez}, {ex, 0, -ez}, {0, 0, -1});
		side({-ex, 0, ez}, {ex, 0, ez}, {0, 0, 1});
		side({-ex, 0, -ez}, {-ex, 0, ez}, {-1, 0, 0});
		side({ex, 0, -ez}, {ex, 0, ez}, {1, 0, 0});
	}
	for (auto& [name, builder] : meshes) {
		auto& g = builder.geometry[CityMetal];
		if (g.indices.empty())
			continue;
		const Vec color = d.Color(name);
		auto e = r.create();
		r.emplace<NameComponent>(e, "Chrono Slope ground " + name);
		r.emplace<TransformComponent>(e).scale = {1, 1, 1};
		auto& mr = r.emplace<MeshRendererComponent>(e);
		mr.modelHandle = render->CreateDynamicMesh(g.vertices, g.indices);
		mr.textureHandle = white_;
		mr.extraTextureHandles = lowMaps_;
		mr.shaderName = "LowpolySurface";
		mr.useCubemap = false;
		mr.color = {color.x, color.y, color.z, 1};
	}

	// ---- 物: 地面の高さに置く。大きな物の足元は、ばらまく物が避ける ----
	int seed = 4100;
	auto noise = [&] { return StageNoise(++seed); };
	auto pick = [&](const std::vector<std::string>& ids) -> const std::string& {
		return ids[size_t(noise() * float(ids.size())) % ids.size()];
	};
	std::vector<CityRect> big, landmarks;
	// 置いた物の上から見た範囲 (向きに合わせた軸に沿う箱)
	auto footprint = [&](const std::string& id, Vec at, float scale, float yaw) {
		const auto& model = LowModelOf(id);
		const Vec size = (model.hi - model.lo) * scale, mid = (model.lo + model.hi) * (.5f * scale);
		const float cs = std::cos(yaw), sn = std::sin(yaw);
		const Vec c = at + Vec{mid.x * cs + mid.z * sn, 0, -mid.x * sn + mid.z * cs};
		const float hx = .5f * (std::abs(cs) * size.x + std::abs(sn) * size.z),
		            hz = .5f * (std::abs(sn) * size.x + std::abs(cs) * size.z);
		return CityRect{c.x - hx, c.z - hz, c.x + hx, c.z + hz};
	};
	auto overlaps = [](const CityRect& a, const CityRect& b, float margin) {
		return a.x0 < b.x1 + margin && a.x1 > b.x0 - margin && a.z0 < b.z1 + margin && a.z1 > b.z0 - margin;
	};
	auto inBig = [&](Vec at, float margin) {
		for (const auto& b : big)
			if (at.x > b.x0 - margin && at.x < b.x1 + margin && at.z > b.z0 - margin && at.z < b.z1 + margin)
				return true;
		return false;
	};
	// 建物 (背の高い大きな物) の足元: 正面のある物がそちらを向かないように使う。occupied は置いた物すべての足元 (重ねて置かない)
	std::vector<CityRect> solid, occupied;
	auto inSolid = [&](Vec at) {
		for (const auto& b : solid)
			if (at.x > b.x0 && at.x < b.x1 && at.z > b.z0 && at.z < b.z1)
				return true;
		return false;
	};
	auto isFront = [&](const std::string& id) {
		return std::find(d.fronts.begin(), d.fronts.end(), id) != d.fronts.end();
	};
	// 足元の点: 向きに合わせて回した足元の四隅・辺の中点・中心 (隅は少し内へ寄せる)
	auto footPoints = [&](const std::string& id, Vec at, float scale, float yaw, std::array<Vec, 9>& out) {
		const auto& model = LowModelOf(id);
		const float cs = std::cos(yaw), sn = std::sin(yaw), in = kPlaceInset * m;
		const float x0 = std::min(model.lo.x * scale + in, 0.f), x1 = std::max(model.hi.x * scale - in, 0.f),
		            z0 = std::min(model.lo.z * scale + in, 0.f), z1 = std::max(model.hi.z * scale - in, 0.f),
		            xm = (x0 + x1) * .5f, zm = (z0 + z1) * .5f;
		const float xs[9] = {x0, x1, x1, x0, xm, x1, xm, x0, xm}, zs[9] = {z0, z0, z1, z1, z0, zm, z1, zm, zm};
		for (int k = 0; k < 9; ++k)
			out[size_t(k)] = at + Vec{xs[k] * cs + zs[k] * sn, 0, -xs[k] * sn + zs[k] * cs};
	};
	// 足元の一番低い地面 (斜面でも物が浮かないように、一番低い所に置く)
	auto lowestUnder = [&](const std::string& id, Vec at, float scale, float yaw) {
		std::array<Vec, 9> pts;
		footPoints(id, at, scale, yaw, pts);
		float low = 1e9f;
		for (const auto& p : pts)
			low = std::min(low, SlopeGround(p));
		return low;
	};
	// 置いた物 (あとでその場で動かす。道を往復する物は moving に入れて除く)
	std::vector<std::pair<entt::entity, std::string>> placedProps;
	std::vector<entt::entity> moving;
	// 確かめずに置く (決めて置く物・家・塀・線に沿う物・乗り物)。足元の一番低い地面に置く
	auto put = [&](const std::string& id, Vec at, float yaw, float fill, float scale, float sink = 0) {
		at.y = lowestUnder(id, at, scale, yaw) - sink;
		auto e = LowProp(r, id, at, scale, yaw, fill);
		placedProps.push_back({e, id});
		const auto foot = footprint(id, at, scale, yaw);
		occupied.push_back(foot);
		if (std::max(foot.x1 - foot.x0, foot.z1 - foot.z0) >= kBigSize * m) {
			big.push_back(foot);
			const auto& model = LowModelOf(id);
			if ((model.hi.y - model.lo.y) * scale >= kSolidHeight * m) {
				solid.push_back(foot);
				if (std::find(d.glideThrough.begin(), d.glideThrough.end(), id) == d.glideThrough.end())
					townSolids_.push_back(
					    {foot.x0, foot.z0, foot.x1, foot.z1, at.y + (model.hi.y - model.lo.y) * scale, uint32_t(e)});
			}
		}
		return e;
	};
	// 確かめてから置く: 足元のどの点も町の中・置いてよい種類のマス・石段から離れていて、地面の高さがそろい (擁壁の縁にかからない)、
	// ほかの物の足元と重ならない所だけ。正面のある物は、正面の先が建物・高い段・深い段でない向きにする (どちらの向きもだめなら置かない)
	auto tryPut = [&](const std::string& id, Vec at, float yaw, float fill, float scale,
	                  uint32_t kinds) -> entt::entity {
		std::array<Vec, 9> pts;
		footPoints(id, at, scale, yaw, pts);
		float low = 1e9f, high = -1e9f;
		for (const auto& p : pts) {
			if (!SlopeInside(p, kEdgeMargin * m) || !(kinds & (1u << int(SlopeKindAt(p)))) ||
			    SlopeOnStairs(p, kStairsMargin * m))
				return entt::null;
			const float g = SlopeGround(p);
			low = std::min(low, g);
			high = std::max(high, g);
		}
		const auto foot = footprint(id, at, scale, yaw);
		const bool large = std::max(foot.x1 - foot.x0, foot.z1 - foot.z0) >= kBigSize * m;
		if (high - low > (large ? kPlaceLevelBig : kPlaceLevel) * m)
			return entt::null;
		for (const auto& o : occupied)
			if (overlaps(foot, o, -kPlaceTouch * m))
				return entt::null;
		if (isFront(id)) {
			const auto& model = LowModelOf(id);
			auto open = [&](float y) {
				const Vec f{std::sin(y), 0, std::cos(y)};
				const float reach = std::max(model.hi.z, -model.lo.z) * scale + kFrontProbe * m;
				const Vec probe = at + f * reach;
				const float rise = SlopeGround(probe) - low;
				return SlopeInside(probe, 0) && !inSolid(probe) && rise < kFrontRise * m && rise > -kFrontDrop * m;
			};
			if (!open(yaw)) {
				if (!open(yaw + kTau * .5f))
					return entt::null;
				yaw += kTau * .5f;
			}
		}
		return put(id, at, yaw, fill, scale);
	};
	constexpr uint32_t kAnyGround =
	    ~((1u << int(SlopeKind::Stairs)) | (1u << int(SlopeKind::Water)) | (1u << int(SlopeKind::Rail)));
	constexpr uint32_t kOffRoad = kAnyGround & ~(1u << int(SlopeKind::Road));
	// 組を置く: 起点と向きに合わせて 1 つずつ確かめて置く。起点と高さが違う所の物は置かない
	auto placeSet = [&](const SlopeSet& set, Vec origin, float yaw, float fill, bool mirror, uint32_t kinds) {
		const Vec f{std::sin(yaw), 0, std::cos(yaw)}, right{std::cos(yaw), 0, -std::sin(yaw)};
		const float base = SlopeGround(origin);
		int placed = 0;
		for (const auto& item : set.items) {
			const Vec at = origin + right * (mirror ? -item.at.x : item.at.x) + f * item.at.z;
			if (std::abs(SlopeGround(at) - base) > kSetLevel * m)
				continue;
			if (tryPut(item.id, at, yaw + (mirror ? -item.yaw : item.yaw), fill, cm * item.mult, kinds) != entt::null)
				++placed;
		}
		return placed;
	};
	// 1 つずつ置く物 (目印の物の足元には、あとで家を建てない)
	// (電柱とこいのぼりは、坂の町のギミックに使う: 電線をつたう・こいのぼりで飛ぶ。DevourTownGimmick.cpp)
	std::vector<TownPole> poles;
	std::vector<TownGlider> gliders;
	auto heightOf = [&](const std::string& id, float scale) {
		const auto& model = LowModelOf(id);
		return (model.hi.y - model.lo.y) * scale;
	};
	// おみこしとこまの輪: 書いた場所の近くで、町の中の平らな地面 (石段・川・線路でない所) へ寄せる。輪の上には家を建てない
	std::vector<Vec> mikoshiAt, komaAt, hydrantAt;
	{
		auto level = [&](Vec p) {
			const float g = SlopeGround(p);
			for (int k = 0; k < 8; ++k) {
				const float a = kTau * float(k) / 8;
				if (std::abs(SlopeGround(p + Vec{std::cos(a), 0, std::sin(a)} * (kRideLevelReach * m)) - g) >
				    kRideLevel * m)
					return false;
			}
			return true;
		};
		auto settle = [&](Vec want) {
			for (int ring = 0; ring <= kRideSearch; ++ring)
				for (int k = 0; k < (ring ? 12 : 1); ++k) {
					const float a = kTau * float(k) / 12;
					const Vec p = want + Vec{std::cos(a), 0, std::sin(a)} * (float(ring) * m);
					const auto kind = SlopeKindAt(p);
					if (!SlopeInside(p, kRideEdge * m) || kind == SlopeKind::Stairs || kind == SlopeKind::Water ||
					    kind == SlopeKind::Rail || SlopeOnStairs(p, kRideEdge * m) || !level(p))
						continue;
					return Vec{p.x, SlopeGround(p), p.z};
				}
			return Vec{want.x, SlopeGround(want), want.z};
		};
		for (const auto& p : d.mikoshi)
			mikoshiAt.push_back(settle(p));
		for (const auto& p : d.koma)
			komaAt.push_back(settle(p));
		for (const auto& p : d.hydrants)
			hydrantAt.push_back(settle(p));
		// (消火栓は道の上なので家の敷地とは重ならない。家を止めると町の並びが変わるので、家は止めない)
		for (const auto* list : {&mikoshiAt, &komaAt})
			for (const auto& p : *list)
				landmarks.push_back(
				    {p.x - kRideKeep * m, p.z - kRideKeep * m, p.x + kRideKeep * m, p.z + kRideKeep * m});
	}
	for (const auto& p : d.props) {
		const float scale = p.fit > 0 ? LowScaleFor(p.id, p.fit) : cm * p.mult;
		const auto e = put(p.id, p.at, p.yaw, p.fill, scale);
		if (p.landmark)
			landmarks.push_back(footprint(p.id, p.at, scale, p.yaw));
		if (p.id == "koinobori")
			gliders.push_back({uint32_t(e), Vec{p.at.x, SlopeGround(p.at), p.at.z}, heightOf(p.id, scale)});
	}
	// 家の敷地: 面する道の種類で家を選び、敷地に収まる大きさで道へ向けて建てる。道と同じ高さの敷地は道ぞいに塀を立て、角に庭の小物を置く
	for (const auto& lot : d.lots) {
		const Vec f{std::sin(lot.yaw), 0, std::cos(lot.yaw)}, right{std::cos(lot.yaw), 0, -std::sin(lot.yaw)};
		const float hw = lot.width * .5f, hd = lot.depth * .5f, ex = std::abs(right.x) * hw + std::abs(f.x) * hd,
		            ez = std::abs(right.z) * hw + std::abs(f.z) * hd;
		const CityRect area{lot.at.x - ex, lot.at.z - ez, lot.at.x + ex, lot.at.z + ez};
		bool blocked = false;
		for (const auto& l : landmarks)
			blocked |= overlaps(area, l, -kLandmarkSlack * m);
		if (blocked)
			continue;
		const auto* road = d.Road(lot.road);
		const std::vector<std::string>* ids = &d.houses.fallback;
		if (road)
			for (const auto& [kind, list] : d.houses.byRoad)
				if (kind == road->kind && !list.empty())
					ids = &list;
		const std::string& id = pick(*ids);
		const auto& model = LowModelOf(id);
		const float sx = std::max(model.hi.x - model.lo.x, 1.f), sz = std::max(model.hi.z - model.lo.z, 1.f);
		put(id, lot.at, lot.yaw, d.houses.fill,
		    std::min(cm, std::min(lot.width * d.houses.fit / sx, lot.depth * d.houses.fit / sz)), d.houses.sink);
		const float street = SlopeGround(lot.at + f * (hd + kStreetProbe * m));
		if (!d.houses.fences.empty() && std::abs(street - lot.at.y) < kFenceLevel * m)
			for (float sgn : {-1.f, 1.f})
				put(pick(d.houses.fences), lot.at + f * (hd - kFenceInset * m) + right * (sgn * (hw - kFenceSide * m)),
				    lot.yaw, d.houses.fenceFill, cm);
		// 庭の組: 家のモデルの組 (駄菓子屋の店先など) を先に、残りは面する道の種類の候補から重ならないように選ぶ。
		// 前の左右の角、後ろの左右の角の順に、角から敷地の内へ向けて置く (左の角は左右を返す)。前の組は道へ、後ろの組は家へ向ける
		std::vector<std::string> chosen;
		for (const auto& [house, list] : d.lotSets.byHouse)
			if (house == id && !list.empty())
				chosen.push_back(pick(list));
		const std::vector<std::string>* pool = &d.lotSets.fallback;
		if (road)
			for (const auto& [kind, list] : d.lotSets.byRoad)
				if (kind == road->kind && !list.empty())
					pool = &list;
		for (int tries = 0; int(chosen.size()) < d.lotSets.count && tries < 16; ++tries) {
			const auto& name = pick(*pool);
			if (std::find(chosen.begin(), chosen.end(), name) == chosen.end())
				chosen.push_back(name);
		}
		for (size_t k = 0; k < chosen.size() && int(k) < d.lotSets.count; ++k) {
			const float side = k % 2 ? 1.f : -1.f;
			const bool back = k >= 2;
			const Vec corner =
			    lot.at + right * (side * (hw - kYardInset * m)) + f * ((back ? -1.f : 1.f) * (hd - kYardInset * m));
			placeSet(
			    d.Set(chosen[k]), corner, lot.yaw + (back ? kTau * .5f : 0.f), d.lotSets.fill, (side > 0) != back,
			    kOffRoad);
		}
	}
	// 線に沿って並べる物
	for (const auto& row : d.rows) {
		const Vec span = row.to - row.from;
		const float length = Length(span);
		const Vec dir = length > 0 ? span * (1 / length) : Vec{1, 0, 0};
		int k = 0;
		for (float s = 0; s <= length + .01f; s += row.step, ++k) {
			if (row.skip > 0 && noise() < row.skip)
				continue;
			const float shift = row.jitter > 0 ? (noise() - .5f) * 2 * row.jitter : 0;
			put(row.ids[size_t(k) % row.ids.size()], row.from + dir * s + Vec{-dir.z, 0, dir.x} * shift, row.yaw,
			    row.fill, cm * row.mult);
		}
	}
	// 道ぞいの物 (電柱・カーブミラー): 左右交互に、道の向きにそろえる
	for (const auto& side : d.roadside)
		for (const auto& road : d.roads) {
			if (!side.roads.empty() && std::find(side.roads.begin(), side.roads.end(), road.id) == side.roads.end())
				continue;
			const float L = SlopeRoadLength(road);
			int k = 0;
			for (float s = side.step * .5f; s < L; s += side.step, ++k) {
				Vec dir;
				const Vec p = SlopeRoadPoint(road, s, &dir);
				const float sgn = k % 2 ? 1.f : -1.f;
				// 道のマスにかからない所まで外へずらしてみる (交差点や曲がり角で道の真ん中に立たないように)
				// (置けなければ道の反対側でも試す)
				for (int step = 0; step < 8; ++step) {
					const float side2 = step < 4 ? sgn : -sgn;
					const Vec at =
					    p + Vec{-dir.z, 0, dir.x} *
					            (side2 * (road.width * .5f + side.offset + float(step % 4) * kRoadsideStep * m));
					const auto& id = side.ids[size_t(k) % side.ids.size()];
					const auto e = tryPut(
					    id, at, std::atan2(dir.x, dir.z) + (side2 > 0 ? kTau * .5f : 0.f), side.fill, cm, kOffRoad);
					if (e == entt::null)
						continue;
					if (id == "denchu")
						poles.push_back(
						    {uint32_t(e), Vec{at.x, SlopeGround(at), at.z}, heightOf(id, cm),
						     int(&road - d.roads.data()), k});
					break;
				}
			}
		}
	// 道ぞいの組 (ごみ置き場・自販機など): 決まった種類の道に、間隔ごとに左右交互に置く。組の前を道へ向ける
	for (const auto& side : d.roadSets)
		for (const auto& road : d.roads) {
			if (!side.kinds.empty() && std::find(side.kinds.begin(), side.kinds.end(), road.kind) == side.kinds.end())
				continue;
			const float L = SlopeRoadLength(road);
			int k = 0;
			for (float s = side.start; s < L; s += side.step, ++k) {
				Vec dir;
				const Vec p = SlopeRoadPoint(road, s, &dir);
				const float sgn = k % 2 ? 1.f : -1.f;
				const Vec out = Vec{-dir.z, 0, dir.x} * sgn;
				const Vec at = p + out * (road.width * .5f + side.offset);
				if (!SlopeInside(at, kEdgeMargin * m) || SlopeOnStairs(at, kStairsMargin * m))
					continue;
				placeSet(
				    d.Set(pick(side.sets)), at, std::atan2(-out.x, -out.z), side.fill, false,
				    side.onRoad ? kAnyGround : kOffRoad);
			}
		}
	// 決まった所に置く組 (公園の遊び道具・境内の鳩など)
	for (const auto& spot : d.spots)
		placeSet(d.Set(spot.set), spot.at, spot.yaw, spot.fill, false, kAnyGround);
	// 道の上に並べる物 (スタートから坂へ誘うおやつの列)
	for (const auto& trail : d.trails) {
		const auto& road = *d.Road(trail.road);
		int k = 0;
		for (float s = trail.from; s <= trail.to; s += trail.step, ++k) {
			Vec dir;
			const Vec p = SlopeRoadPoint(road, s, &dir);
			tryPut(
			    trail.ids[size_t(k) % trail.ids.size()],
			    p + Vec{-dir.z, 0, dir.x} * ((noise() - .5f) * road.width * .4f), noise() * kTau, trail.fill, cm,
			    kAnyGround);
		}
	}
	// 往復する物: 道を行って戻る (道の上は高さに合わせて上り下りする。TickDevourMovers)
	for (const auto& mover : d.movers) {
		std::vector<Vec> path;
		if (!mover.road.empty()) {
			const auto& road = *d.Road(mover.road);
			path = road.path;
			for (size_t i = road.path.size() - 1; i-- > 1;)
				path.push_back(road.path[i]);
		} else {
			path = mover.path;
			for (auto& p : path)
				p.y = SlopeGround(p);
		}
		const auto e = put(mover.id, path[0], 0, mover.fill, cm);
		moving.push_back(e);
		AddRoute(e, path, mover.speed, noise());
	}
	// 道を歩く物 (犬・自転車): 決まった種類の道の、端に寄せた一部を往復する (前は +x。坂は地面に合わせて上り下りする)
	for (const auto& walker : d.walkers) {
		std::vector<const SlopeRoad*> roads;
		for (const auto& road : d.roads)
			if (walker.kinds.empty() ||
			    std::find(walker.kinds.begin(), walker.kinds.end(), road.kind) != walker.kinds.end())
				if (SlopeRoadLength(road) > walker.length + 2 * kWalkerEnd * m)
					roads.push_back(&road);
		for (int k = 0; k < walker.count && !roads.empty(); ++k) {
			const auto& road = *roads[size_t(noise() * float(roads.size())) % roads.size()];
			const float L = SlopeRoadLength(road),
			            s0 = kWalkerEnd * m + noise() * (L - walker.length - 2 * kWalkerEnd * m),
			            sgn = noise() < .5f ? -1.f : 1.f;
			std::vector<Vec> path;
			for (float s = s0; s <= s0 + walker.length + .01f; s += kWalkerStep * m) {
				Vec dir;
				const Vec p = SlopeRoadPoint(road, s, &dir);
				path.push_back(p + Vec{-dir.z, 0, dir.x} * (sgn * (road.width * .5f - walker.inset)));
			}
			if (path.size() < 2)
				continue;
			bool clearPath = true;
			for (const auto& p : path)
				clearPath &= SlopeInside(p, kEdgeMargin * m) && !SlopeOnStairs(p, kStairsMargin * m);
			if (!clearPath)
				continue;
			for (size_t i = path.size() - 1; i-- > 1;)
				path.push_back(path[i]);
			for (auto& p : path)
				p.y = SlopeGround(p);
			const auto e = put(walker.id, path[0], 0, walker.fill, cm);
			moving.push_back(e);
			AddRoute(e, path, walker.speed, noise());
		}
	}
	// ばらまく物: 置いてよい種類のマスだけ。石段・擁壁のきわ・大きな物の足元は避ける (sets のときは組で置く: 鳥の群れ・畑の道具など)
	auto clear = [&](Vec at, uint32_t kinds) {
		if (!(kinds & (1u << int(SlopeKindAt(at)))))
			return false;
		return SlopeInside(at, kEdgeMargin * m) && !SlopeOnStairs(at, kStairsMargin * m) &&
		       !SlopeNearWall(at, kWallMargin * m) && !inBig(at, kBigMargin * m);
	};
	for (const auto& s : d.scatters) {
		int placed = 0;
		for (int tries = 0; placed < s.count && tries < s.count * kScatterTries; ++tries) {
			const Vec at{
			    s.rect.x0 + (s.rect.x1 - s.rect.x0) * noise(), 0, s.rect.z0 + (s.rect.z1 - s.rect.z0) * noise()};
			if (!clear(at, s.kinds))
				continue;
			if (!s.sets.empty()) {
				if (placeSet(d.Set(pick(s.sets)), at, noise() * kTau, s.fill, noise() < .5f, s.kinds & kAnyGround) > 0)
					++placed;
				continue;
			}
			if (tryPut(
			        pick(s.ids), at, noise() * kTau, s.fill, cm * (s.multLow + (s.multHigh - s.multLow) * noise()),
			        s.kinds & kAnyGround) != entt::null)
				++placed;
		}
	}
	// その場を回って歩く物 (鳥・猫・犬): 置いた所を通る小さな輪 (楕円) を回る。輪の点がどこも同じ高さの歩ける所で、建物や大きな物にかからないときだけ。
	// 回らない物と、ほかのその場で動く物 (風鈴・洗濯物・木) は、モデルごとに揺れたり跳ねたりする。始まりの拍はばらばら
	for (const auto& [e, id] : placedProps) {
		if (std::find(moving.begin(), moving.end(), e) != moving.end())
			continue;
		if (const auto* wander = d.Wander(id); wander && noise() < wander->chance) {
			const auto& t = r.get<TransformComponent>(e);
			const Vec at{t.translate.x, 0, t.translate.z};
			const float g0 = SlopeGround(at);
			const float rx = wander->radius * (.6f + .4f * noise()), rz = wander->radius * (.6f + .4f * noise()),
			            turn = noise() * kTau, cs = std::cos(turn), sn = std::sin(turn),
			            dir = noise() < .5f ? 1.f : -1.f;
			const Vec center = at - Vec{rx * cs, 0, rx * sn};
			std::vector<Vec> loop;
			bool ok = true;
			for (int k = 0; k < kWanderPoints && ok; ++k) {
				const float a = dir * kTau * float(k) / float(kWanderPoints);
				const float lx = std::cos(a) * rx, lz = std::sin(a) * rz;
				Vec p = center + Vec{lx * cs - lz * sn, 0, lx * sn + lz * cs};
				const auto kind = SlopeKindAt(p);
				ok = SlopeInside(p, kEdgeMargin * m) && kind != SlopeKind::Stairs && kind != SlopeKind::Water &&
				     kind != SlopeKind::Rail && !SlopeOnStairs(p, kStairsMargin * m) && !inBig(p, kSetBigMargin * m) &&
				     std::abs(SlopeGround(p) - g0) < kWanderLevel * m;
				p.y = SlopeGround(p);
				loop.push_back(p);
			}
			if (ok) {
				moving.push_back(e);
				AddRoute(e, loop, wander->speed, noise(), wander->frontZ);
				continue;
			}
		}
		if (const auto* motion = d.Motion(id))
			AddDance(r, e, motion->speed, motion->sway, motion->hop);
	}
	// 電線とこいのぼりのギミック
	BuildTownGimmicks(r, poles, gliders);
	BuildTownRides(r, mikoshiAt, komaAt, hydrantAt);
	// 変形アイテム (devour_stages.json の itemAt。y は地面の高さに合わせて書く)
	BuildStageItem(r, tools);
}
} // namespace Game
