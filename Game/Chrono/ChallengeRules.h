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
enum class ChallengeKind : uint8_t { Blocks, Tower, Count };
inline constexpr int ChallengeKindCount = int(ChallengeKind::Count);
inline const char* ChallengeKindId(ChallengeKind kind) {
	static constexpr const char* ids[] = {"blocks", "tower"};
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

// 遊べるチャレンジ (積み木クラッシャーだけで勝負する)。ランダムでも、--challenge=id でも、タイトルの隠しコマンドの札でも、ここに入っているものだけを選ぶ
// (タワーブレイカーは積み木クラッシャーと似ているので作らない)
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
                       ChallengeHeavyRehit = .5f;
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

// ---- 結果: こわした積み木の山 (チリも積もれば山となる) ----
// こわした積み木を、積み木の街の広場に 1 個ずつ降らせて立体の山に積む (ChallengeBlocks.cpp の BlocksPile)。カメラはてっぺんと一緒に上る。
// 山は四角い段を重ねた形で、段の一辺は 2 段ごとに 1 つ狭くなる (一本の柱でも、低い台でもない山の形)。一番下の段の一辺 base は、
// 全部の段がそろう数がこわした数を越えない一番大きいもの。余った分は山のふもとのまわりに輪にして置く (先に積む)。
// 段の中は決まった順にばらばらに置き (上から降ってくるように見える)、金の積み木は最後に積むので、山のてっぺんが金になる。
// 高さは段の数 x ChallengePileBlockCm (タイトルのならびの積み木と同じ 4cm)
inline constexpr float ChallengePileBlockCm = 4;
// 1 個の置き場所: 山の真ん中からの x・z (積み木の数) と段 (row。0 が地面)
struct PileSlot {
	float x = 0, z = 0;
	int row = 0;
};
inline int ChallengePileBase(int count) {
	int base = 0;
	while ((base + 1) * (base + 2) * (2 * base + 3) / 3 <= count)
		++base;
	return base;
}
inline std::vector<PileSlot> ChallengePileLayout(int count) {
	std::vector<PileSlot> out;
	if (count <= 0)
		return out;
	out.reserve(size_t(count));
	auto scramble = [](uint32_t v) {
		v ^= v >> 16;
		v *= 0x7feb352du;
		v ^= v >> 15;
		v *= 0x846ca68bu;
		return v ^ (v >> 16);
	};
	// 1 つの段 (一辺 side・高さ row。ring なら外周だけ) を、決まった順にばらばらに並べて足す
	auto layer = [&](int side, int row, bool ring, int limit) {
		std::vector<std::pair<uint32_t, PileSlot>> cells;
		for (int i = 0; i < side; ++i)
			for (int j = 0; j < side; ++j) {
				if (ring && i > 0 && j > 0 && i < side - 1 && j < side - 1)
					continue;
				const PileSlot cell{float(i) - float(side - 1) * .5f, float(j) - float(side - 1) * .5f, row};
				cells.push_back({scramble(uint32_t(i * 73 + j * 9151 + row * 524287 + side * 31)), cell});
			}
		std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& c : cells)
			if (int(out.size()) < limit)
				out.push_back(c.second);
	};
	const int base = ChallengePileBase(count);
	const int full = base * (base + 1) * (2 * base + 1) / 3;
	// ふもとの輪 (余った分)。山の外側に一辺を 2 ずつ広げた輪を、地面に置く
	for (int side = base + 2; int(out.size()) < count - full; side += 2)
		layer(side, 0, true, count - full);
	// 山: 下の段から
	for (int row = 0; base - row / 2 > 0 && int(out.size()) < count; ++row)
		layer(base - row / 2, row, false, count);
	return out;
}
inline int ChallengePileRows(const std::vector<PileSlot>& slots) {
	int rows = 0;
	for (const auto& s : slots)
		rows = std::max(rows, s.row + 1);
	return rows;
}
// 山の高さのランク: ChallengePileRankCm (cm) を越えるごとに C → B → A → S
// (40cm は約 110 個・56cm は約 280 個・72cm は約 570 個)
inline constexpr float ChallengePileRankCm[] = {40, 56, 72};
inline int ChallengePileRankIndex(float cm) {
	int rank = 0;
	for (float line : ChallengePileRankCm)
		if (cm >= line)
			++rank;
	return rank;
}
inline const char* ChallengePileRank(float cm) {
	static constexpr const char* names[] = {"C", "B", "A", "S"};
	return names[ChallengePileRankIndex(cm)];
}
// 宇宙まで積もる: 本当の山 (こわした積み木) が積み終わったら、そのまま同じおもちゃの積み木を山の上に積み続けて、山をひと回りずつ
// 包んでいく (殻。中は見えないので外側の 1 個分だけ)。殻の積み木は前の殻の ChallengePileShellGrow 倍だけ大きい (少しずつしか変わらない)。
// 殻は本当の山と同じ形 (段の一辺は 2 段ごとに 1 つ狭くなる) で、一番下の段は前の殻より積み木 1 個ぶん広い (どの高さでも前の殻の外)。
// 一辺は 2g/(g-1) (約 10) に近づくので、カメラが同じ割合で離れれば、積み木はいつも同じ大きさに見えて、山だけが育っていく。
// 殻の数は、ランクごとの高さ ChallengePileShellMeters (C 空・B 雲・A 空の果て・S 宇宙) にとどくまで
inline constexpr float ChallengePileShellGrow = 1.25f;
inline constexpr float ChallengePileShellMeters[] = {300, 3000, 30000, 200000};
// 1 つの殻: 積み木の大きさ (本当の積み木 1 個が 1)・一番下の段の一辺・段の数・積み木の数
struct PileShell {
	float size = 1;
	int side = 1, rows = 1, blocks = 0;
	float Height() const { return float(rows) * size; }
	float Width() const { return float(side) * size; }
};
// 殻の段 row の一辺と、その段の積み木の数 (外側の輪。2 以下はうめる)
inline int ChallengePileShellSide(const PileShell& s, int row) {
	return std::max(1, s.side - row / 2);
}
inline int ChallengePileShellRowBlocks(int side) {
	return side <= 2 ? side * side : 4 * (side - 1);
}
inline std::vector<PileShell> ChallengePileShells(int count) {
	std::vector<PileShell> out;
	const auto slots = ChallengePileLayout(count);
	if (slots.empty())
		return out;
	float half = 0;
	for (const auto& sl : slots)
		half = std::max({half, std::abs(sl.x), std::abs(sl.z)});
	float width = 2 * half + 1, height = float(ChallengePileRows(slots)), size = 1;
	const float target =
	    ChallengePileShellMeters[ChallengePileRankIndex(height * ChallengePileBlockCm)] * 100 / ChallengePileBlockCm;
	while (height < target && out.size() < 200) {
		PileShell s;
		s.size = size *= ChallengePileShellGrow;
		s.side = int(std::ceil(width / size - 1e-3f)) + 2;
		s.rows = std::max(2 * s.side - 1, int(std::ceil(height / size - 1e-3f)) + 1);
		for (int r = 0; r < s.rows; ++r)
			s.blocks += ChallengePileShellRowBlocks(ChallengePileShellSide(s, r));
		width = s.Width();
		height = s.Height();
		out.push_back(s);
	}
	return out;
}
// 殻の積み木の置き場所 (殻の真ん中からの x・z (殻の積み木の数) と段)。下の段から、段の中は決まった順にばらばら
inline std::vector<PileSlot> ChallengePileShellLayout(const PileShell& s, uint32_t seed) {
	std::vector<PileSlot> out;
	out.reserve(size_t(s.blocks));
	auto scramble = [](uint32_t v) {
		v ^= v >> 16;
		v *= 0x7feb352du;
		v ^= v >> 15;
		v *= 0x846ca68bu;
		return v ^ (v >> 16);
	};
	for (int row = 0; row < s.rows; ++row) {
		const int side = ChallengePileShellSide(s, row);
		std::vector<std::pair<uint32_t, PileSlot>> cells;
		for (int i = 0; i < side; ++i)
			for (int j = 0; j < side; ++j) {
				if (side > 2 && i > 0 && j > 0 && i < side - 1 && j < side - 1)
					continue;
				cells.push_back(
				    {scramble(uint32_t(i * 97 + j * 7919 + row * 104729) ^ seed),
				     {float(i) - float(side - 1) * .5f, float(j) - float(side - 1) * .5f, row}});
			}
		std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& c : cells)
			out.push_back(c.second);
	}
	return out;
}
// 殻の積み木が landed 個着いたときの高さ (本当の積み木 1 個が 1。下の段から積むので、最後に着いた積み木の段まで)
inline float ChallengePileShellTop(const PileShell& s, int landed) {
	int row = 0, total = 0;
	while (row < s.rows && total < landed)
		total += ChallengePileShellRowBlocks(ChallengePileShellSide(s, row++));
	return float(row) * s.size;
}

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
	// (積み木の街だけ) この高さより下から始まる面に置く (BlocksLandRules.h)
	float y = 1;
};
// pattern 4 は専用の立体ステージ「積み木の街」(形と配置は BlocksLandRules.h)。ふだんはこれを使い、0〜3 は平らな場の確認用 (--challenge-pattern=番号・実測)
inline constexpr int ChallengeBlockPatterns = 5, ChallengeBlocksStagePattern = 4;
inline const char* ChallengeBlockPatternName(int pattern) {
	static constexpr const char* names[] = {"長い壁", "円形", "高い塔", "硬い密集地", "積み木の街"};
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

} // namespace Game::Chrono
