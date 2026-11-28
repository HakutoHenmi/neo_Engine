// DevourGimmickRules.h
// 吸収モードの部屋のギミックの形と規則。エンジンに依存しない。
// どのギミックも「床の輪の中で変形キーを 1 回押す」だけで動き、あとは決まった動きを自動で進める。
// 体はその間、液体の粒子で道具の形 (ボール・水鉄砲・ドミノ・紙飛行機・流れる膜・しずく) になる。
// 形の大きさは普段の体と同じくらいの体積にする (粒子の数は決まっているので、大きすぎると表面が途切れる)
#pragma once
#include "DevourRules.h"
#include "SlimeWeaponRules.h"
#include "SlimeHydrantRules.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <string>
#include <vector>
namespace Game::Chrono {
// Glide (こいのぼり)・Wire (電線)・Mikoshi (おみこし)・Koma (こま) は坂の町のギミック (DevourTownGimmick.cpp)、Hose (消火栓の放水) は DevourHose.cpp
enum class GimmickKind { Shoot, Domino, Bowling, Paint, Liquid, Plane, Glide, Wire, Mikoshi, Koma, Hose };
// 体の形の番号 (LifeCloud の番号。武器の形の続き)
// (26 のヘリコプターはギミックではなく、高い所から落ちたときの形。27 こいのぼり・28 電気の玉・29 おみこし・30 こま・31 消火栓は坂の町)
inline constexpr int GimmickBallShape = 20, GimmickGunShape = 21, GimmickDominoShape = 22, GimmickPlaneShape = 23,
                     GimmickSheetShape = 24, GimmickDropShape = 25, GimmickHeliShape = 26, GimmickCarpShape = 27,
                     GimmickSparkShape = 28, GimmickMikoshiShape = 29, GimmickKomaShape = 30, GimmickHoseShape = 31;
// 32〜35 は戦闘フェーズの形 (DevourBattle.cpp): 轢く=車・叩く=ハンマー・守る=盾・回復=ハート
inline constexpr int BattleCarShape = 32, BattleHammerShape = 33, BattleShieldShape = 34, BattleHeartShape = 35;
// 36〜38 は物ごとの能力の形: 剣・ばね・掃除機
inline constexpr int BattleSwordShape = 36, BattleSpringShape = 37, BattleVacuumShape = 38;
// 39 は将棋の駒 (駒の技の間の形)
inline constexpr int BattleShogiShape = 39;
// 40 は消しゴム・41 はだるま
inline constexpr int BattleEraserShape = 40, BattleDarumaShape = 41;
// 100 から先は、変形チャレンジで作った武器の形 (材料 3 つの物のモデルを組み合わせた形。WeaponShapeGrids に作って入れる)
inline constexpr int WeaponShapeFirst = 100;
inline bool GimmickShape(int shape) {
	return shape >= GimmickBallShape && (shape <= BattleDarumaShape || shape >= WeaponShapeFirst);
}
// 形を作る範囲 (x は左右の半分、y は 0 から、z は前後の半分)
struct GimmickBounds {
	float x = 3, y = 6, z = 3;
};
// ---- 戦闘フェーズの形 (普段の体と同じくらいの体積。先が +z) ----
// 車: 車体・屋根・4 つの車輪
inline bool BattleInsideCar(Vec p) {
	if (std::abs(p.x) < 1.7f && p.y > .9f && p.y < 2.2f && std::abs(p.z) < 3.2f)
		return true;
	if (std::abs(p.x) < 1.4f && p.y >= 2.2f && p.y < 3.3f && p.z > -1.8f && p.z < 1.f)
		return true;
	for (float x : {-1.75f, 1.75f})
		for (float z : {-2.1f, 2.1f}) {
			const float dy = p.y - .9f, dz = p.z - z;
			if (std::abs(p.x - x) < .45f && dy * dy + dz * dz < .81f)
				return true;
		}
	return false;
}
// ハンマー: 柄と大きな頭 (柄の根もとを支点に振り下ろす)
inline bool BattleInsideHammer(Vec p) {
	if (std::abs(p.x) < .45f && std::abs(p.z) < .45f && p.y > .4f && p.y < 4.2f)
		return true;
	return std::abs(p.x) < 2.2f && std::abs(p.z) < 1.4f && p.y >= 4.f && p.y < 6.4f;
}
// 盾: 前に立てた丸い板と、真ん中の出っぱり
inline bool BattleInsideShield(Vec p) {
	const float dx = p.x, dy = p.y - 3.f, r = std::sqrt(dx * dx + dy * dy);
	if (r < 3.f && p.z > 1.2f && p.z < 2.4f)
		return true;
	return r < 1.1f && p.z >= 2.4f && p.z < 2.4f + std::sqrt(std::max(0.f, 1.21f - r * r));
}
// 剣: 前 (+z) へ伸ばした刃とつば・柄 (柄の根もと (0,2.2,-1) を軸に左右へ振る)
inline bool BattleInsideSword(Vec p) {
	if (std::abs(p.x) < .6f && std::abs(p.y - 2.2f) < .38f && p.z > 0 && p.z < 7.5f - std::abs(p.x) * 1.5f)
		return true;
	if (std::abs(p.x) < 1.9f && std::abs(p.y - 2.2f) < .55f && p.z > -.5f && p.z <= 0)
		return true;
	return LifeSegmentDistance(p, {0, 2.2f, -2.6f}, {0, 2.2f, -.5f}) < .5f;
}
// ばね: 巻いた線 (半径 1.8、1 巻き 0.7) と、上の板
inline bool BattleInsideSpring(Vec p) {
	const float r = std::sqrt(p.x * p.x + p.z * p.z);
	if (r < 2.2f && p.y > 4.1f && p.y < 4.7f)
		return true;
	if (p.y < 0 || p.y > 4.3f)
		return false;
	const float a = std::atan2(p.z, p.x) / 6.2831853f, t = p.y / .7f - a, frac = t - std::round(t), dy = frac * .7f;
	return (r - 1.8f) * (r - 1.8f) + dy * dy < .42f * .42f;
}
// 掃除機: 本体・持ち手と、前へ伸びる吸い込み口
inline bool BattleInsideVacuum(Vec p) {
	if (std::abs(p.x) < 1.4f && p.y > .6f && p.y < 3.4f && p.z > -3.f && p.z < .4f)
		return true;
	if (LifeSegmentDistance(p, {0, 3.4f, -2.6f}, {0, 4.6f, -.4f}) < .42f)
		return true;
	if (LifeSegmentDistance(p, {0, 1.8f, .4f}, {0, 1.f, 4.2f}) < .7f)
		return true;
	return std::abs(p.x) < 1.4f && p.y > .3f && p.y < 1.6f && p.z > 4.f && p.z < 4.9f;
}
// 将棋の駒: 立てた五角形の板 (先が上。前後に厚みがあり、上ほど薄い)
inline bool BattleInsideShogi(Vec p) {
	if (p.y < 0 || p.y > 6.2f)
		return false;
	const float half = p.y < 4.2f ? 2.6f - .1f * p.y : 2.18f * (6.2f - p.y) / 2.f;
	return std::abs(p.x) < half && std::abs(p.z) < 1.1f - .08f * p.y;
}
// 消しゴム: 横長の箱と、真ん中に巻いた紙の帯 (前後にこする)
inline bool BattleInsideEraser(Vec p) {
	return std::abs(p.x) < 2.9f && p.y > 0 && p.y < 2.6f && std::abs(p.z) < 1.5f;
}
// だるま: 大きな胴と、上にのった頭
// (形の大きさは普段の体と同じくらいの体積にする。大きすぎると粒子が足りず、ばらばらに見える)
inline bool BattleInsideDaruma(Vec p) {
	return Length(p - Vec{0, 2.1f, 0}) < 2.1f || Length(p - Vec{0, 4.6f, 0}) < 1.4f;
}
// ハート: 立てたハートの形 (厚みあり)
inline bool BattleInsideHeart(Vec p) {
	if (std::abs(p.z) > 1.2f * (1 - std::min(1.f, std::abs(p.x) / 3.4f) * .4f))
		return false;
	const float x = p.x / 2.6f, y = (p.y - 3.f) / 2.6f, a = x * x + y * y - 1;
	return a * a * a - x * x * y * y * y <= 0;
}
// ボウリングの玉: 指の穴が 3 つある球 (転がると穴が回って、回っているのが分かる)
inline constexpr float GimmickBallRadius = 2.3f;
inline bool GimmickInsideBall(Vec p) {
	Vec d = p - Vec{0, GimmickBallRadius, 0};
	if (Length(d) > GimmickBallRadius)
		return false;
	const Vec holes[] = {{-.55f, .8f, .35f}, {.55f, .8f, .35f}, {0, .62f, -.78f}};
	for (const auto& h : holes) {
		Vec axis = Unit(h);
		float along = Dot(d, axis);
		if (along > GimmickBallRadius * .5f && Length(d - axis * along) < .38f)
			return false;
	}
	return true;
}
// 水鉄砲: 胴・前の銃身・下の握り・上の水のタンク (前は +z)
inline bool GimmickInsideGun(Vec p) {
	if (std::abs(p.x) < 1.2f && p.y > 1.6f && p.y < 4 && p.z > -2.6f && p.z < 1.4f)
		return true;
	if (LifeSegmentDistance(p, {0, 3.2f, 1.4f}, {0, 3.2f, 5.4f}) < .75f)
		return true;
	if (std::abs(p.x) < .8f && p.y > 0 && p.y < 1.8f && p.z > -2.4f + .3f * (1.8f - p.y) &&
	    p.z < -.8f + .3f * (1.8f - p.y))
		return true;
	return Length(p - Vec{0, 5.3f, -1}) < 1.5f;
}
// ドミノ: 立てた 1 枚の板 (前へ倒れる)
inline constexpr float GimmickDominoHalfX = 1.5f, GimmickDominoHeight = 6.4f, GimmickDominoHalfZ = 1.2f;
inline bool GimmickInsideDomino(Vec p) {
	return std::abs(p.x) < GimmickDominoHalfX && p.y > 0 && p.y < GimmickDominoHeight &&
	       std::abs(p.z) < GimmickDominoHalfZ;
}
// 紙飛行機: 平たい三角の翼と下の折り目 (先が +z)
inline bool GimmickInsidePlane(Vec p) {
	if (p.z < -3.5f || p.z > 4.5f)
		return false;
	float span = 3.8f * (4.5f - p.z) / 8;
	if (p.y > 1.5f && p.y < 2.7f && std::abs(p.x) < span)
		return true;
	return std::abs(p.x) < .5f && p.y > .4f && p.y < 1.6f && p.y > 1.6f - 1.2f * (4.5f - p.z) / 8;
}
// 流れる膜: 薄く広がった楕円 (前後に長い)。液体をつたって壁を登るときの体
inline bool GimmickInsideSheet(Vec p) {
	float r = (p.x / 3.6f) * (p.x / 3.6f) + (p.z / 5.4f) * (p.z / 5.4f);
	return r < 1 && p.y >= 0 && p.y < .25f + .95f * std::sqrt(1 - r);
}
// しずく: 絵の具に飛び込むときの、先のとがった水滴
inline bool GimmickInsideDrop(Vec p) {
	float h = std::sqrt(p.x * p.x + p.z * p.z);
	if (Length(p - Vec{0, 2.1f, 0}) < 2.1f)
		return true;
	return p.y > 2.1f && p.y < 6 && h < 2.1f * (6 - p.y) / 3.9f;
}
// ヘリコプター: 丸い胴・後ろへ伸びる尾・上の軸と、十字の 2 枚の回転翼 (前は +z。翼の高さより上は回る)
inline constexpr float GimmickRotorY = 4.6f, GimmickRotorReach = 5.f;
inline bool GimmickInsideHeli(Vec p) {
	const Vec d = p - Vec{0, 2, .3f};
	if ((d.x / 1.9f) * (d.x / 1.9f) + (d.y / 1.6f) * (d.y / 1.6f) + (d.z / 2.2f) * (d.z / 2.2f) < 1)
		return true;
	if (LifeSegmentDistance(p, {0, 2.3f, -1.6f}, {0, 2.8f, -4.6f}) < .45f)
		return true;
	if (LifeSegmentDistance(p, {0, 3.3f, 0}, {0, GimmickRotorY, 0}) < .35f)
		return true;
	// (翼は細すぎると流体の格子より薄くなって消えるので、厚みと幅を持たせる)
	if (std::abs(p.y - GimmickRotorY) < .32f && ((std::abs(p.x) < .6f && std::abs(p.z) < GimmickRotorReach) ||
	                                             (std::abs(p.z) < .6f && std::abs(p.x) < GimmickRotorReach)))
		return true;
	return false;
}
// こいのぼり: 前 (+z) が口の太い筒で、後ろへ細くなり、尾びれが上下に開く。横に目
inline bool GimmickInsideCarp(Vec p) {
	const float u = (p.z + 4.6f) / 9.2f;
	if (u < 0 || u > 1)
		return false;
	const float radius = u > .82f ? 1.25f : (.45f + .8f * u / .82f);
	const Vec c{0, 2, 0};
	const float r = std::sqrt(p.x * p.x + (p.y - c.y) * (p.y - c.y));
	if (u >= .12f && r < radius && !(u > .97f && r < radius * .55f))
		return true;
	// 尾びれ (後ろの端で上下に開く 2 枚)
	if (u < .12f && std::abs(p.x) < .35f && std::abs(p.y - c.y) < .4f + 2.4f * (.12f - u) / .12f &&
	    std::abs(p.y - c.y) > .15f)
		return true;
	// 目 (左右に出た丸)
	return Length(Vec{std::abs(p.x) - 1.15f, p.y - 2.45f, p.z - 3.2f}) < .42f;
}
// 電気の玉: 丸い芯と、外へ伸びる 6 本のとげ (電線を走るときの体)
inline constexpr float GimmickSparkRadius = 1.6f;
inline bool GimmickInsideSpark(Vec p) {
	const Vec d = p - Vec{0, 2.4f, 0};
	if (Length(d) < GimmickSparkRadius)
		return true;
	const Vec spikes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (const auto& s : spikes) {
		const float along = Dot(d, s);
		if (along > 0 && along < GimmickSparkRadius * 1.5f &&
		    Length(d - s * along) < .55f * (1 - along / (GimmickSparkRadius * 1.5f)))
			return true;
	}
	return false;
}
// おみこし: 井の字に組んだ担ぎ棒の上に台・お社の箱・四方へ広がる屋根・てっぺんの鳳凰 (前は +z)。
// 担ぎ棒は前後へ長く突き出し、前後の横棒は左右へ広い (後ろから見て、横棒と屋根でおみこしと分かるように)。
// 全体を GimmickMikoshiLift だけ浮かせ、下に担ぎ手の子分が入る (DrawMikoshiHelpers)
inline constexpr float GimmickMikoshiPoleX = 1.35f, GimmickMikoshiPoleZ = 4.6f, GimmickMikoshiCrossZ = 3.4f,
                       GimmickMikoshiCrossX = 3.2f, GimmickMikoshiLift = 1.2f,
                       GimmickMikoshiTop = 6.1f + GimmickMikoshiLift;
inline bool GimmickInsideMikoshi(Vec p) {
	p.y -= GimmickMikoshiLift;
	for (float z : {-GimmickMikoshiCrossZ, GimmickMikoshiCrossZ})
		if (LifeSegmentDistance(p, {-GimmickMikoshiCrossX, .55f, z}, {GimmickMikoshiCrossX, .55f, z}) < .4f)
			return true;
	if (LifeSegmentDistance(
	        p, {-GimmickMikoshiPoleX, .55f, -GimmickMikoshiPoleZ}, {-GimmickMikoshiPoleX, .55f, GimmickMikoshiPoleZ}) <
	    .42f)
		return true;
	if (LifeSegmentDistance(
	        p, {GimmickMikoshiPoleX, .55f, -GimmickMikoshiPoleZ}, {GimmickMikoshiPoleX, .55f, GimmickMikoshiPoleZ}) <
	    .42f)
		return true;
	const float ax = std::abs(p.x), az = std::abs(p.z);
	if (p.y > .8f && p.y < 1.4f && ax < 2.1f && az < 2.1f)
		return true; // 台
	if (p.y >= 1.4f && p.y < 3.4f && ax < 1.35f && az < 1.35f)
		return true; // お社の箱
	// 屋根: 軒が広く、上へ細くなる (反りを付けて、ふつうの箱の家と違って見えるように)
	// (軒は箱より大きく張り出し、下の面を反らせる: 軒先ほど高い)
	if (p.y >= 3.2f && p.y < 5.2f) {
		const float u = (p.y - 3.2f) / 2.f, half = 2.9f - 2.6f * std::sqrt(u);
		const float edge = std::max(ax, az);
		if (edge < half && (edge < 1.4f || p.y > 3.2f + .35f * (edge - 1.4f)))
			return true;
	}
	return Length(p - Vec{0, 5.65f, 0}) < .5f; // 鳳凰
}
// こま: 下のとがった芯から広がる胴・丸い上の面・上へ伸びる軸。縁に 4 つの切れ込み (回っているのが見えるように)
inline constexpr float GimmickKomaRadius = 2.9f, GimmickKomaWaist = 2.4f, GimmickKomaTop = 5.f;
inline bool GimmickInsideKoma(Vec p) {
	const float h = std::sqrt(p.x * p.x + p.z * p.z);
	if (p.y > GimmickKomaWaist + .6f && p.y < GimmickKomaTop && h < .5f)
		return true; // 軸
	float limit = 0;
	if (p.y > 0 && p.y <= GimmickKomaWaist)
		limit = GimmickKomaRadius * std::pow(p.y / GimmickKomaWaist, .75f);
	else if (p.y > GimmickKomaWaist && p.y < GimmickKomaWaist + .9f) {
		const float u = (p.y - GimmickKomaWaist) / .9f;
		limit = GimmickKomaRadius * std::sqrt(std::max(0.f, 1 - u * u));
	}
	if (h >= limit)
		return false;
	// 切れ込み: 縁の 4 か所を削る
	if (h > GimmickKomaRadius * .68f && p.y > GimmickKomaWaist - .9f) {
		const float a = std::atan2(p.z, p.x), k = std::remainder(a, 1.5707963f);
		if (std::abs(k) < .2f)
			return false;
	}
	return true;
}
// 形の姿勢: 前へ転がる (spin)・前へ倒れる (tip、下の前の角を軸に)・機首の上下 (pitch)・左右の傾き (roll)
// (scale は形全体の大きさ。変形武器の形が出てくるとき (F5) にだけ使い、ほかの形では 1 のまま)
struct GimmickPose {
	float spin = 0, tip = 0, pitch = 0, roll = 0, squash = 0, scale = 1;
};
// 変形武器の形の姿勢 (WeaponShapeGrids の後ろで定義する)
inline Vec WeaponShapePose(int shape, Vec p, const GimmickPose& pose);
inline Vec GimmickRotateX(Vec p, Vec pivot, float a) {
	Vec d = p - pivot;
	float c = std::cos(a), s = std::sin(a);
	return pivot + Vec{d.x, d.y * c - d.z * s, d.y * s + d.z * c};
}
inline Vec GimmickRotateZ(Vec p, Vec pivot, float a) {
	Vec d = p - pivot;
	float c = std::cos(a), s = std::sin(a);
	return pivot + Vec{d.x * c - d.y * s, d.x * s + d.y * c, d.z};
}
inline Vec GimmickPosePoint(int shape, Vec p, const GimmickPose& pose) {
	if (shape >= WeaponShapeFirst)
		return WeaponShapePose(shape, p, pose);
	if (shape == GimmickBallShape)
		return GimmickRotateX(p, {0, GimmickBallRadius, 0}, pose.spin);
	if (shape == GimmickDominoShape)
		return GimmickRotateX(p, {0, 0, GimmickDominoHalfZ}, pose.tip);
	// こま: 軸のまわりに回り (spin)、とがった先を支点に傾く (roll・pitch)
	if (shape == GimmickKomaShape) {
		const float c = std::cos(pose.spin), s = std::sin(pose.spin);
		p = {p.x * c - p.z * s, p.y, p.x * s + p.z * c};
		p = GimmickRotateZ(p, {0, 0, 0}, pose.roll);
		return GimmickRotateX(p, {0, 0, 0}, pose.pitch);
	}
	if (shape == GimmickPlaneShape || shape == GimmickCarpShape || shape == GimmickMikoshiShape) {
		p = GimmickRotateZ(p, {0, 2, 0}, pose.roll);
		return GimmickRotateX(p, {0, 2, 0}, -pose.pitch);
	}
	if (shape == GimmickSparkShape) {
		const float c = std::cos(pose.spin), s = std::sin(pose.spin);
		const Vec d = p - Vec{0, 2.4f, 0};
		return Vec{0, 2.4f, 0} + Vec{d.x * c - d.y * s, d.x * s + d.y * c, d.z};
	}
	if (shape == GimmickSheetShape)
		return GimmickRotateX(p, {0, 0, 0}, -pose.pitch);
	// ヘリコプター: 回転翼だけが軸の周りを回り、全体は進む向きへ少し前に傾く
	if (shape == GimmickHeliShape) {
		if (p.y > GimmickRotorY - .4f) {
			const float c = std::cos(pose.spin), s = std::sin(pose.spin);
			p = {p.x * c - p.z * s, p.y, p.x * s + p.z * c};
		}
		p = GimmickRotateZ(p, {0, 2, 0}, pose.roll);
		return GimmickRotateX(p, {0, 2, 0}, pose.pitch);
	}
	// 戦闘: ハンマーは柄の根もとを支点に前へ振り下ろし、ハートはゆっくり回る
	if (shape == BattleHammerShape)
		return GimmickRotateX(p, {0, .4f, 0}, pose.pitch);
	// 剣は柄の根もとを軸に左右へ振る (spin)・ばねは縮む (squash)
	if (shape == BattleSwordShape) {
		const float c = std::cos(pose.spin), s = std::sin(pose.spin);
		const Vec d = p - Vec{0, 0, -1.f};
		return Vec{0, 0, -1.f} + Vec{d.x * c + d.z * s, d.y, -d.x * s + d.z * c};
	}
	// 消しゴムは前後にこする (spin をずらす量に使う)・だるまは揺れる (roll)
	if (shape == BattleEraserShape)
		return {p.x, p.y, p.z + pose.spin};
	if (shape == BattleDarumaShape)
		return GimmickRotateZ(p, {0, 0, 0}, pose.roll);
	if (shape == BattleSpringShape) {
		const float k = 1 - pose.squash;
		return {p.x * (1 + .3f * pose.squash), p.y * k, p.z * (1 + .3f * pose.squash)};
	}
	if (shape == BattleHeartShape) {
		const float c = std::cos(pose.spin), s = std::sin(pose.spin);
		return {p.x * c - p.z * s, p.y, p.x * s + p.z * c};
	}
	if (shape == GimmickDropShape) {
		float wide = 1 / std::sqrt(std::max(.2f, 1 - pose.squash));
		return {p.x * wide, p.y * (1 - pose.squash), p.z * wide};
	}
	return p;
}

// 作動範囲の膜の高さ: 小さな輪でも体より十分高く、広い輪 (絵の具) は少し高く
inline float GimmickCurtainHeight(float radius) {
	return std::clamp(16 + radius * .2f, 16.f, 26.f);
}

// 形の中の芯: 体の中の物を、形が崩れないように小さくしてここへ集める。center は形の厚い所の真ん中、
// thin はそこでの厚みの半分 (物の大きさの上限と、物を散らす幅に使う)。
// spineA から spineB が長さを持つ形 (こいのぼり) は、物をその線に沿って体全体へ並べる (真ん中に固まって、はみ出さないように)
struct GimmickCore {
	Vec center{};
	float thin = 1;
	Vec spineA{}, spineB{};
};
// ---- 道具の形のデータテーブル (データドリブン) ----
// 形 1 つ分のデータを 1 行にまとめ、形の番号で引く。形を足すときは番号を足してこの表に 1 行足す
struct GimmickShapeData {
	GimmickBounds bounds;
	GimmickCore core;
	bool (*inside)(Vec);
};
inline const GimmickShapeData& GimmickShapeInfo(int shape) {
	// GimmickBallShape (20) から GimmickHeliShape (26) の順。それ以外の番号はしずく (GimmickDropShape) として扱う
	static const GimmickShapeData table[] = {
	    {{GimmickBallRadius, 2 * GimmickBallRadius, GimmickBallRadius},
	     {{0, GimmickBallRadius, 0}, 1.5f},
	     GimmickInsideBall},                                            // 玉
	    {{1.6f, 6.8f, 5.6f}, {{0, 2.8f, -.6f}, 1.f}, GimmickInsideGun}, // 水鉄砲
	    {{GimmickDominoHalfX, GimmickDominoHeight, GimmickDominoHalfZ},
	     {{0, GimmickDominoHeight * .5f, 0}, 1.f},
	     GimmickInsideDomino},                                           // ドミノ
	    {{3.8f, 2.7f, 4.5f}, {{0, 2.1f, .8f}, .5f}, GimmickInsidePlane}, // 紙飛行機
	    {{3.6f, 1.2f, 5.4f}, {{0, .5f, 0}, .45f}, GimmickInsideSheet},   // 流れる膜
	    {{2.1f, 6, 2.1f}, {{0, 2.1f, 0}, 1.6f}, GimmickInsideDrop},      // しずく
	    {{GimmickRotorReach, GimmickRotorY + .4f, GimmickRotorReach},
	     {{0, 2, .3f}, 1.3f},
	     GimmickInsideHeli},                                                                      // ヘリコプター
	    {{1.6f, 3.3f, 4.6f}, {{0, 2, .5f}, .5f, {0, 2, -2.6f}, {0, 2, 3.2f}}, GimmickInsideCarp}, // こいのぼり
	    {{2.5f, 4.9f, 2.5f}, {{0, 2.4f, 0}, 1.f}, GimmickInsideSpark},                            // 電気の玉
	    {{GimmickMikoshiCrossX + .5f, GimmickMikoshiTop, GimmickMikoshiPoleZ + .5f},
	     {{0, 2.4f + GimmickMikoshiLift, 0}, .5f},
	     GimmickInsideMikoshi},                                                                           // おみこし
	    {{GimmickKomaRadius, GimmickKomaTop, GimmickKomaRadius}, {{0, 2.f, 0}, .45f}, GimmickInsideKoma}, // こま
	    {{2.2f, 5.1f, 2.3f}, {{0, 2.f, 0}, .5f}, LifeInsideHydrant},
	    {{2.3f, 3.3f, 3.2f}, {{0, 1.6f, 0}, .6f}, BattleInsideCar},    // 戦闘: 車
	    {{2.2f, 6.4f, 1.4f}, {{0, 5.2f, 0}, 1.f}, BattleInsideHammer}, // 戦闘: ハンマー
	    {{3.f, 6.f, 3.5f}, {{0, 3.f, 1.8f}, .5f}, BattleInsideShield}, // 戦闘: 盾
	    {{3.4f, 6.3f, 1.2f}, {{0, 3.f, 0}, .8f}, BattleInsideHeart},
	    {{1.9f, 2.8f, 7.5f}, {{0, 2.2f, 1.f}, .4f}, BattleInsideSword}, // 能力: 剣
	    {{2.3f, 4.8f, 2.3f}, {{0, 2.2f, 0}, .5f}, BattleInsideSpring},  // 能力: ばね
	    {{1.5f, 5.f, 4.9f}, {{0, 2.f, -1.2f}, .8f}, BattleInsideVacuum},
	    {{2.7f, 6.3f, 1.2f}, {{0, 2.6f, 0}, .8f}, BattleInsideShogi},
	    {{2.9f, 2.6f, 1.5f}, {{0, 1.3f, 0}, .8f}, BattleInsideEraser}, // 能力: 消しゴム
	    {{2.2f, 6.1f, 2.2f},
	     {{0, 2.1f, 0}, 1.1f},
	     BattleInsideDaruma}}; // 能力: だるま                                                                   // 能力: 将棋の駒                                                                // 能力: 掃除機                                                                    // 戦闘: ハート                                                                     // 消火栓 (戦闘モードと同じ形)                               // こま
	static_assert(BattleDarumaShape - GimmickBallShape + 1 == int(std::size(table)), "形の番号と表の行数をそろえる");
	const int index = shape - GimmickBallShape;
	return table[index >= 0 && index < int(std::size(table)) ? index : GimmickDropShape - GimmickBallShape];
}
// ---- 作った武器の形 (WeaponShapeFirst から) ----
// 形の中を細かい箱 (cell の立方体) に分け、埋まっている箱を fill に持つ。lo は箱の並びの一番下の角 (形の座標)。part はどの部品が作った箱か (1 本体・2 前・3 左右。0 は空)
// anchor は材料の物 (1〜3 番目) を体の中に浮かべる所 (形の座標)、anchorFit はそこに収まる半分の大きさ。anchors は入っている数
struct WeaponShapeGrid {
	std::string key;
	GimmickBounds bounds;
	GimmickCore core;
	Vec lo{};
	float cell = 1;
	int nx = 0, ny = 0, nz = 0;
	std::vector<uint8_t> fill, part;
	std::array<Vec, 3> anchor{};
	std::array<float, 3> anchorFit{};
	int anchors = 0;
	// (F5) 箱ごとの部品の番号 (pieces の番号 + 1。0 は空) と、部品ごとの動き (SdfMotion の値) と支点 (右側の部品の座標)
	struct Piece {
		uint8_t motion = 0;
		Vec pivot{};
	};
	std::vector<uint8_t> piece;
	std::vector<Piece> pieces;
	// (H3) 姿の型紙の名前 (空なら型紙を使っていない) と、形の決まりのために部品にしなかった物の数
	std::string motif;
	int motifDrops = 0;
	// (J2) 土台に半分沈めて並べる本物のモデル: 3 つの中の番号・場所 (形の座標)・向き (axes[m] が物の場の向き m の世界の向き)・
	// 物の場の座標から形の座標への倍率・左右に映した物か・紋章か (作り方は Chrono/MotifShape.h)
	struct Model {
		uint8_t item = 0;
		Vec pos{};
		std::array<Vec, 3> axes{Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}};
		float scale = 1;
		bool mirrored = false, emblem = false;
	};
	std::vector<Model> models;
	int PieceAt(Vec p) const {
		const int x = int(std::floor((p.x - lo.x) / cell)), y = int(std::floor((p.y - lo.y) / cell)),
		          z = int(std::floor((p.z - lo.z) / cell));
		if (piece.empty() || x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz)
			return -1;
		return int(piece[size_t((z * ny + y) * nx + x)]) - 1;
	}
	bool Inside(Vec p) const {
		const int x = int(std::floor((p.x - lo.x) / cell)), y = int(std::floor((p.y - lo.y) / cell)),
		          z = int(std::floor((p.z - lo.z) / cell));
		if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz)
			return false;
		return fill[size_t((z * ny + y) * nx + x)] != 0;
	}
};
inline std::vector<WeaponShapeGrid>& WeaponShapeGrids() {
	static std::vector<WeaponShapeGrid> grids;
	return grids;
}
inline const WeaponShapeGrid* WeaponShapeOf(int shape) {
	const auto& grids = WeaponShapeGrids();
	const int i = shape - WeaponShapeFirst;
	return i >= 0 && i < int(grids.size()) ? &grids[size_t(i)] : nullptr;
}
inline GimmickBounds GimmickShapeBounds(int shape) {
	if (const auto* w = WeaponShapeOf(shape))
		return w->bounds;
	return GimmickShapeInfo(shape).bounds;
}
// 変形武器の形の姿勢: 部品ごとの動き (はばたく: roll・回る: spin・縮む/弾む: tip・ゆらぐ: pitch) のあとに、
// 形全体の反動 (squash: 前後に縮んで横に広がる) と大きさ (scale。床の高さ 0 を中心に)
inline Vec WeaponShapePose(int shape, Vec p, const GimmickPose& pose) {
	const auto* g = WeaponShapeOf(shape);
	if (!g)
		return p;
	const int index = g->PieceAt(p);
	if (index >= 0 && index < int(g->pieces.size())) {
		const auto& w = g->pieces[size_t(index)];
		const float side = p.x < 0 ? -1.f : 1.f;
		const Vec pv{w.pivot.x * side, w.pivot.y, w.pivot.z};
		const Vec d = p - pv;
		switch (w.motion) {
		case 1: {
			const float a = pose.roll * side, c = std::cos(a), s = std::sin(a);
			p = pv + Vec{d.x * c - d.y * s, d.x * s + d.y * c, d.z};
			break;
		}
		case 2: {
			const float c = std::cos(pose.spin), s = std::sin(pose.spin);
			p = pv + Vec{d.x * c - d.z * s, d.y, d.x * s + d.z * c};
			break;
		}
		case 3:
			p.y = pv.y + d.y * (1 - .25f * std::max(0.f, pose.tip));
			break;
		case 4: {
			const float k = 1 + .18f * pose.pitch;
			p = pv + d * k;
			break;
		}
		case 5:
			p.y += .12f * pose.tip * g->cell * 4;
			break;
		default:
			break;
		}
	}
	const float k = pose.squash;
	p = {p.x * (1 + .4f * k), p.y * (1 + .15f * k), p.z * (1 - k)};
	return p * pose.scale;
}
inline bool GimmickInside(int shape, Vec p) {
	if (const auto* w = WeaponShapeOf(shape))
		return w->Inside(p);
	return GimmickShapeInfo(shape).inside(p);
}
inline GimmickCore GimmickShapeCore(int shape) {
	if (const auto* w = WeaponShapeOf(shape))
		return w->core;
	return GimmickShapeInfo(shape).core;
}
// 物の大きさ (半分の幅) の上限と、芯から散らす幅 (形の倍率をかける前)
inline constexpr float GimmickMealSize = .7f, GimmickMealSpread = .45f;

// ---- 道をたどる (液体・紙飛行機・ボウリング) ----
inline float GimmickPathLength(const std::vector<Vec>& path) {
	float total = 0;
	for (size_t i = 1; i < path.size(); ++i)
		total += Length(path[i] - path[i - 1]);
	return total;
}
// 道の上の距離 s の点と、その区間の向き
inline Vec GimmickPathPoint(const std::vector<Vec>& path, float s, Vec* direction = nullptr) {
	if (path.empty())
		return {};
	if (path.size() == 1) {
		if (direction)
			*direction = {0, 0, 1};
		return path[0];
	}
	for (size_t i = 1; i < path.size(); ++i) {
		Vec a = path[i - 1], b = path[i];
		float length = Length(b - a);
		if (s <= length || i + 1 == path.size()) {
			if (direction)
				*direction = length > 1e-4f ? (b - a) * (1 / length) : Vec{0, 0, 1};
			return a + (b - a) * (std::clamp(s, 0.f, length) / std::max(length, 1e-4f));
		}
		s -= length;
	}
	return path.back();
}
// 道の向きの水平の成分 (真上へ登る区間は前後の区間の水平の向きを使う)
inline Vec GimmickPathHeading(const std::vector<Vec>& path, float s) {
	Vec d;
	GimmickPathPoint(path, s, &d);
	Vec flat{d.x, 0, d.z};
	if (Length(flat) > .05f)
		return Unit(flat);
	for (float k = 4; k < 200; k += 4) {
		Vec ahead;
		GimmickPathPoint(path, s + k, &ahead);
		flat = {ahead.x, 0, ahead.z};
		if (Length(flat) > .05f)
			return Unit(flat);
		GimmickPathPoint(path, s - k, &ahead);
		flat = {ahead.x, 0, ahead.z};
		if (Length(flat) > .05f)
			return Unit(flat);
	}
	return {0, 0, 1};
}

// ---- 時間と速さ ----
// 変形にかける時間 (形へ寄せきるまで) と、元の体へ戻る時間
inline constexpr float GimmickFormTime = .35f;
// 液体をつたう速さ・紙飛行機の速さ・ボウリングの玉の速さ (1 秒に進む長さ)
inline constexpr float GimmickFlowSpeed = 78, GimmickPlaneSpeed = 95, GimmickBallSpeed = 190;
// 紙飛行機をスティックでずらせる幅 (道の左右)
inline constexpr float GimmickPlaneSteer = 16;
// だるま落とし: 胴の輪の数・狙いの上下の 1 往復の時間・撃ってから当たるまでの時間・成功で出る小物の数
inline constexpr int GimmickDarumaDiscs = 5;
inline constexpr float GimmickAimPeriod = 1.6f, GimmickShotTime = .18f;
// 狙いの高さ (0 が一番下の輪の真ん中、1 がだるまの頭の真ん中)。上下に往復する
inline float GimmickAim(float clock) {
	float u = std::fmod(clock / GimmickAimPeriod, 1.f);
	return u < .5f ? u * 2 : 2 - u * 2;
}
// 狙いの高さから当たる物を決める: 0..discs-1 は胴の輪 (下から)、discs は頭
inline int GimmickAimTarget(float aim, int discsLeft) {
	return std::clamp(int(std::floor(aim * float(discsLeft) + .5f)), 0, discsLeft);
}
// ドミノが次々に倒れる間隔と、1 枚が倒れきる時間・倒れて止まる角度
inline constexpr float GimmickDominoStep = .085f, GimmickDominoFall = .2f, GimmickDominoRest = 1.2f;
// ボウリング: 玉の通り道のピンは直接倒れ (玉の半径の hitShare 倍 + ピンの半径の幅)、倒れたピンは
// 当たった側の外へ向かって後ろのピンを倒す。真ん中を通せばストライク、端を通すと数本しか倒れない。
// ピンの位置は道の上の 2 次元 (x 前・z 横)、lateral は玉の横の位置
inline constexpr float GimmickPinHitShare = .6f, GimmickPinChain = 1.3f;
inline std::vector<bool>
    GimmickPinsDown(const std::vector<Vec>& pins, float lateral, float ballRadius, float pinRadius, float spacing) {
	std::vector<bool> down(pins.size());
	for (size_t i = 0; i < pins.size(); ++i)
		down[i] = std::abs(pins[i].z - lateral) < ballRadius * GimmickPinHitShare + pinRadius;
	for (int round = 0; round < 4; ++round)
		for (size_t i = 0; i < pins.size(); ++i) {
			if (!down[i])
				continue;
			const float side = pins[i].z - lateral;
			for (size_t k = 0; k < pins.size(); ++k) {
				if (down[k])
					continue;
				Vec d = pins[k] - pins[i];
				if (d.x > 0 && Length(d) < spacing * GimmickPinChain && d.z * side >= -.01f)
					down[k] = true;
			}
		}
	return down;
}
// ヘリコプター: 足元までこの高さ (体の大きさの倍率をかける前) より上で落ち始めたらなる。降りる速さの上限と回転翼の速さ
inline constexpr float GimmickHeliDrop = 9, GimmickHeliFall = 16, GimmickHeliSpin = 20;
inline bool GimmickHeliStarts(float drop, float scale, float fallSpeed, bool grounded) {
	return !grounded && fallSpeed < 0 && drop > GimmickHeliDrop * scale;
}
// ---- 坂の町: こいのぼりで滑空・電線をつたう (単位は 1m = 6) ----
// こいのぼり: 竿を登る時間・飛び出す速さ・ふだんの速さ・遅い/速い上限・下へ引く力・速さを戻す強さ・ふだん下がる速さ・
// 向きを変える速さ (ラジアン/秒)・機首の角度 (何もしない/押し下げ最大/引き上げ最大)・機首を寄せる速さ・飛び出した高さより上へ行ける高さ
inline constexpr float GlideClimbTime = .7f, GlideLaunchSpeed = 90, GlideCruise = 78, GlideMinSpeed = 40,
                       GlideMaxSpeed = 150, GlideGravity = 58.8f, GlideDrag = .35f, GlideSink = 2.4f, GlideTurn = 1.15f,
                       GlidePitchRest = -.08f, GlidePitchDive = -.6f, GlidePitchClimb = .38f, GlidePitchRate = 3,
                       GlideCeiling = 60;
// 機首の角度 (スティックの前後 forward: 前へ倒すと下を向いて速くなり、手前へ引くと上を向いて速さと引き換えに上がる)
inline float GlidePitchFor(float forward) {
	return GlidePitchRest +
	       (forward > 0 ? (GlidePitchDive - GlidePitchRest) * forward : (GlidePitchRest - GlidePitchClimb) * forward);
}
// 1 フレーム進める: 機首が下を向くほど速くなり、上を向くほど遅くなる。遅すぎると機首が下がる。上下の速さは機首の向きと、ふだん下がる分
inline float GlideSpeedStep(float speed, float pitch, float dt) {
	speed += (-GlideGravity * std::sin(pitch) - GlideDrag * (speed - GlideCruise)) * dt;
	return std::clamp(speed, GlideMinSpeed, GlideMaxSpeed);
}
inline float GlideRise(float speed, float pitch) {
	return speed * std::sin(pitch) - GlideSink;
}
// 電線: 柱を登る時間・電線を走る速さ・たるみ (長さに対する割合)・電柱の高さに対する電線の高さ (電柱の先より少し上: 柱の上で電気の玉が電柱に隠れないように)・登れる輪の半径・つなぐ電柱の最大の間隔・別の道の電柱とつなぐ最大の間隔 (m)
// カメラ: 映す範囲を普段の何倍に広げるか・その最小 (体の大きさの倍率をかける前)・進む向きの先を見る距離・先を見る向きが進む向きへ回る速さ
inline constexpr float WireCameraPull = 2.4f, WireCameraLeast = 12, WireCameraAhead = 10, WireCameraTurn = 4;
inline constexpr float WireClimbTime = .45f, WireSpeed = 150, WireSag = .035f, WireHeight = 1.04f,
                       WireRingRadius = 2.6f, WireSpan = 60, WireBridge = 40;
// 電線の上の点 (t は 0〜1。真ん中ほど下がる)
inline Vec WirePoint(Vec a, Vec b, float t) {
	const float length = Length(b - a);
	Vec p = a + (b - a) * t;
	p.y -= length * WireSag * 4 * t * (1 - t);
	return p;
}
// 分かれ道で進む電線を選ぶ: 行きたい向き (水平) に一番近い電線。近い物が無ければ -1
inline int WireChoose(const std::vector<Vec>& directions, Vec wish, float least = .35f) {
	int best = -1;
	float score = least;
	for (size_t i = 0; i < directions.size(); ++i) {
		const float d = Dot(directions[i], wish);
		if (d > score) {
			score = d;
			best = int(i);
		}
	}
	return best;
}
// ---- 坂の町: おみこしとこま (単位は 1m = 6) ----
// おみこし: 担いでいる時間・歩く速さ (ふだんの何倍)・わっしょいの間隔 (秒)・跳ねる高さ (体の大きさの倍率をかける前)・
// 子分の数・子分を出す間隔・子分が探す距離 (体の大きさの倍率をかける前)・子分の走る速さ (同)・体に戻ったとみなす距離 (同)・使った後に休む時間・
// 子分があえて遠くの物を取りに行くときの、探す距離に対する近い側の端 (この割合より遠い物から選ぶ。無ければ一番遠い物)
inline constexpr float MikoshiTime = 14, MikoshiPace = .85f, MikoshiBeat = .62f, MikoshiHop = .55f, MikoshiSend = .1f,
                       MikoshiReach = 34, MikoshiRun = 28, MikoshiHome = 4, MikoshiRest = 6, MikoshiFar = .5f;
inline constexpr int MikoshiHelpers = 8;
// 子分の大きさ (体の大きさの倍率をかける前の半径)
inline constexpr float MikoshiHelperSize = .9f;
// わっしょいの跳ね (0〜1。拍の頭で地面、真ん中で一番高い)
inline float MikoshiBounce(float age) {
	const float u = std::fmod(age, MikoshiBeat) / MikoshiBeat;
	return std::sin(3.1415927f * u);
}
// こま: 回っている時間・飛び出す速さ (体の大きさの平方根をかける)・跳ね返るたびに速くなる倍率と上限 (飛び出す速さの何倍)・
// 平らな所で遅くなる割合 (毎秒)・坂で速さが変わる強さ・スティックで曲がる速さ (ラジアン/秒)・これより遅いと倒れる (飛び出す速さの割合)・
// 見た目の回る速さ (ラジアン/秒)・体の大きさに対して当たりを調べる半径 (倍率をかける前)・使った後に休む時間
inline constexpr float KomaTime = 11, KomaLaunch = 75, KomaBounceGain = 1.06f, KomaMaxGain = 1.7f, KomaFriction = .04f,
                       KomaSlope = 40, KomaSteer = 1.3f, KomaFallSpeed = .35f, KomaSpin = 7, KomaReach = 2.6f,
                       KomaRest = 4;
// 壁の向き normal で跳ね返った向き (水平)
inline Vec KomaReflect(Vec heading, Vec normal) {
	normal.y = 0;
	if (Length(normal) < 1e-4f)
		return heading * -1.f;
	normal = Unit(normal);
	Vec out = heading - normal * (2 * Dot(heading, normal));
	out.y = 0;
	return Length(out) > 1e-4f ? Unit(out) : heading * -1.f;
}
// ---- 坂の町: 消火栓の放水 (DevourHose.cpp。水は GPU 流体の消火栓の水で、速さと重さは SlimeHydrantRules.h と同じ) ----
// 放水できる時間・1 秒に出す水の粒の数・狙いを探す距離 (m ではなく単位)・水が当たる範囲の半径 (体の大きさの平方根をかける)・
// 引き寄せる物が飛んでくる速さ・1 回に引き寄せる数と間隔・押し流す坂の傾きの下限・水の向きと坂の下りの向きのそろい (内積の下限)・
// 押し流す物の速さの上限と、坂で速くなる強さ・止まる速さ・押し流せる大きさ (覆える大きさに対する倍率)・使った後に休む時間
inline constexpr float HoseTime = 12, HoseRate = 5200, HoseSplash = 22, HosePullSpeed = 55,
                       HosePullEvery = .12f, HoseWashSlope = .06f, HoseWashAlign = .15f, HoseWashMax = 42,
                       HoseWashAccel = 160, HoseWashStop = 2.5f, HoseWashSize = 1.6f, HoseRest = 5;
inline constexpr int HosePullCount = 2, HoseWashLimit = 60;
// 扇: 1 つの口から扇に広げる水の筋の数・扇の半分の開き (ラジアン)・筋ごとの速さのばらつき (外の筋ほど遅い)
// 地面の高さの表: 1 辺のマスの数・1 マスの幅 (単位)・作り直す間隔 (秒)・中心がこれだけ動いたら作り直す (マス)
inline constexpr int HoseFanLanes = 9, HoseTerrainCells = 128;
inline constexpr float HoseFanHalf = .45f, HoseFanSlow = .12f, HoseTerrainCell = 3, HoseTerrainEvery = .5f,
                       HoseTerrainShift = 8;
// 狙いへ届く 2 つの弧 (低い弧と高い弧) の初速と飛ぶ時間。届かなければ false
inline bool HoseLaunch(Vec origin, Vec target, bool high, Vec& velocity, float& flight) {
	const Vec delta = target - origin;
	const float d2 = Dot(delta, delta), g = HydrantGravity, v = HydrantJetSpeed;
	const float a = v * v - g * delta.y, disc = a * a - g * g * d2;
	if (d2 < 1e-6f || disc < 0 || a <= 0)
		return false;
	const float root = std::sqrt(disc), t2 = high ? 2 * (a + root) / (g * g) : 2 * d2 / (a + root);
	flight = std::sqrt(std::max(t2, 1e-6f));
	velocity = (delta + Vec{0, .5f * g * flight * flight, 0}) * (1 / flight);
	return true;
}
// 狙い (肩越しのカメラの照準の先): 始めの距離と一番近い距離 (体の大きさの平方根をかける)・一番遠い距離 (水が 45 度で届く 259 の少し内側)
inline constexpr float HoseAimStart = 30, HoseAimLeast = 10, HoseAimMost = 230;
// カメラ: 戦闘の消火栓の肩越しの位置から、さらに上げる高さと右へ寄せる幅 (体の大きさをかける)
inline constexpr float HoseCameraLift = 2.2f, HoseCameraSide = 1.2f;
// 絵の具: 塗った色と同じ色の物を飲むと増える量の倍率
inline constexpr float GimmickPaintBonus = 1.5f;
inline bool GimmickSameColor(float r0, float g0, float b0, float r1, float g1, float b1) {
	return std::abs(r0 - r1) + std::abs(g0 - g1) + std::abs(b0 - b1) < .12f;
}
} // namespace Game::Chrono
