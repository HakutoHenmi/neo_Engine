#pragma once
// ===============================
//  EmissionShapes : パーティクルの放出形状 (Strategy Pattern)
//  - 放出位置と初速の決め方を形状ごとのクラスに分け、ParticleEmitter は IEmissionShape 経由で呼ぶ。
//  - 形状を足すときはクラスを 1 つ追加し EmissionShapeFor に登録するだけで、放出処理 (EmitBurst) は変えない。
//  - 形状オブジェクトはデータを持たない不変の共有インスタンス。半径・角度などは呼び出しごとに渡す。
// ===============================
#include "Matrix4x4.h"
#include <cmath>

namespace Engine {

// 放出形状の種類 (JSON・エディタに保存する値)
enum class EmissionShape {
	Point = 0,
	Sphere = 1,
	Cone = 2
};

// 形状の大きさ (SphereやConeの底面半径 / Coneの広がり角 [ラジアン])
struct EmissionShapeParams {
	float radius = 1.0f;
	float angle = 0.5f;
};

// [min, max] の乱数を返す関数 (エミッターの乱数源を使う)
using EmissionRandom = float (*)(float min, float max);

// 放出形状のインターフェース。position / velocity には基準の値が入っており、形状に合わせて書き換える
class IEmissionShape {
public:
	virtual ~IEmissionShape() = default;
	virtual void Shape(const EmissionShapeParams& params, EmissionRandom random, Vector3& position, Vector3& velocity) const = 0;
};

// 点: 基準の位置・初速のまま
class PointEmission final : public IEmissionShape {
public:
	void Shape(const EmissionShapeParams&, EmissionRandom, Vector3&, Vector3&) const override {}
};

// 球: 球の内部の一様な位置から、外向きに少しブレた初速で出す
class SphereEmission final : public IEmissionShape {
public:
	void Shape(const EmissionShapeParams& params, EmissionRandom random, Vector3& position, Vector3& velocity) const override {
		const float theta = random(0, 3.14159265f * 2.0f);
		const float phi = std::acos(random(-1.0f, 1.0f));
		const float r = params.radius * std::pow(random(0.0f, 1.0f), 1.0f / 3.0f);
		const Vector3 offset = {r * std::sin(phi) * std::cos(theta), r * std::sin(phi) * std::sin(theta), r * std::cos(phi)};
		position.x += offset.x; position.y += offset.y; position.z += offset.z;
		velocity.x += offset.x * 0.5f; velocity.y += offset.y * 0.5f; velocity.z += offset.z * 0.5f;
	}
};

// 円錐: 上向きの円錐の中の方向へ、初速の大きさを保って出す。位置は底面の円の中
class ConeEmission final : public IEmissionShape {
public:
	void Shape(const EmissionShapeParams& params, EmissionRandom random, Vector3& position, Vector3& velocity) const override {
		const float angle = random(0, params.angle);
		const float theta = random(0, 3.14159265f * 2.0f);
		const float r = params.radius * std::tan(angle);
		Vector3 dir = {r * std::cos(theta), 1.0f, r * std::sin(theta)};
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		dir.x /= len; dir.y /= len; dir.z /= len;
		const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
		velocity = {dir.x * speed, dir.y * speed, dir.z * speed};
		const float baseR = params.radius * std::sqrt(random(0, 1));
		position.x += baseR * std::cos(theta);
		position.z += baseR * std::sin(theta);
	}
};

// 種類から形状を選ぶ。未知の値 (壊れた JSON など) は点として扱う
inline const IEmissionShape& EmissionShapeFor(EmissionShape shape) {
	static const PointEmission point;
	static const SphereEmission sphere;
	static const ConeEmission cone;
	switch (shape) {
	case EmissionShape::Sphere: return sphere;
	case EmissionShape::Cone: return cone;
	default: return point;
	}
}

} // namespace Engine
