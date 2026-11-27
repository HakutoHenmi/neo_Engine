// DevourTownGimmick.cpp
// 坂の町のギミック (ChronoSystem の一部)。広い町を移動するための 2 つの乗り物と、まとめて飲みこむ 2 つの乗り物。
//   こいのぼり: 竿の下の輪で変形キーを押すと、竿を登ってこいのぼりの形で飛び出す。紙飛行機と違って道は決まっておらず、
//              スティックの左右で曲がり、前へ倒すと機首が下がって速くなり、手前へ引くと速さと引き換えに上がる。
//              何もしなければゆっくり下がり、地面に着くか変形キーで手を放すと終わる (高い所で放すとヘリコプターで降りる)
//   電線: 町の電柱を道ごとに電線でつなぐ。どの電柱の下でも変形キーで登り、電気の玉になって電線を走る。
//         電柱ではスティックの向きに一番近い電線へ進み、倒していなければまっすぐ続く電線へ進む。変形キーで飛び降りる
// どちらも付いている物 (電柱・こいのぼり) が飲まれると使えなくなる (家くらいの大きさになれば飲める)。電柱が飲まれるとその電線も消える。
//   おみこし: 神社とお寺の前の輪で変形キーを押すと、おみこしになって「わっしょい」と跳ねながら歩く。その間、体と同じ色の
//            子分たちが周りの飲める物へ次々に走り、担いで戻ってくると飲みこむ (おろした後も、持っている物は持ち帰る)
//   こま: 広場や通りの輪で変形キーを押すと、こまになってまっすぐ走る。町の端・高い段差・飲めない物で跳ね返るたびに少し速くなり、
//         通り道の物を飲みこむ。スティックの左右で少し曲がり、遅くなるか時間が来ると倒れて終わる
//   おみこしとこまの輪は使った後しばらく休む。輪の場所は slope_town.json の rides
// 形と速さの規則は Game/Chrono/DevourGimmickRules.h、電柱とこいのぼりの場所は SlopeTown.cpp。
#include "ChronoSystemInternal.h"
#include "CityBuilder.h"
#include "../Chrono/SlopeTownRules.h"

namespace Game {
using Chrono::GimmickKind;
using namespace CityParts;
namespace {
constexpr int kPopRise = 0, kPopHit = 1;
constexpr uint32_t kNone = 0xffffffffu;
// 形の中心 (形の座標の高さ) から体の位置へ: 体の位置は足元より 1.25 上で、形はその足元から組み立てる
Chrono::Vec BodyAt(Chrono::Vec shapeCenter, float centerHeight, float scale) {
	return shapeCenter - Chrono::Vec{0, centerHeight * scale - 1.25f, 0};
}
// 電線の見た目: 細い四角い管を折れ線に沿って並べる
void WireTube(CityBuilder& b, Chrono::Vec p0, Chrono::Vec p1, float half) {
	using Chrono::Vec;
	Vec d = p1 - p0;
	d.y = 0;
	if (Length(d) < 1e-4f)
		return;
	const Vec u = Unit(Vec{-d.z, 0, d.x}) * half, v{0, half, 0};
	const Vec a[4] = {p0 - u - v, p0 + u - v, p0 + u + v, p0 - u + v},
	          c[4] = {p1 - u - v, p1 + u - v, p1 + u + v, p1 - u + v};
	const Vec normals[4] = {Vec{0, -1, 0}, Unit(u), Vec{0, 1, 0}, Unit(u) * -1.f};
	for (int k = 0; k < 4; ++k) {
		const int n = (k + 1) % 4;
		b.Polygon(CityMetal, {a[k], a[n], c[n], c[k]}, normals[k]);
	}
}
} // namespace

// ---- こいのぼり: 自由に滑空する ----
class ChronoSystem::GlideGimmick final : public ChronoSystem::GimmickBehavior {
public:
	const char* SignModel() const override {
		return "sign_plane";
	}
	void Begin(ChronoSystem& sys, DevourGimmick& g, GimmickRide& ride, const Chrono::LifeBody&, float) const override {
		// 竿の上へ登ってから、カメラの向いている方へ飛び出す
		ride.shape = Chrono::GimmickCarpShape;
		ride.anchor = g.spots.empty() ? g.at : g.spots[0];
		ride.top = ride.anchor.y;
		ride.speed = Chrono::GlideLaunchSpeed;
		ride.heading = {std::sin(sys.yaw_), 0, std::cos(sys.yaw_)};
		ride.pose = {};
		sys.devourPops_.push_back({g.at + V{0, 6, 0}, GimmickWord("gimmickClimb"), 0, .5f, kPopRise, GimmickSeed()});
	}
	void Step(ChronoSystem& sys, GimmickRideStep& st) const override {
		auto& ride = st.ride;
		const float dt = st.dt, scale = st.scale;
		if (ride.age < Chrono::GlideClimbTime) {
			const float t = LifeSmooth(ride.age / Chrono::GlideClimbTime);
			V at = Lerp(ride.from, ride.anchor, t);
			at.y = ride.from.y + (ride.anchor.y - ride.from.y) * t;
			st.next = BodyAt(at, 2, scale);
			return;
		}
		// 手を放した: その場から落ちる (高ければヘリコプターでゆっくり降りる)
		if (ride.release >= 0) {
			st.end = true;
			st.next = st.body.at;
			return;
		}
		V at = st.body.at + V{0, 2 * scale - 1.25f, 0};
		const V right{ride.heading.z, 0, -ride.heading.x};
		const float steer = std::clamp(Dot(st.wish, right), -1.f, 1.f),
		            forward = std::clamp(Dot(st.wish, ride.heading), -1.f, 1.f);
		// 曲がる。町の端へ向かっているときは内へ戻す
		ride.heading = Unit(ride.heading + right * (steer * Chrono::GlideTurn * dt));
		if (!Chrono::SlopeInside(at + ride.heading * (30 * scale), 0)) {
			V in{-at.x, 0, -at.z};
			if (Length(in) > .01f)
				ride.heading = Unit(ride.heading + Unit(in) * (2.5f * dt));
		}
		// 機首: スティックの前後で上下。遅すぎると下がる
		float want = Chrono::GlidePitchFor(forward);
		if (ride.speed < Chrono::GlideMinSpeed * 1.15f)
			want = std::min(want, -.25f);
		ride.pose.pitch += (want - ride.pose.pitch) * std::min(1.f, dt * Chrono::GlidePitchRate);
		ride.speed = Chrono::GlideSpeedStep(ride.speed, ride.pose.pitch, dt);
		at = at + ride.heading * (ride.speed * std::cos(ride.pose.pitch) * dt);
		at.y += Chrono::GlideRise(ride.speed, ride.pose.pitch) * dt;
		at.y = std::min(at.y, ride.top + Chrono::GlideCeiling);
		ride.pose.roll += (-.7f * steer - ride.pose.roll) * std::min(1.f, dt * 4);
		// カメラを進む向きへ回す
		{
			const float yaw = std::atan2(ride.heading.x, ride.heading.z),
			            turn = std::remainder(yaw - sys.yaw_, 6.2831853f);
			sys.yaw_ += turn * (1 - std::exp(-2.5f * dt));
		}
		if (std::fmod(ride.age, .8f) < dt)
			sys.devourPops_.push_back(
			    {at + V{0, 4 * scale, 0} - ride.heading * (6 * scale), GimmickWord("gimmickGlide"), 0, .3f, kPopRise,
			     GimmickSeed()});
		// 建物にぶつかったら手を放して落ちる (飲まれた建物は通り抜ける)
		{
			const float reach = DevourBodyRadius(st.body.mass) * .6f;
			for (const auto& solid : sys.townSolids_) {
				if (solid.visual == st.g.anchor || (solid.prop >= 0 && !sys.life_.props[size_t(solid.prop)].active))
					continue;
				if (at.x < solid.x0 - reach || at.x > solid.x1 + reach || at.z < solid.z0 - reach ||
				    at.z > solid.z1 + reach || at.y - 2 * scale > solid.top)
					continue;
				st.end = true;
				st.next = st.body.at - ride.heading * (reach * .5f);
				sys.devourPops_.push_back(
				    {at + V{0, 4 * scale, 0}, GimmickWord("bumpSoft"), 0, .7f, kPopHit, GimmickSeed()});
				sys.PlayDevourSound(sys.devourSounds_.bounce, .6f);
				sys.LifeImpact(st.ctx, ImpactDevour, at, ride.heading * -1.f, .8f);
				return;
			}
		}
		// 地面に着いたら終わり
		const float ground = sys.InkGround(at);
		if (at.y - 2 * scale <= ground) {
			st.end = true;
			at.y = ground + 2 * scale;
			sys.devourPops_.push_back(
			    {at + V{0, 5 * scale, 0}, GimmickWord("gimmickSplash"), 0, .6f, kPopHit, GimmickSeed()});
			sys.PlayDevourSound(sys.devourSounds_.splat, .6f);
			sys.LifeImpact(st.ctx, ImpactDevour, at, {0, 1, 0}, .7f);
		}
		st.next = BodyAt(at, 2, scale);
	}
	bool RideInput(ChronoSystem&, DevourGimmick&, GimmickRide& ride, bool pressed, bool) const override {
		if (pressed && ride.age >= Chrono::GlideClimbTime && ride.release < 0)
			ride.release = 0;
		return pressed;
	}
	void RidePrompts(std::vector<UI::Scale::Prompt>& out) const override {
		out.push_back({UI::Scale::KeyMove, "左右で曲がる・前後で上下"});
		out.push_back({UI::Scale::KeyMorph, "手を放す"});
	}
};

// ---- 電線: 電柱を登って電気の玉で走る ----
class ChronoSystem::WireGimmick final : public ChronoSystem::GimmickBehavior {
public:
	const char* SignModel() const override {
		return "sign_plane";
	}
	void Begin(
	    ChronoSystem& sys, DevourGimmick& g, GimmickRide& ride, const Chrono::LifeBody& body, float) const override {
		ride.shape = Chrono::GimmickSparkShape;
		ride.node = g.wireNode;
		ride.edge = -1;
		ride.anchor = sys.wireNodes_[size_t(g.wireNode)].top;
		V f{body.forward.x, 0, body.forward.z};
		ride.heading = Length(f) > .01f ? Unit(f) : V{0, 0, 1};
		ride.look = ride.heading;
		sys.devourPops_.push_back({g.at + V{0, 6, 0}, GimmickWord("gimmickClimb"), 0, .5f, kPopRise, GimmickSeed()});
	}
	void Step(ChronoSystem& sys, GimmickRideStep& st) const override {
		auto& ride = st.ride;
		const float dt = st.dt, scale = st.scale;
		ride.pose.spin += dt * 14;
		constexpr float center = 2.4f;
		if (ride.age < Chrono::WireClimbTime) {
			const float t = LifeSmooth(ride.age / Chrono::WireClimbTime);
			V at = Lerp(ride.from, ride.anchor, t);
			at.y = ride.from.y + (ride.anchor.y - ride.from.y) * t;
			st.next = BodyAt(at, center, scale);
			return;
		}
		V at = ride.anchor;
		if (ride.release >= 0) {
			st.end = true;
			st.next = st.body.at;
			return;
		}
		const auto& nodes = sys.wireNodes_;
		const auto& edges = sys.wireEdges_;
		V wish{st.wish.x, 0, st.wish.z};
		const bool steering = Length(wish) > .3f;
		if (steering)
			wish = Unit(wish);
		// 柱の上: 進む電線を選ぶ (倒していなければ、来た向きにまっすぐ続く電線)
		auto choose = [&](int node, bool straight) {
			std::vector<V> dirs;
			std::vector<int> ids;
			for (int e : nodes[size_t(node)].edges) {
				if (!edges[size_t(e)].alive)
					continue;
				const int other = edges[size_t(e)].a == node ? edges[size_t(e)].b : edges[size_t(e)].a;
				V d = nodes[size_t(other)].top - nodes[size_t(node)].top;
				d.y = 0;
				if (Length(d) < .01f)
					continue;
				dirs.push_back(Unit(d));
				ids.push_back(e);
			}
			const int pick = steering   ? Chrono::WireChoose(dirs, wish)
			                 : straight ? Chrono::WireChoose(dirs, ride.heading, .5f)
			                            : -1;
			return pick < 0 ? -1 : ids[size_t(pick)];
		};
		if (ride.edge < 0) {
			const int e = choose(ride.node, false);
			if (e >= 0) {
				ride.edge = e;
				ride.edgeFrom = ride.node;
				ride.s = 0;
				const auto& edge = edges[size_t(e)];
				ride.length = Length(nodes[size_t(edge.b)].top - nodes[size_t(edge.a)].top);
			}
		}
		if (ride.edge >= 0) {
			ride.s += Chrono::WireSpeed * dt;
			// 柱に着いたら、そのまま次の電線へ (倒していなければまっすぐ)。そのコマで柱を越えた分は次の電線で進む
			while (ride.edge >= 0 && ride.s >= ride.length) {
				const auto& edge = edges[size_t(ride.edge)];
				const int to = edge.a == ride.edgeFrom ? edge.b : edge.a;
				const float over = ride.s - ride.length;
				ride.node = to;
				ride.anchor = nodes[size_t(to)].top;
				ride.edge = -1;
				ride.s = 0;
				const int e = choose(to, true);
				if (e >= 0) {
					ride.edge = e;
					ride.edgeFrom = to;
					ride.s = over;
					const auto& next = edges[size_t(e)];
					ride.length = Length(nodes[size_t(next.b)].top - nodes[size_t(next.a)].top);
				}
			}
			if (ride.edge >= 0) {
				const auto& edge = edges[size_t(ride.edge)];
				const int to = edge.a == ride.edgeFrom ? edge.b : edge.a;
				const V a = nodes[size_t(ride.edgeFrom)].top, b = nodes[size_t(to)].top;
				V flat = b - a;
				flat.y = 0;
				if (Length(flat) > .01f)
					ride.heading = Unit(flat);
				at = Chrono::WirePoint(a, b, ride.s / std::max(ride.length, 1e-3f));
			} else
				at = ride.anchor;
			if (std::fmod(ride.age, .22f) < dt) {
				sys.devourPops_.push_back(
				    {at + V{0, 3 * scale, 0}, GimmickWord("gimmickZap"), 0, .35f, kPopRise, GimmickSeed()});
				sys.PlayDevourSound(sys.devourSounds_.tick, .25f, 1.6f);
			}
		} else
			at = ride.anchor;
		// カメラが先を見る向きは、進む向きへゆっくり回す (折り返しで先を見る点が反対側へ跳んで、カメラが大きく揺れないように)
		{
			V look = ride.look;
			if (Dot(look, ride.heading) < -.9f)
				look = look + V{-look.z, 0, look.x} * .25f;
			ride.look = Unit(Lerp(look, ride.heading, 1 - std::exp(-Chrono::WireCameraTurn * dt)));
		}
		st.next = BodyAt(at, center, scale);
	}
	bool RideInput(ChronoSystem&, DevourGimmick&, GimmickRide& ride, bool pressed, bool) const override {
		if (pressed && ride.age >= Chrono::WireClimbTime && ride.release < 0)
			ride.release = 0;
		return pressed;
	}
	void RidePrompts(std::vector<UI::Scale::Prompt>& out) const override {
		out.push_back({UI::Scale::KeyMove, "進む電線を選ぶ"});
		out.push_back({UI::Scale::KeyMorph, "飛び降りる"});
	}
	// 体の向きもゆっくり回す: 折り返しで向きが一度に反対になると、液体の粒子が全部いっせいに形の反対側へ動いて重くなる
	V Facing(const GimmickRide& ride) const override {
		return ride.look;
	}
	// カメラを大きく引き、進む向きの少し先を見る (次の電柱や分かれ道が見えるように)
	bool CameraSubject(
	    const ChronoSystem& sys, const DevourGimmick&, Chrono::LifeCameraSubject& subject) const override {
		const float scale = LifeScale(sys.life_.bodies[0].mass);
		subject.center = subject.center + sys.gimmickRide_.look * (Chrono::WireCameraAhead * scale);
		subject.radius = std::max(subject.radius * Chrono::WireCameraPull, Chrono::WireCameraLeast * scale);
		return true;
	}
};

// ---- おみこし: わっしょいと担いで歩き、子分が周りの小物を取ってくる ----
class ChronoSystem::MikoshiGimmick final : public ChronoSystem::GimmickBehavior {
public:
	const char* SignModel() const override {
		return "sign_plane";
	}
	void Begin(ChronoSystem& sys, DevourGimmick& g, GimmickRide& ride, const Chrono::LifeBody&, float) const override {
		ride.shape = Chrono::GimmickMikoshiShape;
		ride.heading = {std::sin(sys.yaw_), 0, std::cos(sys.yaw_)};
		ride.look = ride.heading;
		ride.beat = 0;
		sys.mikoshiSend_ = 0;
		sys.devourPops_.push_back({g.at + V{0, 6, 0}, GimmickWord("mikoshiCall"), 0, .6f, kPopRise, GimmickSeed()});
	}
	void Step(ChronoSystem& sys, GimmickRideStep& st) const override {
		auto& ride = st.ride;
		auto& body = st.body;
		const float dt = st.dt, scale = st.scale;
		V at = body.at;
		// おろす (変形キー・時間切れ): 輪はしばらく休む。子分は持っている物を持ち帰ってくる
		if (ride.release >= 0 || ride.age >= Chrono::MikoshiTime) {
			st.end = true;
			st.g.rest = Chrono::MikoshiRest;
			at.y = sys.InkGround(at) + 1.25f;
			st.next = at;
			sys.devourPops_.push_back(
			    {at + V{0, 5 * scale, 0}, GimmickWord("gimmickSplash"), 0, .5f, kPopHit, GimmickSeed()});
			sys.PlayDevourSound(sys.devourSounds_.splat, .5f);
			return;
		}
		// 担ぎ始めはその場で形を作る
		if (ride.age >= Chrono::GimmickFormTime) {
			V wish{st.wish.x, 0, st.wish.z};
			const float push = std::min(1.f, Length(wish));
			if (push > .05f) {
				wish = Unit(wish);
				const V turned = Lerp(ride.heading, wish, 1 - std::exp(-6 * dt));
				ride.heading = Length(turned) > .05f ? Unit(turned) : wish;
			}
			const float speed = sys.life_.recipe.Speed() * LifeTravelScale(body) * DevourSpeedScale(body.mass) *
			                    Chrono::MikoshiPace * push;
			const float radius = DevourBodyRadius(body.mass) * .7f;
			V move = ride.heading * (speed * dt), normal;
			// ぶつかったら壁に沿って滑る
			auto free = [&](V delta) {
				V to = at + delta;
				to.y = sys.InkGround(to) + 1.25f;
				if (sys.TownRideHit(at, to, radius, normal))
					return false;
				at = to;
				return true;
			};
			if (!free(move) && !free(V{move.x, 0, 0}))
				free(V{0, 0, move.z});
		}
		// わっしょい: 拍ごとに跳ねて、掛け声を出す
		const float hop = Chrono::MikoshiBounce(ride.age) * Chrono::MikoshiHop * scale;
		const int beat = int(ride.age / Chrono::MikoshiBeat);
		if (beat > ride.beat) {
			ride.beat = beat;
			if (beat % 2 == 0)
				sys.devourPops_.push_back(
				    {at + V{0, (Chrono::GimmickMikoshiTop + 1) * scale, 0}, GimmickWord("mikoshiCall"), 0, .45f,
				     kPopRise, GimmickSeed()});
			sys.PlayDevourSound(sys.devourSounds_.tick, .35f, beat % 2 ? 1.2f : .9f);
			sys.LifeImpact(st.ctx, ImpactDevour, at, {0, 1, 0}, .25f);
		}
		ride.pose.roll = .12f * std::sin(ride.age * 3.1415927f / Chrono::MikoshiBeat);
		ride.pose.pitch = .05f * std::sin(ride.age * 6.2831853f / Chrono::MikoshiBeat);
		{
			V look = ride.look;
			if (Dot(look, ride.heading) < -.9f)
				look = look + V{-look.z, 0, look.x} * .25f;
			ride.look = Unit(Lerp(look, ride.heading, 1 - std::exp(-5 * dt)));
		}
		at.y = sys.InkGround(at) + 1.25f + hop;
		st.next = at;
	}
	bool RideInput(ChronoSystem&, DevourGimmick&, GimmickRide& ride, bool pressed, bool) const override {
		if (pressed && ride.age >= Chrono::GimmickFormTime && ride.release < 0)
			ride.release = 0;
		return pressed;
	}
	void RidePrompts(std::vector<UI::Scale::Prompt>& out) const override {
		out.push_back({UI::Scale::KeyMove, "担いで歩く"});
		out.push_back({UI::Scale::KeyMorph, "おろす"});
	}
	V Facing(const GimmickRide& ride) const override {
		return ride.look;
	}
	// 少し引いて、子分が走り回るまわりも映す
	bool CameraSubject(
	    const ChronoSystem& sys, const DevourGimmick&, Chrono::LifeCameraSubject& subject) const override {
		const float scale = LifeScale(sys.life_.bodies[0].mass);
		subject.radius = std::max(subject.radius * 1.6f, 10 * scale);
		return true;
	}
};

// ---- こま: まっすぐ走り、壁や飲めない物で跳ね返るたびに速くなる。通り道の物を飲みこむ ----
class ChronoSystem::KomaGimmick final : public ChronoSystem::GimmickBehavior {
public:
	const char* SignModel() const override {
		return "sign_plane";
	}
	void Begin(ChronoSystem& sys, DevourGimmick& g, GimmickRide& ride, const Chrono::LifeBody&, float) const override {
		// 形は回すので体の向きは変えない (跳ね返るたびに体の向きを変えると、粒子がいっせいに反対側へ動いてしまう)
		ride.shape = Chrono::GimmickKomaShape;
		ride.heading = {std::sin(sys.yaw_), 0, std::cos(sys.yaw_)};
		ride.look = ride.heading;
		ride.speed = 0;
		ride.bounces = 0;
		ride.pose = {};
		sys.devourPops_.push_back({g.at + V{0, 6, 0}, GimmickWord("komaSpin"), 0, .6f, kPopRise, GimmickSeed()});
	}
	void Step(ChronoSystem& sys, GimmickRideStep& st) const override {
		auto& ride = st.ride;
		auto& body = st.body;
		const float dt = st.dt, scale = st.scale, launch = Chrono::KomaLaunch * std::sqrt(scale);
		V at = body.at;
		const float spinUp = std::min(1.f, ride.age / Chrono::GimmickFormTime);
		ride.pose.spin = std::remainder(ride.pose.spin + Chrono::KomaSpin * spinUp * dt, 6.2831853f);
		// 回し始めはその場で形を作る
		if (ride.age < Chrono::GimmickFormTime) {
			at.y = sys.InkGround(at) + 1.25f;
			st.next = at;
			return;
		}
		if (!ride.fired) {
			ride.fired = true;
			ride.speed = launch;
			sys.PlayDevourSound(sys.devourSounds_.pounce, .5f, 1.3f);
		}
		// 止める・時間切れ・遅くなった: ころんと倒れて終わる
		if (ride.release >= 0 || ride.age >= Chrono::KomaTime || ride.speed < launch * Chrono::KomaFallSpeed) {
			st.end = true;
			st.g.rest = Chrono::KomaRest;
			at.y = sys.InkGround(at) + 1.25f;
			st.next = at;
			sys.devourPops_.push_back(
			    {at + V{0, 5 * scale, 0}, GimmickWord("komaFall"), 0, .5f, kPopHit, GimmickSeed()});
			sys.PlayDevourSound(sys.devourSounds_.bounce, .45f, .8f);
			return;
		}
		// スティックの左右で少し曲げる
		{
			const V right{ride.heading.z, 0, -ride.heading.x};
			const float steer = std::clamp(Dot(st.wish, right), -1.f, 1.f);
			ride.heading = Unit(ride.heading + right * (steer * Chrono::KomaSteer * dt));
		}
		// 下り坂で速く、上り坂で遅く。平らな所では少しずつ遅くなる
		{
			const float probe = 3 * scale,
			            slope = (sys.InkGround(at + ride.heading * probe) - sys.InkGround(at)) / probe;
			ride.speed = std::min(
			    launch * Chrono::KomaMaxGain,
			    ride.speed * (1 - Chrono::KomaFriction * dt) - slope * Chrono::KomaSlope * std::sqrt(scale) * dt);
		}
		// 進む: 速いので細かく区切って当たりを調べ、ぶつかったら跳ね返る (区切った残りは跳ね返った向きへ進む)
		const float radius = Chrono::KomaReach * scale;
		float remain = ride.speed * dt;
		int hits = 0;
		while (remain > 1e-4f && hits < 4) {
			const float step = std::min(remain, radius * .5f);
			V to = at + ride.heading * step, normal;
			to.y = sys.InkGround(to) + 1.25f;
			if (sys.TownRideHit(at, to, radius, normal)) {
				ride.heading = Chrono::KomaReflect(ride.heading, normal);
				++hits;
				++ride.bounces;
				ride.speed = std::min(ride.speed * Chrono::KomaBounceGain, launch * Chrono::KomaMaxGain);
				const V spark = at - normal * radius;
				sys.devourPops_.push_back(
				    {spark + V{0, 3 * scale, 0}, GimmickWord("komaHit"), 0,
				     std::min(1.f, .4f + .05f * float(ride.bounces)), kPopHit, GimmickSeed()});
				sys.PlayDevourSound(sys.devourSounds_.bounce, .55f, 1 + .05f * float(std::min(ride.bounces, 14)));
				sys.LifeImpact(st.ctx, ImpactDevour, spark, normal, .45f);
				ride.pose.roll += .25f;
				continue;
			}
			at = to;
			remain -= step;
		}
		// 首振り: 遅くなるほど大きく傾く
		const float slow = 1 - std::clamp(ride.speed / launch, 0.f, 1.f), wobble = .06f + .25f * slow;
		ride.pose.roll += (wobble * std::sin(ride.age * 9) - ride.pose.roll) * std::min(1.f, dt * 6);
		ride.pose.pitch = wobble * std::cos(ride.age * 9);
		at.y = sys.InkGround(at) + 1.25f;
		st.next = at;
	}
	bool RideInput(ChronoSystem&, DevourGimmick&, GimmickRide& ride, bool pressed, bool) const override {
		if (pressed && ride.age >= Chrono::GimmickFormTime && ride.release < 0)
			ride.release = 0;
		return pressed;
	}
	void RidePrompts(std::vector<UI::Scale::Prompt>& out) const override {
		out.push_back({UI::Scale::KeyMove, "左右で曲げる"});
		out.push_back({UI::Scale::KeyMorph, "止める"});
	}
	V Facing(const GimmickRide& ride) const override {
		return ride.look;
	}
	// 引いて、跳ね返る先の壁まで映す
	bool CameraSubject(
	    const ChronoSystem& sys, const DevourGimmick&, Chrono::LifeCameraSubject& subject) const override {
		const float scale = LifeScale(sys.life_.bodies[0].mass);
		subject.radius = std::max(subject.radius * 1.7f, 11 * scale);
		return true;
	}
};

const ChronoSystem::GimmickBehavior& ChronoSystem::GlideBehavior() {
	static const GlideGimmick glide;
	return glide;
}
const ChronoSystem::GimmickBehavior& ChronoSystem::WireBehavior() {
	static const WireGimmick wire;
	return wire;
}
const ChronoSystem::GimmickBehavior& ChronoSystem::MikoshiBehavior() {
	static const MikoshiGimmick mikoshi;
	return mikoshi;
}
const ChronoSystem::GimmickBehavior& ChronoSystem::KomaBehavior() {
	static const KomaGimmick koma;
	return koma;
}

// 町を作るとき: 電柱をつないで電線を張り、柱ごとに登る輪を置く。こいのぼりごとに飛ぶ輪を置く
void ChronoSystem::BuildTownGimmicks(
    entt::registry& r, const std::vector<TownPole>& poles, const std::vector<TownGlider>& gliders) {
	const float m = Chrono::SlopeData().unit;
	auto* render = Engine::Renderer::GetInstance();
	wireNodes_.clear();
	wireEdges_.clear();
	for (const auto& pole : poles)
		wireNodes_.push_back({pole.at + V{0, pole.height * Chrono::WireHeight, 0}, pole.visual, {}});
	auto linked = [&](int a, int b) {
		for (int e : wireNodes_[size_t(a)].edges)
			if (wireEdges_[size_t(e)].a == b || wireEdges_[size_t(e)].b == b)
				return true;
		return false;
	};
	auto link = [&](int a, int b) {
		if (a == b || linked(a, b))
			return;
		wireEdges_.push_back({a, b});
		const int e = int(wireEdges_.size()) - 1;
		wireNodes_[size_t(a)].edges.push_back(e);
		wireNodes_[size_t(b)].edges.push_back(e);
	};
	auto flat = [&](int a, int b) {
		V d = wireNodes_[size_t(a)].top - wireNodes_[size_t(b)].top;
		d.y = 0;
		return Length(d);
	};
	// 同じ道の、順番が隣の電柱をつなぐ (間が空きすぎていればつながない)
	for (size_t i = 0; i < poles.size(); ++i) {
		int best = -1;
		for (size_t j = 0; j < poles.size(); ++j)
			if (poles[j].road == poles[i].road && poles[j].order > poles[i].order &&
			    (best < 0 || poles[j].order < poles[size_t(best)].order))
				best = int(j);
		if (best >= 0 && flat(int(i), best) < Chrono::WireSpan * m)
			link(int(i), best);
	}
	// 別の道の一番近い電柱ともつなぐ (町じゅうを電線で回れるように)
	for (size_t i = 0; i < poles.size(); ++i) {
		int best = -1;
		float d = Chrono::WireBridge * m;
		for (size_t j = 0; j < poles.size(); ++j)
			if (poles[j].road != poles[i].road && flat(int(i), int(j)) < d) {
				d = flat(int(i), int(j));
				best = int(j);
			}
		if (best >= 0)
			link(int(i), best);
	}
	// 電線の見た目: 電線ごとに 1 つ (電柱が飲まれたら消す)。2 本を横に並べ、真ん中ほど下がる
	for (auto& edge : wireEdges_) {
		CityBuilder b;
		const V a = wireNodes_[size_t(edge.a)].top, c = wireNodes_[size_t(edge.b)].top;
		V across{-(c.z - a.z), 0, c.x - a.x};
		across = Length(across) > .01f ? Unit(across) * (.35f * m) : V{};
		constexpr int segments = 12;
		for (float side : {-1.f, 1.f})
			for (int k = 0; k < segments; ++k) {
				const V p0 = Chrono::WirePoint(a, c, float(k) / segments) + across * side,
				        p1 = Chrono::WirePoint(a, c, float(k + 1) / segments) + across * side;
				WireTube(b, p0, p1, .045f * m);
			}
		auto& g = b.geometry[CityMetal];
		if (g.indices.empty())
			continue;
		auto e = r.create();
		r.emplace<NameComponent>(e, "Chrono Slope wire");
		r.emplace<TransformComponent>(e).scale = {1, 1, 1};
		auto& mr = r.emplace<MeshRendererComponent>(e);
		mr.modelHandle = render->CreateDynamicMesh(g.vertices, g.indices);
		mr.textureHandle = white_;
		mr.extraTextureHandles = lowMaps_;
		mr.shaderName = "LowpolySurface";
		mr.useCubemap = false;
		mr.color = {.16f, .16f, .18f, 1};
		edge.mesh = uint32_t(e);
	}
	// 登る輪 (膜は範囲に入ったときだけ出す。輪の中の物は押し出さない)
	for (size_t i = 0; i < poles.size(); ++i) {
		if (wireNodes_[i].edges.empty())
			continue;
		const int k =
		    AddGimmick(r, GimmickKind::Wire, "電柱を登る", poles[i].at, Chrono::WireRingRadius * m, 0, 0, {0, -1, 0});
		auto& g = gimmicks_[size_t(k)];
		g.wireNode = int(i);
		g.anchor = poles[i].visual;
		g.quiet = true;
		g.clearArea = false;
	}
	for (const auto& glider : gliders) {
		const int k = AddGimmick(r, GimmickKind::Glide, "こいのぼりで飛ぶ", glider.at, 3.2f * m, 0, 0, {0, -1, 0});
		auto& g = gimmicks_[size_t(k)];
		g.spots = {glider.at + V{0, glider.height * .92f, 0}};
		g.anchor = glider.visual;
		g.clearArea = false;
	}
}
// 毎フレーム: 付いている物が飲まれたギミックを止め、飲まれた電柱の電線を消す
void ChronoSystem::TickTownGimmicks(entt::registry& r) {
	for (auto& g : gimmicks_) {
		if (g.anchorProp < 0 || g.done || size_t(g.anchorProp) >= life_.props.size() ||
		    life_.props[size_t(g.anchorProp)].active)
			continue;
		if (gimmickRide_.index >= 0 && &gimmicks_[size_t(gimmickRide_.index)] == &g)
			continue;
		g.done = true;
		if (g.wireNode < 0)
			continue;
		for (int e : wireNodes_[size_t(g.wireNode)].edges) {
			auto& edge = wireEdges_[size_t(e)];
			if (!edge.alive)
				continue;
			edge.alive = false;
			const auto entity = entt::entity(edge.mesh);
			if (edge.mesh != kNone && r.valid(entity) && r.all_of<MeshRendererComponent>(entity))
				r.get<MeshRendererComponent>(entity).enabled = false;
		}
	}
}

// おみこしとこまの輪を置く (膜はいつも出して、遠くからも見つけられるようにする)
void ChronoSystem::BuildTownRides(
    entt::registry& r, const std::vector<V>& mikoshi, const std::vector<V>& koma, const std::vector<V>& hydrants) {
	const float m = Chrono::SlopeData().unit;
	for (const auto& at : mikoshi)
		AddGimmick(r, GimmickKind::Mikoshi, "おみこしを担ぐ", at, 3.2f * m, 0, 0, {0, -1, 0});
	for (const auto& at : koma)
		AddGimmick(r, GimmickKind::Koma, "こまを回す", at, 3.f * m, 0, 0, {0, -1, 0});
	// 消火栓: 輪の横に消火栓の置物を立てる (飲めない目印)
	for (const auto& at : hydrants) {
		AddGimmick(r, GimmickKind::Hose, "消火栓になる", at, 2.4f * m, 0, 0, {0, -1, 0});
		const V beside = at + V{1.9f * m, 0, 0};
		LowProp(r, "shokasen", V{beside.x, InkGround(beside), beside.z}, LowScaleFor("shokasen", .6f * m), 0, 0);
	}
}
// 町の乗り物の当たり。from の時点で当たっている物は、離れる向きなら止めない (はまって抜けられなくならないように)
bool ChronoSystem::TownRideHit(V from, V to, float radius, V& normal) const {
	// 町の端: どちらの軸で出たかで面の向きを決める
	if (!DevourWalkable(to, radius) && DevourWalkable(from, radius)) {
		const bool bx = !DevourWalkable(V{to.x, to.y, from.z}, radius),
		           bz = !DevourWalkable(V{from.x, from.y, to.z}, radius);
		normal = {bx ? (to.x > from.x ? -1.f : 1.f) : 0.f, 0, bz ? (to.z > from.z ? -1.f : 1.f) : 0.f};
		if (!bx && !bz) {
			V in{-to.x, 0, -to.z};
			normal = Length(in) > .01f ? Unit(in) : V{0, 0, 1};
		}
		return true;
	}
	// 高い段差 (体の大きさで乗り越えられる高さより高い)。下りは止めない
	const auto& body = life_.bodies[0];
	const float climb = LifeScale(body.mass) + .1f, g0 = InkGround(from);
	if (InkGround(to) > g0 + climb) {
		const bool bx = InkGround(V{to.x, to.y, from.z}) > g0 + climb,
		           bz = InkGround(V{from.x, from.y, to.z}) > g0 + climb;
		normal = {bx ? (to.x > from.x ? -1.f : 1.f) : 0.f, 0, bz ? (to.z > from.z ? -1.f : 1.f) : 0.f};
		if (!bx && !bz) {
			V back = from - to;
			back.y = 0;
			normal = Length(back) > 1e-4f ? Unit(back) : V{0, 0, 1};
		}
		return true;
	}
	// 飲めない物 (地面に触れる部分ごとの丸)
	const float mass = body.mass, tall = 2 * LifeScale(mass);
	for (const auto& prop : life_.props) {
		if (!prop.active || prop.sealed || prop.foot <= 0 || prop.morphItem != LifeMorphItem::None ||
		    DevourCanCover(mass, prop.radius))
			continue;
		const float base = prop.at.y - std::min(1.2f, prop.height * .5f);
		if (from.y > base + prop.height || from.y + tall < base)
			continue;
		auto test = [&](V center, float reach) {
			V d = to - center;
			d.y = 0;
			const float distance = Length(d);
			if (distance >= radius + reach)
				return false;
			V d0 = from - center;
			d0.y = 0;
			if (Length(d0) < distance)
				return false;
			normal = distance > 1e-3f ? d * (1 / distance) : V{0, 0, 1};
			return true;
		};
		if (prop.feet.empty()) {
			if (test(prop.at, prop.foot))
				return true;
			continue;
		}
		for (const auto& foot : prop.feet)
			if (test(prop.at + V{foot.x, 0, foot.z}, foot.y))
				return true;
	}
	return false;
}
// おみこしの子分: 担いでいる間は体のまわりの小物へ次々に走り、担いで戻ってきたら飲む。
// おろした後も、持っている物は持ち帰る (取りに行く途中の子分は手ぶらで戻って消える)
void ChronoSystem::TickMikoshiHelpers(entt::registry& r, V home, float dt) {
	auto& body = life_.bodies[0];
	const float scale = LifeScale(body.mass);
	const bool sending =
	    GimmickRidingKind() == int(GimmickKind::Mikoshi) && gimmickRide_.age >= Chrono::GimmickFormTime && !finished_;
	float gained = 0;
	int eaten = 0;
	for (auto& h : mikoshiHelpers_) {
		h.age += dt;
		const bool valid = h.prop >= 0 && size_t(h.prop) < life_.props.size();
		V goal = home;
		if (h.phase == 0) {
			// 取りに行く物が無くなった (体が先に飲んだ) か、おろした: 手ぶらで戻る
			if (!valid || !life_.props[size_t(h.prop)].active || !sending) {
				if (valid && life_.props[size_t(h.prop)].active)
					life_.props[size_t(h.prop)].sealed = false;
				h.prop = -1;
				h.phase = 2;
			} else
				goal = life_.props[size_t(h.prop)].at;
		}
		V d = goal - h.at;
		d.y = 0;
		const float distance = Length(d), step = Chrono::MikoshiRun * scale * (h.phase == 0 ? 1.f : 1.15f) * dt;
		if (h.phase == 0) {
			auto& prop = life_.props[size_t(h.prop)];
			if (distance <= step + prop.radius * .5f) {
				h.phase = 1;
				// 持ち上げる: 元の場所の見た目は隠し、子分の頭の上に描く
				if (prop.visual != kNone) {
					auto e = entt::entity(prop.visual);
					if (r.valid(e) && r.all_of<MeshRendererComponent>(e))
						r.get<MeshRendererComponent>(e).enabled = false;
				}
			} else
				h.at = h.at + d * (step / std::max(distance, 1e-4f));
		} else if (distance <= step + Chrono::MikoshiHome * scale) {
			if (h.phase == 1 && valid && life_.props[size_t(h.prop)].active) {
				auto& prop = life_.props[size_t(h.prop)];
				prop.at = home;
				devourEatBy_ = 6;
				gained += DevourEat(r, size_t(h.prop));
				++eaten;
			}
			h.phase = 3;
		} else
			h.at = h.at + d * (step / std::max(distance, 1e-4f));
		h.at.y = InkGround(h.at);
		if (h.phase == 1 && valid)
			life_.props[size_t(h.prop)].at = h.at + V{0, 1.6f * scale, 0};
	}
	mikoshiHelpers_.erase(
	    std::remove_if(
	        mikoshiHelpers_.begin(), mikoshiHelpers_.end(), [](const MikoshiHelper& h) { return h.phase == 3; }),
	    mikoshiHelpers_.end());
	if (eaten) {
		devourClock_.combo += eaten;
		devourClock_.comboClock = 1.4f;
		const float weight = DevourGulpWeight(eaten, gained, body.mass);
		PlayDevourGulp(weight, devourClock_.combo);
		devourSizePulse_ = 1;
		if (body.aspect < 1.3f)
			body.aspectVelocity = std::max(body.aspectVelocity, 3 + 6 * weight);
		// (持ってくるたびに出すと文字が重なって読めないので、間を空ける)
		if (mikoshiTalk_ <= 0) {
			mikoshiTalk_ = .7f;
			devourPops_.push_back(
			    {body.at + body.forward * (3 * scale) + V{0, (Chrono::GimmickMikoshiTop - 1) * scale, 0},
			     GimmickWord("mikoshiBring"), 0, weight, kPopRise, GimmickSeed()});
		}
	}
	mikoshiTalk_ -= dt;
	// 新しい子分を出す: 届く所の飲める物のうち、あえて遠くの物へ (遠い側の物から 1 つを選ぶ。子分がいろいろな向きへ散らばって走る。
	// 遠い側に無ければ一番遠い物。近くの物は体が歩いて飲める)
	mikoshiSend_ -= dt;
	int out = 0;
	for (const auto& h : mikoshiHelpers_)
		out += h.phase < 2;
	if (!sending || mikoshiSend_ > 0 || out >= Chrono::MikoshiHelpers)
		return;
	const float reach = Chrono::MikoshiReach * scale;
	int best = -1, pickFar = -1;
	float farthest = 0;
	std::vector<int> band;
	for (size_t i = 0; i < life_.props.size(); ++i) {
		const auto& prop = life_.props[i];
		if (!prop.active || prop.sealed || prop.morphItem != LifeMorphItem::None ||
		    !DevourCanCover(body.mass, prop.radius))
			continue;
		V d = prop.at - home;
		if (std::abs(d.y) > reach * .5f)
			continue;
		d.y = 0;
		const float distance = Length(d);
		if (distance > reach)
			continue;
		if (distance >= reach * Chrono::MikoshiFar)
			band.push_back(int(i));
		if (distance > farthest) {
			farthest = distance;
			pickFar = int(i);
		}
	}
	best = band.empty() ? pickFar : band[std::min(band.size() - 1, size_t(GimmickSeed() * float(band.size())))];
	if (best < 0)
		return;
	life_.props[size_t(best)].sealed = true;
	mikoshiSend_ = Chrono::MikoshiSend;
	MikoshiHelper h;
	h.prop = best;
	h.seed = GimmickSeed();
	V toward = life_.props[size_t(best)].at - home;
	toward.y = 0;
	toward = Length(toward) > .01f ? Unit(toward) : body.forward;
	h.at = home + toward * (DevourBodyRadius(body.mass) * .6f);
	h.at.y = InkGround(h.at);
	h.from = h.at;
	mikoshiHelpers_.push_back(h);
}
// ステージを作り直した: 子分を全部消す (持っていた物は元の場所に戻す)
void ChronoSystem::EndMikoshiHelpers(entt::registry& r) {
	for (auto& h : mikoshiHelpers_) {
		if (h.prop < 0 || size_t(h.prop) >= life_.props.size())
			continue;
		auto& prop = life_.props[size_t(h.prop)];
		if (!prop.active)
			continue;
		prop.sealed = false;
		if (h.phase == 1) {
			prop.at = h.at + V{0, 1.2f, 0};
			if (prop.visual != kNone) {
				auto e = entt::entity(prop.visual);
				if (r.valid(e) && r.all_of<MeshRendererComponent>(e))
					r.get<MeshRendererComponent>(e).enabled = true;
			}
		}
	}
	mikoshiHelpers_.clear();
	mikoshiSend_ = 0;
}
// 子分を描く: 体と同じ色の小さなスライムが跳ねながら走り、持ってくる物を頭の上に載せる。
// 担いでいる間は、担ぎ棒の下に担ぎ手の子分 4 匹もいて、わっしょいに合わせて跳ねる
void ChronoSystem::DrawMikoshiHelpers(GameContext& ctx) {
	if (life_.bodies.empty())
		return;
	const auto& body = life_.bodies[0];
	const float scale = LifeScale(body.mass), size = Chrono::MikoshiHelperSize * scale;
	const auto rgb = life_.recipe.color.ToRGB();
	auto blob = [&](V foot, V dir, float hop) {
		const V right{dir.z, 0, -dir.x};
		const float squash = 1 - .25f * (1 - hop);
		const V center = foot + V{0, size * (.8f + .9f * hop), 0};
		Engine::Transform t;
		t.translate = EV(center);
		t.scale = {size / squash, size * squash, size / squash};
		ctx.renderer->DrawMeshInstanced(sphere_, white_, t, {rgb.x, rgb.y, rgb.z, 1});
		for (float side : {-1.f, 1.f}) {
			Engine::Transform e;
			e.translate = EV(center + dir * (size * .82f) + right * (side * size * .38f) + V{0, size * .25f, 0});
			e.scale = {size * .17f, size * .24f, size * .17f};
			ctx.renderer->DrawMeshInstanced(sphere_, white_, e, {.05f, .05f, .06f, 1});
		}
		return center + V{0, size * squash, 0};
	};
	if (GimmickRidingKind() == int(GimmickKind::Mikoshi) && gimmickRide_.age > Chrono::GimmickFormTime) {
		const V f = gimmickRide_.look, right{f.z, 0, -f.x};
		const float hop = Chrono::MikoshiBounce(gimmickRide_.age),
		            ground = body.at.y - 1.25f - hop * Chrono::MikoshiHop * scale;
		for (float side : {-1.f, 1.f})
			for (float end : {-1.f, 1.f}) {
				V foot = body.at + right * (side * Chrono::GimmickMikoshiPoleX * scale) +
				         f * (end * (Chrono::GimmickMikoshiPoleZ - .6f) * scale);
				foot.y = ground + hop * Chrono::MikoshiHop * scale;
				blob(foot, f, hop);
			}
	}
	if (mikoshiHelpers_.empty())
		return;
	for (const auto& h : mikoshiHelpers_) {
		const bool valid = h.prop >= 0 && size_t(h.prop) < life_.props.size();
		V dir = (h.phase == 0 && valid ? life_.props[size_t(h.prop)].at : body.at) - h.at;
		dir.y = 0;
		dir = Length(dir) > .01f ? Unit(dir) : V{0, 0, 1};
		const V right{dir.z, 0, -dir.x};
		const float hop = std::abs(std::sin(h.age * 11 + h.seed * 6.f)), squash = 1 - .25f * (1 - hop);
		const V center = h.at + V{0, size * (.8f + .9f * hop), 0};
		Engine::Transform t;
		t.translate = EV(center);
		t.scale = {size / squash, size * squash, size / squash};
		ctx.renderer->DrawMeshInstanced(sphere_, white_, t, {rgb.x, rgb.y, rgb.z, 1});
		// 目
		for (float side : {-1.f, 1.f}) {
			Engine::Transform e;
			e.translate = EV(center + dir * (size * .82f) + right * (side * size * .38f) + V{0, size * .25f, 0});
			e.scale = {size * .17f, size * .24f, size * .17f};
			ctx.renderer->DrawMeshInstanced(sphere_, white_, e, {.05f, .05f, .06f, 1});
		}
		// 持っている物
		if (h.phase == 1 && valid) {
			const auto& prop = life_.props[size_t(h.prop)];
			const float fit = std::min(1.f, 2.4f * size / std::max(prop.radius, .01f));
			DrawCityPropInTentacle(ctx, prop.visual, center + V{0, size * squash + prop.radius * fit * .9f, 0}, fit);
		}
	}
}
} // namespace Game
