#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include <list>
#include <string>
#include <vector>

// 移動のタイプ
enum MoveType {
	Walk,
	Fly,
};

// ターゲットにするオブジェクトのタイプ
enum TargetType {
	// 単体オブジェクト
	Player,
	Core,

	// 複数存在し得るオブジェクト
	Defender,
};

// 複数ターゲットを選ぶ基準
enum TargetPriority {
	Near,
	Far,
};

namespace Game {

class EnemyBehavior : public IScript {
public:
	// 初期化処理（シーン開始時に1回呼ばれる）
	void Start(entt::entity entity, GameScene* scene) override;

	// 毎フレーム処理
	void Update(entt::entity entity, GameScene* scene, float dt) override;

	// オブジェクト破棄時の処理
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	// ImGuiでパラメータをいじる
	void OnEditorUI() override;

	// パラメーターの個別保存・読み込み用 (エディター用)
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	// ターゲットを検索して更新する関数
	void SearchTarget(entt::entity entity, GameScene* scene);

	// オブジェクトの移動処理
	void Move(entt::entity entity, GameScene* scene, float dt);

	// デバッグ情報表示
	void Debug();

private: // メンバ変数
	// 自身の情報を持たせておく
	uint32_t ownerId_ = 0; // 自分のオブジェクトのID
	GameScene* pCurrentScene_ = nullptr; // 現在のシーンへのポインタ

	// 動きのタイプ
	MoveType type_ = Walk;	// とりあえず初期値はWalk

	// 追尾するオブジェクトのタイプ
	TargetType targetType_ = Player;	// 初期値はPlayer

	// 複数ターゲットから選ぶ
	TargetPriority priority_ = Near;	// 初期値はNear

	// 参照するObjectのポインタと位置
	// 初期値はPlayer
	std::string targetName_ = "Player";	// 一旦初期値をPlayerに
	uint32_t targetId_ = 0;
	DirectX::XMFLOAT3 myPos_ = {};
	float groundHeight_ = 0.0f;	// FlyTypeが地面の高さを取るため
	DirectX::XMFLOAT3 targetPos_ = {}; // 移動速度
	float totalTime_ = 0.0f;
	float speed_ = 2.0f;
	float separationRadius_ = 1.5f; // 近づきすぎないための半径
	float separationWeight_ = 2.0f; // 離れる力の強さ
	float scanTimer_ = 0.0f; // ★追加: 走査の頻度を下げるためのタイマー
	bool showDebugGrid_ = true; // ★追加: 重いデバッグ表示を制御
};

} // namespace Game
