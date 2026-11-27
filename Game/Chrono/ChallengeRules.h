// ChallengeRules.h
// 変形チャレンジ (吸収のあとの第 2 フェーズ) の規則。エンジンに依存しない。数値と文章は Resources/Data/challenges.json
//  - 成長 (吸収) とチャレンジで、ステージの制限時間 (devour_stages.json の time) を分け合う
//  - チャレンジはステージを始める前にランダムで 1 つ決まり、成長の間も HUD に出る
//  - 好きなときに長押しでチャレンジへ移れる。残り ChallengeAutoAt 秒になったら自動で移る
//  - 物を使っても体は縮まない。大きさはチャレンジの力 (威力・硬い物を壊せるか) になる
//  - チャレンジのスコアは自己ベストとして残すだけで、次のステージへはいつでも進める
#pragma once
#include "DevourStageRules.h"
#include "DevourBattleRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
namespace Game::Chrono {
// チャレンジの種類 (challenges.json の id と同じ順)
enum class ChallengeKind : uint8_t { Blocks, Tower, Territory, Count };
inline constexpr int ChallengeKindCount = int(ChallengeKind::Count);
inline const char* ChallengeKindId(ChallengeKind kind) {
	static constexpr const char* ids[] = {"blocks", "tower", "territory"};
	return ids[std::min(int(kind), ChallengeKindCount - 1)];
}
// 画面に出す文章: 名前・ひとこと・遊び方・おすすめの能力 (collection.json の action の名前)
struct ChallengeInfo {
	ChallengeKind kind = ChallengeKind::Blocks;
	std::string id, name, tagline, goal;
	std::vector<std::string> recommend;
};
inline const std::vector<ChallengeInfo>& Challenges() {
	static const std::vector<ChallengeInfo> list = [] {
		const char* file = "challenges.json";
		const auto& json = Data::Load(file);
		std::vector<ChallengeInfo> out(static_cast<size_t>(ChallengeKindCount));
		std::vector<bool> seen(static_cast<size_t>(ChallengeKindCount), false);
		for (const auto& c : Data::Require(json, "challenges", file)) {
			ChallengeInfo info;
			info.id = c.at("id").get<std::string>();
			int index = -1;
			for (int i = 0; i < ChallengeKindCount; ++i)
				if (info.id == ChallengeKindId(ChallengeKind(i)))
					index = i;
			if (index < 0)
				throw std::runtime_error(std::string(file) + ": unknown challenge " + info.id);
			info.kind = ChallengeKind(index);
			info.name = c.at("name").get<std::string>();
			info.tagline = c.value("tagline", std::string());
			info.goal = c.at("goal").get<std::string>();
			info.recommend = c.value("recommend", std::vector<std::string>{});
			out[size_t(index)] = std::move(info);
			seen[size_t(index)] = true;
		}
		for (int i = 0; i < ChallengeKindCount; ++i)
			if (!seen[size_t(i)])
				throw std::runtime_error(
				    std::string(file) + ": missing challenge " + ChallengeKindId(ChallengeKind(i)));
		return out;
	}();
	return list;
}
inline const ChallengeInfo& ChallengeOf(ChallengeKind kind) {
	return Challenges()[size_t(std::min(int(kind), ChallengeKindCount - 1))];
}
// id からチャレンジ (知らない id なら -1)
inline int ChallengeIndexOf(const std::string& id) {
	for (int i = 0; i < ChallengeKindCount; ++i)
		if (id == ChallengeKindId(ChallengeKind(i)))
			return i;
	return -1;
}

// ---- 時間 ----
// 残りがこの秒数になったら自動でチャレンジへ移る (最低限チャレンジを遊べるように)
inline constexpr float ChallengeAutoAt = 30;
// 自分で移るときの長押しの秒 (うっかり押して移らないように)
inline constexpr float ChallengeMoveHold = .8f;
// チャレンジの時間: 成長の残り時間 (自動で移ったときは ChallengeAutoAt 秒)。少なすぎると遊べないので下限を付ける
inline constexpr float ChallengeMinTime = 10;
inline float ChallengeTimeFor(float growthLeft) {
	return std::max(ChallengeMinTime, growthLeft);
}
// 始まりの演出の秒
inline constexpr float ChallengeIntro = 2.5f;
// 体の中心の床からの高さ (unit。DevourBattle の歩きと同じ)
inline constexpr float ChallengeBodyGround = 1.25f;

// ---- スコア ----
// 続けて得点するとコンボ: この秒数のうちに次の得点が入れば続く。倍率は 1 回ごとに +0.1 で、上限 2 倍
inline constexpr float ChallengeComboWindow = 3, ChallengeComboStep = .1f, ChallengeComboMax = 2;
inline float ChallengeComboScale(int combo) {
	return std::min(ChallengeComboMax, 1 + ChallengeComboStep * float(std::max(0, combo - 1)));
}
// 自己ベストを覚えるキー (ステージとチャレンジの組ごと)
inline std::string ChallengeBestKey(const std::string& stage, ChallengeKind kind) {
	return stage + "/" + ChallengeKindId(kind);
}

// できあがっているチャレンジ (ランダムに選ぶのはここに入っているものだけ。--challenge=id ならどれでも選べる)
// (タワーブレイカーは積み木クラッシャーと似ているので作らない。スライム陣取りは作っている途中なので、できるまでランダムには出さない)
inline bool ChallengeReady(ChallengeKind kind) {
	return kind == ChallengeKind::Blocks;
}

// ---- どのチャレンジでも同じ、自分に効く能力 ----
// 回復 (ハート): 残り時間が増える・急須: 少し増える・リモコン: チャレンジの時計を止める (BattlePauseTime 秒)
// 守る・だるま: 体が重く硬くなり、触れた物に当たる (ChallengeHeavyDamage。同じ物には ChallengeHeavyRehit 秒ごと)
// ティッシュ: ふわふわの間はコンボが切れにくい (続けて得点できる秒が ChallengeFluffCombo 倍)
// (時間が増えるのは 1 回のチャレンジで合わせて ChallengeTimeBonusMax 秒まで。変形武器で回復を何度も使っても延びすぎないように)
inline constexpr float ChallengeTimeBonusMax = 12;
inline constexpr float ChallengeHealBonus = 3, ChallengeBrewBonus = 2, ChallengeHeavyDamage = 14,
                       ChallengeHeavyRehit = .5f, ChallengeFluffCombo = 2;
// 回復 (アメ・スイカなど): 時間に加えて「おやつパワー」。ChallengeSnackTime 秒のあいだ、ほかの能力の威力が ChallengeSnackPower 倍
inline constexpr float ChallengeSnackTime = 6, ChallengeSnackPower = 1.5f;
// 投げる (ハト・空き缶など): 投げた物の大きさ (BattleItemScale) で当たる数が増え、床に落ちても転がって当たり続ける
inline int ChallengeThrowHits(float itemScale) {
	return std::clamp(2 + int(std::round(itemScale * 1.5f)), 3, 5);
}
inline constexpr float ChallengeThrowRoll = 1.6f, ChallengeThrowSlow = .75f;
// 守る (バケツ・段ボールなど): 盾を構えて前へ短く突進する「盾タックル」(ChallengeTackleTime 秒)。触れた物に強く当てて押し出す
inline constexpr float ChallengeTackleTime = .45f, ChallengeTackleDamage = 22, ChallengeTackleReach = 1.3f;
// 積み木の塔: 前に物があればその手前に建て、物のある側へ倒れる
inline constexpr float ChallengeTowerGap = .4f;
// まんが: 重い本を広げて前へ落とし、範囲の中の物に当てる (止めるのではなく、押しつぶす)
inline constexpr float ChallengeBookDamage = 18;

// ---- 積み木クラッシャー ----
// 積み木は 3 種類: 普通 (小さくても壊せる)・硬い (当たりから armor を引いて削る。弱い当たりは効かない)・金色 (高得点。まわりを囲まれている)
// 大きさの基準 unit (チャレンジを始めたときの体の半径) で 1 辺 ChallengeBlockSize の立方体。体が大きいほど威力 (BattlePower) が上がり、硬い積み木を壊しやすい
enum class BlockType : uint8_t { Normal, Hard, Gold };
inline constexpr int ChallengeBlockScore[] = {10, 30, 100};
inline constexpr float ChallengeBlockHp[] = {10, 40, 20}, ChallengeBlockArmor[] = {0, 12, 6};
inline constexpr float ChallengeBlockSize = 1;
// 衝撃の相手としての番号 (この番号から積み木の並びの順)
inline constexpr uint32_t ChallengeBlockIdBase = 0x40000000u;
// ひび (消しゴム・まんが): 受ける当たりが ChallengeFragileTake 倍で、硬さを無視する。色ぬり: 塗った積み木は得点が ChallengePaintScore 倍
// においの雲: 中の積み木を 1 秒に ChallengeSmellDps ずつ削る (硬さは引く)。吸い寄せた積み木は、引き寄せる中心から ChallengeSuckReach (積み木の大きさの倍率) で回収して得点
inline constexpr float ChallengeFragileTake = 2, ChallengePaintScore = 1.5f, ChallengeSmellDps = 14,
                       ChallengeSuckReach = 1.6f;
// 配置: 体の位置 (戦う円の真ん中) から前 (+z) に並べる。pattern は 0 長い壁・1 円形・2 高い塔・3 硬い密集地
// x・z は unit を単位にした位置、level は下から何段目
struct BlockSpot {
	float x = 0, z = 0;
	int level = 0;
	BlockType type = BlockType::Normal;
};
inline constexpr int ChallengeBlockPatterns = 4;
inline const char* ChallengeBlockPatternName(int pattern) {
	static constexpr const char* names[] = {"長い壁", "円形", "高い塔", "硬い密集地"};
	return names[std::clamp(pattern, 0, ChallengeBlockPatterns - 1)];
}
inline std::vector<BlockSpot> ChallengeBlockLayout(int pattern) {
	std::vector<BlockSpot> out;
	const float s = ChallengeBlockSize;
	auto add = [&](float x, float z, int level, BlockType type) { out.push_back({x, z, level, type}); };
	switch (std::clamp(pattern, 0, ChallengeBlockPatterns - 1)) {
	case 0: {
		// 長い壁: 前に 3 枚。真ん中の壁の中央は硬く、その奥に金色を埋める。奥の壁の両端の上にも金色
		for (int wall = 0; wall < 3; ++wall) {
			const float z = 8 + 8 * float(wall);
			for (int i = 0; i < 20; ++i)
				for (int level = 0; level < 3; ++level) {
					const float x = (float(i) - 9.5f) * s;
					BlockType type = BlockType::Normal;
					if (wall == 1 && std::abs(x) < 3.5f)
						type = BlockType::Hard;
					if (wall == 1 && level == 1 && std::abs(x) < .6f)
						type = BlockType::Gold;
					if (wall == 2 && level == 2 && (i == 2 || i == 17))
						type = BlockType::Gold;
					add(x, z, level, type);
				}
		}
		break;
	}
	case 1: {
		// 円形: 外の輪 (普通・2 段) と内の輪 (硬い・3 段)。真ん中に金色の柱
		const float cx = 0, cz = 18;
		for (int i = 0; i < 48; ++i) {
			const float a = 6.2831853f * float(i) / 48;
			for (int level = 0; level < 2; ++level)
				add(cx + std::cos(a) * 10, cz + std::sin(a) * 10, level, BlockType::Normal);
		}
		for (int i = 0; i < 24; ++i) {
			const float a = 6.2831853f * (float(i) + .5f) / 24;
			for (int level = 0; level < 3; ++level)
				add(cx + std::cos(a) * 4.5f, cz + std::sin(a) * 4.5f, level, BlockType::Hard);
		}
		for (int level = 0; level < 3; ++level)
			add(cx, cz, level, BlockType::Gold);
		break;
	}
	case 2: {
		// 高い塔: 3x3 の塔を 6 本。芯は硬く、2 本のてっぺんの真ん中が金色
		const float towers[][2] = {{-12, 10}, {0, 13}, {12, 10}, {-8, 24}, {8, 24}, {0, 32}};
		for (int t = 0; t < 6; ++t)
			for (int i = -1; i <= 1; ++i)
				for (int j = -1; j <= 1; ++j)
					for (int level = 0; level < 5; ++level) {
						BlockType type = (i == 0 && j == 0) ? BlockType::Hard : BlockType::Normal;
						if (i == 0 && j == 0 && level == 4 && (t == 1 || t == 5))
							type = BlockType::Gold;
						add(towers[t][0] + float(i) * s, towers[t][1] + float(j) * s, level, type);
					}
		break;
	}
	default: {
		// 硬い密集地: 6x6 の硬い固まり (2 段) を普通の積み木の囲い (8x8 の縁・2 段) が守る。固まりの真ん中の下の段に金色が 4 つ
		const float cz = 18;
		for (int i = 0; i < 8; ++i)
			for (int j = 0; j < 8; ++j) {
				const bool edge = i == 0 || j == 0 || i == 7 || j == 7;
				for (int level = 0; level < 2; ++level) {
					BlockType type = edge ? BlockType::Normal : BlockType::Hard;
					if (!edge && level == 0 && (i == 3 || i == 4) && (j == 3 || j == 4))
						type = BlockType::Gold;
					add((float(i) - 3.5f) * s, cz + (float(j) - 3.5f) * s, level, type);
				}
			}
		// 左右に低い壁も置く (小さくても稼げる場所)
		for (int side = -1; side <= 1; side += 2)
			for (int i = 0; i < 10; ++i)
				add(float(side) * 14, 8 + float(i) * 2, 0, BlockType::Normal);
		break;
	}
	}
	return out;
}

// ---- スライム陣取り ----
// 専用ステージ「ひろい子ども部屋」: 戦う場所の床の上に、unit (チャレンジを始めたときの体の半径) を単位にした ChallengeTerritoryCells 四方のラグを敷き、1 x 1 のマスに分ける (+z が奥)。
// マスは床・陣地・家具 (通れず、陣地に数えない) のどれか。スライムの体液が能力の当たりの形で床に広がって陣地になる。歩いた跡も細く残る
//  (体液を使っても体は縮まない。体が大きいほど 1 回で広がる量が増える)
enum class TerritoryCell : uint8_t { Floor, Owned, Furniture };
inline constexpr int ChallengeTerritoryCells = 80;
inline constexpr float ChallengeTerritoryHalf = ChallengeTerritoryCells * .5f;
// 始める位置 (手前の真ん中)・歩いた跡の半径 (体の半径の倍率)
inline constexpr float ChallengeTerritoryStartZ = -32, ChallengeTerritoryTrail = .45f;
// 家具: モデル・ラグの真ん中からの位置 (unit)・一番長い辺の長さ (unit)・向き (ラジアン)。家具の下のマスは陣地に数えない
struct TerritoryFurniture {
	const char* model;
	float x, z, width, yaw;
};
inline constexpr TerritoryFurniture ChallengeTerritoryFurniture[] = {
    {"bed", -29.3f, 26.7f, 11.5f, 0},
    {"desk", 28.7f, 34.6f, 8, 3.1416f},
    {"chair", 28.7f, 29.9f, 3, 0},
    {"kotatsu", 0, 5.3f, 8, 0},
    {"toybox", -35, -8, 4, 1.5708f},
    {"bookshelf", 36.6f, -2.7f, 9, -1.5708f},
    {"storage_case", -16.7f, -36, 4.6f, 0},
    {"toybox", 17.3f, -21.3f, 4, .35f},
    {"bookshelf", -8, 36.7f, 7, 3.1416f},
    {"storage_case", 30.7f, -29.3f, 4.6f, .6f},
    {"chair", -12, 12, 3, 2.2f},
    {"toybox", -24, -20, 3.6f, -.4f},
    {"storage_case", 10, 22, 4, 1.2f}};
// 部屋の壁 (奥と左右。手前はカメラがあるので作らない) の高さとラグの縁からの距離 (unit)
inline constexpr float ChallengeTerritoryWall = 5, ChallengeTerritoryWallGap = 1.5f;
// ---- 陣地を取り返すもの (倒せない。能力で一瞬じゃまできるだけ) ----
// お掃除ロボ: ラグの上を走り、通った所の体液をふき取る (半径 RobotWipe)。RobotTurnEvery 秒ごとに、前方 RobotLook (unit) までの体液の多い向きを選んで曲がる
//  当てる・押し出す: はじかれて向きが変わる (RobotPush unit/秒)・引き寄せる: そちらへ向かう・止める/閉じ込める: 止まる (秒)・弱らせる/やけど/置いた物: しばらく遅い (RobotSlow 倍)
inline constexpr float RobotSpeed = 5, RobotRadius = 1.3f, RobotWipe = 1.3f, RobotTurnEvery = 1.6f, RobotLook = 8,
                       RobotPush = 7, RobotStun = 2.2f, RobotSlow = .4f, RobotSlowTime = 3;
inline constexpr float RobotStartX = -30, RobotStartZ = -30;
// 首振り扇風機: 置いた所で首を左右 FanSweep (ラジアン) に FanPeriod 秒で振り、FanEvery 秒ごとに前の扇 (半分の角度の cos が FanCone、長さ FanReach) の体液を FanBlowTime 秒で吹き飛ばす
//  止める: 首振りと送風がしばらく止まる・当てる: 首の向きがずれる (FanNudge)・積み木の塔が前にあると、その 1 回の風を受け止める
inline constexpr float FanX = 14, FanZ = -8, FanSweep = 1.05f, FanPeriod = 7, FanEvery = 4, FanCone = .5f,
                       FanReach = 11, FanBlowTime = .7f, FanStun = 2.2f, FanNudge = .6f;
inline constexpr uint32_t ChallengeRobotId = 0x70000000u, ChallengeFanId = 0x70000001u;
// 体液の量: 能力を 1 回使うと、当たりの形の中の床を当たりの中心に近い順にぬる。ぬれる数は ChallengeTerritoryUseMax マスまで。
// 使い終わって (飛んだ物・置いた物も終わって) ぬった数が ChallengeTerritoryUseMin に足りなければ、最後にぬった所のまわりに足す
// (どの能力でも 1 回の量がそろう。形だけが能力ごとに違う)。体が大きいほど量が増える: 威力の平方根 (ChallengeTerritoryScaleLow〜High 倍)
inline constexpr int ChallengeTerritoryUseMin = 25, ChallengeTerritoryUseMax = 65, ChallengeTerritoryCandyMax = 4;
inline constexpr float ChallengeTerritoryScaleLow = .6f, ChallengeTerritoryScaleHigh = 1.6f;
inline float ChallengeTerritoryScale(float power) {
	return std::clamp(std::sqrt(std::max(0.f, power)), ChallengeTerritoryScaleLow, ChallengeTerritoryScaleHigh);
}
// 飛んだ物が落ちた・消えた所のしぶきの半径 (unit。飴玉は小さい)・熱いお茶が外へ広がる最後の半径 (お茶の届く長さの倍率)
// ぬる形は当たりの形より少し広い (当たりの長さ・太さを ChallengeTerritoryGrow 倍して ChallengeTerritoryPad (unit) 足す。小さな当たりでも形が床に見えるように)
inline constexpr float ChallengeTerritoryGrow = 1.5f, ChallengeTerritoryPad = .8f;
// 見た目と陣地の広さ (インクマップ): ラグ全体を覆う ChallengeTerritoryInk 四方の量の地図 (1 unit に 6 点)。
// 陣地になったマスごとに、しぶき (波打つ縁の丸。向きと大きさは毎回ばらばら) を押し、まわりに小さなしずくを飛ばす。量が半分以上の点が陣地 (割合はこの点の数で数える)
inline constexpr int ChallengeTerritoryInk = ChallengeTerritoryCells * 6;
// しぶきは押した瞬間に完成させず、小さく出て ChallengeTerritoryGrowLow〜High 秒で形まで広がる (始めは速く、だんだん遅く)。
// ぬれたての度合いは ChallengeTerritoryWetTime 秒で消える (その間は縁が揺れ、強く光る)
// 体液は流体の粒子 (体と同じ GPU の流体。決まった放物線で飛ぶ種類) のしずくで飛ばし、落ちた時刻と場所でしぶきを押す。
// 飛ぶ秒は ChallengeTerritoryFlightLow + 距離 / ChallengeTerritoryFlightSpeed (unit/秒) で、High まで。出す所からの距離が ChallengeTerritoryFlightMin (unit) より近ければ飛ばさずにその場で広げる。
// しずく 1 つは ChallengeTerritoryDropParticles 粒。重力は流体の計算と同じ 18 (位置は v t - 9 t^2)
inline constexpr float ChallengeTerritoryFlightLow = .1f, ChallengeTerritoryFlightHigh = .6f,
                       ChallengeTerritoryFlightSpeed = 16, ChallengeTerritoryFlightMin = 1.2f,
                       ChallengeTerritoryFlightGravity = 9;
inline constexpr int ChallengeTerritoryDropParticles = 3;
// しずくの大きさの下限。流体は、体と飛んでいるしずくを全部囲む範囲を、同じ数 (横 InkVolumeColumns) の升で描く。
// しずくが広く散るほど升が粗くなり、升 1 つほどの小さなしずくは面にならず消える。
// そこで、飛ばすときの散らばり (体・飛んでいるしずく・これから飛ぶしずくを囲む広さ) から升の大きさを見積もり、升 InkDropCells 個ぶん以上の大きさにする
// (近くに散るときは小さく、部屋の端まで飛ぶときは大きくなる)
inline constexpr float InkVolumeColumns = 168, InkDropCells = 1.3f;
// しずくの大きさは、体の半径の InkDropBody 倍より小さくしない (升より大きくても、体に比べて小さすぎると見えない)
inline constexpr float InkDropBody = .38f;
// しずくの不ぞろい (規則正しく並んで見えないように): 大きさの倍率の幅・出す所のずれ (unit)・出る時刻のずれ (秒)・飛ぶ秒の倍率の幅・
// 粒子を飛ばさないしずくの割合 (そのマスは近くのしずくから広がったように見える)。大きいしずくほど粒が多い (2〜4)
inline constexpr float InkDropSizeLow = .6f, InkDropSizeHigh = 1.35f, InkDropScatter = .4f, InkDropLag = .09f,
                       InkDropTimeLow = .8f, InkDropTimeHigh = 1.25f, InkDropSkip = .3f;
// 体液の飛ばし方 (道具の形と動きに合わせた 11 の型。スプラトゥーンのブキの種類にならう)。どこから出して、いつ届くかが型ごとに違う
//  Burst 炸裂: 当たった所の上から、まわりへ放物線で飛び散る (ばね・王・桂馬・叩く・まんが・おとり・積み木の塔・ドミノ倒し・閉じ込める)
//  Lay 転がし: 体 (や転がる物) の真下にそのまま広がる (粒子は飛ばさない。轢く・ラジコン・転がる・香車・守る・飛ぶ・ビー玉)
//  Swing 振り回し: 体の縁 (刃の先) から、振った向きの順に飛ぶ (剣・消しゴム)
//  Spray 発射: 前の口から、近い順に吹き出す (吸い込む)
//  Drip 投げ物: 飛んでいる物からしずくが垂れる (投げる・はじく・円盤・はねる・追いかける・扇投げ・突き抜ける・貼り付ける・水鉄砲・飴玉)
//  Line 一直線: 体から線の先へ、体液が走るように順に届く (角・飛車・歩・銀・金)
//  Spin 回転: 体の縁の少し手前の角度から、うず巻きにまく (回る翼・こま)
//  Pool 水たまり: 真ん中から外へ、粒子を飛ばさずにじわっと広がる (色ぬり・熱いお茶)
//  Rain 雨: 上から少しずつ降る (におい)
//  Pulse 噴き出す: 体の表面から、まわりへ輪になって飛ぶ (回復・お茶で回復・ふわふわ・起き上がり・一時停止)
//  Gather 招き寄せる: まわりの外側から、招いた所へ集まる (招き猫)
//  Topple 崩れる: 倒れる物 (積み木の塔・ドミノ) の上の方から、倒れた向きへ降りかかる
enum class InkStyle : uint8_t { Burst, Lay, Swing, Spray, Drip, Line, Spin, Pool, Rain, Pulse, Gather, Topple };
// 投げた物・飛び道具・飴玉・転がるビー玉は、動いている間ずっとしずくを垂らす (平均 InkDripEvery 秒ごと)。
// 垂れたしずくは小さく (しずくの大きさの InkDripSize 倍)、落ちた所に小さな点 (半径 InkDripSpot unit) を残す。能力の体液の量とは別
inline constexpr float InkDripEvery = .09f, InkDripSize = .6f, InkDripSpot = .28f;
inline InkStyle InkStyleOf(BattleAction a) {
	switch (a) {
	case BattleAction::Ride:
	case BattleAction::Rc:
	case BattleAction::Roll:
	case BattleAction::Kyo:
	case BattleAction::Fly:
	case BattleAction::Marbles:
		return InkStyle::Lay;
	case BattleAction::Guard:
		return InkStyle::Spray;
	case BattleAction::Tower:
	case BattleAction::Domino:
		return InkStyle::Topple;
	case BattleAction::Sword:
	case BattleAction::Erase:
		return InkStyle::Swing;
	case BattleAction::Vacuum:
		return InkStyle::Spray;
	case BattleAction::Throw:
	case BattleAction::Coin:
	case BattleAction::Disc:
	case BattleAction::Bounce:
	case BattleAction::Homing:
	case BattleAction::Fan:
	case BattleAction::Pierce:
	case BattleAction::Sticky:
	case BattleAction::Water:
		return InkStyle::Drip;
	case BattleAction::Kaku:
	case BattleAction::Hi:
	case BattleAction::Fu:
	case BattleAction::Gin:
	case BattleAction::Kin:
		return InkStyle::Line;
	case BattleAction::Heli:
	case BattleAction::Spin:
		return InkStyle::Spin;
	case BattleAction::Paint:
	case BattleAction::Tea:
		return InkStyle::Pool;
	case BattleAction::Smell:
		return InkStyle::Rain;
	case BattleAction::Heal:
	case BattleAction::Brew:
	case BattleAction::Fluff:
	case BattleAction::Roly:
	case BattleAction::Pause:
		return InkStyle::Pulse;
	case BattleAction::Beckon:
		return InkStyle::Gather;
	default:
		return InkStyle::Burst;
	}
}
// 能力ごとの体液の量の倍率 (しずくの数と下限にかける)。しずくの重なり方は飛び散り方で違うので、
// 自動チェック (--challenge-matrix-territory) で、まだぬれていない床に 1 回使ったときの広さが 32〜60 マスに入るように合わせた
inline float InkAmountOf(BattleAction a) {
	switch (a) {
	case BattleAction::Throw:
		return 1.5f;
	case BattleAction::Ride:
	case BattleAction::Rc:
	case BattleAction::Fly:
		return 1.2f;
	case BattleAction::Smash:
		return 1.8f;
	case BattleAction::Guard:
		return 1.25f;
	case BattleAction::Water:
	case BattleAction::Kyo:
		return 1.3f;
	case BattleAction::Roll:
	case BattleAction::Marbles:
		return 1.15f;
	case BattleAction::Fu:
		return 1.8f;
	case BattleAction::Tower:
		return 2.3f;
	case BattleAction::Domino:
		return 1.2f;
	case BattleAction::Cage:
		return 2.2f;
	case BattleAction::Erase:
		return 1.1f;
	case BattleAction::Sticky:
		return 1.05f;
	case BattleAction::Kaku:
		return .85f;
	case BattleAction::Paint:
		return .83f;
	case BattleAction::Beckon:
		return .75f;
	case BattleAction::Read:
	case BattleAction::Smell:
		return .92f;
	case BattleAction::Decoy:
	case BattleAction::Ou:
	case BattleAction::Pause:
	case BattleAction::Heal:
	case BattleAction::Brew:
	case BattleAction::Fluff:
	case BattleAction::Roly:
		return .95f;
	default:
		return 1;
	}
}
// 物ごとの飛び散り方の数値 (unit・秒):
//  炸裂は王冠の形: 近い所へも山なりに飛ぶよう、飛ぶ秒の下限 InkCrownTime。出す所は当たった所の床すれすれ (InkCrownHeight)
//  叩くはハンマーの頭 (体の前 InkHammerReach x 体の半径) から。王は体から輪が広がる (届くまでの秒 = 距離 / InkWaveSpeed)
//  噴き出す・王は、体のまわりの輪 (体の半径の InkRingInner 倍から InkRingWidth unit の幅) に、角度をそろえて均等に落とす
//  転がしは InkTailShare の割合を、車輪 (体の後ろ) から後ろと横へ巻き上げる (前の落ちる所は後ろへ折り返す)
//  崩れるは、物の高さ InkToppleHeight (unit) までのどこかから降りかかる。色ぬりは InkBurstShare を前へ大きな塊で飛ばし、残りは水たまり。
//  熱いお茶は最初の InkPourShare を体の前の高い所 (InkPourHeight) から注ぐ
inline constexpr float InkCrownTime = .3f, InkCrownHeight = .35f, InkHammerReach = 1.6f, InkWaveSpeed = 18,
                       InkRingInner = 1.4f, InkRingWidth = 3.5f, InkTailShare = .4f;
inline constexpr float InkToppleHeight = 3.5f, InkBurstShare = .35f, InkPourShare = .3f, InkPourHeight = 1.8f;
// 型ごとの数値 (unit・秒): 一直線の走る速さ・振り回しの時間・水たまりの広がる速さ・雨の高さと降る時間・招き寄せの出す距離と高さ
// (一直線のしずくは、届く所の InkLineHop 手前・高さ InkLineHeight から跳ねる)
inline constexpr float InkLineHop = 2.2f, InkLineHeight = .6f;
inline constexpr float InkLineSpeed = 35, InkSwingTime = .12f, InkPoolSpeed = 12, InkRainHeight = 3.5f,
                       InkRainTime = .8f, InkGatherReach = 3, InkGatherHeight = 1.6f;
inline constexpr float ChallengeTerritoryGrowLow = .2f, ChallengeTerritoryGrowHigh = .35f,
                       ChallengeTerritoryGrowStart = .25f, ChallengeTerritoryWetTime = 1.f;
// 能力の体液はしずくの数で数える: 1 回で使えるしずくは、ぬれるマスの目安 (ChallengeTerritoryUseMin〜Max) を、しずく 1 つでぬれる広さ (InkCellsPerDrop マス) で割った数。
// 当たりの形の中に、広さに比例した数の落ちる所をばらばらに選び、それぞれへしずくを飛ばす (もうぬれている所へも飛ぶ。どの方向にも飛び散る)。
// 毎フレーム当たる能力 (転がし・回転・発射・投げ物) は、しずくを能力の時間 (InkContinuousSpan 秒以上) に分けて少しずつ出す
// 陣取りのテンポ: どの能力も床をぬるので、続けて使えるよう待ち時間を ChallengeTerritoryWaitScale 倍 (ChallengeTerritoryWaitLow〜High 秒) にし、
// 前の能力が ChallengeTerritoryChain 秒たてば終わる前でも次を使える
inline constexpr float ChallengeTerritoryWaitScale = .45f, ChallengeTerritoryWaitLow = .3f,
                       ChallengeTerritoryWaitHigh = .9f, ChallengeTerritoryChain = .35f;
inline float ChallengeTerritoryWait(float wait) {
	return std::clamp(wait * ChallengeTerritoryWaitScale, ChallengeTerritoryWaitLow, ChallengeTerritoryWaitHigh);
}
inline constexpr float InkCellsPerDrop = 1.5f, InkContinuousSpan = .35f, InkSampleReach = 7;
inline constexpr float ChallengeTerritorySplatLow = .6f, ChallengeTerritorySplatHigh = .8f,
                       ChallengeTerritorySplatJitter = .22f, ChallengeTerritoryDropChance = .35f;
inline constexpr float ChallengeTerritorySplash = 1.4f, ChallengeTerritoryCandySplash = .6f,
                       ChallengeTerritoryTeaGrow = 1.5f;
} // namespace Game::Chrono
