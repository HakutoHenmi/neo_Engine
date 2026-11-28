// ChallengeBlocks.cpp
// 変形チャレンジの物 (ChronoSystem の一部)。今は「積み木クラッシャー」(規則と配置は Chrono/ChallengeRules.h)。
//  - 積み木は能力の衝撃 (BattleImpact) を受けて壊れる。効き目ごとに反応が違うので、44 種のどの能力でも何かが起きる
//      当てる: 削る (硬い積み木は弱い当たりが効かない)・引き寄せる: 吸い寄せて回収 (硬い積み木は動かない)・押し出す (においの雲): 少しずつ削る
//      止める: 当たりがあれば削る。無ければひび (まんが)・弱らせる: ひび・やけど: 硬さを無視して少しずつ削る・閉じ込める: そのまま回収
//      場所に置く: 色ぬりの床の上の積み木は得点が上がる
//  - 下の積み木が無くなると、上の積み木は落ちて次の支えに乗る
//  - どのチャレンジでも同じ、自分に効く能力 (ApplyChallengeSelf): 回復で時間が増える・リモコンで時計が止まる・守る/だるまで体当たり
//  - 専用の立体ステージ「積み木の街」(形と配置は Chrono/BlocksLandRules.h、見た目と体の止め方は ChallengeBlocksLand.cpp)。
//    低い所からの短い当たりは、崖の上の積み木に届かない
#include "ChronoSystemInternal.h"
#include <chrono>
#include <future>
#include <thread>
#include <random>

namespace Game {
namespace {
std::mt19937& BlockRandom() {
	static std::mt19937 random{std::random_device{}()};
	return random;
}
float BR(float low, float high) {
	return std::uniform_real_distribution<float>(low, high)(BlockRandom());
}
// 積み木の色: 普通はおもちゃの色を並べる。硬いは青灰色、金色は金
Engine::Vector4 BlockColor(BlockType type, uint32_t id) {
	static constexpr Engine::Vector4 toys[] = {{.95f, .42f, .38f, 1}, {.98f, .78f, .3f, 1}, {.4f, .72f, .95f, 1},
	                                           {.5f, .85f, .45f, 1},  {.95f, .6f, .85f, 1}, {.98f, .95f, .88f, 1}};
	switch (type) {
	case BlockType::Hard:
		return {.42f, .48f, .58f, 1};
	case BlockType::Gold:
		return {1, .78f, .18f, 1};
	default:
		return toys[id % 6];
	}
}
} // namespace

// 積み木の街か (積み木クラッシャーで、専用ステージの配置のとき)
bool ChronoSystem::BlocksStageActive() const {
	const auto& b = battle_;
	return b.arenaRadius > 0 && b.challenge == ChallengeKind::Blocks &&
	       b.blockPattern == ChallengeBlocksStagePattern;
}

// 結果を「こわした積み木の山」で見せるか: 積み木クラッシャーを遊んで (やり直しも含む) 終わったとき
bool ChronoSystem::BlocksPileResult() const {
	const auto& b = battle_;
	return devour_ && finished_ && b.tries > 0 && b.challenge == ChallengeKind::Blocks;
}
int ChronoSystem::BlocksPileCount() const {
	const auto& b = battle_;
	return b.broken[0] + b.broken[1] + b.broken[2];
}
// こわした積み木を、山に積む順に並べる (普通と硬いを先に、金を最後に。色は場で見えていた色)
void ChronoSystem::BlocksPileBlocks(std::vector<Engine::Vector4>& colors, std::vector<bool>& gold) const {
	colors.clear();
	gold.clear();
	for (int pass = 0; pass < 2; ++pass)
		for (const auto& blk : battle_.blocks)
			if (!blk.alive && (blk.type == BlockType::Gold) == (pass == 1)) {
				colors.push_back(BlockColor(blk.type, blk.id));
				gold.push_back(pass == 1);
			}
}

namespace {
// 景色の模様 (決まった乱数・値のノイズ・何段も重ねたノイズ)。長さは m
uint32_t PileHash(uint32_t v) {
	v ^= v >> 16;
	v *= 0x7feb352du;
	v ^= v >> 15;
	v *= 0x846ca68bu;
	return v ^ (v >> 16);
}
float PileRand(uint32_t& s) {
	s = PileHash(s + 0x9e3779b9u);
	return float(s >> 8) / 16777216.f;
}
float PileLattice(int64_t x, int64_t z, uint32_t seed) {
	return float(PileHash(uint32_t(x) * 73856093u ^ uint32_t(z) * 19349663u ^ uint32_t(x >> 32) * 83492791u ^ seed) >> 8) /
	       16777216.f;
}
// (-1〜1)
float PileNoise(double x, double z, uint32_t seed) {
	const double fx = std::floor(x), fz = std::floor(z);
	const int64_t ix = int64_t(fx), iz = int64_t(fz);
	float tx = float(x - fx), tz = float(z - fz);
	tx = tx * tx * (3 - 2 * tx);
	tz = tz * tz * (3 - 2 * tz);
	const float a = PileLattice(ix, iz, seed), b = PileLattice(ix + 1, iz, seed), c = PileLattice(ix, iz + 1, seed),
	            d = PileLattice(ix + 1, iz + 1, seed);
	return (a + (b - a) * tx + (c - a) * tz + (a - b - c + d) * tx * tz) * 2 - 1;
}
// 波の長さ longest から shortest まで、半分ずつ (細かい段ほど弱い)
float PileFbm(double x, double z, double longest, double shortest, uint32_t seed) {
	float sum = 0, amp = 1;
	for (double wl = longest; wl >= shortest; wl *= .5) {
		sum += amp * PileNoise(x / wl, z / wl, seed);
		amp *= .55f;
		seed = PileHash(seed);
	}
	return sum;
}
float PileSmooth(float a, float b, float v) {
	const float t = std::clamp((v - a) / (b - a), 0.f, 1.f);
	return t * t * (3 - 2 * t);
}
Vec PileMix(Vec a, Vec b, float t) {
	return a + (b - a) * t;
}
// 地球の絵の範囲: 真ん中 (場の真ん中) から PileEarthNear (m) ~ 地球の 4 分の 1 周まで。縦は距離の対数、横は向き
constexpr int PileEarthW = 2048, PileEarthH = 2048;
constexpr double PileEarthRadius = 6371000;
double PileEarthFar() {
	return PileEarthRadius * 1.5707963;
}
// 地球の絵の 1 点の色 (r は真ん中からの距離 m、x・z は位置 m、t はこの点の絵の 1 マスの大きさ m)。
// どの大きさで見ても模様があるように、その大きさまでのノイズを重ねる: 大陸と海・森と草原と乾いた土地と山・畑のつぎはぎとあぜ・川・町
Vec PileEarthColor(double r, double x, double z, double t) {
	const double fine = std::max(3 * t, 1.);
	// 海 (近くは陸。遠くは大陸の形のノイズ。海岸は細かい所まで入り組む)
	bool water = false;
	float coast = 1;
	// (山のまわりの大陸は、遠くへ行くほどなめらかに海へ変わる。境目の丸が見えないように、近くでも同じノイズを使う)
	{
		const float m = PileFbm(x, z, 5e6, std::max(fine, 200.), 11) + 2.4f * float(std::exp(-std::pow(r / 1.1e6, 2))) - .1f;
		water = m < 0;
		coast = std::abs(m);
	}
	if (water) {
		const Vec deep{.07f, .22f, .48f}, shallow{.2f, .5f, .68f};
		return PileMix(shallow, deep, PileSmooth(0, .35f, coast));
	}
	// 湖 (真ん中のすぐそばには作らない)
	if (r > 900 && PileFbm(x, z, 24000, std::max(fine, 30.), 23) > .78f)
		return {.22f, .48f, .7f};
	// 川 (ノイズの尾根。陸の上を細く曲がる)
	if (r > 600 && std::abs(PileFbm(x, z, 300000, std::max(fine, 30.), 37)) < .012f)
		return {.26f, .5f, .72f};
	// 地方の色: 草原・森・乾いた土地・砂漠・山 (雪)
	const Vec meadow{.44f, .64f, .3f}, forest{.2f, .42f, .2f}, dry{.7f, .64f, .4f}, sand{.86f, .75f, .52f},
	    rock{.52f, .47f, .41f}, snow{.94f, .95f, .97f};
	const float wet = PileFbm(x, z, 3e6, std::max(fine, 5000.), 41) + .8f * float(std::exp(-std::pow(r / 2e6, 2))), wood = PileFbm(x, z, 250000, std::max(fine, 8.), 43);
	Vec c = PileMix(meadow, forest, PileSmooth(-.1f, .45f, wood));
	c = PileMix(c, dry, PileSmooth(-.2f, -.55f, wet));
	c = PileMix(c, sand, PileSmooth(-.55f, -.85f, wet));
	const float ridge = 1 - std::abs(PileFbm(x, z, 800000, std::max(fine, 400.), 47));
	c = PileMix(c, rock, PileSmooth(.82f, .93f, ridge) * (r > 30000 ? 1.f : 0.f));
	c = PileMix(c, snow, PileSmooth(.95f, .99f, ridge) * (r > 30000 ? 1.f : 0.f));
	// 畑のつぎはぎ (近いほど多い)。区切りの大きさごとに、見える大きさ (絵のマスの 4 倍) 以上なら描く。区切りの境目はあぜ (濃い緑)
	const float farm = PileSmooth(120, 400, float(r)) * std::clamp(1.1f - float(r) / 40000.f + PileFbm(x, z, 60000, 15000, 53) * .5f, 0.f, 1.f) *
	                   (1 - PileSmooth(.2f, .5f, wood)) * (1 - PileSmooth(-.3f, -.6f, wet));
	if (farm > 0) {
		static const Vec crops[] = {{.82f, .76f, .42f}, {.58f, .76f, .36f}, {.36f, .58f, .27f},
		                            {.56f, .46f, .32f}, {.46f, .68f, .3f},  {.66f, .8f, .4f}};
		const double cells[] = {2400, 600, 150, 40};
		for (int n = 0; n < 4; ++n) {
			const double cell = cells[n];
			const float see = PileSmooth(float(4 * t), float(10 * t), float(cell));
			if (see <= 0)
				continue;
			const double ang = .5 + n * 1.1, ca = std::cos(ang), sa = std::sin(ang);
			const double u = (x * ca - z * sa) / cell, v = (x * sa + z * ca) / cell * 1.7;
			const double fu = std::floor(u), fv = std::floor(v);
			const uint32_t h = PileHash(uint32_t(int64_t(fu)) * 9781u ^ uint32_t(int64_t(fv)) * 6271u ^ uint32_t(n) * 131u);
			c = PileMix(c, crops[h % 6], see * farm * .8f);
			const double edge = std::min({u - fu, 1 - (u - fu), (v - fv) / 1.7, (1 - (v - fv)) / 1.7}) * cell;
			if (edge < t * .8 && cell > 12 * t)
				c = PileMix(c, Vec{.24f, .4f, .2f}, see * farm * .7f);
		}
	}
	// 町 (区切り 3km に 1 つくらい・大きな街は 60km に 1 つくらい。屋根の色が混ざる)
	auto town = [&](double cell, float chance, double radius, uint32_t seed) {
		const double fx = std::floor(x / cell), fz = std::floor(z / cell);
		const uint32_t h = PileHash(uint32_t(int64_t(fx)) * 7919u ^ uint32_t(int64_t(fz)) * 104729u ^ seed);
		if (float(h & 0xffff) / 65536.f > chance)
			return;
		const double cx = (fx + .25 + .5 * double((h >> 16) & 255) / 255) * cell,
		             cz = (fz + .25 + .5 * double((h >> 24) & 255) / 255) * cell;
		const double d = std::hypot(x - cx, z - cz) / radius + PileNoise(x / (radius * .3), z / (radius * .3), seed) * .3;
		if (d > 1 || radius < 2 * t)
			return;
		const float roof = PileNoise(x / std::max(t * 2, radius * .02), z / std::max(t * 2, radius * .02), seed + 7);
		const Vec city{.74f, .71f, .66f}, red{.72f, .36f, .3f}, gray{.55f, .55f, .56f};
		c = PileMix(PileMix(city, gray, float(d)), red, PileSmooth(.3f, .7f, roof));
	};
	if (r > 400)
		town(3000, .22f, 420, 61);
	if (r > 8000)
		town(60000, .3f, 7000, 67);
	// 細かいむら (近くの草)
	const float grain = PileFbm(x, z, 30, std::max(fine, .3), 71);
	return c * (1 + grain * .08f);
}
// 地球の絵を描く (重いので、結果が始まったときに別の糸で。描き終わるまでは野原の色だけ)
std::vector<uint8_t> PileEarthPaint() {
	const double nearM = double(ChallengeBlocksHalf) * ChallengePileBlockCm / 100 / (ChallengeBlockSize * BlocksPileScale),
	             span = std::log(PileEarthFar() / nearM);
	std::vector<uint8_t> pixels(size_t(PileEarthW) * PileEarthH * 4);
	// (行を 2 つの糸に分けて描く。全部の糸を使うと、遊んでいる間のフレームが遅れる)
	const int workers = 2;
	std::vector<std::thread> threads;
	for (int w = 0; w < workers; ++w)
		threads.emplace_back([&, w] {
	for (int j = w; j < PileEarthH; j += workers) {
		const double r = nearM * std::exp((double(j) + .5) / PileEarthH * span);
		const double t = std::max(r * 6.2831853 / PileEarthW, r * span / PileEarthH);
		for (int i = 0; i < PileEarthW; ++i) {
			const double ph = 6.2831853 * (double(i) + .5) / PileEarthW;
			const Vec c = PileEarthColor(r, r * std::cos(ph), r * std::sin(ph), t);
			uint8_t* px = &pixels[(size_t(j) * PileEarthW + size_t(i)) * 4];
			// (絵は色をそのまま読むので、明るさの線形の値で入れる)
			px[0] = uint8_t(std::clamp(std::pow(c.x, 2.2f), 0.f, 1.f) * 255 + .5f);
			px[1] = uint8_t(std::clamp(std::pow(c.y, 2.2f), 0.f, 1.f) * 255 + .5f);
			px[2] = uint8_t(std::clamp(std::pow(c.z, 2.2f), 0.f, 1.f) * 255 + .5f);
			px[3] = 255;
		}
	}
		});
	for (auto& t : threads)
		t.join();
	return pixels;
}
// 月の絵 (灰色の地に、暗い海と明るい縁のクレーター)
std::vector<uint8_t> PileMoonPaint() {
	constexpr int w = 256, h = 128;
	std::vector<uint8_t> pixels(size_t(w) * h * 4);
	for (int j = 0; j < h; ++j)
		for (int i = 0; i < w; ++i) {
			const double ph = 6.2831853 * i / w, th = 3.1415927 * (j + .5) / h;
			const double x = std::sin(th) * std::cos(ph) * 8, y = std::cos(th) * 8, z = std::sin(th) * std::sin(ph) * 8;
			float v = .72f + PileFbm(x + y * .7, z - y * .3, 4, .05, 91) * .07f;
			if (PileFbm(x * .5 + 3, z * .5 - y * .4, 3, .4, 93) > .25f)
				v *= .62f;
			const float pit = PileNoise(x * 3, z * 3 + y * 2, 97);
			v *= pit > .7f ? .82f : pit > .6f ? 1.12f : 1.f;
			const uint8_t g = uint8_t(std::clamp(std::pow(v, 2.2f), 0.f, 1.f) * 255);
			uint8_t* px = &pixels[(size_t(j) * w + size_t(i)) * 4];
			px[0] = px[1] = px[2] = g;
			px[3] = 255;
		}
	return pixels;
}
// 地球の形 (半径 1 の半球。てっぺんが原点。真ん中から外へ、距離の対数で輪を分ける。絵の縦と同じ割合)
struct PileEarthShape {
	std::vector<Engine::VertexData> vertices;
	std::vector<uint32_t> indices;
};
PileEarthShape PileEarthBuild() {
	PileEarthShape out;
	auto& vertices = out.vertices;
	auto& indices = out.indices;
	const double nearM = double(ChallengeBlocksHalf) * ChallengePileBlockCm / 100 / (ChallengeBlockSize * BlocksPileScale),
	             span = std::log(PileEarthFar() / nearM);
	constexpr int rings = 256, slices = 256;
	for (int k = 0; k <= rings; ++k) {
		const double th = nearM * std::exp(double(k) / rings * span) / PileEarthRadius;
		for (int j = 0; j <= slices; ++j) {
			const double ph = 6.2831853 * double(j) / slices;
			Engine::VertexData v{};
			const double s = std::sin(th), half = std::sin(th * .5);
			v.position = {float(s * std::cos(ph)), float(-2 * half * half), float(s * std::sin(ph)), 1};
			v.normal = {float(s * std::cos(ph)), float(std::cos(th)), float(s * std::sin(ph))};
			v.texcoord = {float(j) / float(slices), float(k) / float(rings)};
			vertices.push_back(v);
		}
	}
	// (表の向き: CityBuilder と同じく、cross(p1-p0, p2-p0) が外向きになる順)
	auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
		const auto &pa = vertices[a].position, &pb = vertices[b].position, &pc = vertices[c].position;
		const Vec e1{pb.x - pa.x, pb.y - pa.y, pb.z - pa.z}, e2{pc.x - pa.x, pc.y - pa.y, pc.z - pa.z};
		const Vec n{vertices[a].normal.x, vertices[a].normal.y, vertices[a].normal.z};
		if (Dot(SwarmCross(e1, e2), n) >= 0)
			indices.insert(indices.end(), {a, b, c});
		else
			indices.insert(indices.end(), {a, c, b});
	};
	for (int k = 0; k < rings; ++k)
		for (int j = 0; j < slices; ++j) {
			const uint32_t a = uint32_t(k * (slices + 1) + j), b = a + 1, c = a + uint32_t(slices + 1), d = c + 1;
			tri(a, c, b);
			tri(b, c, d);
		}
	return out;
}
std::future<std::vector<uint8_t>>& PileEarthJob() {
	static std::future<std::vector<uint8_t>> job;
	return job;
}
std::future<std::vector<uint8_t>>& PileMoonJob() {
	static std::future<std::vector<uint8_t>> job;
	return job;
}
std::future<PileEarthShape>& PileShapeJob() {
	static std::future<PileEarthShape> job;
	return job;
}
template <class T> bool PileReady(std::future<T>& job, bool wait) {
	return job.valid() && (wait || job.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
}
} // namespace

// 結果の積み木の山: こわした積み木が積み木の街の広場 (スタートの近く) に 1 個ずつ降って、立体の山になる (山の形は ChallengePileLayout)。
// 積み木 1 個の大きさは場の積み木の BlocksPileScale 倍。スライムは山のてっぺんに乗って一緒に上る。カメラは BlocksPileCamera。
// 積み終わったら、そのまま同じ積み木を積み続けて、山をひと回りずつ包む殻にする (ChallengePileShells。宇宙まで)。
// 景色: 街は地球の上の野原に置いたおもちゃ。まわりには、遠いほど大きいおもちゃの街・木・池 (遠くでは本物の町や森に見える) と雲。地面は地球の絵
void ChronoSystem::TickBlocksPile(float dt) {
	auto& p = pile_;
	const int count = BlocksPileCount();
	if (!p.active) {
		p = {};
		p.active = true;
		p.slots = ChallengePileLayout(count);
		BlocksPileBlocks(p.colors, p.gold);
		p.block = battle_.unit * ChallengeBlockSize * BlocksPileScale;
		p.at = ArenaPoint(BlocksPileX, BlocksPileZ);
		float half = 0;
		for (const auto& sl : p.slots)
			half = std::max({half, std::abs(sl.x), std::abs(sl.z)});
		p.reach = p.cover = (half + 1) * p.block;
		const auto& shells = UI::Scale::PileShells(count);
		for (int k = 0; k < int(shells.size()); ++k) {
			BlocksPileShell sh;
			sh.shape = shells[size_t(k)];
			sh.at = UI::Scale::PileShellAt(count, k);
			sh.slots = ChallengePileShellLayout(sh.shape, PileHash(uint32_t(k) + 17));
			p.shells.push_back(std::move(sh));
		}
		p.meter = p.block / (ChallengePileBlockCm / 100);
		p.earth = float(PileEarthRadius) * p.meter;
		// (地面は場の床の板の下の面。すり鉢の底より下)
		p.groundY = p.at.y - battle_.unit * BlocksPileGroundDrop;
		p.top = std::max(p.block, 1e-3f);
		p.span = p.reach;
		p.height = 1e-4f;
		// 景色の物 (Blender で作ったローポリのモデル。scripts/lowpoly): 場の真ん中から、距離の対数でまんべんなく (遠い物ほど大きい)。
		// 場の板の上には置かない。近くは野原に転がるおもちゃと木と積み木の塔、遠くは家と林 (遠くでは町と森に見える)。空には雲のモデル
		{
			if (lowPalette_ == 0) {
				auto* render = Engine::Renderer::GetInstance();
				lowPalette_ = render->LoadTexture2D("Resources/Textures/Lowpoly/palette.png");
				lowMaps_ = {render->LoadTexture2D("Resources/Textures/PBR/NeutralNormal.png", false), white_, white_};
			}
			const V center = battle_.arenaAt;
			const float board = (ChallengeBlocksHalf + 3) * battle_.unit;
			uint32_t seed = 12345;
			auto ground = [&](V at) {
				const float d = std::hypot(at.x - center.x, at.z - center.z);
				return p.groundY - d * d / (2 * p.earth);
			};
			// モデルを置く (width は横の大きさ。底を地面から lift の高さに付ける。squash は縦の割合)
			auto place = [&](const char* id, V at, float width, float lift = 0, float squash = 1, bool cloud = false) {
				const auto& m = LowModelOf(id);
				const float s = width / std::max({m.hi.x - m.lo.x, m.hi.z - m.lo.z, 1e-4f});
				BlocksPileDecor d;
				d.mesh = m.mesh;
				d.half = V{s, s * squash, s};
				d.yaw = PileRand(seed) * 6.2831853f;
				d.at = V{at.x, ground(at) + lift - m.lo.y * s * squash, at.z};
				d.size = std::max({m.hi.x - m.lo.x, (m.hi.y - m.lo.y) * squash, m.hi.z - m.lo.z}) * s * .5f;
				d.cloud = cloud;
				p.decor.push_back(d);
			};
			auto block = [&](V at, float half, Vec color) {
				BlocksPileDecor d;
				d.at = V{at.x, ground(at) + at.y, at.z};
				d.half = V{half, half, half};
				d.size = half;
				d.color = Engine::Vector4{color.x, color.y, color.z, 1};
				p.decor.push_back(d);
			};
			auto pick = [&](std::initializer_list<const char*> ids) { return ids.begin()[PileHash(seed++) % ids.size()]; };
			// (街のまわりの景色 (StageScenery) の外から置く)
			const float nearM = StageSceneryFar * battle_.unit / p.meter, farM = 6000;
			const int objects = 340;
			for (int n = 0; n < objects; ++n) {
				const float rm = nearM * std::pow(farM / nearM, (float(n) + PileRand(seed)) / float(objects)),
				            ang = PileRand(seed) * 6.2831853f, size = rm * .16f * p.meter;
				const V at = center + V{std::cos(ang), 0, std::sin(ang)} * (rm * p.meter);
				if (std::abs(at.x - center.x) < board + size && std::abs(at.z - center.z) < board + size)
					continue;
				const bool distant = rm > 150;
				const float roll = PileRand(seed);
				auto spot = [&](float spread) {
					const float a = PileRand(seed) * 6.2831853f, d = std::sqrt(PileRand(seed)) * spread;
					return at + V{std::cos(a) * d, 0, std::sin(a) * d};
				};
				if (roll < (distant ? .4f : .15f)) {
					// 家 (遠くでは村や町)
					const int houses = distant ? 3 + int(PileRand(seed) * 7) : 1;
					for (int b = 0; b < houses; ++b)
						place(distant ? pick({"house_hiraya", "house_kawara", "house_tin", "my_house", "apartment", "danchi", "dagashiya"})
						          : pick({"house_hiraya", "house_kawara", "my_house"}),
						      spot(size * .5f), size * (.16f + PileRand(seed) * .1f));
				} else if (roll < (distant ? .85f : .55f)) {
					// 木 (遠くでは林)
					const int trees = distant ? 5 + int(PileRand(seed) * 10) : 2 + int(PileRand(seed) * 4);
					for (int b = 0; b < trees; ++b)
						place(distant ? pick({"sugi", "ki_maru", "matsu", "sugi"}) : pick({"kaki_no_ki", "sakura", "ki_maru", "matsu", "ki_maru"}),
						      spot(size * .45f), size * (.09f + PileRand(seed) * .06f));
				} else if (roll < (distant ? .93f : .82f)) {
					// 野原に転がるおもちゃ (遠くでは大きなおもちゃ)
					place(pick({"toybox", "minicar", "toy_ball", "daruma", "nuigurumi", "rc_car", "toy_plane", "bowling_pin", "kei_car"}),
					      at, size * (.18f + PileRand(seed) * .12f));
				} else {
					// 積み木の塔 (山と同じおもちゃの積み木)
					const int levels = 3 + int(PileRand(seed) * 5);
					const float w = size * (.05f + PileRand(seed) * .04f);
					for (int b = 0; b < levels; ++b) {
						const auto c = BlockColor(BlockType::Normal, PileHash(seed++));
						block(V{at.x + (PileRand(seed) - .5f) * w * .3f, (float(b) * 2 + 1) * w, at.z + (PileRand(seed) - .5f) * w * .3f}, w,
						      Vec{c.x, c.y, c.z});
					}
				}
			}
			// 雲のモデル: もこもこの雲 (高さ 1.2〜2.8km) と、宇宙から見える広い雲 (高さ 6km。平たくして大きく並べる)
			for (int n = 0; n < 150; ++n) {
				const float rm = 700 * std::pow(250000.f / 700, (float(n) + PileRand(seed)) / 150.f), ang = PileRand(seed) * 6.2831853f;
				const V at = center + V{std::cos(ang), 0, std::sin(ang)} * (rm * p.meter);
				const float w = (700 + PileRand(seed) * 1100) * (1 + rm / 60000) * p.meter;
				place(pick({"kumo_a", "kumo_b", "kumo_c", "kumo_a"}), at, w, (1200 + PileRand(seed) * 1600) * p.meter, 1, true);
			}
			for (int n = 0; n < 180; ++n) {
				const float rm = 60000 * std::pow(3e6f / 60000, (float(n) + PileRand(seed)) / 180.f), ang = PileRand(seed) * 6.2831853f;
				const V at = center + V{std::cos(ang), 0, std::sin(ang)} * (rm * p.meter);
				place(pick({"kumo_a", "kumo_c"}), at, rm * (.06f + PileRand(seed) * .08f) * p.meter, 6000 * p.meter, .12f, true);
			}
		}
		// (体を山のふもとへ移すので、粒子も置き直す)
		lifeReseed_ = true;
	}
	// 積もった高さ (本当の山と、着いた殻の積み木のてっぺん) と、映す裾の広さ (殻は、積み始める少し前から広げる)・隠れた広さ
	float top = 0, span = p.reach;
	for (int i = 0; i < count && finishAge_ >= UI::Scale::PileLand(i, count); ++i)
		top = std::max(top, float(p.slots[size_t(i)].row + 1) * p.block);
	for (int k = 0; k < int(p.shells.size()); ++k) {
		const auto& sh = p.shells[size_t(k)];
		if (finishAge_ < sh.at - 1.f)
			break;
		span = std::max(span, sh.shape.Width() * p.block * .6f);
		if (finishAge_ < sh.at)
			break;
		const int landed = UI::Scale::PileShellLanded(sh.at, k, sh.shape.blocks, finishAge_);
		top = std::max(top, ChallengePileShellTop(sh.shape, landed) * p.block);
		if (landed >= sh.shape.blocks)
			p.cover = std::max(p.cover, sh.shape.Width() * p.block * .5f);
	}
	// (何倍にも変わるので、対数でなめらかに追う)
	auto follow = [&](float& value, float target, float rate) {
		value = std::exp(std::log(std::max(value, 1e-4f)) +
		                 (std::log(std::max(target, 1e-4f)) - std::log(std::max(value, 1e-4f))) * (1 - std::exp(-rate * dt)));
	};
	follow(p.height, std::max(top, 1e-4f), 10);
	// カメラ: 殻を積む間は、積み木が着くたびの高さではなく、予定の高さと広さ (殻が積み終わる時刻ごとの高さと次の殻の広さを、
	// 対数でつないだ線) を追う。殻が速くなっても、カメラは段のたびに止まったり跳ねたりせず、なめらかに上る
	if (!p.shells.empty() && finishAge_ >= p.shells[0].at - .3f) {
		float t0 = p.shells[0].at - .3f, h0 = std::log(std::max(float(ChallengePileRows(p.slots)) * p.block, p.block * 2)),
		      s0 = std::log(p.shells[0].shape.Width() * p.block * .6f);
		float hNow = h0, sNow = s0;
		for (int k = 0; k < int(p.shells.size()); ++k) {
			const auto& sh = p.shells[size_t(k)];
			const float t1 = sh.at + UI::Scale::PileShellTime(k) * .85f, h1 = std::log(sh.shape.Height() * p.block),
			            s1 = std::log(p.shells[size_t(std::min(k + 1, int(p.shells.size()) - 1))].shape.Width() * p.block * .6f);
			hNow = h1;
			sNow = s1;
			if (finishAge_ < t1) {
				const float f = std::clamp((finishAge_ - t0) / std::max(t1 - t0, 1e-3f), 0.f, 1.f);
				hNow = h0 + (h1 - h0) * f;
				sNow = s0 + (s1 - s0) * f;
				break;
			}
			t0 = t1;
			h0 = h1;
			s0 = s1;
		}
		follow(p.top, std::exp(hNow), 5);
		follow(p.span, std::exp(sNow), 5);
	} else {
		follow(p.top, std::max(p.height, p.block * 2), 3);
		follow(p.span, span, 4);
	}
	// 空から宇宙へ: 積もった高さ (本当の積み木 1 個が 4cm) が 20km から 200km へ上がるにつれて、空を星空に変える
	{
		const float meters = p.top / p.meter;
		p.space = std::clamp((std::log10(std::max(meters, 1.f)) - std::log10(20000.f)) / 1.f, 0.f, 1.f);
	}
	auto& body = life_.bodies[0];
	body.at = p.at + V{0, p.height + ChallengeBodyGround, 0};
	body.velocity = {};
	body.forward = Unit(V{-.35f, 0, -1});
}

// 景色の地球の形・地球の絵・月の絵を作る (重いので、チャレンジが始まるときに別の糸で作り始める。できたら GPU へ送る。wait なら待つ)
void ChronoSystem::PreparePileWorld(bool wait) {
	auto* render = Engine::Renderer::GetInstance();
	// (絵と形は別の糸で作り、できたものから GPU へ送る)
	if (earthTex_ == 0 && !PileEarthJob().valid())
		PileEarthJob() = std::async(std::launch::async, PileEarthPaint);
	if (moonTex_ == 0 && !PileMoonJob().valid())
		PileMoonJob() = std::async(std::launch::async, PileMoonPaint);
	if (earthMesh_ == 0 && !PileShapeJob().valid())
		PileShapeJob() = std::async(std::launch::async, PileEarthBuild);
	if (earthTex_ == 0 && PileReady(PileEarthJob(), wait)) {
		const auto pixels = PileEarthJob().get();
		earthTex_ = render->UpdatePaintTexture("pile_earth", PileEarthW, PileEarthH, pixels.data());
	}
	if (moonTex_ == 0 && PileReady(PileMoonJob(), wait)) {
		const auto pixels = PileMoonJob().get();
		moonTex_ = render->UpdatePaintTexture("pile_moon", 256, 128, pixels.data());
	}
	if (earthMesh_ == 0 && PileReady(PileShapeJob(), wait)) {
		const auto shape = PileShapeJob().get();
		earthMesh_ = render->CreateDynamicMesh(shape.vertices, shape.indices);
	}
}

// 景色: 地球 (場の真ん中を中心にした半球。絵は真ん中からの距離の対数で描いてあるので、どの高さから見ても細かい)・地面の物と雲 (小さく見える物は描かない)・
// 宇宙に出たら月
void ChronoSystem::DrawPileWorld(GameContext& ctx) {
	auto& p = pile_;
	PreparePileWorld(false);
	if (earthMesh_ == 0)
		return;
	auto lin = [](Vec c) { return Engine::Vector4{std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f), 1}; };
	// 地球 (真ん中は場の板の下に隠れる穴)
	{
		Engine::Transform t;
		const V center = battle_.arenaAt;
		t.translate = EV(V{center.x, p.groundY, center.z});
		t.scale = {p.earth, p.earth, p.earth};
		ctx.renderer->DrawMeshInstanced(
		    earthMesh_, earthTex_ ? earthTex_ : white_, t,
		    earthTex_ ? Engine::Vector4{1, 1, 1, 1} : lin(Vec{.44f, .64f, .3f}));
	}
	// 地面の物と雲 (画面で 1.5 点より小さい物と、カメラの奥の限りより遠い物は描かない)
	const float scale = ctx.viewportSize.y / (2 * std::tan(.45f));
	// (カメラの後ろの物と、画面で 3 点より小さく見える物と、奥の限りより遠い物は描かない)
	const V forward = Unit(p.cameraLook - p.cameraAt);
	// (山を見る線: カメラから山の真ん中の高さへ。雲がこの線のまわり (山の大きさの円すい) の手前にあると、山が隠れるので描かない)
	const V mountain = p.at + V{0, p.top * .5f, 0} - p.cameraAt;
	const float toMountain = Length(mountain), mountainRadius = std::max(p.span / .6f * .5f, p.top * .5f) * 1.4f;
	const V axis = mountain * (1 / std::max(toMountain, 1e-4f));
	for (const auto& d : p.decor) {
		const V to = d.at - p.cameraAt;
		const float dist = Length(to);
		if (d.size * 2 * scale < dist * 3 || dist - d.size > p.farClip || Dot(to, forward) < -d.size)
			continue;
		if (d.cloud) {
			const float along = Dot(to, axis);
			if (along > 0 && along < toMountain + mountainRadius &&
			    Length(to - axis * along) < mountainRadius * along / std::max(toMountain, 1e-4f) + d.size)
				continue;
		}
		Engine::Transform t;
		t.translate = EV(d.at);
		t.scale = EV(d.half);
		if (d.mesh) {
			t.rotate = {0, d.yaw, 0};
			ctx.renderer->DrawMeshInstanced(d.mesh, lowPalette_, t, {1, 1, 1, 1}, "LowpolySurface", lowMaps_);
		} else
			ctx.renderer->DrawMeshInstanced(cube_, white_, t, lin(Vec{d.color.x, d.color.y, d.color.z}));
	}
	// 月 (宇宙に出たら。山の向こうの空の高い所。遠くの物なので、カメラから同じ割合の距離に置く)
	if (p.space > .02f) {
		const float d = Length(p.cameraLook - p.cameraAt), yaw = BlocksPileYaw + .55f;
		const V dir = Unit(V{-std::sin(yaw), .55f, std::cos(yaw)});
		Engine::Transform t;
		t.translate = EV(p.cameraAt + dir * (d * 30));
		const float r = d * 30 * .075f;
		t.scale = {r, r, r};
		const float k = p.space;
		ctx.renderer->DrawMeshInstanced(sphere_, moonTex_ ? moonTex_ : white_, t, Engine::Vector4{k, k, k * .97f, 1});
	}
}

// 結果の積み木の山を描く: 景色・本当の山 (最初の殻が積み終わるまで)・殻 (次の殻が積み終わるまで。中は隠れるので外側の殻だけ)
void ChronoSystem::DrawBlocksPile(GameContext& ctx) {
	const auto& p = pile_;
	if (!p.active || cube_ == 0)
		return;
	DrawPileWorld(ctx);
	// (遊んでいる間の、街のまわりの木と家と遠くの板も、そのまま見せる)
	DrawStageScenery(ctx, false);
	const int count = int(p.slots.size());
	auto cube = [&](V at, V half, Engine::Vector4 c) {
		Engine::Transform t;
		t.translate = EV(at);
		t.scale = EV(half);
		ctx.renderer->DrawMeshInstanced(
		    cube_, white_, t, Engine::Vector4{std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f), 1});
	};
	auto covered = [&](int k) {
		return k < int(p.shells.size()) && finishAge_ >= p.shells[size_t(k)].at + UI::Scale::PileShellTime(k) + .05f;
	};
	// こわした積み木 (着く少し前に上から落ちてくる)
	if (!covered(0)) {
		const float half = p.block * .48f;
		for (int i = 0; i < count; ++i) {
			const float start = UI::Scale::PileLand(i, count) - UI::Scale::PileDrop;
			if (finishAge_ < start)
				break;
			const float k = std::clamp((finishAge_ - start) / UI::Scale::PileDrop, 0.f, 1.f);
			const float lift = BlocksPileFall * (1 - k * k);
			const auto& sl = p.slots[size_t(i)];
			cube(p.at + V{sl.x * p.block, (float(sl.row) + .5f + lift) * p.block, sl.z * p.block}, V{half, half, half},
			     p.colors[size_t(i)]);
		}
	}
	// 殻: 同じおもちゃの積み木を下の段から積む (色はこわした積み木の色から選ぶ)。一番下の段は地面まで伸ばす (街の板の外で浮かないように)
	for (int k = 0; k < int(p.shells.size()); ++k) {
		const auto& sh = p.shells[size_t(k)];
		const float drop = std::min(UI::Scale::PileDrop, UI::Scale::PileShellTime(k) * .5f + .08f);
		if (finishAge_ < sh.at - drop)
			break;
		if (covered(k + 1))
			continue;
		const float size = sh.shape.size * p.block, half = size * .48f;
		for (int i = 0; i < sh.shape.blocks; ++i) {
			const float start = UI::Scale::PileShellLand(sh.at, k, i, sh.shape.blocks) - drop;
			if (finishAge_ < start)
				break;
			const float f = std::clamp((finishAge_ - start) / drop, 0.f, 1.f);
			const float lift = BlocksPileFall * (1 - f * f);
			const auto& sl = sh.slots[size_t(i)];
			const uint32_t pick = PileHash(uint32_t(i) * 2654435761u ^ uint32_t(k) * 40503u);
			const Engine::Vector4 color =
			    p.colors.empty() ? BlockColor(BlockType::Normal, pick) : p.colors[pick % p.colors.size()];
			V at = p.at + V{sl.x * size, (float(sl.row) + .5f + lift) * size, sl.z * size};
			V halfSize{half, half, half};
			if (sl.row == 0 && lift <= 0) {
				const float bottom = p.groundY, topY = p.at.y + size * .98f;
				at.y = (bottom + topY) * .5f;
				halfSize.y = (topY - bottom) * .5f;
			}
			cube(at, halfSize, color);
		}
	}
}

// 結果のカメラ: 山の南西から、山の上の方を見る。山が高くなるほど離れて上る (積み木の街の塔や橋や丘が下へ遠ざかる)。
// 山は画面の右寄り (左に記録の札があるので)。積み終わったら、ゆっくり山のまわりを回る
void ChronoSystem::BlocksPileCamera(float dt, V& wanted, V& view) {
	auto& p = pile_;
	if (!p.active)
		return;
	const int count = int(p.slots.size());
	if (finishAge_ > UI::Scale::PileCompareAt(count))
		p.orbit += dt * BlocksPileOrbit;
	// (高さ・裾の広さは TickBlocksPile でなめらかにしてある。大きさが何倍にも変わるので、位置ではなくこれらから作る)
	// (殻を積む間は、山全体が画面に収まるまで離れる。近いと大きな積み木が次々に動いて目が疲れる)
	float away = 1;
	if (!p.shells.empty()) {
		const float k = std::clamp((finishAge_ - p.shells[0].at + .5f) / 1.5f, 0.f, 1.f);
		away = 1 + BlocksPileShellAway * k * k * (3 - 2 * k);
	}
	const float d = (p.span * BlocksPileNear + p.top * BlocksPileAway) * away, yaw = BlocksPileYaw + p.orbit;
	V look = p.at + V{0, p.top * .55f, 0};
	p.nearClip = std::max(.05f, d * .01f);
	// (奥の限りは遠くの雲や地球の丸みまで。奥行きの細かさは手前の面でほぼ決まる)
	p.farClip = std::max(2000.f, d * 3000);
	const V dir = Unit(V{std::sin(yaw), BlocksPileRise, -std::cos(yaw)});
	V cam = look + dir * d;
	const V forward = Unit(look - cam), right = Unit(V{forward.z, 0, -forward.x});
	look = look - right * (d * BlocksPileShift);
	cam = cam - right * (d * BlocksPileShift);
	p.cameraAt = cam;
	p.cameraLook = look;
	p.cameraReady = true;
	wanted = p.cameraAt;
	view = Unit(p.cameraLook - p.cameraAt);
}

// チャレンジの場の位置 (unit を単位にした、場の真ん中からの x,z と床からの高さ)
Vec ChronoSystem::ArenaPoint(float x, float z, float y) const {
	const float u = battle_.unit;
	V at = battle_.arenaAt + V{x * u, 0, z * u};
	at.y = InkGround(at) + y * u;
	return at;
}

// チャレンジの物を作る (始めるときとやり直しのとき)。配置はふだん積み木の街 (--challenge-pattern=番号 で平らな場の配置も選べる)
void ChronoSystem::BuildChallenge() {
	auto& b = battle_;
	b.blocks.clear();
	b.debris.clear();
	b.broken = {};
	b.timeStop = b.heavy = 0;
	if (b.challenge != ChallengeKind::Blocks)
		return;
	// (ふだんは専用の立体ステージ「積み木の街」。平らな場の配置 0〜3 は確認用)
	b.blockPattern = b.forcedPattern >= 0 ? b.forcedPattern : ChallengeBlocksStagePattern;
	if (const wchar_t* arg = wcsstr(GetCommandLineW(), L"--challenge-pattern="))
		b.blockPattern = std::clamp(_wtoi(arg + 20), 0, ChallengeBlockPatterns - 1);
	// 結果の景色のモデルは、ここで先に読んでおく (結果が始まったときに読むと、一瞬止まる)
	for (const char* id : {"house_hiraya", "house_kawara", "house_tin", "my_house", "apartment", "danchi", "dagashiya", "sugi",
	                       "ki_maru", "matsu", "kaki_no_ki", "sakura", "toybox", "minicar", "toy_ball", "daruma", "nuigurumi",
	                       "rc_car", "toy_plane", "bowling_pin", "kei_car", "kumo_a", "kumo_b", "kumo_c"})
		LowModelOf(id);
	// (地球の絵はステージを始めたときに描き始めてある。ここで描き終わりを待って GPU へ送る。遊んでいる途中で送ると止まる)
	PreparePileWorld(true);
	const float u = b.unit, size = u * ChallengeBlockSize;
	const V center = b.arenaAt;
	const bool land = b.blockPattern == ChallengeBlocksStagePattern;
	if (land && wcsstr(GetCommandLineW(), L"--blocks-land-check"))
		WriteBlocksLandCheck();
	for (const auto& spot : land ? BlocksLandLayout() : ChallengeBlockLayout(b.blockPattern)) {
		ChallengeBlock blk;
		blk.id = ChallengeBlockIdBase + uint32_t(b.blocks.size());
		blk.type = spot.type;
		blk.size = size;
		blk.at = center + V{spot.x * u, 0, spot.z * u};
		// (積み木の街は、spot.y より下から始まる面の上: 橋の下・トンネルの中・塔の上を選べる)
		blk.at.y = (land ? center.y + Land().Support(spot.x, spot.z, spot.y) * u : InkGround(blk.at)) +
		           size * (.5f + float(spot.level));
		blk.hp = blk.maxHp = ChallengeBlockHp[int(spot.type)];
		b.blocks.push_back(blk);
	}
	// 確認用 (--challenge-pile-test=個数): 始めから、その個数の積み木をこわしたことにする (結果の山の見た目を決まった数で撮る)
	if (const wchar_t* arg = wcsstr(GetCommandLineW(), L"--challenge-pile-test=")) {
		const int want = std::min(_wtoi(arg + 22), int(b.blocks.size()));
		for (int i = 0; i < want; ++i) {
			auto& blk = b.blocks[size_t(i) * b.blocks.size() / size_t(std::max(want, 1))];
			if (!blk.alive)
				continue;
			blk.alive = false;
			++b.broken[size_t(blk.type)];
		}
	}
}

// 状況の文字 (HUD の左上の札の 3 行目)
std::string ChronoSystem::ChallengeStatus() const {
	const auto& b = battle_;
	if (b.challenge != ChallengeKind::Blocks)
		return {};
	int left = 0;
	for (const auto& blk : b.blocks)
		if (blk.alive)
			++left;
	return std::string(ChallengeBlockPatternName(b.blockPattern)) + "  壊した " +
	       std::to_string(b.broken[0] + b.broken[1] + b.broken[2]) + "  (金 " + std::to_string(b.broken[2]) +
	       ")  のこり " + std::to_string(left);
}

// 今の威力: 体の大きさの威力に、おやつパワー (回復のあとしばらく) をかける
float ChronoSystem::ChallengePowerNow() const {
	const float base = BattlePower(CurrentDevourStage(), life_.bodies[0].mass);
	return battle_.snack > 0 ? base * ChallengeSnackPower : base;
}

// どのチャレンジでも同じ、自分に効く能力
void ChronoSystem::ApplyChallengeSelf(BattleAction action) {
	auto& b = battle_;
	auto& body = life_.bodies[0];
	const V top = body.at + V{0, DevourBodyRadius(body.mass) * 1.4f, 0};
	switch (action) {
	case BattleAction::Heal:
	case BattleAction::Brew: {
		// 時間を足す (合わせて ChallengeTimeBonusMax 秒まで)
		const float add = std::min(
		    action == BattleAction::Heal ? ChallengeHealBonus : ChallengeBrewBonus,
		    std::max(0.f, ChallengeTimeBonusMax - b.timeBonus));
		b.time += add;
		b.timeBonus += add;
		if (action == BattleAction::Heal)
			b.snack = ChallengeSnackTime;
		b.pops.push_back(
		    {top,
		     (add > 0 ? "+" + std::to_string(int(std::round(add))) + "秒  " : std::string()) +
		         (action == BattleAction::Heal ? "おやつパワー！"
		          : add > 0                    ? ""
		                                       : "もう時間は増えない"),
		     0,
		     {.5f, .95f, .5f, 1}});
		break;
	}
	case BattleAction::Pause:
		b.timeStop = std::max(b.timeStop, BattlePauseTime);
		b.message = "一時停止！ 時計が止まった";
		b.messageAge = 1.5f;
		break;
	case BattleAction::Guard:
	case BattleAction::Roly:
		b.heavy =
		    std::max(b.heavy, action == BattleAction::Guard ? BattleGuardTime : BattleActionTime(BattleAction::Roly));
		b.heavySource = b.impactSerial++;
		b.heavyBy = action;
		b.message = action == BattleAction::Guard ? "盾タックル！ しばらく体当たりで壊せる"
		                                          : "体が重く硬くなった！ 体当たりで壊せる";
		b.messageAge = 1.5f;
		break;
	default:
		break;
	}
}

// 積み木を削る: ひびがあれば強く効き、硬さを無視する。硬さに負けたら効かない (「かたい！」)
void ChronoSystem::DamageBlock(GameContext& ctx, ChallengeBlock& blk, float amount, V push, bool ignoreArmor) {
	if (!blk.alive || amount <= 0)
		return;
	if (blk.fragile > 0) {
		amount *= ChallengeFragileTake;
		ignoreArmor = true;
	}
	if (!ignoreArmor) {
		amount -= ChallengeBlockArmor[int(blk.type)];
		if (amount <= 0) {
			if (blk.flash <= 0) {
				battle_.pops.push_back({blk.at + V{0, blk.size * .6f, 0}, "かたい！", 0, {.75f, .8f, .9f, 1}});
				PlayDevourSound(devourSounds_.bounce, .3f, 1.6f);
			}
			blk.flash = .12f;
			return;
		}
	}
	blk.hp -= amount;
	blk.flash = .12f;
	if (blk.hp <= 0)
		BreakBlock(ctx, blk, push, nullptr);
}

// 積み木が壊れた: 得点 (色ぬりなら増える)・かけらを飛ばす・上の積み木を落とす
void ChronoSystem::BreakBlock(GameContext& ctx, ChallengeBlock& blk, V push, const char* how) {
	auto& b = battle_;
	if (!blk.alive)
		return;
	blk.alive = false;
	const int type = int(blk.type);
	++b.broken[size_t(type)];
	const float points = float(ChallengeBlockScore[type]) * (blk.painted ? ChallengePaintScore : 1.f);
	AddChallengeScore(
	    int(std::round(points)), blk.at + V{0, blk.size * .6f, 0},
	    how                           ? how
	    : blk.type == BlockType::Gold ? "金！"
	    : blk.type == BlockType::Hard ? "硬い！"
	                                  : "");
	const Engine::Vector4 color = BlockColor(blk.type, blk.id);
	for (int k = 0; k < 4; ++k) {
		ChallengeDebris d;
		d.at = blk.at + V{BR(-.3f, .3f), BR(-.3f, .3f), BR(-.3f, .3f)} * blk.size;
		d.size = blk.size * .45f;
		d.color = color;
		d.velocity = push * .6f + V{BR(-1, 1), 0, BR(-1, 1)} * (b.unit * 3) + V{0, b.unit * BR(4, 8), 0};
		d.spin = V{BR(-8, 8), BR(-8, 8), BR(-8, 8)};
		b.debris.push_back(d);
	}
	if (blk.type == BlockType::Gold) {
		PlayDevourSound(devourSounds_.rankUp, .7f, 1.3f);
		LifeImpact(ctx, ImpactKill, blk.at, {0, 1, 0}, .6f);
	} else
		PlayDevourSound(
		    blk.type == BlockType::Hard ? devourSounds_.shatter : devourSounds_.hit,
		    blk.type == BlockType::Hard ? .5f : .3f, BR(.9f, 1.3f));
	DropBlocksAbove(blk);
}

// 下が無くなった: 真上に重なっている積み木を全部落とす
void ChronoSystem::DropBlocksAbove(const ChallengeBlock& below) {
	for (auto& blk : battle_.blocks) {
		if (!blk.alive || &blk == &below || blk.at.y <= below.at.y)
			continue;
		if (std::abs(blk.at.x - below.at.x) < blk.size * .9f && std::abs(blk.at.z - below.at.z) < blk.size * .9f)
			blk.falling = true;
	}
}

// 積み木が衝撃を受ける (効き目ごとの反応)。何かが起きたら true
bool ChronoSystem::ApplyImpactToBlock(GameContext& ctx, ChallengeBlock& blk, const BattleImpact& i) {
	if (!blk.alive)
		return false;
	auto& b = battle_;
	const float unit = b.unit;
	// 積み木の街: 低い所からの短い当たり (床に沿った当たり) は、崖の上の積み木に届かない
	if (i.flat && BlocksStageActive() && i.at.y + unit * 1.5f + i.reach * .3f < blk.at.y - blk.size * .5f)
		return false;
	V d = blk.at - i.at;
	d.y = 0;
	const float dist = Length(d);
	V flatDir{i.dir.x, 0, i.dir.z};
	flatDir = Length(flatDir) > .001f ? Unit(flatDir) : V{0, 0, 1};
	const V radial = dist > .01f ? d * (1 / dist) : flatDir;
	const V push = (i.radial ? radial : flatDir) * (unit * i.push);
	switch (i.effect) {
	case ImpactEffect::Hit:
		DamageBlock(ctx, blk, i.damage, push, false);
		return true;
	case ImpactEffect::Pull: {
		// 硬い積み木は重くて動かない。それ以外は中心へ滑り、中心まで来たら回収する
		if (blk.type == BlockType::Hard)
			return false;
		if (dist < blk.size * ChallengeSuckReach) {
			BreakBlock(ctx, blk, V{}, "回収");
			return true;
		}
		// (引く速さ: 中心までの距離 (pullCap まで) に比例。距離は場の長さなので unit はかけない。かけると unit の 2 乗で速くなり、
		//  大きな体では積み木が中心を通り越して飛んでいき、弾に当たらない)
		const float strength = i.pullCap > 0 ? std::min(dist, i.pullCap) * i.push * 3 : unit * i.push * 8;
		if (!blk.loose) {
			blk.loose = true;
			DropBlocksAbove(blk);
		}
		blk.velocity = blk.velocity - radial * strength;
		return true;
	}
	case ImpactEffect::PushOut:
		if (blk.type == BlockType::Hard)
			return false;
		DamageBlock(ctx, blk, ChallengeSmellDps * b.frameDt, V{}, true);
		return true;
	case ImpactEffect::Stun:
		if (i.action == BattleAction::Pause)
			return false; // リモコンは時計を止める (ApplyChallengeSelf)
		if (i.damage > 0)
			DamageBlock(ctx, blk, i.damage, push, false);
		else
			blk.fragile = std::max(blk.fragile, i.seconds);
		return true;
	case ImpactEffect::Weaken:
		blk.fragile = std::max(blk.fragile, i.seconds);
		DamageBlock(ctx, blk, i.damage, push, false);
		return true;
	case ImpactEffect::Burn:
		blk.burn = i.seconds;
		blk.burnDps = i.damage;
		blk.burnTick = 0;
		return true;
	case ImpactEffect::Capture:
		BreakBlock(ctx, blk, V{}, "回収");
		return true;
	case ImpactEffect::Zone:
		if (i.action != BattleAction::Paint)
			return false;
		blk.painted = true;
		return true;
	default:
		return false;
	}
}

// チャレンジの物を進める: 体当たり (重い体)・ひびとやけど・吸い寄せられて滑る・落ちる・かけら
void ChronoSystem::TickChallenge(GameContext& ctx, float dt) {
	auto& b = battle_;
	auto& body = life_.bodies[0];
	const float R = DevourBodyRadius(body.mass);
	// 守る・だるま: 体に触れた物に当てる
	if (b.heavy > 0) {
		b.heavy -= dt;
		BattleImpact i;
		i.action = b.heavyBy;
		i.at = body.at;
		i.dir = body.forward;
		i.reach = R * 1.05f;
		i.damage = ChallengeHeavyDamage * ChallengePowerNow();
		i.push = 4;
		i.radial = true;
		i.source = b.heavySource;
		i.rehit = ChallengeHeavyRehit;
		EmitBattleImpact(ctx, i);
	}
	if (b.blocks.empty() && b.debris.empty())
		return;
	const float g = b.unit * 30;
	for (auto& blk : b.blocks) {
		if (!blk.alive)
			continue;
		blk.flash = std::max(0.f, blk.flash - dt);
		blk.fragile = std::max(0.f, blk.fragile - dt);
		if (blk.burn > 0) {
			blk.burn -= dt;
			blk.burnTick -= dt;
			if (blk.burnTick <= 0) {
				blk.burnTick = .5f;
				DamageBlock(ctx, blk, blk.burnDps * .5f, V{}, true);
				if (!blk.alive)
					continue;
			}
		}
		// 吸い寄せられて滑る (床の上を摩擦で止まる)
		if (blk.loose) {
			blk.at = blk.at + V{blk.velocity.x, 0, blk.velocity.z} * dt;
			blk.velocity = blk.velocity * std::exp(-4 * dt);
			if (Length(blk.velocity) < b.unit * .2f) {
				blk.loose = false;
				blk.velocity = {};
			}
			blk.falling = true;
		}
		// 落ちる: 真下の一番高い支え (止まっている積み木の上の面か床) まで
		if (blk.falling) {
			float support = InkGround(blk.at);
			for (const auto& o : b.blocks) {
				if (!o.alive || &o == &blk || o.falling || o.at.y >= blk.at.y)
					continue;
				if (std::abs(o.at.x - blk.at.x) < blk.size * .9f && std::abs(o.at.z - blk.at.z) < blk.size * .9f)
					support = std::max(support, o.at.y + o.size * .5f);
			}
			blk.velocity.y -= g * dt;
			blk.at.y += blk.velocity.y * dt;
			if (blk.at.y - blk.size * .5f <= support) {
				blk.at.y = support + blk.size * .5f;
				blk.velocity.y = 0;
				if (!blk.loose)
					blk.falling = false;
			}
		}
	}
	for (auto& d : b.debris) {
		d.age += dt;
		d.velocity.y -= g * dt;
		d.at = d.at + d.velocity * dt;
		const float floor = InkGround(d.at) + d.size * .5f;
		if (d.at.y < floor) {
			d.at.y = floor;
			d.velocity = d.velocity * .4f;
			d.velocity.y = std::abs(d.velocity.y);
		}
	}
	b.debris.erase(
	    std::remove_if(b.debris.begin(), b.debris.end(), [](const ChallengeDebris& d) { return d.age > d.life; }),
	    b.debris.end());
}

// 積み木とかけらを描く (ひびは白っぽく、やけどは橙、色ぬりは明るく、当たった瞬間は白く光る)
void ChronoSystem::DrawChallenge(GameContext& ctx) {
	const auto& b = battle_;
	// (変形武器を作る画面では描かない: 見本の窓にスライムだけが映るように)
	if (cube_ == 0 || b.phase == BattleStep::Craft)
		return;
	auto box = [&](V at, float size, V rotate, Engine::Vector4 c) {
		Engine::Transform t;
		t.translate = EV(at);
		t.scale = {size * .5f, size * .5f, size * .5f};
		t.rotate = EV(rotate);
		const Engine::Vector4 linear{std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f), c.w};
		ctx.renderer->DrawMeshInstanced(cube_, white_, t, linear);
	};
	auto mix = [](Engine::Vector4 a, Engine::Vector4 c, float k) {
		return Engine::Vector4{a.x + (c.x - a.x) * k, a.y + (c.y - a.y) * k, a.z + (c.z - a.z) * k, 1};
	};
	for (const auto& blk : b.blocks) {
		if (!blk.alive)
			continue;
		// (結果の積み木の山の場所に残っていた積み木は描かない)
		if (pile_.active && std::abs(blk.at.x - pile_.at.x) < pile_.cover && std::abs(blk.at.z - pile_.at.z) < pile_.cover)
			continue;
		Engine::Vector4 c = BlockColor(blk.type, blk.id);
		if (blk.painted)
			c = mix(c, {1, .35f, .75f, 1}, .35f);
		if (blk.fragile > 0)
			c = mix(c, {1, 1, 1, 1}, .35f);
		if (blk.burn > 0)
			c = mix(c, {1, .45f, .15f, 1}, .45f);
		if (blk.flash > 0)
			c = mix(c, {1, 1, 1, 1}, .7f);
		// 削れた分だけ少し縮める (硬い・金色)
		const float worn = .9f + .1f * std::clamp(blk.hp / std::max(1.f, blk.maxHp), 0.f, 1.f);
		box(blk.at, blk.size * .96f * worn, {0, 0, 0}, c);
	}
	for (const auto& d : b.debris) {
		const float fade = 1 - std::clamp((d.age - d.life * .6f) / (d.life * .4f), 0.f, 1.f);
		box(d.at, d.size * fade, d.spin * d.age, d.color);
	}
}

} // namespace Game
