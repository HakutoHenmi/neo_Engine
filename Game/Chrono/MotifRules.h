// MotifRules.h
// 姿の型紙と当てはめ (H2。エンジンに依存しない)。3 つの物の組み合わせを、何かの姿 (大砲・戦車・カメ…) に見立てる。
//  - 型紙 (Motif): 姿の名前・種類 (兵器・乗り物・生き物・キャラ)・合う性質・合う意味のタグ・物をはめる「役の枠」(MotifSlot)
//  - 役の枠: ほしい形の種類 (丸・長い・平たい・輪・箱ごとの合い方)・ほしい特徴 (持ち手・立つ・穴)・ほしい大きさ (3 つの中で大きい物か)・
//    主な枠か (1 つ目の物 = 本体を入れたい所)・空いたときの減点 (スライムだけで作る)・左右に 2 つ置くか
//  - 当てはめ: 型紙ごとに、3 つの物を枠に入れる並べ方を全部試し、点数 (形の合い方 + 性質の合い方 + 意味の合い方 + 本体の物を主な枠に入れたか
//    - 使われなかった物 - 空いた枠) の一番高いものを選ぶ。同じ点なら、物の id から作った決まった乱数で選ぶ (いつも同じ姿になる)
//  - 「配慮」の物 (神仏・お墓) は枠に入れない (体の中に浮かべるだけ。使われなくても減点しない)
//  - どの型紙も点数が MotifFallbackScore に届かなければ、型紙を使わない (今の「型 + 部品」の作り方に戻る)
// 型紙は Resources/Data/motifs.json に書く (読むのはゲームの側。ここは読んだ型紙を受け取って当てはめるだけ)
#pragma once
#include "ItemProfileRules.h"
#include "WeaponCraftRules.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace Game::Chrono {
// 役の枠のほしい大きさ (3 つの物の中での大きさの順)
enum class MotifSize : uint8_t { Small, Medium, Big };
// 枠の形の置き方 (H3。使うのは MotifShape.h): 付ける所 at (土台の枠の半分の大きさに対する割合。土台は on の枠、-1 なら主な枠)・
// ずらす量 off (自分の半分の大きさに対する割合)・大きさ scale (主な枠の物の一番長い向きの半分に対する割合)・
// 物の一番長い向き long と一番短い向き thin をどちらへ向けるか (0 x・1 y・2 z・-1 決めない)・keep は物の向きのまま・動き・
// 空いたときにスライムだけで作る形 (fill。half は主な枠の大きさに対する半分の大きさ)
struct MotifPlace {
	Vec at{}, off{};
	float scale = 1;
	int longAxis = -1, thinAxis = -1, on = -1;
	bool keep = false, hasFill = false;
	SdfMotion motion = SdfMotion::None;
	SdfKind fillKind = SdfKind::Ellipsoid;
	Vec fillHalf{.5f, .5f, .5f};
};
struct MotifSlot {
	MotifPlace place;
	std::string name;
	std::array<float, size_t(ItemShapeClass::Count)> shapeFit{};
	float handle = 0, upright = 0, hole = 0, emptyPenalty = 0;
	MotifSize size = MotifSize::Medium;
	bool mirror = false, main = false;
};
// (J1) 土台の部品: スライムだけで作る決まった形。中心・世界の向きの半分の大きさ (円すいは 根もとの半径・先の半径・長さの半分)・
// 軸 (カプセルと円すい。0 x・1 y・2 z)・角の丸み (箱)・左右に 2 つ置くか
struct MotifBasePart {
	SdfKind kind = SdfKind::Ellipsoid;
	Vec center{}, half{.5f, .5f, .5f};
	int axis = 2;
	float round = .2f;
	bool mirror = false;
};
// (J1) 物を並べる所: 繰り返し (2 つ目の物を並べる) か紋章 (3 つ目の物を 1 つ)。並べ方は 輪 (中心・軸・2 つの半径・角度の範囲)・
// 列 (from から to)・点。normal が 0 なら土台の面の向きを使う。左右に 2 つ置くか
enum class MotifPattern : uint8_t { Ring, Line, Point };
struct MotifSocket {
	std::string name;
	bool emblem = false, mirror = false;
	MotifPattern pattern = MotifPattern::Point;
	Vec at{}, axis{0, 1, 0}, from{}, to{}, normal{};
	float radius1 = .3f, radius2 = .3f, angle0 = 0, angle1 = 360;
};
struct Motif {
	std::string id, name, family, nameFormat;
	std::vector<MotifSlot> slots;
	std::vector<MotifBasePart> base;
	std::vector<MotifSocket> sockets;
	std::array<float, FuseTraitCount> traitFit{};
	std::map<std::string, float> tagFit;
};
// 当てはめに使う物: プロフィール・性質 (2 つまで)・配慮の物か・型紙ごとの意味の合い方 (PrepareMotifItem で作る)
struct MotifItem {
	const ItemProfile* profile = nullptr;
	std::array<FuseTrait, 2> traits{FuseTrait::Count, FuseTrait::Count};
	bool sensitive = false;
	std::vector<float> tagScore;
};
// 当てはめの結果: 型紙の番号 (-1 は型紙なし)・物ごとに入れた枠 (-1 は入れない)・点数とその内わけ
struct MotifChoice {
	int motif = -1;
	std::array<int, 3> slotOf{-1, -1, -1};
	float score = -1e9f, shape = 0, trait = 0, tag = 0;
};
// 点数の重み: 性質・意味・大きさの合い方・本体を主な枠に入れたとき・使われなかった物 1 つ・型紙を使う下限
inline constexpr float MotifTraitWeight = .6f, MotifTagWeight = .5f, MotifSizeWeight = .3f, MotifOrderBonus = .5f,
                       MotifUnusedPenalty = .8f, MotifFallbackScore = 1.2f;

inline MotifItem
    PrepareMotifItem(const std::vector<Motif>& motifs, const ItemProfile& profile, std::array<FuseTrait, 2> traits) {
	MotifItem item;
	item.profile = &profile;
	item.traits = traits;
	item.sensitive = std::find(profile.tags.begin(), profile.tags.end(), "配慮") != profile.tags.end();
	for (const auto& m : motifs) {
		float score = 0;
		for (const auto& t : profile.tags)
			if (auto found = m.tagFit.find(t); found != m.tagFit.end())
				score += found->second;
		item.tagScore.push_back(score);
	}
	return item;
}
// 物 1 つを枠に入れたときの合い方 (sizeRank は 3 つの中での大きさの順: 0 小さい・2 大きい)
inline float MotifSlotFit(const MotifSlot& slot, const ItemProfile& p, int sizeRank) {
	float fit = slot.shapeFit[size_t(p.shape)];
	if (p.Has(ItemHasHandle))
		fit += slot.handle;
	if (p.Has(ItemUpright))
		fit += slot.upright;
	if (p.Has(ItemHasHole))
		fit += slot.hole;
	return fit + MotifSizeWeight * (1 - float(std::abs(sizeRank - int(slot.size))) * .5f);
}
// 決まった乱数 (同じ点のときに選ぶ)
inline uint32_t MotifHash(uint32_t a, uint32_t b) {
	uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	return h;
}
inline MotifChoice
    ChooseMotif(const std::vector<Motif>& motifs, const std::array<const MotifItem*, 3>& items, uint32_t seed) {
	// (作る画面で物がまだ 3 つそろっていないときは、無い物を nullptr で渡す。無い物は枠に入れず、減点もしない。
	//  型紙を使わない下限 MotifFallbackScore も使わない: 1 つ目から同じ作り方の形を見せて、物を足すたびに形が育つように)
	int present = 0;
	for (const auto* it : items)
		present += it != nullptr;
	// 3 つの中での大きさの順 (同じなら並べた順)
	std::array<int, 3> rank{};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			if (j != i && items[size_t(i)] && items[size_t(j)] &&
			    (items[size_t(j)]->profile->realSize < items[size_t(i)]->profile->realSize ||
			     (items[size_t(j)]->profile->realSize == items[size_t(i)]->profile->realSize && j < i)))
				++rank[size_t(i)];
	FuseLevels lv{};
	for (const auto* it : items)
		for (FuseTrait t : it ? it->traits : std::array<FuseTrait, 2>{FuseTrait::Count, FuseTrait::Count})
			if (t != FuseTrait::Count)
				++lv[size_t(t)];
	MotifChoice best;
	for (size_t m = 0; m < motifs.size(); ++m) {
		const auto& motif = motifs[m];
		float trait = 0, tag = 0;
		for (int t = 0; t < FuseTraitCount; ++t)
			trait += motif.traitFit[size_t(t)] * float(lv[size_t(t)]);
		trait /= 3;
		for (const auto* it : items)
			if (it)
				tag += it->tagScore[m];
		tag /= 3;
		const int slots = int(motif.slots.size());
		// 物ごとの枠の合い方
		std::array<std::array<float, 8>, 3> fit{};
		for (int i = 0; i < 3; ++i)
			for (int s = 0; s < std::min(slots, 8) && items[size_t(i)]; ++s)
				fit[size_t(i)][size_t(s)] =
				    MotifSlotFit(motif.slots[size_t(s)], *items[size_t(i)]->profile, rank[size_t(i)]);
		const float jitter = float(MotifHash(seed, uint32_t(m)) & 1023) / 1023.f * .01f;
		for (int a = -1; a < slots; ++a)
			for (int b = -1; b < slots; ++b)
				for (int c = -1; c < slots; ++c) {
					const std::array<int, 3> pick{a, b, c};
					if ((a >= 0 && (a == b || a == c)) || (b >= 0 && b == c))
						continue;
					float shape = 0;
					bool ok = true;
					std::array<bool, 8> used{};
					for (int i = 0; i < 3; ++i) {
						const int s = pick[size_t(i)];
						if (!items[size_t(i)]) {
							ok = s < 0;
							if (!ok)
								break;
							continue;
						}
						if (s < 0) {
							if (!items[size_t(i)]->sensitive)
								shape -= MotifUnusedPenalty;
							continue;
						}
						if (items[size_t(i)]->sensitive || s >= 8) {
							ok = false;
							break;
						}
						used[size_t(s)] = true;
						shape += fit[size_t(i)][size_t(s)];
						if (i == 0 && motif.slots[size_t(s)].main)
							shape += MotifOrderBonus;
					}
					if (!ok)
						continue;
					for (int s = 0; s < std::min(slots, 8); ++s)
						if (!used[size_t(s)])
							shape -= motif.slots[size_t(s)].emptyPenalty;
					const float score = shape + MotifTraitWeight * trait + MotifTagWeight * tag + jitter;
					if (score > best.score) {
						best.motif = int(m);
						best.slotOf = pick;
						best.score = score;
						best.shape = shape;
						best.trait = trait;
						best.tag = tag;
					}
				}
	}
	if (present == 3 && best.score < MotifFallbackScore)
		best.motif = -1;
	return best;
}
} // namespace Game::Chrono
