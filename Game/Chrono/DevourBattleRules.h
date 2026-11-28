// DevourBattleRules.h
// チャレンジ (吸収のあとの第 2 フェーズ) の、物の能力と衝撃の規則。エンジンに依存しない。
//  - 吸収した物は図鑑の分類 (collection.json の battle) と能力 (action) を持つ。能力は変形武器の性質の元になる (WeaponCraftRules.h)
//  - 威力は今の大きさから決める
#pragma once
#include "DevourRules.h"
#include "DevourStageRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
namespace Game::Chrono {
// 使い道の種類 (Candy は吸収していなくても使える飴玉)
enum class BattleKind : uint8_t { Throw, Ride, Smash, Guard, Heal, Candy, Count };
// collection.json の battle の文字から種類を引く。building は戦闘で戻す建物 (使い道なし)
inline BattleKind BattleKindOf(const std::string& name, bool& building) {
	building = name == "building";
	if (name == "ride")
		return BattleKind::Ride;
	if (name == "smash")
		return BattleKind::Smash;
	if (name == "guard")
		return BattleKind::Guard;
	if (name == "heal")
		return BattleKind::Heal;
	return BattleKind::Throw;
}

// ---- 物ごとの能力 (collection.json の action。省略すると battle の分類の動き) ----
// Throw 投げる・Ride 轢く (車)・Smash 叩く (ハンマー)・Guard 守る (盾)・Heal 回復 (ハート) は分類の動き。
// 残りは物ごとの能力: Heli 竹とんぼ・Water 水鉄砲・Roll 転がる (ボウリングの玉)・Spin こま・Sword 剣・Fly 飛行機・Spring ばね・Vacuum 掃除機・Rc ラジコン
// 将棋の駒は駒の動きの技: Fu 歩 (前を突く)・Kyo 香車 (まっすぐ突進)・Kei 桂馬 (斜め前へ跳んで着地で押しつぶす)・Gin 銀 (前と斜め 4 方向を突く)・
// Kin 金 (後ろの斜め以外の 6 方向を突く)・Kaku 角 (斜め 4 方向のビーム)・Hi 飛車 (十字 4 方向のビーム)・Ou 王 (まわり全部を吹き飛ばし、しばらく無敵)
// 置く・まく物: Marbles ビー玉をまく (踏んだ敵が転んで止まる)・Tower 積み木の塔 (敵を止め、最後に崩れて前へダメージ)・
// Domino ドミノ倒し (前へ一列に並べて順に倒す)・Paint 絵の具 (塗った床で敵は遅く、自分は速い)・Decoy ぬいぐるみ (敵を引きつけ、最後に弾ける)
// 飛び道具 (投げるけれど飛び方が違う): Coin 十円玉 (敵から敵へ跳ね移る)・Disc だるま落としの輪 (床を滑り、ステージの縁で跳ね返る)・
// Bounce ゴムボール (床と敵で何度も跳ねる)・Homing 紙飛行機 (近い敵を追う)・Fan トランプ (5 枚を扇に投げる)・Pierce 鉛筆 (敵を突き抜ける)
enum class BattleAction : uint8_t {
	Throw,
	Ride,
	Smash,
	Guard,
	Heal,
	Heli,
	Water,
	Roll,
	Spin,
	Sword,
	Fly,
	Spring,
	Vacuum,
	Rc,
	Fu,
	Kyo,
	Kei,
	Gin,
	Kin,
	Kaku,
	Hi,
	Ou,
	Marbles,
	Tower,
	Domino,
	Paint,
	Decoy,
	Coin,
	Disc,
	Bounce,
	Homing,
	Fan,
	Pierce,
	// 止める・弱らせる: Pause リモコン (敵が全部しばらく止まる)・Read まんが (まわりの敵が座り込んで読みふける)・Cage かご (前の敵 1 体を閉じ込める)・
	// Sticky ノート (紙を貼り付けて止める)・Erase 消しゴム (前をこすって弱らせる: 受けるダメージが増える)・Tea 湯のみ (熱いお茶をまいてやけど)
	// 自分を強くする: Roly だるま (起き上がりこぼし: 無敵で、殴った敵にはね返す)・Beckon 招き猫 (敵を招き寄せ、落とす物が 2 倍)・
	// Brew 急須 (少しずつ回復し続ける)・Fluff ティッシュ (ふわふわの雲で攻撃が当たりにくい)・Smell 靴下 (においの雲で敵が近づけない)
	Pause,
	Read,
	Cage,
	Sticky,
	Erase,
	Tea,
	Roly,
	Beckon,
	Brew,
	Fluff,
	Smell,
	Count
};
inline const char* BattleActionName(BattleAction a) {
	static constexpr const char* names[] = {"投げる",       "轢く",       "叩く",           "守る",       "回復",
	                                        "回る翼",       "水鉄砲",     "転がる",         "こま",       "剣",
	                                        "飛ぶ",         "ばね",       "吸い込む",       "ラジコン",   "歩の突き",
	                                        "香車の突進",   "桂馬跳び",   "銀の突き",       "金の突き",   "角のビーム",
	                                        "飛車のビーム", "王の威光",   "ばらまき",       "積み木の塔", "ドミノ倒し",
	                                        "色ぬり",       "おとり",     "はじく",         "円盤",       "はねる",
	                                        "追いかける",   "扇投げ",     "突き抜ける",     "一時停止",   "読みふける",
	                                        "閉じ込める",   "貼り付ける", "消して弱らせる", "熱いお茶",   "起き上がり",
	                                        "招く",         "お茶で回復", "ふわふわ",       "におい"};
	return names[std::min(int(a), int(BattleAction::Count) - 1)];
}
inline BattleAction BattleActionOf(const std::string& action, const std::string& category) {
	static constexpr const char* keys[] = {
	    "throw",   "ride",   "smash",  "guard",  "heal",  "heli", "water", "roll",   "spin",   "sword", "fly",
	    "spring",  "vacuum", "rc",     "fu",     "kyo",   "kei",  "gin",   "kin",    "kaku",   "hi",    "ou",
	    "marbles", "tower",  "domino", "paint",  "decoy", "coin", "disc",  "bounce", "homing", "fan",   "pierce",
	    "pause",   "read",   "cage",   "sticky", "erase", "tea",  "roly",  "beckon", "brew",   "fluff", "smell"};
	for (int i = 0; i < int(BattleAction::Count); ++i)
		if (action == keys[i])
			return BattleAction(i);
	if (category == "ride")
		return BattleAction::Ride;
	if (category == "smash")
		return BattleAction::Smash;
	if (category == "guard")
		return BattleAction::Guard;
	if (category == "heal")
		return BattleAction::Heal;
	return BattleAction::Throw;
}
// 能力の長さ (秒)・使った後の待ち時間
inline float BattleActionTime(BattleAction a) {
	static constexpr float t[] = {0,    1.f, .74f, 5,   .9f,  1.6f, 1.4f, 1.4f, 2.f, .5f,  1.1f, 1.f, 1.6f, 1.8f, .4f,
	                              1.2f, .6f, .45f, .5f, .55f, .55f, 3.f,  .3f,  .4f, 1.1f, .5f,  .4f, 0,    0,    0,
	                              0,    0,   0,    0,   0,    0,    0,    .6f,  0,   4.f,  0,    0,   0,    0};
	return t[std::min(int(a), int(BattleAction::Count) - 1)];
}
// 能力の強さ: 回る翼・こまの 1 回の当たり (0.2 秒ごと)・水の 1 粒・転がる/飛ぶ/ラジコンの当たり・剣・ばねの着地・掃除機の 1 回の当たり
inline constexpr float BattleHeliDamage = 7, BattleSpinDamage = 8, BattleWaterDamage = 4, BattleRollDamage = 12,
                       BattleFlyDamage = 18, BattleRcDamage = 14, BattleSwordDamage = 22, BattleSpringDamage = 30,
                       BattleVacuumDamage = 4;
// 届く範囲 (体の半径に対する倍率)
inline constexpr float BattleHeliReach = 2.2f, BattleSpinReach = 2.f, BattleSwordReach = 2.8f, BattleSpringReach = 3.2f,
                       BattleVacuumReach = 4.5f;
// 突進の速さの倍率 (歩く速さに対して): 轢く・転がる・飛ぶ・ラジコン
inline float BattleDashBoost(BattleAction a) {
	return a == BattleAction::Roll  ? 2.2f
	       : a == BattleAction::Fly ? 3.4f
	       : a == BattleAction::Rc  ? 3.f
	       : a == BattleAction::Kyo ? 3.6f
	       : a == BattleAction::Kei ? 4.2f
	       : a == BattleAction::Fu  ? 2.4f
	                                : 2.6f;
}
// 将棋の駒の技の強さ (1 回の当たり)・突きの長さ (体の半径に対する倍率)・突きとビームの太さ (体の半径に対する倍率)
inline constexpr float BattleFuDamage = 32, BattleKyoDamage = 24, BattleKeiDamage = 32, BattleGinDamage = 20,
                       BattleKinDamage = 18, BattleKakuDamage = 24, BattleHiDamage = 24, BattleOuDamage = 38;
inline constexpr float BattleThrustLength = 3.2f, BattleThrustWidth = .8f, BattleBeamWidth = .7f, BattleOuReach = 4.f,
                       BattleKeiReach = 2.6f;
// ---- 置く・まく物 ----
// ビー玉: まく数・残る秒・転んで止まる秒・当たり
inline constexpr int BattleMarbleCount = 9;
inline constexpr float BattleMarbleLife = 12, BattleMarbleStun = 1.8f, BattleMarbleDamage = 5;
// 積み木の塔: 置く距離と横幅 (体の半径に対する倍率)・残る秒・崩れたときの当たり
inline constexpr float BattleTowerAhead = 2.6f, BattleTowerHalf = 1.8f, BattleTowerLife = 6, BattleTowerDamage = 34;
// ドミノ: 並べる数・間隔 (体の半径に対する倍率)・倒れ始めるまでと 1 枚ごとの間・1 枚の当たり
inline constexpr int BattleDominoCount = 9;
inline constexpr float BattleDominoGap = 1.15f, BattleDominoStart = .35f, BattleDominoStep = .13f,
                       BattleDominoDamage = 16;
// 絵の具: 塗る半径 (体の半径に対する倍率)・残る秒・自分の速さ
inline constexpr float BattlePaintRadius = 5, BattlePaintLife = 8, BattlePaintBoost = 1.4f;
// ぬいぐるみ: 置く距離 (体の半径に対する倍率)・引きつける秒・弾ける範囲と当たり
inline constexpr float BattleDecoyAhead = 3, BattleDecoyLife = 5, BattleDecoyReach = 3.6f, BattleDecoyDamage = 32;
// ---- 飛び道具 ----
// 当たり・当たる回数 (十円玉は跳ね移る数、輪とボールは当たれる数)・残る秒
inline constexpr float BattleCoinDamage = 13, BattleDiscDamage = 11, BattleBounceDamage = 10, BattleHomingDamage = 28,
                       BattleFanDamage = 10, BattlePierceDamage = 24;
inline constexpr int BattleCoinHops = 5, BattleDiscHits = 10, BattleBounceHits = 7, BattleFanCards = 5;
// ---- 止める・弱らせる・自分を強くする ----
// リモコン: 止める秒 (ボスは短い)・まんが: 届く範囲 (体の半径に対する倍率) と止める秒・かご: 届く範囲と閉じ込める秒・ノート: 止める秒
inline constexpr float BattlePauseTime = 2.2f, BattlePauseBoss = .8f, BattleReadReach = 4, BattleReadTime = 3,
                       BattleCageReach = 6, BattleCageTime = 6, BattleStickyTime = 2.5f, BattleStickyDamage = 6;
// 消しゴム: 届く範囲・弱らせる秒・こすった当たり。湯のみ: 届く範囲・やけどの秒と 1 秒の当たり
inline constexpr float BattleEraseReach = 2.6f, BattleWeakTime = 6, BattleEraseDamage = 6,
                       BattleTeaReach = 3.6f, BattleBurnTime = 4, BattleBurnDps = 7;
// 招き猫: 招く範囲。急須: 効き目の秒。ティッシュ: 効き目の秒。靴下: 雲の半径と秒
inline constexpr float BattleBeckonReach = 9, BattleBrewTime = 6, BattleFluffTime = 6, BattleSmellRadius = 4,
                       BattleSmellLife = 7;
inline constexpr float BattleCoinLife = 2.5f, BattleDiscLife = 4.5f, BattleBounceLife = 5, BattleHomingLife = 4,
                       BattlePierceLife = 1.6f, BattleHomingTurn = 5;


// 時間の基準 (チャレンジの時間が決まる前の値)
inline constexpr float BattleTime = 120;

// 威力の倍率: クリアの大きさ (最初の目標) で 1
inline float BattlePower(const DevourStage& stage, float mass) {
	return std::clamp(std::sqrt(DevourSizeCm(stage, mass) / std::max(.01f, stage.goals.front())), .35f, 4.f);
}

inline float BattleUnitAt(float startMass) {
	return DevourBodyRadius(startMass);
}
// 体の動く速さ (戦闘を始めたときより大きいほど少し速い)
inline float BattleMoveSpeed(float unit, float radius) {
	return unit * (3.5f + 1.5f * std::clamp(std::sqrt(radius / std::max(.01f, unit)), .35f, 4.f));
}
// 専用の広いステージ: 戦う円の半径 (クリアの大きさの体の半径に対する倍率) と、マップの端からの離し方 (半径に対する倍率)
inline constexpr float BattleArenaUnits = 45, BattleArenaGap = 3, BattleArenaFar = 2500;

// 攻撃の強さ (威力の倍率をかける前)
inline constexpr float BattleThrowDamage = 14, BattleRideDamage = 20, BattleSmashDamage = 26;
// 守るの秒数
inline constexpr float BattleGuardTime = 5;
// 轢くの秒数と速さの倍率・叩くの届く距離 (体の半径に対する倍率)
inline constexpr float BattleSmashReach = 2.4f;
// 叩く: ハンマーの形になって振りかぶる秒と、振り下ろす秒 (振り下ろした瞬間に当たる)
inline constexpr float BattleSmashWindup = .3f, BattleSmashSwing = .14f;
// 投げた物の大きさで威力を変える (戦闘の大きさの基準に対して)
inline float BattleItemScale(float unit, float radius) {
	return std::clamp(std::sqrt(radius / std::max(.01f, unit * .5f)), .5f, 2.f);
}

// ---- 能力の当たり (衝撃) ----
// どの能力も、当たりは「衝撃 (BattleImpact)」として出す (ChronoSystem::EmitBattleImpact)。
// 衝撃を受ける側 (旧来の戦闘の敵・チャレンジの物) は、効き目 (effect) を見て自分の動きで反応する。
// これで、能力を 1 つずつ作り込まなくても、全部の能力がどのチャレンジでも何かを起こせる。
//  形: Sphere は at を中心に reach の円 (flat なら高さを見ない)・Cone は at から dir の向きの扇 (dir との内積が cone 以上)・
//      Segment は at から to の線から width 以内・Strip は at から dir の向きへ -back〜reach、横に width 以内 (相手の半径は横だけに足す)
//  効き目: Hit 当てる (damage と push)・Pull 引き寄せる (at へ push の強さ。pullCap があれば距離に比例)・PushOut 範囲の外へ押し出す (push は 1 フレームに押す割合)・
//      Wall 線から width の外へ押し出す・Stun 止める (seconds。damage もあれば当てる)・Weaken 弱らせる (seconds の間、受ける当たりが増える)・
//      Burn やけど (seconds の間、1 秒に damage)・Capture 閉じ込める (seconds)・Zone 場所に効く物を置いた (絵の具の床・ぬいぐるみなど。範囲だけを知らせる)・
//      Self 自分に効く (守る・回復など。体のまわりを知らせる)
enum class ImpactShape : uint8_t { Sphere, Cone, Segment, Strip };
enum class ImpactEffect : uint8_t { Hit, Pull, PushOut, Wall, Stun, Weaken, Burn, Capture, Zone, Self, Count };
inline const char* ImpactEffectName(ImpactEffect e) {
	static constexpr const char* names[] = {"hit",    "pull", "push_out", "wall", "stun",
	                                        "weaken", "burn", "capture",  "zone", "self"};
	return names[std::min(int(e), int(ImpactEffect::Count) - 1)];
}
struct BattleImpact {
	BattleAction action = BattleAction::Throw;
	ImpactEffect effect = ImpactEffect::Hit;
	ImpactShape shape = ImpactShape::Sphere;
	Vec at{}, to{}, dir{0, 0, 1};
	float reach = 0, cone = -1, width = 0, back = 0;
	bool flat = true;
	// 強さ: damage は当たり (威力の倍率をかけたあと)、push は押す強さ (戦闘の大きさの基準 unit の何倍か)。radial なら at から外向き (負なら内向き)、そうでなければ dir の向き
	float damage = 0, push = 0, pullCap = 0, seconds = 0;
	bool radial = false;
	// 大きい相手 (ボス・重い物) への効き目の倍率と、大きい相手には効かないか。onlyFree は止まっていない相手だけ。limit は当てる数の上限 (0 は無制限)
	float bigScale = 1;
	bool spareBig = false, onlyFree = false;
	int limit = 0;
	// 同じ相手に続けて当てない: source が同じ衝撃は、同じ相手に rehit 秒たつまで当たらない (0 なら毎回当たる)
	uint32_t source = 0;
	float rehit = 0;
	// only が 0 でなければ、その番号の相手だけに当てる (かごで選んだ 1 体など)
	uint32_t only = 0;
	// 当たった相手の上に出す文字 (無ければ出さない)
	const char* label = nullptr;
};
// 相手 (中心 p・半径 radius) が衝撃の形に入るか
inline bool BattleImpactTouches(const BattleImpact& i, Vec p, float radius) {
	Vec d = p - i.at;
	if (i.flat)
		d.y = 0;
	switch (i.shape) {
	case ImpactShape::Sphere:
		return Length(d) <= i.reach + radius;
	case ImpactShape::Cone: {
		const float dist = Length(d);
		if (dist > i.reach + radius)
			return false;
		if (dist <= .01f)
			return true;
		Vec aim = i.dir;
		if (i.flat)
			aim.y = 0;
		return Dot(d * (1 / dist), Unit(aim)) >= i.cone;
	}
	case ImpactShape::Segment: {
		Vec a = i.at, b = i.to;
		if (i.flat) {
			a.y = b.y = p.y;
		}
		const Vec ab = b - a;
		const float t = std::clamp(Dot(p - a, ab) / std::max(1e-6f, Dot(ab, ab)), 0.f, 1.f);
		return Length(p - (a + ab * t)) <= i.width + radius;
	}
	case ImpactShape::Strip: {
		Vec f = i.dir;
		f.y = 0;
		f = Unit(f);
		const Vec side{f.z, 0, -f.x};
		const float along = Dot(d, f);
		return std::abs(Dot(d, side)) <= i.width + radius && along >= -i.back && along <= i.reach;
	}
	}
	return false;
}
// 衝撃の数え方 (確認用): 能力ごとに出した数と、何かに当たった数
struct BattleImpactTally {
	std::array<int, size_t(BattleAction::Count)> emitted{}, touched{};
	std::array<std::array<int, size_t(ImpactEffect::Count)>, size_t(BattleAction::Count)> effects{};
};
// 全部の能力の衝撃を確かめる (--challenge-impact-check): 1 つの能力を使う間隔の秒
inline constexpr float BattleImpactCheckStep = 1.6f;
} // namespace Game::Chrono
