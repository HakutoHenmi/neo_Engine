// GameSceneEntities.cpp
// GameScene のエンティティの管理: 選択、生成と破棄、名前での検索、地面の高さとレイキャスト、
// ワールド行列 (親子関係)、タグ検索のキャッシュ、シーンの全消去。
#include <cfloat>
// シーンの BGM
#include "../UI/SceneMusic.h"
// Windows のヘッダーの min / max マクロを止める
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
// プレイヤー向けの UI (共通部品・強化カード・グラフィック設定)
#include "../UI/GameUI.h"
#include "../UI/RogueliteUI.h"
#include "../UI/GraphicsUI.h"
// 流体の放出と時間の管理 (エンジン側)
#include "../../Engine/FluidEmission.h"
#include "../../Engine/Time/TimeManager.h"
// 缶の装備 (旧アクションゲーム)
#include "../CanLoadout.h"
#include "../ObjectTypes.h"
// 音・パス・シーン管理 (エンジン側)
#include "../../Engine/Audio.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
// エディタとスクリプト
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Scripts/ScriptLifecycle.h"
// ゲームの処理を分担するシステム群
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/FluidSystem.h"
#include "../Systems/WeaponSystem.h" // 武器の管理
#include "../../Engine/Input.h"      // キーボード・マウス入力

#include "../Systems/HealthSystem.h"
#include "../Systems/MotionSystem.h" // キーフレームのモーション
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/PlayerActionSystem.h" // プレイヤーアクション
#include "../Systems/CombatSystem.h"       // 戦闘の当たり判定
#include "../Systems/CanAbilitySystem.h"
#include "../Systems/EnemyAISystem.h"      // 敵の AI
#include "../Systems/BossActionSystem.h"    // ボスの AI
#include "../Systems/WaveSystem.h"         // ウェーブ管理
#include "../Scripts/BossTestScript.h"      // ボスの生成に使う

#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "../Systems/PostProcessSystem.h" // ポストエフェクト
// Web プロファイラ (外部ツールから値を見たり変えたりする)
#include "../../Engine/NetworkProfiler.h"
// JSON の読み書き
#include "../../externals/nlohmann/json.hpp"
#include "../../externals/imgui/imgui.h" // ImGui (エディタの画面)
#include <Windows.h> // デバッグ出力 (OutputDebugStringA) を使うため
#include <algorithm>
#include <cmath>
#include <filesystem> // 空のテクスチャ検索などのファイル操作

namespace Game {

// 選択を解除する
void GameScene::ClearSelection() {
    // 主な選択と、複数選択の一覧を空にする
    selectedEntity_ = entt::null;
    selectedEntities_.clear();
}

// 1つだけ選択する (無効なエンティティなら解除のみ)
void GameScene::SelectEntity(entt::entity entity) {
    ClearSelection();
    // 存在すれば選ぶ
    if (registry_.valid(entity)) {
        selectedEntity_ = entity;
        selectedEntities_.insert(entity);
    }
}

// 名前もトランスフォームも付けない空のエンティティを作る
entt::entity GameScene::CreateEmptyEntity() {
    // 生成・破棄の予約と同時に触らないようロックする
    std::lock_guard<std::mutex> lock(spawnMutex_);
    return registry_.create();
}

// エディタ用の一時エンティティ (プレビューなど) を即座に削除する
void GameScene::DestroyEditorEntity(entt::entity entity) {
    RemoveEntity(entity);
}

// 即座に削除する。選択中なら選択から外し、行列のキャッシュも捨てる
void GameScene::RemoveEntity(entt::entity entity) {
    // 存在しなければ何もしない
    if (!registry_.valid(entity)) return;
    // 選択から外し、主な選択が消えたら残りの先頭にする
    selectedEntities_.erase(entity);
    if (selectedEntity_ == entity) selectedEntity_ = selectedEntities_.empty() ? entt::null : *selectedEntities_.begin();
    // ECS から消し、行列のキャッシュを捨てる
    registry_.destroy(entity);
    ClearMatrixCache();
}

// エディタの Game ビューの位置・大きさと、内部解像度に換算したマウス座標を保存する
void GameScene::SetEditorViewport(DirectX::XMFLOAT2 offset, DirectX::XMFLOAT2 size, float mouseX, float mouseY) {
    ctx_.viewportOffset = offset;
    ctx_.viewportSize = size;
    ctx_.overrideMouseX = mouseX;
    ctx_.overrideMouseY = mouseY;
    ctx_.useOverrideMouse = true;
}

// エンティティを生成する (名前とトランスフォームを付ける)
entt::entity GameScene::CreateEntity(const std::string& name) {
	// 生成・破棄の予約と同時に触らないようロックする
	std::lock_guard<std::mutex> lock(spawnMutex_);
	auto entity = registry_.create();
	// 名前とトランスフォームを付ける
	registry_.emplace<NameComponent>(entity, name);
	registry_.emplace<TransformComponent>(entity);
	return entity;
}

// ID を指定して破棄を予約する
void GameScene::DestroyObject(uint32_t id) {
	// 生成・破棄の予約と同時に触らないようロックする
	std::lock_guard<std::mutex> lock(spawnMutex_);
	// ID をそのまま entt::entity として扱う
	pendingDestroys_.push_back(static_cast<entt::entity>(id));
}

// 名前でエンティティを探す
entt::entity GameScene::FindObjectByName(const std::string& name) {
	// 名前を持つエンティティを順に調べる
	auto view = registry_.view<NameComponent>();
	for (auto entity : view) {
		if (view.get<NameComponent>(entity).name == name) {
			return entity;
		}
	}
	// 見つからなければ無し
	return entt::null;
}

// 指定座標の真下にあるメッシュ表面の高さを求める
float GameScene::GetHeightAt(float x, float z, float startY, uint32_t excludeId) {
	// 当たった中で一番高い面の高さ。何にも当たらなければ -10000
	float maxHeight = -1000.0f;
	bool hitAny = false;

	// startY (既定 1000) から真下へレイを飛ばす
	DirectX::XMVECTOR rayPos = DirectX::XMVectorSet(x, startY, z, 1.0f);
	DirectX::XMVECTOR rayDir = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

	// 位置を持つエンティティを順に調べる
	auto view = registry_.view<TransformComponent>();
	for (auto entity : view) {
		// 除外するもの (自分自身など) は飛ばす
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		// 敵・弾・プレイヤー・エフェクトは地面として扱わない
		bool isEnemyOrBullet = false;
		if (registry_.all_of<TagComponent>(entity)) {
			// タグを調べる
			const auto tag = registry_.get<TagComponent>(entity).tag;
			if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Projectile || tag == TagType::VFX) {
				isEnemyOrBullet = true;
			}
		}
		if (isEnemyOrBullet)
			continue;

		// 当たり判定用のメッシュがあればそれを使い、無ければ描画用のメッシュを使う
		uint32_t modelHandle = 0;
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
			if (mc.enabled)
				modelHandle = mc.meshHandle;
		}
		if (modelHandle == 0 && registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled)
				modelHandle = mr.modelHandle;
		}

		// メッシュが無ければ飛ばす
		if (modelHandle == 0)
			continue;

		// モデルの実体 (無ければ飛ばす)
		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		// 当たった距離・位置と、このエンティティのワールド行列
		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = this->GetWorldMatrix(static_cast<int>(entity));

		// 当たったら、一番高い位置を覚える
		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (hitPoint.y > maxHeight) {
				maxHeight = hitPoint.y;
				hitAny = true;
			}
		}
	}

	// 当たったものがあればその高さ、無ければ -10000
	return hitAny ? maxHeight : -10000.0f;
}

// レイキャスト: 地形などのメッシュに当たった最も近い距離を返す (敵・弾・プレイヤーなどは無視)
bool GameScene::RayCast(const Engine::Vector3& origin, const Engine::Vector3& direction, float maxDist, uint32_t excludeId, float& outDist) {
	// 当たったか・一番近い距離 (最初は最大距離)
	bool hitAny = false;
	float minDist = maxDist;

	// 始点と向きを DirectXMath の形にする
	DirectX::XMVECTOR rayPos = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&origin));
	DirectX::XMVECTOR rayDir = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&direction));

	// 位置を持つエンティティを順に調べる
	auto view = registry_.view<TransformComponent>();
	for (auto entity : view) {
		// 除外するもの (自分自身など) は飛ばす
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		// タグで対象外のものを除く
		if (registry_.all_of<TagComponent>(entity)) {
			const auto tag = registry_.get<TagComponent>(entity).tag;
			if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Projectile || tag == TagType::VFX)
				continue;
		}

		// 当たり判定用のメッシュがあればそれを使い、無ければ描画用のメッシュを使う
		uint32_t modelHandle = 0;
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
			if (mc.enabled)
				modelHandle = mc.meshHandle;
		}
		if (modelHandle == 0 && registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled)
				modelHandle = mr.modelHandle;
		}
		if (modelHandle == 0)
			continue;

		// モデルの実体 (無ければ飛ばす)
		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		// 当たった距離・位置と、このエンティティのワールド行列
		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = GetWorldMatrix(static_cast<int>(entity));

		// 当たったら、一番近い距離を覚える
		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (dist < minDist) {
				minDist = dist;
				hitAny = true;
			}
		}
	}

	// 当たったものがあれば距離を返す
	if (hitAny) {
		outDist = minDist;
		return true;
	}
	return false;
}

// エンティティのワールド行列 (親子関係を含む)
Engine::Matrix4x4 GameScene::GetWorldMatrix(int entityId) const { return GetWorldMatrixRecursive(static_cast<entt::entity>(entityId), 0); }

// ワールド行列を親へたどって計算する (深さ 32 を超える循環は単位行列で打ち切り、結果はフレーム内でキャッシュする)
Engine::Matrix4x4 GameScene::GetWorldMatrixRecursive(entt::entity e, int depth) const {
	// 深すぎるときは循環しているとみなして打ち切る
	if (depth > 32)
		return Engine::Matrix4x4::Identity();

	// このフレームで計算済みならそれを返す
	auto it = matrixCache_.find(e);
	if (it != matrixCache_.end())
		return it->second;

	// 存在しないか位置が無ければ単位行列
	if (!registry_.valid(e) || !registry_.all_of<TransformComponent>(e))
		return Engine::Matrix4x4::Identity();
	// 自分のローカル行列
	const auto& tc = registry_.get<TransformComponent>(e);
	Engine::Matrix4x4 local = tc.ToMatrix();

	Engine::Matrix4x4 world = local;
	// 親があれば親のワールド行列を掛ける
	if (registry_.all_of<HierarchyComponent>(e)) {
		const auto& hc = registry_.get<HierarchyComponent>(e);
		if (hc.parentId != entt::null && registry_.valid(hc.parentId)) {
			world = Engine::Matrix4x4::Multiply(local, GetWorldMatrixRecursive(hc.parentId, depth + 1));
		}
	}
	// キャッシュに入れて返す
	matrixCache_[e] = world;
	return world;
}

// =====================================================
// タグ検索の実装
// =====================================================

// タグでエンティティの一覧を返す (無ければ空の一覧)
const std::vector<entt::entity>& GameScene::GetEntitiesByTag(TagType tag) {
	// 見つからないときに返す空の一覧
	static const std::vector<entt::entity> emptyResult;
	// タグの一覧を探す
	auto it = tagCache_.find(tag);
	if (it != tagCache_.end()) {
		return it->second;
	}
	return emptyResult;
}

// タグを設定する (キャッシュへの反映は次の Update で行う)
void GameScene::SetTag(entt::entity entity, TagType tag) {
	// 存在しなければ何もしない
	if (!registry_.valid(entity)) {
		return;
	}
	// タグのコンポーネント (無ければ付ける) に書き込む
	auto& tc = registry_.get_or_emplace<TagComponent>(entity);
	tc.tag = tag;
	// すぐには同期せず、同期待ちの一覧に入れる
	pendingTagSync_.push_back(entity);
}

// タグが付いたとき
void GameScene::OnTagAdded(entt::registry& /*registry*/, entt::entity entity) {
	// すぐには同期せず、次の Update で同期する
	pendingTagSync_.push_back(entity);
}

// タグが外れたとき
void GameScene::OnTagRemoved(entt::registry& /*registry*/, entt::entity entity) {
	// すぐには消さず、次の Update の最初に消す
	// (反復処理の途中でコンテナを書き換えて壊さないため)
	pendingTagRemoved_.push_back(entity);
}

// スクリプトが外れたとき
void GameScene::OnScriptDestroyed(entt::registry& registry, entt::entity entity) {
	// スクリプトが破棄されるときに C++ スクリプトの OnDestroy を呼ぶ
	// スクリプトのコンポーネントがあれば止める
	if (auto* sc = registry.try_get<ScriptComponent>(entity)) {
		StopScripts(*sc, entity, this);
	}
}

// タグのキャッシュにエンティティを追加する
void GameScene::SyncTag(entt::entity entity) {
	// 存在しないかタグが無ければ何もしない
	if (!registry_.valid(entity) || !registry_.all_of<TagComponent>(entity)) {
		return;
	}

	// 新しいタグの一覧に追加する (古い一覧からの削除は Update の最初にまとめて行う前提)
	const TagType tag = registry_.get<TagComponent>(entity).tag;
	
	// 1件ずつ重複を調べると遅いので、Update 側の削除を信頼する
	tagCache_[tag].push_back(entity);
}

// 文字列のタグで探す / 設定する
const std::vector<entt::entity>& GameScene::GetEntitiesByTag(const std::string& tag) {
	return GetEntitiesByTag(StringToTag(tag));
}

void GameScene::SetTag(entt::entity entity, const std::string& tagStr) {
	// 文字列をタグの列挙値に変換して設定する
	SetTag(entity, StringToTag(tagStr));
}

// 流体のプレビューを初期状態にする (GPU 流体を空にし、エミッターの放出数を 0 に戻す)
void GameScene::ResetFluidPreview() {
	// GPU 流体を空にする
	if (renderer_) renderer_->ResetGPUFluid();
	gpuSlimeEmitted_ = false;
	// 流体エミッターの出した数と端数を 0 に戻す
	auto emitters = registry_.view<FluidEmitterComponent>();
	for (auto entity : emitters) {
		auto& emitter = emitters.get<FluidEmitterComponent>(entity);
		emitter.emittedParticles = 0;
		emitter.emissionRemainder = 0;
	}
}

// シーンを完全に空にする
void GameScene::ClearScene() {
	gpuSlimeEmitted_ = false;
    // 選択を外し、止めていた世界を動かす
    ClearSelection();
    if(renderer_)renderer_->SetRogueWorldFrozen(false);
	// すべてのスクリプトを止める
	StopAllScripts(registry_, this);
	// 1. 各システムの状態をリセットする
	for (auto& sys : systems_) {
		sys->Clear(registry_);
	}

	// 2. 予約やキャッシュを全部消す
	{
		std::lock_guard<std::mutex> lock(spawnMutex_);
		pendingDestroys_.clear();
		pendingSpawns_.clear();
		pendingTagSync_.clear();
		pendingTagRemoved_.clear();
		tagCache_.clear();
		matrixCache_.clear();
	}

	// 3. レジストリを空にする
	// 全消去の間は OnDestroy などのシグナルで二重に破棄しないよう (反復処理の破壊を防ぐ)、シグナルを一時的に外す
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);

	registry_.clear();

	// シグナルをつなぎ直す
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().connect<&GameScene::OnScriptDestroyed>(this);

	// 4. CPU 側スライムメッシュを未初期化に戻す
	slimeCpuLogic_.initialized = false;
	projectileCpuLogic_.initialized = false;

	if (renderer_) {
		// GPU 流体を空にする
		renderer_->ClearGPUFluid();
	}
}

// ポーズメニューは共通の UI キャンバスで描くので、ここでは何も作らない
void GameScene::CreatePauseMenu() { /* 共通の UI キャンバスで描くので、ここでは何もしない */ }

// ポーズメニュー用のエンティティを破棄する
void GameScene::DestroyPauseMenu() {
	// ポーズメニューのタグを持つものを破棄する
	const auto& pauseUIs = GetEntitiesByTag(TagType::PauseUI);
	for (auto e : pauseUIs) {
		if (registry_.valid(e)) {
			DestroyObject(static_cast<uint32_t>(e));
		}
	}
}

} // namespace Game
