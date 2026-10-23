// RingEffectScript.cpp
// 衝撃波のリングの実装。
#include "RingEffectScript.h"
// シーン・コンポーネント・スクリプト登録の定義
#include "GameScene.h"
#include "../ObjectTypes.h"
#include "ScriptEngine.h"
// std::pow を使う
#include <cmath>

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

// 開始: 歪みシェーダーの板と、カメラを向いた向き・小さい大きさを用意する
void RingEffectScript::Start(entt::entity entity, GameScene* scene) {
	// このシーンの ECS
	auto& registry = scene->GetComponents();
	
	// 見た目が無ければ、板のモデルに波紋の法線マップを貼り、歪みシェーダーで描く
	if (!registry.all_of<MeshRendererComponent>(entity)) {
		auto& dmr = registry.emplace<MeshRendererComponent>(entity);
		dmr.modelPath = "Resources/Models/plane.obj";
		dmr.texturePath = "Resources/Textures/ripple_normal.png";
		dmr.shaderName = "Distortion";
		dmr.color = { 1.0f, 1.0f, 1.0f, 1.0f };

		// モデルとテクスチャを読み込んでハンドルを覚える
		auto* renderer = scene->GetRenderer();
		if (renderer) {
			dmr.modelHandle = renderer->LoadObjMesh(dmr.modelPath);
			dmr.textureHandle = renderer->LoadTexture2D(dmr.texturePath);
		}
	}
	
	// 位置・回転・大きさが無ければ、小さい大きさで付ける
	if (!registry.all_of<TransformComponent>(entity)) {
		auto& dtc = registry.emplace<TransformComponent>(entity);
		dtc.scale = { 0.1f, 0.1f, 0.1f };
		
		// カメラがあれば板をカメラの方へ向ける (板は寝ているので X 軸を 90 度起こす)
		auto* camera = &scene->GetCamera();
		if (camera) {
			dtc.rotate = camera->Rotation();
			dtc.rotate.x -= DirectX::XM_PIDIV2;
		} else {
			// カメラが無ければ回転なし
			dtc.rotate = { 0, 0, 0 };
		}
	}
}

// 更新: 減速しながら広げて透明にし、攻撃判定も同じ大きさに合わせる。時間が来たら削除する
void RingEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	// 経過時間を進める
	timer_ += dt;
	
	auto& registry = scene->GetComponents();
	// 位置と見た目の両方があるときだけ動かす
	if (registry.all_of<TransformComponent>(entity) && registry.all_of<MeshRendererComponent>(entity)) {
		auto& tc = registry.get<TransformComponent>(entity);
		auto& mr = registry.get<MeshRendererComponent>(entity);
		
		// 進み具合 (0〜1)
		float t = timer_ / duration_;
		if (t > 1.0f) t = 1.0f;
		
		// 最初は速く、だんだん遅く広がる (3 乗のイーズアウト)
		float easeT = 1.0f - std::pow(1.0f - t, 3.0f);
		// 開始の大きさから終わりの大きさへ広げる
		float s = startScale_ + (endScale_ - startScale_) * easeT;
		tc.scale = { s, s, s };
		// 広がるほど透明にする
		mr.color.w = 1.0f - easeT;

		// ビルボードなら毎フレームカメラの方へ向け直す
		auto* camera = &scene->GetCamera();
		if (isBillboard_ && camera) {
			tc.rotate = camera->Rotation();
			tc.rotate.x -= DirectX::XM_PIDIV2;
		}
		
		// 攻撃判定があれば、横幅と奥行きをリングの直径に合わせる
		if (registry.all_of<HitboxComponent>(entity)) {
			auto& hb = registry.get<HitboxComponent>(entity);
			hb.size.x = s * 2.0f;
			hb.size.z = s * 2.0f;
		}
	}

	// 時間が来たら自分を削除する
	if (timer_ >= duration_) {
		if (scene) {
			scene->DestroyObject(static_cast<uint32_t>(entity));
		}
	}
}

// 破棄時の処理は無い / パラメータは保存しない
void RingEffectScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

std::string RingEffectScript::SerializeParameters() {
	return "{}";
}

void RingEffectScript::DeserializeParameters(const std::string& /*data*/) {
}

// ScriptEngine へ自動登録する
REGISTER_SCRIPT(RingEffectScript);

} // namespace Game
