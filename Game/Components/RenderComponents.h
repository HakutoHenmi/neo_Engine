// RenderComponents.h
// 見た目のコンポーネント: メッシュ・アニメーター・パーティクル・ライト・川・流体エミッター・プレイヤーの影。
// ObjectTypes.h がすべての Components/*.h をまとめて読み込む。
#pragma once
#include "ComponentBase.h"

namespace Game {

// メッシュを描画するための情報 (モデル・テクスチャ・シェーダー・色)
struct MeshRendererComponent : public Component {
	// Renderer が返すモデル/テクスチャのハンドル (0 は未読み込み)
	uint32_t modelHandle = 0;
	uint32_t textureHandle = 0;
	// 保存用のファイルパス (読み込み時にここからハンドルを作り直す)
	std::string modelPath;
	std::string texturePath;
	// 乗算色 (RGBA)
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	// マテリアルとライトマップの設定 (UV の繰り返し・ずらし、焼き込み光)
	DirectX::XMFLOAT2 uvTiling = {1, 1};
	DirectX::XMFLOAT2 uvOffset = {0, 0};
	uint32_t lightmapHandle = 0;
	std::string lightmapPath;
	std::vector<uint32_t> extraTextureHandles; // 追加テクスチャのハンドル (法線マップなど)
	std::vector<std::string> extraTexturePaths; // 追加テクスチャのファイルパス
	std::string shaderName = "Default"; // 使用するシェーダー名
	float reflectivity = 0.0f; // 環境マップの反射率 (0.0〜1.0)
	bool useCubemap = true; // キューブマップを使うか (課題要件のため初期値は true)
	MeshRendererComponent() { type = ComponentType::MeshRenderer; }
};

// スキニングモデルのアニメーション再生状態
struct AnimatorComponent : public Component {
	std::string currentAnimation;
	float time = 0.0f;
	float speed = 1.0f;
	bool isPlaying = false;
	bool loop = true;
	bool drawSkeleton = false; // デバッグ用に骨 (スケルトン) を線で描くか
	// 直前のアニメーションと、切り替え時に混ぜる (クロスフェード) ための情報
	
	std::string prevAnimation;
	float prevTime = 0.0f;
	bool prevLoop = true;
	float crossfadeTimer = 0.0f;
	float crossfadeDuration = 0.0f;
	
	AnimatorComponent() { type = ComponentType::Animator; }
};

// CPU パーティクルの発生源
struct ParticleEmitterComponent : public Component {
	Engine::ParticleEmitter emitter;
	std::string assetPath = ""; // 保存されたパーティクル設定ファイル (.json) のパス
	bool isInitialized = false;

	ParticleEmitterComponent() { type = ComponentType::ParticleEmitter; }
};

// ライト (平行光源・点光源・スポットライト)
// 平行光源 (太陽光)
struct DirectionalLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	DirectionalLightComponent() { type = ComponentType::DirectionalLight; }
};

// 点光源
struct PointLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 10.0f;
	DirectX::XMFLOAT3 atten = {1.0f, 0.1f, 0.01f}; // 減衰係数 (一定・線形・二次)
	PointLightComponent() { type = ComponentType::PointLight; }
};

// スポットライト
struct SpotLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 20.0f;
	float innerCos = 0.98f; // 内側の角度の cos (ここまでは最大の明るさ)
	float outerCos = 0.90f; // 外側の角度の cos (ここで 0 になる)
	DirectX::XMFLOAT3 atten = {1.0f, 0.1f, 0.01f};
	SpotLightComponent() { type = ComponentType::SpotLight; }
};

// 川 (制御点を通るスプラインに沿って水面メッシュを作る)
struct RiverComponent : public Component {
	std::vector<DirectX::XMFLOAT3> points; // スプライン制御点 (ローカル座標)
	float width = 2.0f;                    // 川の基本幅
	float flowSpeed = 1.0f;                // 流れの速さ
	float uvScale = 1.0f;
	uint32_t meshHandle = 0;               // 動的生成メッシュハンドル
	std::string texturePath = "Resources/Water/water.png";
	RiverComponent() { type = ComponentType::River; }
};

// 流体エミッター (毎フレーム GPU 流体に粒子を追加する)
struct FluidEmitterComponent : public Component {
	int emitCountPerFrame = 400;
	float emissionRemainder = 0.0f; // 1フレームに出しきれなかった端数 (実行時専用)
	bool retainWater = false; // 水のプレビュー: 落ち着いた粒子をリセットまで残す
	int particleLimit = 0; // 出す粒子の上限 (0 は上限なしで出し続ける)
	int emittedParticles = 0; // 前回の流体リセットから出した数 (実行時専用)
	DirectX::XMFLOAT4 color = {0.2f, 0.8f, 1.0f, 1.0f}; // 湧き水の色
	DirectX::XMFLOAT3 velocity = {0.0f, -5.0f, 0.0f};
	float fluidType = 1.0f; // 流体の種類 (0 = プレイヤーの核, 1 = 水しぶき)
	
	FluidEmitterComponent() { type = ComponentType::FluidEmitter; }
};

// プレイヤーの丸影を追従させるための影エンティティ
struct PlayerShadowComponent : public Component {
	entt::entity shadowEntity = entt::null;
	PlayerShadowComponent() { type = static_cast<ComponentType>(999); }
};

} // namespace Game
