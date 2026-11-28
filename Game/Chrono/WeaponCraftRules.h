// WeaponCraftRules.h
// 変形武器づくり (準備フェーズのあと、チャレンジの前) の規則。エンジンに依存しない。
//  - 集めた物から 3 つ選んで、武器を 1 本だけ作る (どちらのチャレンジでも同じ)
//  - 3 つの物の技は、順に出るのではなく混ざって 1 つの新しい武器になる (合成)。
//    物ごとに「性質」(追尾・貫通・重い・跳ねる・爆発…) を持ち、3 つの性質を全部あわせ持った弾を撃つ。
//    例: 鉛筆 (貫通・速い) + ボウリングの玉 (転がる・重い) + 紙飛行機 (追尾) = 重い玉が転がって追いかけ、突き抜ける
//    同じ性質が重なると強くなる (性質のレベル)。1 つ目の物 (本体) は、撃ち出す物の見た目と、体の変形の形の土台になる
//  - 体の形は 3 つの物のモデルを組み合わせて作るので、材料が 1 つでも違えば (並べる順が違っても) 違う形になる
//  - 回数の制限はない。枠が埋まったあとに入れた物は強化 (弾数・威力・連射) の点になり、大きい物ほど点が高い (入れなかった物は使わない)。
//    強化の伸びは大きく、1 つに極端に振ると 1 回で何十発・巨大な弾・機関銃のような連射になる (上限は動きが重くならない所で止める)
#pragma once
#include "DevourBattleRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
namespace Game::Chrono {
inline constexpr int CraftParts = 3;
// 作る画面が開いているか (開いている間は、プレイ中でもマウスカーソルを真ん中に固定しない: GameScene)
inline bool& CraftScreenOpen() {
	static bool open = false;
	return open;
}
inline const char* CraftPartRole(int part) {
	static constexpr const char* names[] = {"本体", "材料", "材料"};
	return names[std::clamp(part, 0, CraftParts - 1)];
}

// ---- 性質 ----
// Homing 追いかける・Pierce 突き抜ける・Bounce 跳ねる・Roll 床を転がる・Heavy 重い (大きく強いが遅い)・Split 当たると分かれる・
// Explode 当たる/落ちると爆発・Chain 次の相手へ跳び移る・Spin 回って当たりが広い・Stun 揺らす (当たった所のまわりの積み木にも当たる)・
// Burn 熱い (当てた積み木を焦がして少しずつ崩す)・
// Magnet まわりを吸い寄せる・Speed 速い・Paint 通った跡もたっぷり塗る・Wall 止まった所に壁 (積み木の塔) を建てる・Float ふわふわ長く浮く・
// Weaken ひび (当てた積み木とそのまわりにひびを入れ、次の当たりを強くする)・Return ブーメランのように戻ってくる
// (どの性質も、積み木を崩すための物。相手を止める・弱らせるような、生き物の相手に向けた効き目は無い)
enum class FuseTrait : uint8_t {
	Homing,
	Pierce,
	Bounce,
	Roll,
	Heavy,
	Split,
	Explode,
	Chain,
	Spin,
	Stun,
	Burn,
	Magnet,
	Speed,
	Paint,
	Wall,
	Float,
	Weaken,
	Return,
	Count
};
inline constexpr int FuseTraitCount = int(FuseTrait::Count);
inline const char* FuseTraitName(FuseTrait t) {
	static constexpr const char* names[] = {"追尾", "貫通", "跳ねる", "転がる",   "重い", "分裂",
	                                        "爆発", "連鎖", "回転",   "揺らす",   "熱い", "吸い寄せ",
	                                        "速い", "塗り", "壁",     "ふわふわ", "ひび", "戻る"};
	return names[std::min(int(t), FuseTraitCount - 1)];
}
// 武器の名前に付ける言葉
inline const char* FuseTraitWord(FuseTrait t) {
	static constexpr const char* words[] = {"追いかける", "突き抜ける",   "跳ねる",    "転がる",       "重い",
	                                        "分かれる",   "爆発する",     "跳び移る",  "回る",         "揺さぶる",
	                                        "熱い",       "吸い寄せる",   "速い",      "たっぷり塗る", "壁を建てる",
	                                        "ふわふわ",   "ひびを入れる", "戻ってくる"};
	return words[std::min(int(t), FuseTraitCount - 1)];
}
// 物の技ごとの性質 (2 つまで。Count は無し)。ふつうに投げる物は性質なし (本体の大きさだけ効く)
inline std::array<FuseTrait, 2> FuseTraitsOf(BattleAction a) {
	using T = FuseTrait;
	constexpr T N = T::Count;
	switch (a) {
	case BattleAction::Ride:
		return {T::Speed, T::Roll};
	case BattleAction::Rc:
		return {T::Speed, T::Homing};
	case BattleAction::Fly:
		return {T::Homing, T::Speed};
	case BattleAction::Smash:
		return {T::Heavy, T::Explode};
	case BattleAction::Guard:
		return {T::Bounce, T::Wall};
	case BattleAction::Heal:
		return {T::Paint, T::Float};
	case BattleAction::Heli:
		return {T::Return, T::Float};
	case BattleAction::Water:
		return {T::Paint, T::Speed};
	case BattleAction::Roll:
		return {T::Roll, T::Heavy};
	case BattleAction::Spin:
		return {T::Spin, N};
	case BattleAction::Sword:
		return {T::Pierce, T::Spin};
	case BattleAction::Spring:
		return {T::Bounce, N};
	case BattleAction::Vacuum:
		return {T::Magnet, N};
	case BattleAction::Fu:
		return {T::Pierce, N};
	case BattleAction::Kyo:
		return {T::Pierce, T::Speed};
	case BattleAction::Kei:
		return {T::Bounce, T::Split};
	case BattleAction::Gin:
		return {T::Split, N};
	case BattleAction::Kin:
		return {T::Split, T::Heavy};
	case BattleAction::Kaku:
		return {T::Pierce, T::Split};
	case BattleAction::Hi:
		return {T::Pierce, T::Speed};
	case BattleAction::Ou:
		return {T::Explode, T::Heavy};
	case BattleAction::Marbles:
		return {T::Split, T::Roll};
	case BattleAction::Tower:
		return {T::Wall, N};
	case BattleAction::Domino:
		return {T::Chain, T::Wall};
	case BattleAction::Paint:
		return {T::Paint, N};
	case BattleAction::Decoy:
		return {T::Explode, T::Magnet};
	case BattleAction::Coin:
		return {T::Chain, T::Speed};
	case BattleAction::Disc:
		return {T::Roll, T::Bounce};
	case BattleAction::Bounce:
		return {T::Bounce, N};
	case BattleAction::Homing:
		return {T::Homing, N};
	case BattleAction::Fan:
		return {T::Split, T::Speed};
	case BattleAction::Pierce:
		return {T::Pierce, T::Speed};
	case BattleAction::Pause:
		return {T::Stun, N};
	case BattleAction::Read:
		return {T::Stun, T::Heavy};
	case BattleAction::Cage:
		return {T::Stun, T::Magnet};
	case BattleAction::Sticky:
		return {T::Stun, N};
	case BattleAction::Erase:
		return {T::Weaken, N};
	case BattleAction::Tea:
		return {T::Burn, N};
	case BattleAction::Roly:
		return {T::Bounce, T::Heavy};
	case BattleAction::Beckon:
		return {T::Magnet, N};
	case BattleAction::Brew:
		return {T::Burn, T::Paint};
	case BattleAction::Fluff:
		return {T::Float, N};
	case BattleAction::Smell:
		return {T::Burn, T::Float};
	default:
		return {N, N};
	}
}
using FuseLevels = std::array<int, FuseTraitCount>;
inline FuseLevels FuseLevelsOf(const std::array<BattleAction, CraftParts>& actions, int filled) {
	FuseLevels lv{};
	for (int i = 0; i < filled; ++i)
		for (FuseTrait t : FuseTraitsOf(actions[size_t(i)]))
			if (t != FuseTrait::Count)
				++lv[size_t(t)];
	return lv;
}
// 武器の名前: 強い順に性質の言葉を 3 つまで + 本体の名前 (性質が無ければ本体の名前だけ)
inline std::string FuseWeaponName(const FuseLevels& lv, const std::string& body) {
	std::vector<int> order;
	for (int t = 0; t < FuseTraitCount; ++t)
		if (lv[size_t(t)] > 0)
			order.push_back(t);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return lv[size_t(a)] > lv[size_t(b)]; });
	std::string name;
	for (size_t i = 0; i < order.size() && i < 3; ++i)
		name += (i ? "・" : "") + std::string(FuseTraitWord(FuseTrait(order[i])));
	return name.empty() ? body : name + " " + body;
}

// ---- 弾の強さ (性質のレベルで決まる。unit は戦闘の大きさの基準) ----
// 速さ (unit/秒)・残る秒・大きさ (半径。unit に対する倍率)・当たり・当たれる数
inline constexpr float FuseSpeed = 14, FuseSpeedStep = .35f, FuseHeavySlow = .22f, FuseFloatSlow = .45f;
inline constexpr float FuseLife = 2.6f, FuseFloatLife = 1.6f, FuseBounceLife = .8f, FuseReturnLife = 1.2f;
inline constexpr float FuseRadius = .32f, FuseHeavyGrow = .4f, FuseSpinGrow = .45f;
// 撃ち出す高さ (撃つ所の地面から。unit の何倍。地面の積み木 1 段の真ん中あたり)・山なりの弾の一番高い所 (撃ち出した高さから。
// 速い弾ほど高く上がって、間の積み木の上を越えないように)・ふわふわの弾の浮く高さ (宙を飛ぶ弾の高さに足す) と揺れ
// 戻る弾が戻り始める距離 (体から。unit の何倍。遅い弾でも、ここまでは行く。届かないうちに残りの 7 割の時間が過ぎたら戻る)
inline constexpr float FuseReturnReach = 13;
inline constexpr float FuseLaunchHeight = .55f, FuseArcApex = .85f, FuseFloatHeight = .35f, FuseFloatWave = .2f;
inline constexpr float FuseDamageHeavy = .6f;
inline constexpr int FusePierceHits = 3, FuseBounces = 3, FuseChainHops = 3, FuseSplitPieces = 3;
// 追尾の曲がる速さ (ラジアン/秒、レベルごと)・爆発の半径 (unit)・吸い寄せの半径 (unit)・重力 (unit/秒^2)
inline constexpr float FuseHomingTurn = 3.f, FuseExplodeReach = 1.6f, FuseExplodeStep = 1.1f, FuseMagnetReach = 3.5f,
                       FuseGravity = 9;
// 揺らす: 当たった積み木のまわりの届く距離 (unit。レベルが上がるごとに FuseShakeStep 広がる) と、まわりへの当たり (弾の当たりに対する割合)。
// ひび: まわりの積み木にも入れる距離 (unit)
inline constexpr float FuseShakeReach = 1.4f, FuseShakeStep = .6f, FuseShakeShare = .45f, FuseCrackReach = 1.3f;
// 分かれた弾の大きさ・場に出せる弾の数の上限 (重くならないように)
inline constexpr float FuseSplitScale = .6f;
inline constexpr int FuseShotLimit = 220;

// ---- 撃ち方 (性質の組み合わせで決まる) ----
// Single 1 発を前へ・Fan 扇に広げる・Burst 同じ向きへ続けて撃つ・Ring 体のまわり全部へ・Orbit 体のまわりを回り続ける・
// Mortar 山なりに遠くへ落とす・Rain 近くの物の上から降らせる・Lance まっすぐ速く突き抜ける・Mine 前の床に置いて近くの物ごと壊す・
// Twin 左右から弧を描いて挟む
enum class FusePattern : uint8_t { Single, Fan, Burst, Ring, Orbit, Mortar, Rain, Lance, Mine, Twin, Count };
// 上から順に、当てはまった撃ち方になる (組み合わせの撃ち方を先に、1 つの性質で決まる撃ち方を後に)
inline FusePattern FusePatternOf(const FuseLevels& lv) {
	auto L = [&](FuseTrait t) { return lv[size_t(t)]; };
	using T = FuseTrait;
	using P = FusePattern;
	if (L(T::Spin) && (L(T::Return) || L(T::Float) || L(T::Magnet) || L(T::Spin) >= 2))
		return P::Orbit;
	if (L(T::Float) && (L(T::Homing) || L(T::Split) || L(T::Float) >= 2))
		return P::Rain;
	if (L(T::Explode) && (L(T::Heavy) || L(T::Explode) >= 2))
		return P::Mortar;
	if (L(T::Wall) || (L(T::Stun) && (L(T::Explode) || L(T::Magnet) || L(T::Paint))))
		return P::Mine;
	if (L(T::Return))
		return P::Twin;
	if (L(T::Pierce) >= 2 || (L(T::Pierce) && L(T::Speed)))
		return P::Lance;
	if ((L(T::Bounce) && (L(T::Split) || L(T::Explode))) || L(T::Bounce) >= 2 || (L(T::Spin) && L(T::Burn)))
		return P::Ring;
	if (L(T::Speed) && (L(T::Chain) || L(T::Homing) || L(T::Speed) >= 2))
		return P::Burst;
	if (L(T::Split))
		return P::Fan;
	return P::Single;
}
inline const char* FusePatternName(FusePattern p) {
	static constexpr const char* names[] = {"一発", "扇撃ち",   "連なり撃ち", "全方位", "衛星",
	                                        "大砲", "降り注ぎ", "槍",         "置き罠", "挟み撃ち"};
	return names[std::min(int(p), int(FusePattern::Count) - 1)];
}
inline const char* FusePatternPhrase(FusePattern p) {
	static constexpr const char* phrases[] = {
	    "前へ 1 発ずつ投げる",      "扇に広げて撃つ",         "同じ向きへ続けて撃つ",
	    "体のまわり全部へ撃つ",     "体のまわりを回り続ける", "山なりに遠くへ落とす",
	    "近くの物の上から降らせる", "まっすぐ速く突き抜ける", "前の床に置き、近くの物ごと壊す",
	    "左右から弧を描いて挟む"};
	return phrases[std::min(int(p), int(FusePattern::Count) - 1)];
}
// 撃ち方ごとの 1 回の数 (強化の弾数は、この上に足す)・間隔の倍率・同時に場に出せる数 (衛星と罠)
inline int FusePatternBase(FusePattern p) {
	static constexpr int base[] = {1, 3, 3, 6, 3, 1, 3, 1, 1, 2};
	return base[std::min(int(p), int(FusePattern::Count) - 1)];
}
inline float FusePatternInterval(FusePattern p) {
	// (衛星と置き罠は連打で出すので、押す速さが間隔になる。待ちは短め: 長いと連打が空振りする)
	static constexpr float k[] = {1, 1.1f, 1.4f, 1.6f, 1.f, 1.7f, 1.5f, .8f, .8f, 1.2f};
	return k[std::min(int(p), int(FusePattern::Count) - 1)];
}
inline constexpr int FuseOrbitLimit = 10, FuseMineLimit = 8;
// 連打で出す撃ち方 (衛星・置き罠): 場に残る物を出すので、押し続けると出し続けて上限で入れ替わり続ける。押した瞬間だけ出す
// (待ちの間に押した分は FuseTapBuffer 秒だけ覚えて、待ちが明けたら出す。連打が空振りしないように)
inline bool FusePatternTap(FusePattern p) {
	return p == FusePattern::Orbit || p == FusePattern::Mine;
}
inline constexpr float FuseTapBuffer = .45f;
// 連なり撃ちの 1 発ごとの間 (秒)・衛星の回る速さ (ラジアン/秒) と半径 (体の半径に対する倍率)・衛星の残る秒・
// 大砲の飛ぶ秒と届く距離 (unit)・降り注ぎの高さ (unit)・挟み撃ちの曲がる速さ (ラジアン/秒)・罠が効く距離 (unit) と残る秒
inline constexpr float FuseBurstGap = .07f, FuseOrbitTurn = 4.2f, FuseOrbitRadius = 2.2f, FuseOrbitLife = 4.5f,
                       FuseMortarTime = .95f, FuseMortarReach = 11, FuseRainHeight = 9, FuseTwinCurve = 1.9f,
                       FuseMineReach = 1.8f, FuseMineLife = 9;
// 狙いの補正 (追尾の体の特徴): 前のこの角度 (ラジアン) の中の、一番近い相手へ向けて撃つ
inline constexpr float FuseAimCone = .6f;

// ---- 体の特徴 (性質でスライム自身も変わる) ----
// 動く速さの倍率 (速い・転がるで速く、重いで少し遅く)
inline float FuseMoveScale(const FuseLevels& lv) {
	auto L = [&](FuseTrait t) { return float(lv[size_t(t)]); };
	return std::clamp(
	    (1 + .2f * L(FuseTrait::Speed) + .1f * L(FuseTrait::Roll) + .06f * L(FuseTrait::Float)) /
	        (1 + .1f * L(FuseTrait::Heavy)),
	    .7f, 1.9f);
}
// 体当たり: 回る・重い・熱い・揺らす があると、触れた積み木に当たる (間隔の秒・強さは投げる弾に対する倍率)
inline constexpr float FuseBodyHitEvery = .3f, FuseBodyHitDamage = .35f;
inline bool FuseBodyHits(const FuseLevels& lv) {
	return lv[size_t(FuseTrait::Spin)] || lv[size_t(FuseTrait::Heavy)] || lv[size_t(FuseTrait::Burn)] ||
	       lv[size_t(FuseTrait::Stun)];
}
// 体の特徴の説明 (画面の説明に続けて並べる)
inline std::vector<std::string> FuseBodyPhrases(const FuseLevels& lv) {
	auto L = [&](FuseTrait t) { return lv[size_t(t)]; };
	std::vector<std::string> phrases;
	const float move = FuseMoveScale(lv);
	if (move > 1.01f)
		phrases.push_back("体が " + std::to_string(int(std::lround((move - 1) * 100))) + "% 速く動く");
	else if (move < .99f)
		phrases.push_back("体が少し重い");
	if (FuseBodyHits(lv))
		phrases.push_back(L(FuseTrait::Spin) ? "体が回って触れた物を壊す" : "体当たりで触れた物をはじく");
	if (L(FuseTrait::Homing))
		phrases.push_back("近くの物へ狙いを合わせる");
	if (L(FuseTrait::Paint))
		phrases.push_back("歩いた跡も塗る");
	return phrases;
}

// 武器の動きの説明。画面側は、返された句を幅に合わせて並べる。
inline std::vector<std::string> FuseWeaponDescriptionPhrases(const FuseLevels& levels) {
	auto level = [&](FuseTrait trait) { return levels[size_t(trait)]; };
	std::vector<std::string> phrases;
	const FusePattern pattern = FusePatternOf(levels);
	if (pattern != FusePattern::Single) {
		phrases.push_back(
		    std::string("【") + FusePatternName(pattern) + (FusePatternTap(pattern) ? "・連打" : "") + "】" +
		    FusePatternPhrase(pattern));
	} else if (level(FuseTrait::Roll)) {
		phrases.push_back("床を転がり");
	} else if (level(FuseTrait::Homing) || level(FuseTrait::Float)) {
		phrases.push_back("宙を飛び");
	} else {
		phrases.push_back("放物線で飛び");
	}

	auto append = [&](FuseTrait trait, const std::string& phrase) {
		if (level(trait)) {
			phrases.push_back(phrase);
		}
	};
	// (チャレンジに相手はいない: 積み木に何が起きるかと、弾がどう動くかだけを書く)
	append(FuseTrait::Homing, "近くの物を追いかけ");
	append(FuseTrait::Speed, "とても速く");
	append(FuseTrait::Heavy, "大きく重く");
	append(FuseTrait::Pierce, std::to_string(FusePierceHits * level(FuseTrait::Pierce) + 1) + "個まで突き抜け");
	append(FuseTrait::Bounce, std::to_string(FuseBounces * level(FuseTrait::Bounce)) + "回跳ね");
	append(FuseTrait::Chain, "次の物へ跳び移り");
	append(FuseTrait::Spin, "回ってまとめて壊し");
	append(FuseTrait::Magnet, "まわりの物を吸い寄せ");
	append(FuseTrait::Return, "戻ってきて");
	append(FuseTrait::Paint, "通った跡を塗り");
	append(FuseTrait::Split, "当たると分かれ");
	append(FuseTrait::Stun, "当たった所のまわりを揺さぶって崩し");
	append(FuseTrait::Weaken, "当てた所にひびを入れて次の当たりを 2 倍にし");
	append(FuseTrait::Burn, "当てた積み木を焦がして崩し");
	append(FuseTrait::Explode, "爆発する");
	append(FuseTrait::Wall, "止まった所に積み木の壁を建てる");
	for (auto& p : FuseBodyPhrases(levels))
		phrases.push_back(p);
	return phrases;
}

// ---- 材料の点と強化 ----
// 材料の点: 物の大きさ (半径。unit に対する割合) で 1〜3 点
inline constexpr float CraftValueMid = .45f, CraftValueBig = .9f;
inline int CraftValue(float radius, float unit) {
	const float r = radius / std::max(.01f, unit);
	return r < CraftValueMid ? 1 : r < CraftValueBig ? 2 : 3;
}
// 強化の種類: 弾数 (1 回で撃つ数)・威力 (弾の大きさ・当たり・塗る量)・連射 (撃つ間隔)
enum class CraftBoost : uint8_t { Shots, Power, Rate, Count };
inline constexpr int CraftBoostCount = int(CraftBoost::Count);
inline const char* CraftBoostName(int boost) {
	static constexpr const char* names[] = {"弾数", "威力", "連射"};
	return names[std::clamp(boost, 0, CraftBoostCount - 1)];
}
// 強化の上限: 最大になる点 (弾数・威力・連射)。ステージごとに決める (devour_stages.json の craftMax)。
// そのステージの物をほぼ全部 (約 9 割) 集めて、3 つに分けて入れたときに 3 つとも最大になるようにする
// (部屋: 全部で 718 点 → 各 215 点。坂の町: 全部で 2806 点 → 各 840 点。数え方は --devour-battle-all --craft-census)
using CraftCaps = std::array<int, CraftBoostCount>;
inline constexpr CraftCaps CraftDefaultCaps{215, 215, 215};
// 強化のレベル: 集めた物を変えた cm (大きさ。1 点 = 1cm) を入れて、Lv0 から CraftLevels まで上げる。能力が上がるのはレベルが上がったときだけ
// (途中の cm は次のレベルへの貯め)。レベル n までに要る cm の合計は cap * n(n+1) / (L(L+1)):
// 始めのレベルは安く (部屋の Lv1 は 4)、後のレベルほど 1 つ上げるのに多く要る (部屋の Lv10 は 39)。最後のレベルでちょうど cap
inline constexpr int CraftLevels = 10;
inline int CraftLevelNeed(int level, int cap) {
	const int n = std::clamp(level, 0, CraftLevels);
	return int(std::lround(float(cap) * float(n * (n + 1)) / float(CraftLevels * (CraftLevels + 1))));
}
inline int CraftLevelOf(int points, int cap) {
	int level = 0;
	while (level < CraftLevels && points >= CraftLevelNeed(level + 1, cap))
		++level;
	return level;
}
// 強化の伸び: レベルの割合で、最大までまっすぐ伸びる。弾数 1→40 発・威力 1→8 倍・連射 (1 秒の回数) 約 1.8→25 回
inline constexpr int CraftShotsMax = 40;
inline constexpr float CraftPowerMax = 8, CraftIntervalBase = .55f, CraftIntervalMin = .04f;
inline float CraftShare(int points, int cap) {
	return float(CraftLevelOf(points, cap)) / float(CraftLevels);
}
inline bool CraftBoostFull(int points, int cap) {
	return CraftLevelOf(points, cap) >= CraftLevels;
}
inline int CraftShots(int points, int cap) {
	return 1 + int(float(CraftShotsMax - 1) * CraftShare(points, cap) + 1e-4f);
}
// 1 回で撃つ数 (強化の弾数の点から)。強化していなければ撃ち方の数のまま
inline int FuseShotCount(FusePattern p, int shotPoints, int cap) {
	return FusePatternBase(p) + CraftShots(shotPoints, cap) - 1;
}
inline float CraftPower(int points, int cap) {
	return 1 + (CraftPowerMax - 1) * CraftShare(points, cap);
}
// 連射: 1 秒に撃つ回数を割合でまっすぐ伸ばす (重い物は始めが遅いが、最大では同じ)
inline float CraftInterval(int points, int heavy, int cap) {
	const float slow = 1 / (CraftIntervalBase * (1 + .25f * float(heavy))), fast = 1 / CraftIntervalMin;
	return 1 / (slow + (fast - slow) * CraftShare(points, cap));
}
// 何発も撃つときの扇の広がり (ラジアン、1 発ごと・最大)
inline constexpr float CraftSpreadStep = .11f, CraftSpreadMax = 1.4f;
// 体が武器の形でいる秒 (撃つたびに延びる)
inline constexpr float CraftFormTime = .5f;
// (F5) 形が出てくる (ぽんと大きくなる) 秒・1 発の反動 (前後に縮む割合)
// (CraftFlap は翼のはばたきの角度 (ラジアン)。後ろからのカメラでも見えるよう大きめ)
inline constexpr float CraftPopTime = .28f, CraftKick = .14f, CraftFormHold = .4f, CraftFlap = .55f;

// ---- 形の組み合わせ方 (形の座標。先が +z、下が y=0) ----
// 本体はモデルの一番長い辺を CraftBaseSize にそろえる。材料の部品は本体に対する大きさ
//  2 つ目: 本体の前 (+z) の上の方に、少し重ねて付ける
//  3 つ目: 本体の左右に 1 つずつ (左は鏡に映す)、高さの真ん中に付ける
inline constexpr float CraftBaseSize = 1, CraftFrontScale = .72f, CraftSideScale = .56f, CraftOverlap = .22f,
                       CraftFrontHeight = .55f, CraftSideHeight = .45f;
// (F2) なめらかな基本形で作るとき: 太さの下限 (最後の大きさ。F1 で測った「液体の表面で消える太さ」1.6 より少し太く)・
// つなぎ目の丸み (本体の一番長い辺を 1 とした長さ)・前の部品を本体の高さの何割上げるか・左右の部品を本体の長さの何割後ろへずらすか
inline constexpr float CraftMinThick = 1.9f, CraftBlend = .06f, CraftFrontLift = .25f, CraftSideBack = .15f;
// 組み合わせた形の体積 (粒子の数は決まっているので、どの形もこの体積にそろえる)・形を作る箱の細かさ (一番長い辺を何個に分けるか)
// (体積は液体の体 (半径 2.55・1.8・2.55 のだ円体 = 約 49) に合わせる。小さいと、液体の体より小ぢんまりした形になる)
// (CraftShapeLongest は一番長い辺の上限。翼や履帯を大きく張り出した形も、体積をそろえた大きさまで出せるように)
inline constexpr float CraftShapeVolume = 48, CraftShapeLongest = 12;
inline constexpr int CraftItemCells = 12, CraftShapeCells = 44;
} // namespace Game::Chrono
