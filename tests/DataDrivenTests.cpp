// DataDrivenTests.cpp
// if / switch からデータテーブルへ置き換えた箇所の回帰テスト。
// 期待値は置き換え前の分岐のコードに書かれていた値そのもので、表を引いた結果が変わっていないことを確認する。
#include "../Game/Chrono/SlimeWeaponRules.h"
#include "../Game/Chrono/DevourStageRules.h"
#include "../Game/Chrono/DevourSpillRules.h"
#include "../Game/Chrono/DevourGimmickRules.h"
#include "../Game/Chrono/DevourAnimation.h"
#include "../Game/Chrono/DomainRules.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace Game::Chrono;

namespace {
int failures = 0;
void Require(bool condition, const char* message) {
	if (!condition) {
		std::printf("FAILED: %s\n", message);
		++failures;
	}
}
bool Same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
bool Near(float a, float b) { return std::abs(a - b) < 1e-6f; }

// ---- 体の行動のテーブル ----
void TestLifeActions() {
	Require(LifeActionTable.size() == size_t(LifeAction::Spring) + 1, "LifeActionTable has one row per LifeAction");
	using A = LifeAction;
	// 変形に必要な量 (以前: ハンマー・剣・飛行機は個別の量、アイテムの変形は 1、それ以外は 0)
	Require(LifeRequiredMass(A::Hammer) == LifeHammerMass && LifeRequiredMass(A::Sword) == LifeSwordMass && LifeRequiredMass(A::Plane) == LifePlaneMass, "weapon masses");
	for (A a : {A::Tire, A::Hydrant, A::Signal, A::Vacuum, A::Spring}) Require(LifeRequiredMass(a) == 1, "item forms need mass 1");
	for (A a : {A::Idle, A::Engulf, A::Restore}) Require(LifeRequiredMass(a) == 0 && !LifeCanMorph(1e6f, a), "non-forms cannot morph");
	// 攻撃の長さ (以前: ハンマー .58・剣 .64・ほか .95) とダメージ (24・16・ほか 20)
	Require(Near(LifeWeaponDuration(A::Hammer), .58f) && Near(LifeWeaponDuration(A::Sword), .64f) && Near(LifeWeaponDuration(A::Tire), .95f), "weapon durations");
	Require(LifeActionInfo(A::Hammer).weaponDamage == 24 && LifeActionInfo(A::Sword).weaponDamage == 16 && LifeActionInfo(A::Spring).weaponDamage == 20, "weapon damage");
	// 流体の形の番号 (以前: ハンマー 1 … バネ 8、ほか 0)
	const int shapes[] = {0, 0, 1, 2, 3, 4, 5, 0, 6, 7, 8};
	for (size_t i = 0; i < LifeActionTable.size(); ++i) Require(LifeActionTable[i].fluidShape == shapes[i], "fluid shape numbers");
	// HUD のアイコンと名前 (吸収モードの 3 つの変形)
	Require(Same(LifeActionInfo(A::Tire).icon, "form_tire") && Same(LifeActionInfo(A::Spring).icon, "form_spring") && Same(LifeActionInfo(A::Vacuum).icon, "form_vacuum"), "item icons");
	Require(Same(LifeActionInfo(A::Tire).name, "タイヤ") && Same(LifeActionInfo(A::Spring).name, "バネ") && Same(LifeActionInfo(A::Vacuum).name, "掃除機"), "item names");
	// 変形の案内: 吸収モードで変わるのはハンマーとタイヤだけ
	Require(LifeEnterHint(A::Hammer, true) != LifeEnterHint(A::Hammer, false) && LifeEnterHint(A::Tire, true) != LifeEnterHint(A::Tire, false), "devour-specific hints");
	Require(LifeEnterHint(A::Sword, true) == LifeEnterHint(A::Sword, false), "shared hints");
	for (A a : {A::Hammer, A::Sword, A::Plane, A::Tire, A::Hydrant, A::Signal, A::Vacuum, A::Spring})
		Require(LifeActionInfo(a).lockedHint && LifeEnterHint(a, false), "every form has locked and enter hints");
	// 未解放の案内に必要な量が入る
	char hint[256];
	std::snprintf(hint, sizeof(hint), LifeActionInfo(A::Hammer).lockedHint, int(LifeActionInfo(A::Hammer).requiredMass));
	Require(std::string(hint) == "ゴミを吸い込んで本体180まで育つと、ハンマーに変形できる", "hammer locked hint text");
	// 変形アイテム → 変形 (以前: タイヤ・バネ以外は掃除機)
	Require(DevourItemForm(LifeMorphItem::Tire) == A::Tire && DevourItemForm(LifeMorphItem::Spring) == A::Spring, "item forms");
	for (auto item : {LifeMorphItem::None, LifeMorphItem::Hydrant, LifeMorphItem::Signal, LifeMorphItem::Vacuum})
		Require(DevourItemForm(item) == A::Vacuum, "other items become vacuum");
}

// ---- スライムの型 ----
void TestStyles() {
	SlimeRecipe recipe;
	recipe.mix = {100, 0, 0, 0};
	Require(Same(recipe.StyleName(), "さらさら回収型") && Same(recipe.Weakness(), "注意：止まりにくい・被弾で大きくちぎれる"), "style 0");
	recipe.mix = {0, 0, 0, 100};
	Require(Same(recipe.StyleName(), "ふわふわ群れ型") && Same(recipe.Strength(), "得意：長い滞空・子が速く何度も攻撃"), "style 3");
	recipe.mix = {25, 25, 25, 25};
	Require(Same(recipe.StyleName(), "バランス型") && Same(recipe.Weakness(), "材料を半分以上にすると個性が強くなる"), "balanced style");
}

// ---- 液体・塊・技の名前・ギミックの形 ----
void TestOtherTables() {
	Require(SpillParamsOf(SpillKind::Honey).topSpeed == 7 && SpillParamsOf(SpillKind::Glue).topSpeed == 5 && SpillParamsOf(SpillKind::Juice).topSpeed == 22, "spill params");
	Require(SpillParamsOf(SpillKind::Juice).lane == 3 && SpillParamsOf(SpillKind::Honey).lane == 1.6f && SpillParamsOf(SpillKind::Glue).lane == 1.6f, "spill lanes");

	for (int i = 0; i <= 4; ++i) Require(DevourBlobKindFromName(DevourBlobKindName(DevourBlobKind(i))) == DevourBlobKind(i), "blob kind names round-trip");
	Require(DevourBlobKindFromName("unknown") == DevourBlobKind::Lump && Same(DevourBlobKindName(DevourBlobKind::Gulp), "gulp"), "blob kind fallback");

	Require(Same(DomainSkillName(DomainShape::Loop), "HOMING DOMAIN") && Same(DomainSkillName(DomainShape::Infinity), "TWIN STORM"), "domain names");

	// ギミックの形 (以前の switch の値)
	Require(GimmickShapeBounds(GimmickGunShape).z == 5.6f && GimmickShapeBounds(GimmickSheetShape).y == 1.2f, "gimmick bounds");
	Require(GimmickShapeCore(GimmickPlaneShape).thin == .5f && GimmickShapeCore(GimmickHeliShape).thin == 1.3f, "gimmick cores");
	Require(GimmickShapeBounds(GimmickDropShape).y == 6 && GimmickShapeBounds(999).y == 6 && GimmickShapeCore(0).thin == 1.6f, "unknown shapes use the drop");
	Require(GimmickInside(GimmickBallShape, {0, GimmickBallRadius, 0}) && !GimmickInside(GimmickBallShape, {0, 50, 0}), "gimmick inside uses the shape's function");
}
} // namespace

int main() {
	TestLifeActions();
	TestStyles();
	TestOtherTables();
	if (failures) {
		std::printf("%d data-driven check(s) failed\n", failures);
		return 1;
	}
	std::printf("Data-driven table tests passed\n");
	return 0;
}
