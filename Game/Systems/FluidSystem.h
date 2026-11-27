// FluidSystem.h
// 旧スライムの GPU 流体 (SPH) のシミュレーションと描画。
// 粒子をコンピュートシェーダーで動かし、点の板ポリゴンとして描く。SSFR 用のテクスチャも用意する。
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "d3dx12.h"
#include <vector>
#include <random>
#include <cmath>
#include "Math/Matrix4x4.h"
#include "../../Engine/Graphics/Renderer.h"
#include "../../Engine/Core/PathUtils.h"
#include <d3dcompiler.h>

namespace Game {

// 流体の粒子 1 個 (GPU のバッファと同じ並び)
struct FluidParticle {
	// 位置と密度
	DirectX::XMFLOAT3 position;
	float density;
	// 速度と圧力
	DirectX::XMFLOAT3 velocity;
	float pressure;
	// 力 (詰め物で 16 バイトにそろえる)
	DirectX::XMFLOAT3 force;
	float pad1;
	DirectX::XMFLOAT3 restPosition; // 形を覚えるための元の位置
	float pad2;
};

// 定数バッファの 256 バイトの整列で出る警告を止める
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
// シミュレーション用の定数 (コンピュートシェーダーへ渡す)
struct alignas(256) FluidConstants {
	// 核の位置と粒子の数
	DirectX::XMFLOAT3 corePosition;
	uint32_t numParticles;

	// 時間の刻み・影響の半径・粒子の質量・目標の密度
	float deltaTime;
	float smoothingLength;
	float particleMass;
	float restDensity;

	// 圧力の強さ・粘性・重力・減衰
	float gasStiffness;
	float viscosity;
	float gravity;
	float damping;

	// 床の高さ・計算のパス (0: 密度, 1: 力)・モード
	float floorWorldY;
	uint32_t passType;
	uint32_t simMode; // 0: スライム球, 1: 液状化
	float pad0;
	// 塊の半径
	DirectX::XMFLOAT3 blobRadii;
	float pad1;
	DirectX::XMFLOAT3 playerInputForce; // プレイヤーから加える外力
	float pad2;
};

// 描画用の定数 (カメラの行列・時刻・中心・液状化中か・色)
struct alignas(256) FluidRenderConstants {
	// ビュー・射影・ビュー射影・それぞれの逆行列
	Engine::Matrix4x4 view;
	Engine::Matrix4x4 projection;
	Engine::Matrix4x4 viewProj;
	Engine::Matrix4x4 invProjection;
	Engine::Matrix4x4 invView;
	// カメラの位置と時刻
	DirectX::XMFLOAT3 cameraPos;
	float time;
	// 核の位置と液状化中か
	DirectX::XMFLOAT3 corePosition;
	float isLiquidated;
	// スライムの色
	DirectX::XMFLOAT3 blobColor;
	float padColor;
};

// 粒子を作り直すときの形 (スライムの塊 / 水たまり)
enum class FluidResetShape { Blob, Puddle };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// 流体の管理 (シングルトン)
class FluidSystem {
public:
	// 唯一のインスタンス
	static FluidSystem* GetInstance() {
		static FluidSystem instance;
		return &instance;
	}

	// SSFR 用の深度・厚み・なめらかにした深度のテクスチャ
	// SSFR用のリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> ssfrDepthTex_;
	Microsoft::WRL::ComPtr<ID3D12Resource> ssfrThicknessTex_;
	Microsoft::WRL::ComPtr<ID3D12Resource> ssfrSmoothedDepthTex_;
	Microsoft::WRL::ComPtr<ID3D12Resource> ssfrDsvTex_; // 深度テスト用のバッファ

	// SSFR用のディスクリプタヒープ (RTV/SRV/DSV)
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ssfrRtvHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ssfrSrvHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ssfrDsvHeap_; // 深度テスト用のヒープ
	// ディスクリプタの大きさ
	uint32_t rtvDescriptorSize_ = 0;
	uint32_t srvDescriptorSize_ = 0;

	// 画面サイズ（初期化時に設定）
	uint32_t screenWidth_ = 1920;
	uint32_t screenHeight_ = 1080;

	// 初期化: ルートシグネチャ・シェーダー・パイプライン・粒子と定数のバッファを作る
	bool Initialize(ID3D12Device* device);

	// スライムの形 (広く平らな裾野と小さな頭) の中に粒子を無作為に置く
	void FillLocalBlobParticles(std::vector<FluidParticle>& out, DirectX::XMFLOAT3 radii);

	// 水たまりの形 (平たい円) に粒子を置き、少し下向きの速度を付ける
	void FillLocalPuddleParticles(std::vector<FluidParticle>& out, DirectX::XMFLOAT3 radii);

	// 指定の形で粒子を作り直し、アップロード用バッファへ書く
	void RebuildParticles(FluidResetShape shape, DirectX::XMFLOAT3 radii);

	// 毎フレームのシミュレーション: 必要なら粒子を作り直し、密度と圧力 → 力と移動の2回に分けて計算する
	void Update(
	    ID3D12GraphicsCommandList* list, float dt, const DirectX::XMFLOAT3& corePos, DirectX::XMFLOAT3 blobRadii,
	    bool isLiquidated, DirectX::XMFLOAT3 inputForce = {0.0f, 0.0f, 0.0f});

	// 粒子を板ポリゴンとしてメイン画面に描く
	void Draw(
	    ID3D12GraphicsCommandList* list, Engine::Camera* camera, float time, DirectX::XMFLOAT3 corePos,
	    DirectX::XMFLOAT3 blobRadii, bool isLiquidated, D3D12_CPU_DESCRIPTOR_HANDLE mainRtv,
	    D3D12_CPU_DESCRIPTOR_HANDLE mainDsv);

	// 次の更新で粒子を指定の形に作り直す
	void RequestParticleReset(FluidResetShape shape, DirectX::XMFLOAT3 radii);

	// 粒子のバッファ・粒子の数・初期化済みか
	ID3D12Resource* GetParticleBuffer() const {
		return particleBuffer_.Get();
	}
	uint32_t GetNumParticles() const {
		return numParticles_;
	}
	bool IsInitialized() const {
		return isInitialized_;
	}

private:
	FluidSystem() = default;

	// デバイス・シミュレーションと描画のパイプライン
	ID3D12Device* device_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigRender_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoRender_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigComposite_; // 合成用のルートシグネチャ
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoComposite_;     // 合成用のパイプライン

	// 粒子のバッファ・アップロード用バッファ・定数バッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> particleUploadBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> cbUploadBuffer_;
	FluidConstants* cbMapped_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> cbRenderUploadBuffer_;
	FluidRenderConstants* cbRenderMapped_ = nullptr;

	uint32_t numParticles_ = 2048; // O(N^2) の重い計算を避けるため粒子の数を減らしている
	// 初期化済みか・最初のフレームか・作り直しの依頼
	bool isInitialized_ = false;
	bool firstFrame_ = true;
	bool resetRequested_ = false;
	FluidResetShape resetShape_ = FluidResetShape::Blob;
	DirectX::XMFLOAT3 resetRadii_ = {1.2f, 0.85f, 1.2f};

	// SSFR (画面空間での流体描画) 用のテクスチャとビューを作る
	void CreateSSFRResources();
};

} // namespace Game
