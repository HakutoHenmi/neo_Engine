// Engine/Renderer.h
#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <array>
#include <memory> // std::shared_ptr のため
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

// テキスト描画用
#include "DynamicGlyphCache.h"

#include <d3d12.h>
#include <wrl/client.h>

#include "../Camera/Camera.h"
#include "../Math/Matrix4x4.h"
#include "../Math/Transform.h"
#include "../Core/WindowDX.h"

// Modelクラスを利用するためインクルード
#include "Model.h"

namespace Game {
struct BoxColliderComponent;
struct ContactInfo;
} // namespace Game

namespace Engine {

class Renderer final {
public:
	std::array<uint32_t, 2> DynamicSrvFrameRange() const {
		return {srvDynamicEnd_ - WindowDX::kSrvDynamicCountPerFrame, srvDynamicEnd_};
	}
	using MeshHandle = uint32_t;
	using TextureHandle = uint32_t;

	struct SpriteDesc {
		float x = 0.0f;
		float y = 0.0f;
		float w = 64.0f;
		float h = 64.0f;
		float rotationRad = 0.0f;
		Vector4 color{1, 1, 1, 1};
		Vector4 uvScaleOffset{1.0f, 1.0f, 0.0f, 0.0f}; // UVスケール・オフセット
		int layer = 0;                                 // 描画レイヤー（大きいほど手前）
	};

	struct CollisionRequest {
		Matrix4x4 worldA;
		Matrix4x4 worldB;
		Vector3 obbCenter;
		float _pad0;
		Vector3 obbExtents;
		float _pad1;
		Vector3 obbAxisX;
		float _pad2;
		Vector3 obbAxisY;
		float _pad3;
		Vector3 obbAxisZ;
		float _pad4;
		uint32_t resultIndex;
		uint32_t numBvhNodes;
		uint32_t meshB; // グルーピング用
		uint32_t vertexCount;
		uint32_t indexCount;
		uint32_t bvhIndexCount;
		uint32_t _pad5;
		uint32_t _pad6;
	};

	// --- ライト構造体 ---
	struct DirectionalLight {
		Vector3 direction{0, -1, 0};
		float _pad0;
		Vector3 color{1, 1, 1};
		float _pad1;
		uint32_t enabled = 0;
		float _pad2[3];
	};

	struct PointLight {
		Vector3 position{0, 0, 0};
		float _pad0;
		Vector3 color{1, 1, 1};
		float range = 10.0f;
		Vector3 atten{1.0f, 0.1f, 0.01f};
		float _pad1;
		uint32_t enabled = 0;
		float _pad2[3];
	};

	struct SpotLight {
		Vector3 position{0, 0, 0};
		float _pad0;
		Vector3 direction{0, -1, 0};
		float range = 20.0f;
		Vector3 color{1, 1, 1};
		float innerCos = 0.98f;
		Vector3 atten{1.0f, 0.1f, 0.01f};
		float outerCos = 0.90f;
		uint32_t enabled = 0;
		float _pad[3];
	};

	struct AreaLight {
		Vector3 position{0, 0, 0};
		float _pad0;
		Vector3 color{1, 1, 1};
		float range = 10.0f;
		Vector3 right{1, 0, 0};
		float halfWidth = 1.0f;
		Vector3 up{0, 1, 0};
		float halfHeight = 1.0f;
		Vector3 direction{0, 0, 1};
		float _pad1;
		Vector3 atten{1.0f, 0.1f, 0.01f};
		float _pad2;
		uint32_t enabled = 0;
		float _pad3[3];
	};

	static constexpr int kMaxDirLights = 1;
	static constexpr int kMaxPointLights = 4;
	static constexpr int kMaxSpotLights = 4;
	static constexpr int kMaxAreaLights = 4;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
	struct alignas(256) LightCB {
		Vector3 ambientColor{0.1f, 0.1f, 0.1f};
		float _pad0 = 0.0f;

		DirectionalLight dirLights[kMaxDirLights];
		PointLight pointLights[kMaxPointLights];
		SpotLight spotLights[kMaxSpotLights];
		AreaLight areaLights[kMaxAreaLights];

		Matrix4x4 shadowMatrix; // シャドウマッピング用行列
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	struct PostProcessParams {
		float time = 0.0f;
		float noiseStrength = 0.0f;
		float distortion = 0.0f;
		float chromaShift = 0.0f;
		float vignette = 0.0f;
		float scanline = 0.0f;
		float san = 0.0f;
		float grow = 0.0f; // 大きい物を飲んで育っている強さ (ChronoFocus の鼓動の膨らみと金色の縁)
		float dofFocus = 30.f, dofRange = 30.f, dofStrength = 0.f, dofBossDepth = -1000.f;
		bool cinematicCamera = false;
	};
	struct GraphicsSettings {
		float bloom = .16f, lensFlare = .035f, grading = .55f, ambientOcclusion = .5f, motionBlur = .2f, dof = 1.f,
		      exposure = 1.05f;
		bool dlssQuality = false, rtShadows = false, rtReflections = false, rtIndirect = false;
	};
	void ApplyGraphicsSettings(GraphicsSettings settings);
	void SetDlssQuality(bool enabled);
	void SetRtShadows(bool enabled);
	void SetRtReflections(bool enabled);
	void SetRtIndirect(bool enabled);
	const GraphicsSettings& GetGraphicsSettings() const {
		return graphicsSettings_;
	}
	bool SaveGraphicsSettings() const;
	void LoadGraphicsSettings();
	bool SupportsRayTracing() const;
	bool RtShadowsAvailable() const {
		return bool(psoRtShadow_);
	}
	bool RtLightingAvailable() const {
		return bool(psoRtLighting_);
	}
	uint32_t RtLightingFrames() const {
		return rtLightingFrames_;
	}
	uint32_t RtReflectionPixels() const {
		return rtReflectionPixels_;
	}
	uint32_t RtIndirectPixels() const {
		return rtIndirectPixels_;
	}
	uint32_t RtReflectionHits() const {
		return rtReflectionHits_;
	}
	uint32_t RtIndirectHits() const {
		return rtIndirectHits_;
	}
	uint32_t RtShadowFrames() const {
		return rtShadowFrames_;
	}
	uint32_t RtShadowPixels() const {
		return rtShadowPixels_;
	}
	uint32_t RtLitPixels() const {
		return rtLitPixels_;
	}
	bool SupportsDlss() const;
	bool DlssActive() const {
		return dlssActive_ && SupportsDlss();
	}
	// 小さい画像からのアップスケール (QUALITY)。そうでなければ有効な DLSS は DLAA
	bool DlssUpscaling() const {
		return DlssActive() && (sceneWidth_ != WindowDX::kW || sceneHeight_ != WindowDX::kH);
	}
	uint32_t DlssEvaluatedFrames() const {
		return dlssEvaluatedFrames_;
	}
	uint32_t DlssHistoryResets() const {
		return dlssHistoryResets_;
	}
	std::string DlssStatus() const;

public:
	Renderer() = default;
	~Renderer();

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	// カスタム描画ジョブの遅延実行
	using CustomDrawJob = std::function<void(ID3D12GraphicsCommandList*)>;
	void SetCustomDrawJob(CustomDrawJob job) {
		customDrawJob_ = job;
	}

	void SetPostEffect(const std::string& name);

private:
	CustomDrawJob customDrawJob_ = nullptr;

public:
	bool Initialize(WindowDX* window);
	void Shutdown();

	void WaitGPU(); // 外部から安全な同期処理を呼べるようにする

	ID3D12Device* GetDevice() const {
		return dev_;
	}
	ID3D12GraphicsCommandList* GetCommandList() const {
		return list_;
	}
	static Renderer* GetInstance() {
		return instance_;
	}
	struct SceneryLodStats {
		uint64_t originalIndices = 0, selectedIndices = 0, originalVertices = 0, selectedVertices = 0,
		         visibleIndices = 0, shadowIndices = 0;
		std::array<uint32_t, 3> instances{};
		// 影のパスで描いた数 (1 つずつの描画・インスタンスの描画の呼び出し・インスタンスの数) と、その三角形の頂点番号の数
		uint32_t shadowDraws = 0, shadowInstancedCalls = 0, shadowInstances = 0;
		uint64_t shadowAllIndices = 0;
	};
	uint32_t RtTlasBuilds() const {
		return rtTlasBuilds_;
	}
	void PrepareDistanceLods(uint32_t mesh, bool grassCards = false, bool authoredLeaves = false);
	void SetDistanceLodEnabled(bool enabled) {
		distanceLodEnabled_ = enabled;
	}
	const SceneryLodStats& GetSceneryLodStats() const {
		return lastSceneryLodStats_;
	}
	std::array<uint32_t, 3> GetDistanceLodMeshes(uint32_t mesh) const;

	void BeginFrame(const float clearColorRGBA[4]);
	void EndFrame();

	void SetPostProcessEnabled(bool on) {
		ppEnabled_ = on;
	}
	bool GetPostProcessEnabled() const {
		return ppEnabled_;
	}

	void SetPostProcessParams(const PostProcessParams& p) {
		ppParams_ = p;
	}
	const PostProcessParams& GetPostProcessParams() const {
		return ppParams_;
	}

	void SetUseCubemapBackground(bool use) {
		cbFrame_.useCubemapBackground = use ? 1 : 0;
	}
	bool GetUseCubemapBackground() const {
		return cbFrame_.useCubemapBackground != 0;
	}

	// プロファイラ用統計データ取得
	uint32_t frameDrawCalls_ = 0;
	uint32_t frameParticleCount_ = 0;
	uint32_t GetDrawCallCount() const {
		return frameDrawCalls_;
	}
	uint32_t GetParticleCount() const {
		return frameParticleCount_;
	}
	Vector3 GetPlayerPos() const {
		return cbFrame_.playerPos;
	}

	static constexpr uint32_t kFluidProfileStageCount = 16;
	enum FluidProfileStage : uint32_t {
		FluidSimulation,
		FluidShapes,
		FluidSplat,
		FluidFiltering,
		FluidRaymarch,
		FluidImpostors,
		FluidShadow,
		SceneRender,
		SceneryShadow,
		RtAcceleration,
		RtTrace,
		RtDenoise,
		DlssPrepare,
		DlssUpscale,
		RtComposite,
		FluidScreenWater
	};
	struct FluidProfileStats {
		bool available = false;
		bool valid = false;
		uint32_t framesSinceSample = 0;
		uint32_t particleSlots = 0;
		uint32_t substeps = 0;
		float simulatedDtMs = 0;
		float cpuSimulationMs = 0;
		float cpuVolumeMs = 0;
		std::array<float, kFluidProfileStageCount> lastMs{};
		std::array<bool, kFluidProfileStageCount> sampledStages{};
		std::array<float, kFluidProfileStageCount> averageMs{};
		std::array<float, kFluidProfileStageCount> peakMs{};
		float totalMs = 0;
		std::array<float, 120> totalHistory{};
		uint32_t historyCount = 0;
	};
	void SetFluidProfilerEnabled(bool enabled) {
		if (enabled && !fluidProfilerAvailable_)
			InitFluidProfiler();
		fluidProfilerEnabled_ = enabled && fluidProfilerAvailable_;
	}
	bool IsFluidProfilerEnabled() const {
		return fluidProfilerEnabled_;
	}
	const FluidProfileStats& GetFluidProfileStats() const {
		return fluidProfileStats_;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetPostProcessSRV() const {
		return ppSrvGpu_;
	}

	// Gameシーンの最終出力テクスチャ。エディタUIからここを描画する。
	D3D12_GPU_DESCRIPTOR_HANDLE GetGameFinalSRV() const {
		return finalSrvGpu_;
	}

	// 描画領域を強制上書きするメソッド
	// これを呼び出すと、次回の BeginFrame()/EndFrame() において指定領域に描画される
	void SetGameViewport(float x, float y, float w, float h);

	// 描画領域を元（フルスクリーン）に戻すメソッド
	void ResetGameViewport();

	// 現在のメインレンダーターゲットと深度バッファの取得
	D3D12_CPU_DESCRIPTOR_HANDLE GetMainRTV() const {
		return ppRtv_;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE GetMainDSV() const {
		return ppDepthDsv_;
	}

	// SRVヒープの取得（CustomDrawJob用）
	ID3D12DescriptorHeap* GetSrvHeap() const {
		return srvHeap_;
	}

	// スライム屈折用: 現在のシーン色をバックドロップへコピー
	void SnapshotSceneForRefraction(ID3D12GraphicsCommandList* list);
	D3D12_CPU_DESCRIPTOR_HANDLE GetBackdropSrvCpuMaster() const {
		return backdropSrvCpuMaster_;
	}

	// 液体メタボール用の専用レンダーターゲット
	void BeginLiquidPass();
	void EndLiquidPass();
	D3D12_GPU_DESCRIPTOR_HANDLE GetLiquidSrv() const {
		return liquidSrv_[0];
	}

	// 外部（ParticleEditorなど）用のカスタムレンダーターゲット
	struct CustomRenderTarget {
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		Microsoft::WRL::ComPtr<ID3D12Resource> depth;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
		uint32_t width = 0;
		uint32_t height = 0;
	};

	CustomRenderTarget CreateRenderTarget(uint32_t width, uint32_t height);
	void BeginCustomRenderTarget(const CustomRenderTarget& target);
	void EndCustomRenderTarget();

	void SetCamera(const Camera& camera);

	void SetAmbientColor(const Vector3& color);
	Vector3 GetAmbientColor() const {
		return lightCB_.ambientColor;
	}
	D3D12_GPU_VIRTUAL_ADDRESS GetLightCBAddr() const {
		return cbLightAddr_;
	}
	void SetDirectionalLight(const Vector3& dir, const Vector3& color, bool enabled = true);
	void SetPointLight(
	    int index, const Vector3& pos, const Vector3& color, float range, const Vector3& atten = {1.0f, 0.1f, 0.01f},
	    bool enabled = true);
	void SetSpotLight(
	    int index, const Vector3& pos, const Vector3& dir, const Vector3& color, float range, float innerCos,
	    float outerCos, const Vector3& atten = {1.0f, 0.1f, 0.01f}, bool enabled = true);
	void SetAreaLight(
	    int index, const Vector3& pos, const Vector3& color, float range, const Vector3& right, const Vector3& up,
	    float halfW, float halfH, const Vector3& atten = {1.0f, 0.1f, 0.01f}, bool enabled = true);

	void SetLightCB(const LightCB& cb) {
		lightCB_ = cb;
	}
	LightCB GetLightCB() const {
		return lightCB_;
	}

	TextureHandle LoadTexture2D(const std::string& filePath, bool sRGB = true);
	// NTC のテクスチャセットを GPU で BCn に復元する。names の順にハンドルを返す。
	// libntc.dll が無い・GPU が非対応などで失敗したら空を返すので、呼び出し側が代わりを使う
	std::vector<TextureHandle> LoadNtcTextureSet(const std::string& filePath, const std::vector<std::string>& names);
	// Begin～End の間の LoadTexture2D は場所 (ハンドル) だけ確保し、PrepareDistanceLods は予約だけする。
	// End で LOD の簡略化と画像の展開を並列に行い、まとめて GPU へ送る。End まで描画しないこと (ロード中の組み立て処理で使う)
	void BeginAssetBatch() {
		++assetBatchDepth_;
	}
	void EndAssetBatch();
	// 検証用: テクスチャの GPU リソース (PIXEL_SHADER_RESOURCE 状態)
	ID3D12Resource* GetTextureResource(TextureHandle handle) const {
		return handle < textures_.size() ? textures_[handle].res.Get() : nullptr;
	}
	// 名前付きで大きさに上限のある RGBA8 の塗りアトラスを使い回す。アップロードは呼び出し側がまとめる。
	TextureHandle UpdatePaintTexture(const std::string& name, uint32_t width, uint32_t height, const uint8_t* pixels);
	struct PendingPaint {
		TextureHandle handle;
		uint32_t width, height;
		std::vector<uint8_t> pixels;
	};
	std::vector<PendingPaint> pendingPaint_;
	void FlushPaintUploads();
	MeshHandle LoadObjMesh(const std::string& objFilePath);
	bool LoadAdditionalAnimation(MeshHandle handle, const std::string& animPath);

	// キューブマップ読み込み (DDS) と Skybox 設定
	TextureHandle LoadCubeMap(const std::string& ddsPath);
	void SetSkyboxTexture(TextureHandle cubeMap);
	// 平行光源のシャドウマップ: 正方形の広さと光の向きの奥行き (ワールド単位)、カメラの前方へ
	// (広さに対する割合で) どれだけずらして中心を置くか。既定値は草原に合わせてある。
	void SetDirectionalShadowRange(float extent, float depth, float lead = 0) {
		shadowExtent_ = extent;
		shadowDepth_ = depth;
		shadowLead_ = lead;
	}
	// 和風ポストプロセス用テクスチャ
	void SetSumiETextures(TextureHandle paper, TextureHandle vignette) {
		sumiEPaperTex_ = paper;
		sumiEVignetteTex_ = vignette;
	}

	TextureHandle GetSkyboxTexture() const {
		return skyboxCubeMapHandle_;
	}

	// 動的メッシュの作成と更新
	MeshHandle CreateDynamicMesh(const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices);
	void UpdateDynamicMesh(MeshHandle handle, const std::vector<VertexData>& vertices);

	// プリミティブ形状の動的生成
	MeshHandle CreateRingMesh(float outerRadius, float innerRadius, uint32_t segments);
	MeshHandle CreateCylinderMesh(float radius, float height, uint32_t segments);

	// テクスチャのSRVハンドルを取得 (ImGui::Imageでサムネイル表示用)
	UINT TextureMipLevels(TextureHandle handle) const {
		return handle < textures_.size() && textures_[handle].res ? textures_[handle].res->GetDesc().MipLevels : 0;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrvGpu(TextureHandle handle) const {
		if (handle < textures_.size())
			return textures_[handle].srvGpu;
		return D3D12_GPU_DESCRIPTOR_HANDLE{0};
	}

	// 通常メッシュ描画
	void DrawMesh(
	    MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor,
	    const std::string& shaderName = "Default", float reflectivity = 0.0f, bool useCubemap = false);
	void DrawMesh(
	    MeshHandle mesh, TextureHandle texture, const Matrix4x4& worldMatrix, const Vector4& mulColor,
	    const std::string& shaderName = "Default", float reflectivity = 0.0f, bool useCubemap = false);

	// インスタンス描画の予約
	void DrawMeshInstanced(
	    MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor,
	    const std::string& shaderName = "Default", const std::vector<TextureHandle>& extraTex = {});
	void DrawMeshInstanced(
	    MeshHandle mesh, TextureHandle texture, const Matrix4x4& worldMatrix, const Vector4& mulColor,
	    const std::string& shaderName = "Default", const std::vector<TextureHandle>& extraTex = {},
	    uint64_t motionId = 0);

	// パーティクル インスタンス描画
	void DrawParticleInstanced(
	    MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor,
	    const Vector4& uvScaleOffset, const std::string& shaderName = "Particle");

	// 液体（スライムメタボール）パーティクル インスタンス描画
	void DrawLiquidParticleInstanced(
	    MeshHandle mesh, TextureHandle tex, const Transform& transform, const Vector4& color,
	    const Vector4& uvScaleOffset, const std::string& shaderName);

	// GPU流体パーティクルシステム
	struct GPUFluidParticle {
		Vector3 position;
		float pad0;
		Vector3 velocity;
		float pad1;
		Vector4 color;
		float type;
		Vector3 pad2; // 0=プレイヤー, 1=水しぶき
	};
	// 目標はフレーム中は変えない。割り当ては GPU 上で元の粒子の枠ごとに持つ。
	// 液体の体 (shape.w=1) の blend: 動きの量 (0〜1) と、足元の傾き (体の右・前の向きに 1 進むごとの高さ、-1〜1) を
	// 1 つの数にまとめる (各 101 段階)。まとめた値は kLiquidBlendPacked 以上 (最大約 203 万で float に正確に入る) で、
	// それより小さい値は今まで通り動きの量そのもの (傾きなし) として扱う。GPU では FluidMorph.hlsli の MorphSlope で戻す。
	// 1 粒子 128 バイトの目標とルート定数 (64 DWORD の上限まであと 1) に空きが無いため、液体では使っていない精度を使う
	static constexpr float kLiquidBlendPacked = 1000000.f;
	static float PackLiquidBlend(float motion, float slopeRight, float slopeForward) {
		auto step = [](float v, float lo, float hi) {
			return std::round((std::clamp)((v - lo) / (hi - lo), 0.f, 1.f) * 100.f);
		};
		return kLiquidBlendPacked + step(motion, 0, 1) +
		       101.f * (step(slopeRight, -1, 1) + 101.f * step(slopeForward, -1, 1));
	}
	static float LiquidBlendMotion(float blend) {
		return blend < kLiquidBlendPacked ? blend : std::fmod(std::round(blend - kLiquidBlendPacked), 101.f) / 100.f;
	}
	struct FluidMorphTarget {
		Vector3 local;
		float blend;
		Vector3 origin;
		float density;
		Vector3 forward;
		float omega;
		Vector4 color;
		Vector4 material; // 減衰, 粘性, 表面張力, 地面の高さ
		Vector4 motion;   // 速度 xyz, 空中の混ざり具合
		Vector4 shape;    // 半径 xyz。w: 1=自由な液体, 0=包み込み, -2/-3/-4=武器のばね
		Vector4 reserved; // 128 バイトの間隔は、アップロード用リングの 256 バイトの境界を割り切る。
	};
	static_assert(sizeof(FluidMorphTarget) == 128);
	std::vector<FluidMorphTarget> fluidMorphTargets_;
	// 掃いた液体の表面は、シミュレーションする体の密度場と光学的な性質を共有する。
	struct FluidTendril {
		Vector3 start;
		float radius;
		Vector3 end;
		float endRadius;
	};
	static_assert(sizeof(FluidTendril) == 32);
	std::vector<FluidTendril> fluidTendrils_;
	void SetFluidTendrils(std::vector<FluidTendril> tendrils) {
		fluidTendrils_ = std::move(tendrils);
	}
	const std::vector<FluidTendril>& GetFluidTendrils() const {
		return fluidTendrils_;
	}
	// 変形のボリュームの領域が覆わなければならない追加の球 (中心 xyz, 半径 w)。投げたスライムなど。
	std::vector<Vector4> fluidVolumeExtras_;
	void SetFluidVolumeExtras(std::vector<Vector4> spheres) {
		fluidVolumeExtras_ = std::move(spheres);
	}
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoVolumeTendrils_;
	uint64_t fluidMorphRevision_ = 0, fluidMorphApplied_ = ~uint64_t(0);
	Microsoft::WRL::ComPtr<ID3D12Resource> fluidMorphScratch_, fluidMorphDummy_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoMorphKeys_, psoMorphSort_, psoMorphAssign_;
	ID3D12Resource* fluidMorphFrame_ = nullptr;
	uint32_t fluidMorphOffset_ = 0;
	float fluidSamplingScale_ = 1; // 持ち主の間隔の最大値 (描画の作業量に上限を設けるため)。
	void InitFluidMorph();
	// extraEntries: 箱と球の後ろに置いた坂の町の地面の高さの表の数 (見る範囲に含めないと、表は全部 0 と読まれる)
	void BindFluidMorphResources(ID3D12Resource* aabb, uint32_t offset = 0, uint32_t extraEntries = 0);
	static constexpr size_t kFluidMorphBodyCount = 16;
	static constexpr uint32_t kFluidMorphAllBodies = (1u << kFluidMorphBodyCount) - 1;
	uint32_t fluidMorphRetargetMask_ = kFluidMorphAllBodies;
	bool fluidMorphSeedPending_ = false;
	float fluidMorphDt_ = -1;
	void SetFluidMorphTargets(
	    std::vector<FluidMorphTarget> targets, uint64_t revision, uint32_t mask = kFluidMorphAllBodies,
	    bool seed = false, float frameDt = -1) {
		fluidMorphTargets_ = std::move(targets);
		fluidMorphRevision_ = revision;
		fluidMorphRetargetMask_ |= mask;
		fluidMorphSeedPending_ |= seed;
		fluidMorphDt_ = frameDt;
	}
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoMorphMeasure_;
	Microsoft::WRL::ComPtr<ID3D12Resource> morphReadback_[2];
	bool morphPending_[2]{};
	uint64_t morphReadbackRevision_[2]{}, morphMeasuredRevision_ = 0;
	std::array<float, kFluidMorphBodyCount> morphErrors_ = []() {
		std::array<float, kFluidMorphBodyCount> values{};
		values.fill(1000);
		return values;
	}();
	void QueueMorphMeasure();
	void CollectMorphMeasure(uint32_t frame);
	bool FluidMorphReady(uint64_t revision, size_t body, float tolerance) const {
		return morphMeasuredRevision_ == revision && body < kFluidMorphBodyCount && morphErrors_[body] < tolerance;
	}

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigFluid_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuFluidBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuFluidGridCountBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuFluidGridOffsetBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuFluidSortedParticlesBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuFluidOriginalIndicesBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuFluidPreviousBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuFluidScratchBuffer_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidSavePrevious_, psoFluidDelta_, psoFluidApply_,
	    psoFluidVelocity_;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidEmit_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidExtract_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidSyncLostGroup_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidAbsorbLostGroup_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidInit_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidClearOriginal_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidClearCount_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoGPUFluidShadow_; // シャドウパス用
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidCount_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidPrefixSum_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidSort_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidSortVelocity_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidDensity_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidForce_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidWriteBack_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidDepthRender_[3];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidRender_[3];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFluidDebug_;
	static constexpr uint32_t kFrameCount = 2;
	static constexpr uint32_t kPlayerFluidParticles = 6600;
	// ゲーム中の接触の塗りに使う、フェンスで守ったスナップショット (GPU を決して止めない)。
	struct FluidBodySnapshot {
		std::vector<Vector3> offsets;
		Vector3 core{};
		uint64_t serial = 0;
	};
	const FluidBodySnapshot& GetFluidBodySnapshot() const {
		return fluidBodySnapshot_;
	}
	FluidBodySnapshot fluidBodySnapshot_;
	Microsoft::WRL::ComPtr<ID3D12Resource> fluidBodyReadback_[kFrameCount];
	Vector3 fluidBodyReadbackCore_[kFrameCount]{};
	bool fluidBodyPending_[kFrameCount]{};
	void CollectFluidBody(uint32_t frame);
	void QueueFluidBody();
	static constexpr uint32_t kHydrantAttackParticles = 12288, kHydrantGuardParticles = 3072;
	static constexpr uint32_t kHydrantWaterParticles = kHydrantAttackParticles + kHydrantGuardParticles;
	uint32_t hydrantAttackCursor_ = kPlayerFluidParticles,
	         hydrantGuardCursor_ = kPlayerFluidParticles + kHydrantAttackParticles;
	struct HydrantWaterSample {
		Vector3 at{}, velocity{};
		float type = 0, contactAge = 0;
	};
	struct HydrantWaterSnapshot {
		std::vector<HydrantWaterSample> particles;
		uint64_t serial = 0;
	};
	HydrantWaterSnapshot hydrantWaterSnapshot_;
	const HydrantWaterSnapshot& GetHydrantWaterSnapshot() const {
		return hydrantWaterSnapshot_;
	}
	bool hydrantWaterReadbackEnabled_ = false;
	void EnableHydrantWaterReadback(bool enabled) {
		hydrantWaterReadbackEnabled_ = enabled;
		if (!enabled) {
			hydrantWaterSnapshot_.particles.clear();
			for (auto& pending : hydrantWaterPending_)
				pending = false;
			++hydrantWaterSnapshot_.serial;
		}
	}
	Microsoft::WRL::ComPtr<ID3D12Resource> hydrantWaterReadback_[kFrameCount];
	bool hydrantWaterPending_[kFrameCount]{};
	// 読み戻した粒の範囲 (坂の町の放水では水専用の流体の範囲を使う)
	uint32_t hydrantWaterReadCount_[kFrameCount]{};
	void CollectHydrantWater(uint32_t frame);
	void QueueHydrantWater();
	Vector3 slimeGround_{};
	bool slimeGrounded_ = false;
	// 消火栓の水が当たる地面の高さの表 (坂の町の放水)。原点 (x, z) から 1 マス cell の n×n (z の行ごと)。
	// 空なら今まで通り高さ 0.22 の平らな床。箱と球の後ろに積んで流体の計算へ渡す
	std::vector<float> hydrantTerrain_;
	float hydrantTerrainX_ = 0, hydrantTerrainZ_ = 0, hydrantTerrainCell_ = 1;
	uint32_t hydrantTerrainSize_ = 0;
	void SetHydrantTerrain(float x, float z, float cell, uint32_t n, std::vector<float> heights) {
		hydrantTerrainX_ = x;
		hydrantTerrainZ_ = z;
		hydrantTerrainCell_ = cell;
		hydrantTerrainSize_ = n;
		hydrantTerrain_ = std::move(heights);
	}
	void ClearHydrantTerrain() {
		hydrantTerrain_.clear();
		hydrantTerrainSize_ = 0;
	}
	bool HydrantTerrainActive() const {
		return hydrantTerrainSize_ > 1 && hydrantTerrain_.size() == size_t(hydrantTerrainSize_) * hydrantTerrainSize_;
	}
	void SetSlimeGround(Vector3 slopeHeight, bool grounded) {
		slimeGround_ = slopeHeight;
		slimeGrounded_ = grounded;
	}
	float slimeFaceCamera_ = 0;
	void SetSlimeFaceCamera(float blend) {
		slimeFaceCamera_ = blend;
	}
	static constexpr uint32_t kEffectFluidEnd = 28000;
	static constexpr uint32_t kTrailFluidStart = 32000, kTrailFluidCapacity = 16000;
	// 水専用の流体 (たまる水)。プレイヤーのスライムと同じ PBF の計算と体積の描画を、水の性質 (水の相) で使う。
	// 種類の値 kPoolWaterType で出すと、ほかの水しぶきと分けた専用の粒の範囲に入り、時間で消えない (いっぱいになると古い粒から出口へ戻る)
	static constexpr uint32_t kPoolFluidStart = kTrailFluidStart + kTrailFluidCapacity, kPoolFluidCapacity = 24576;
	static constexpr float kPoolWaterType = 1.3f;
	static bool IsPoolWater(float type) {
		return type > 1.25f && type < 1.35f;
	}
	uint32_t gpuFluidMaxParticles_ = kPoolFluidStart + kPoolFluidCapacity;
	uint32_t gpuFluidEmitCursorPool_ = kPoolFluidStart;
	// 流体を細かく描く範囲 (箱庭など)。設定するとスライムがいなくても体積の升をこの箱に合わせ、遠くの水の升は使わない
	bool fluidVolumeFocusActive_ = false;
	Vector3 fluidVolumeFocusLow_{}, fluidVolumeFocusHigh_{};
	void SetFluidVolumeFocus(const Vector3& low, const Vector3& high) {
		fluidVolumeFocusActive_ = true;
		fluidVolumeFocusLow_ = low;
		fluidVolumeFocusHigh_ = high;
	}
	void ClearFluidVolumeFocus() {
		fluidVolumeFocusActive_ = false;
	}
	std::vector<GPUFluidParticle> guidedTrailParticles_;
	uint32_t previousTrailParticleCount_ = 0;
	void SetGuidedLiquidTrail(std::vector<GPUFluidParticle> particles) {
		guidedTrailParticles_ = std::move(particles);
	}
	uint32_t gpuFluidEmitCursorPlayer_ = 0;
	uint32_t gpuFluidEmitCursorSplash_ = kPlayerFluidParticles;
	uint32_t gpuFluidExtractCursor_ = 0;
	uint32_t gpuFluidActiveParticleCount_ = 0;
	// 安全側に倒した寿命のマスク: CPU のタイマーで、生きている GPU の粒子を隠してはいけない。
	// ビットを消すのは流体のプール全体を消すときだけ。
	uint32_t gpuFluidPhaseMask_ = 0;
	bool isGPUFluidReady_ = false;
	bool isGPUFluidInitialized_ = false;

	void InitGPUFluid();
	void UpdateGPUFluid(float dt);
	void PrepareGPUFluidFrame();
	bool fluidFrameUpdated_ = false;
	void ResetGPUFluid();
	void ClearGPUFluid() {
		gpuFluidActiveParticleCount_ = 0;
		gpuFluidPhaseMask_ = 0;
		volumeHistoryValid_ = false;
		isGPUFluidInitialized_ = false;
		fluidMorphTargets_.clear();
		fluidTendrils_.clear();
		fluidVolumeExtras_.clear();
		fluidVolumeFocusActive_ = false;
		fluidMorphApplied_ = ~uint64_t(0);
		fluidMorphRetargetMask_ = kFluidMorphAllBodies;
		gpuFluidSplashSpheres_.clear();
		EnableHydrantWaterReadback(false);
	}
	void EmitGPUFluid(
	    const Vector3& pos, const Vector3& velocityDir, const Vector4& color, int count, float type = 0.0f,
	    bool retainWater = false, float nozzleRadius = 0);
	uint32_t ExtractGPUFluidFromPlayer(const Vector3& pos, const Vector3& velocityDir, int count);
	void SyncLostGPUFluidGroup(uint32_t groupId, const Vector3& pos);
	void AbsorbLostGPUFluidGroup(uint32_t groupId);
	void DrawGPUFluid(TextureHandle texture);
	bool InitFluidVolume();
	void PrepareFluidVolume();
	void DrawFluidVolume();
	void DrawFluidVolumeShadow(const Matrix4x4& lightVP);
	// 0: マテリアル, 1: 相 (phase), 2: 法線, 3: 厚み, 4: 領域 / 歩数の予算。
	void SetFluidVolumeDebugMode(uint32_t mode) {
		fluidVolumeDebugMode_ = mode;
	}
	uint32_t GetFluidVolumeDebugMode() const {
		return fluidVolumeDebugMode_;
	}
	bool IsFluidVolumeReady() const {
		return fluidVolumeReady_;
	}
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigVolumeCompute_, rootSigVolumeDraw_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigVolumeShadow_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoVolumeShadow_;
	D3D12_GPU_VIRTUAL_ADDRESS volumeFrameSettings_ = 0;
	D3D12_GPU_VIRTUAL_ADDRESS farWaterFrameSettings_ = 0;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoFarWaterSplat_;
	Microsoft::WRL::ComPtr<ID3D12Resource> farWaterAccum_, farWaterMomentum_, farWaterTextures_[4];
	D3D12_GPU_DESCRIPTOR_HANDLE farWaterSrv_[4]{}, farWaterUav_[4]{};
	uint32_t volumeDrawCount_ = 0;
	Vector3 volumeShadowLow_{}, volumeShadowHigh_{};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoVolumeClear_, psoVolumeShapes_, psoVolumeSplat_, psoVolumeResolve_,
	    psoVolumeSmooth_, psoVolumeTemporal_, psoVolumeOccupancy_, psoVolumeRaymarch_, psoVolumeSpray_;
	Microsoft::WRL::ComPtr<ID3D12Resource> volumeAccum_, volumeMomentum_, volumeShapes_;
	// 0/1: 空間の交互用, 2/3: 時間の交互用, 4: 速度。
	// 5: 安全側に見積もった 4x4x4 の空き空間の加速格子 (周囲の余白を含む)。
	Microsoft::WRL::ComPtr<ID3D12Resource> volumeTextures_[6];
	D3D12_GPU_DESCRIPTOR_HANDLE volumeSrv_[6]{}, volumeUav_[6]{};
	Microsoft::WRL::ComPtr<ID3D12Resource> volumeSurfaceDepth_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> volumeRtvHeap_;
	D3D12_GPU_DESCRIPTOR_HANDLE volumeSurfaceDepthSrv_{};
	// 坂の町の放水の水を画面の上で面にして描く (Resources/Shaders/FluidScreenWater.hlsl)。
	// 0: 粒の近さ (1/奥行きの最大)、1: 厚みと速さ (足し合わせ)、2/3: なめらかにした奥行き (交互に使う)。
	// 体積の升は放水の届く広さ (数十 m) に合わせると粗くなるので、消火栓の水だけはこちらで描く
	Microsoft::WRL::ComPtr<ID3D12Resource> screenWaterTargets_[4];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> screenWaterRtvHeap_;
	D3D12_GPU_DESCRIPTOR_HANDLE screenWaterSrv_[4]{};
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigScreenWater_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoScreenWaterSprite_, psoScreenWaterSmooth_, psoScreenWaterComposite_;
	bool screenWaterReady_ = false;
	bool InitScreenWater();
	void DrawScreenWater(D3D12_GPU_VIRTUAL_ADDRESS camera);
	// 坂の町の放水の水の描き方。既定は箱庭のデモ (水専用の流体) と同じ体積の水 (HydrantVolumeWater)。
	// true にすると画面の上の面 (FluidScreenWater.hlsl) で描く (軽いが、映り込みと厚みはデモほど出ない)
	bool hydrantScreenWaterEnabled_ = false;
	bool HydrantScreenWater() const {
		return screenWaterReady_ && hydrantScreenWaterEnabled_ && HydrantTerrainActive();
	}
	bool HydrantVolumeWater() const {
		return HydrantTerrainActive() && !HydrantScreenWater();
	}
	// 流体の近くの粒を探す表の大きさ (変形中は 8192、それ以外は 65536。FluidSimCS の GetGridHash と同じ。体積の BuildShapes へ渡す)
	uint32_t fluidHashBuckets_ = 65536;
	Vector3 volumePreviousOrigin_{};
	Vector4 volumeColors_[3] = {{0.02f, 0.8f, 0.06f, 1}, {1, 0.9f, 0.1f, 1}, {0.1f, 0.65f, 1, 1}};
	void SetSlimeColor(Vector4 color) {
		volumeColors_[0] = color;
	}
	// 平行光源のシャドウマップの解像度。シェーダーも PCF のテクセルの大きさに同じ値を使う。
	static constexpr uint32_t kShadowMapSize = 4096;
	float shadowExtent_ = 100, shadowDepth_ = 100, shadowLead_ = 0;
	float volumePreviousVoxelSize_ = 0;
	bool fluidVolumeReady_ = false, volumeHistoryValid_ = false;
	uint32_t volumeHistoryIndex_ = 0, fluidVolumeDebugMode_ = 0;
	float fluidSimulatedDt_ = 0.0f;
	float fluidDecorationTime_ = 0; // 強化中の戦闘が止まっている間も、見た目の泡は動き続ける。
	void DrawGPUFluidShadow();      // シャドウパス描画用
	void DrawGPUFluidDebug();
	void SetGPUFluidCore(
	    const Vector3& pos, float attraction, const Vector3& scale = {1.0f, 1.0f, 1.0f},
	    const Vector3& forward = {0.0f, 0.0f, 1.0f}, float mode = 0.0f, float flowSpeed = 0.0f);
	// 既存のプレイヤーの粒子を変形させる。腕の別メッシュや追加の放出は使わない。
	void SetGPUFluidTether(const Vector3& tip, bool active) {
		gpuFluidTetherTip_ = tip;
		gpuFluidTetherActive_ = active;
	}
	void SetRogueWorldFrozen(bool value) {
		rogueWorldFrozen_ = value;
	}
	bool GetRogueWorldFrozen() const {
		return rogueWorldFrozen_;
	}
	void SetChronoFluidPresentation(float opacity, float flash, bool paused) {
		chronoFluidOpacity_ = opacity;
		chronoFluidFlash_ = flash;
		chronoFluidPaused_ = paused;
	}
	Vector3 gpuFluidTetherTip_{};
	bool gpuFluidTetherActive_ = false;
	float gpuFluidTetherBlend_ = 0.0f;
	void SetGPUFluidDecoy(
	    const Vector3& pos, float attraction, const Vector3& scale = {1.0f, 1.0f, 1.0f},
	    const Vector3& forward = {0.0f, 0.0f, 1.0f}); // デコイ用コア情報設定

	Vector3 gpuFluidCorePos_ = {0, 0, 0};
	Vector3 gpuFluidCoreVelocity_ = {0, 0, 0};
	float gpuFluidCoreAttraction_ = 0.0f;
	Vector3 gpuFluidCoreScale_ = {1.0f, 1.0f, 1.0f};
	Vector3 gpuFluidCoreForward_ = {0.0f, 0.0f, 1.0f};
	float gpuFluidCoreMode_ = 0.0f;
	float chronoFluidOpacity_ = 1, chronoFluidFlash_ = 0;
	// 結果画面の「おなかの中」: 体の中から見る量と、体の中の物として見せる深さ (ビュー空間の z)
	float fluidInterior_ = 0, fluidInteriorDepth_ = 0;
	void SetFluidInterior(float amount, float itemDepth) {
		fluidInterior_ = amount;
		fluidInteriorDepth_ = itemDepth;
	}
	bool chronoFluidPaused_ = false, rogueWorldFrozen_ = false;
	float gpuFluidCoreFlowSpeed_ = 0.0f;

	// デコイ用コア情報
	Vector3 gpuFluidDecoyPos_ = {0, 0, 0};
	float gpuFluidDecoyAttraction_ = 0.0f;
	Vector3 gpuFluidDecoyScale_ = {1.0f, 1.0f, 1.0f};
	Vector3 gpuFluidDecoyForward_ = {0.0f, 0.0f, 1.0f};

	// 流体シミュレーション用のAABBコリジョン
	struct FluidAABB {
		Vector3 min;
		float pad0;
		Vector3 max;
		float pad1;
	};
	void SetFluidAABBs(const std::vector<FluidAABB>& aabbs) {
		gpuFluidAABBs_ = aabbs;
	}
	// 消火栓の水だけが当たって跳ねる、動く障害物 (中心 xyz, 半径 w)。
	void SetFluidSplashSpheres(const std::vector<Vector4>& spheres) {
		gpuFluidSplashSpheres_ = spheres;
	}
	static constexpr size_t kFluidBoxCapacity = 128, kFluidSphereCapacity = 32;
	std::vector<FluidAABB> gpuFluidAABBs_;
	std::vector<Vector4> gpuFluidSplashSpheres_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuFluidAABBBuffer_;

	// Compute Shader Skinning
	void ComputeSkinning(Model* model, const std::vector<Matrix4x4>& skeletonParams);

	// デバッグベクトル表示フラグ
	bool drawFluidDebugArrows_ = false;
	void SetDrawFluidDebugArrows(bool b) {
		drawFluidDebugArrows_ = b;
	}
	bool GetDrawFluidDebugArrows() const {
		return drawFluidDebugArrows_;
	}

	// 深度ベースのメタボール用
	// 相 (phase) ごとのレイヤーを分けることで、手前のプレイヤー / 分身が透明の合成の前に
	// 水 (や別の流体の体) を消してしまうのを防ぐ。
	Microsoft::WRL::ComPtr<ID3D12Resource> liquidRT_[3];
	// 視点からの深度と密度。相ごとに独立に再構成する。
	Microsoft::WRL::ComPtr<ID3D12Resource> liquidSurface_[3][2];
	D3D12_CPU_DESCRIPTOR_HANDLE liquidSurfaceRtv_[3][2]{};
	D3D12_GPU_DESCRIPTOR_HANDLE liquidSurfaceSrv_[3][2]{};
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigLiquid_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoLiquidFilter_;
	D3D12_GPU_DESCRIPTOR_HANDLE liquidNullCubeSrv_{};
	D3D12_CPU_DESCRIPTOR_HANDLE liquidRtv_[3]{};
	D3D12_GPU_DESCRIPTOR_HANDLE liquidSrv_[3]{};
	D3D12_RESOURCE_STATES liquidState_[3] = {
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
	Microsoft::WRL::ComPtr<ID3D12Resource> liquidDepthRT_[3];
	D3D12_CPU_DESCRIPTOR_HANDLE liquidDepthRtv_[3]{};
	D3D12_GPU_DESCRIPTOR_HANDLE liquidDepthSrv_[3]{};
	D3D12_RESOURCE_STATES liquidDepthState_[3] = {
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};

	// パーティクル描画 (UVスケール・オフセット付き)
	void DrawParticle(
	    MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor,
	    const Vector4& uvScaleOffset, const std::string& shaderName = "Particle");
	void DrawParticle(
	    MeshHandle mesh, TextureHandle texture, const Matrix4x4& worldMatrix, const Vector4& mulColor,
	    const Vector4& uvScaleOffset, const std::string& shaderName = "Particle");

	// スキニングメッシュ描画
	void DrawSkinnedMesh(
	    MeshHandle mesh, TextureHandle texture, const Transform& transform, const std::vector<Matrix4x4>& bones,
	    const Vector4& mulColor = {1, 1, 1, 1});
	void DrawSkinnedMesh(
	    MeshHandle mesh, TextureHandle texture, const Matrix4x4& worldMatrix, const std::vector<Matrix4x4>& bones,
	    const Vector4& mulColor = {1, 1, 1, 1});

	// スプライト描画
	struct SpriteDrawCall {
		TextureHandle tex;
		SpriteDesc desc;
	};

	struct Sprite9SliceDesc {
		float x = 0.0f, y = 0.0f, w = 64.0f, h = 64.0f;
		float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f; // ピクセル単位の枠の幅
		Vector4 color{1, 1, 1, 1};
		float rotationRad = 0.0f;
		int layer = 0; // 描画レイヤー

		Sprite9SliceDesc() : x(0), y(0), w(64), h(64), left(0), right(0), top(0), bottom(0), rotationRad(0), layer(0) {}
	};
	void DrawSprite(TextureHandle texH, const SpriteDesc& s);
	void DrawSprite9Slice(TextureHandle texH, const Sprite9SliceDesc& s);
	void FlushSprites();                                                  // スプライトの描画実行
	// 重ねて描く待ち行列が有効な間に積んだスプライト / 文字は、シーンのすべての
	// スプライトと文字の後に描く (シーンの切り替え、ロード画面)。
	void SetOverlayQueue(bool on) {
		overlayQueue_ = on;
	}
	// 積んだ 2D の UI だけを、バックバッファーに直接描くフレーム。レンダラー専用の
	// UI 用コマンドリストとアップロード用リングを使う。タイトル / セレクト画面は
	// 3D パイプラインができる前にこれを使い、ロード画面はメインスレッドが読み込みを
	// 続けている間に自分のスレッドからこれを呼ぶ。
	// 合成役がいるときは、エディタを描く前に UI を Game のテクスチャへ描く。
	void RenderUIFrame(
	    const std::function<void()>& draw, const std::function<void(ID3D12GraphicsCommandList*)>& composite = {});
	// クライアント座標。エディタの Game ビューだけが渡す。空の矩形なら
	// Game タブが隠れている間もエディタを保つ。
	void SetEditorLoadingViewport(float x, float y, float w, float h);
	void CaptureEditorLoadingBackground(ID3D12GraphicsCommandList* commands = nullptr);
	bool PrepareEditorLoadingFrame();
	void FinishEditorLoadingFrame() {
		preserveEditorLoading_ = false;
	}
	bool MapLoadingPointer(float& x, float& y) const;
	bool UIReady() const {
		return pso2D_.Get() != nullptr && uiList_ != nullptr;
	}
	// 重い 3D の準備 (パイプライン・ポスト処理・流体・レイトレーシング・空) は、
	// ワールドを描く最初のシーンが必要とするまで後回しにする。
	bool InitializeDeferred();
	bool Ready3D() const {
		return ready3D_;
	}
	// ロード用スレッドの補助。UI スレッドが動いている間は他のスレッドからの 2D の描画を無視し、
	// UI スレッドはそのためにキャッシュしたテクスチャだけを読む。
	void BeginUIThread() {
		tUiThread_ = true;
	}
	void SetUIThreadActive(bool active) {
		uiThreadActive_ = active;
	}
	void SetRecordUITextures(bool record) {
		recordUITextures_ = record;
	}

	// テキスト描画
	// text: UTF-8 文字列
	// x, y: スクリーン座標 (左上基準, ピクセル)
	// scale: フォントのスケール (1.0 = デフォルトサイズ)
	// color: 文字色 (RGBA)
	void DrawString(
	    const std::string& text, float x, float y, float scale = 1.0f, const Vector4& color = {1, 1, 1, 1},
	    const std::string& fontPath = "C:\\Windows\\Fonts\\msgothic.ttc");
	void FlushText(); // テキストの描画実行

	// テキストシステムの初期化 (フォントファイルの読み込み)
	bool InitTextSystem(const std::string& fontPath, float pixelHeight = 48.0f);

	// テキスト描画の幅を事前計算 (レイアウト用)
	float MeasureTextWidth(
	    const std::string& text, float scale = 1.0f, const std::string& fontPath = "C:\\Windows\\Fonts\\msgothic.ttc");
	float GetTextLineHeight(float scale = 1.0f, const std::string& fontPath = "C:\\Windows\\Fonts\\msgothic.ttc") const;

	// 3Dライン描画（エディタ用ギズモ・グリッドなど）
	void DrawLine3D(const Vector3& p0, const Vector3& p1, const Vector4& color, bool xray = false);
	void FlushLines();

	// 現在のドローコールを直ちにフラッシュ（描画発行）し、キューをクリアする
	void FlushDrawCalls();

	// コンピュートシェーダーで衝突判定を実行
	void BeginCollisionCheck(uint32_t maxPairs = 1024);
	void DispatchCollision(
	    MeshHandle meshA,                      // メッシュ (対象)
	    uint32_t meshBHandle,                  // メッシュ (元 / 地形)
	    const Transform& trA,                  // 物体 A の変換
	    const Game::BoxColliderComponent& bcA, // 物体 A の箱の当たり判定
	    const Transform& trB,                  // メッシュ B の変換
	    uint32_t resultIndex);

	// 古いコードとの互換用: 箱 対 メッシュ (trB=単位行列)
	void DispatchCollision(
	    MeshHandle meshA, const Transform& trA, const Game::BoxColliderComponent& bcA, uint32_t resultIndex);

	// 古いコードとの互換用: メッシュ 対 メッシュ
	void DispatchCollision(
	    MeshHandle meshA, const Transform& trA, MeshHandle meshB, const Transform& trB, uint32_t resultIndex);

	void EndCollisionCheck();
	bool GetCollisionResult(uint32_t resultIndex, Game::ContactInfo& outInfo) const;
	bool GetCollisionResult(uint32_t resultIndex) const;
	uint32_t CurrentFrameIndex() const {
		return window_ ? window_->FrameIndex() : 0;
	}
	uint32_t CollisionDispatchCount() const {
		return collisionDispatchCount_;
	}

	bool CreateShaderPipeline(const std::string& shaderName, const std::wstring& vsPath, const std::wstring& psPath);
	const std::vector<std::string>& GetShaderNames() const {
		return shaderNames_;
	}

	// Modelへのポインタを取得
	Model* GetModel(MeshHandle handle);
	// 透明/加算 シェーダー登録
	bool CreateShaderPipelineTransparent(
	    const std::string& shaderName, const std::wstring& vsPath, const std::wstring& psPath, bool additive,
	    bool depthTest = true);
	// 透明/加算 用 PSO作成
	bool CreatePSO_Transparent(
	    const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob, bool additive, bool depthTest = true);

	bool CreatePSOAlpha(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob);

	// 風とプレイヤーの位置設定
	void SetWindParams(const Vector4& p) {
		cbFrame_.windParams = p;
	}
	void SetPlayerPos(const Vector3& p) {
		cbFrame_.playerPos = p;
	}

	// DynamicGlyphCache からの SRV 割り当て用
	uint32_t AllocateTextSrvIndex() {
		return AllocateSrvIndex(1);
	}
	WindowDX* GetWindow() const {
		return window_;
	}
	ID3D12CommandQueue* GetQueue() const {
		return queue_;
	}

private:
	struct Mesh {
		Microsoft::WRL::ComPtr<ID3D12Resource> vb;
		Microsoft::WRL::ComPtr<ID3D12Resource> ib;
		D3D12_VERTEX_BUFFER_VIEW vbView{};
		D3D12_INDEX_BUFFER_VIEW ibView{};
		uint32_t indexCount = 0;
	};

	struct DrawCall {
		MeshHandle mesh;
		TextureHandle tex;
		std::vector<TextureHandle> extraTex; // 地形用の追加テクスチャ
		Matrix4x4 worldMatrix;
		Vector4 color;
		std::string shaderName;
		bool isSkinned = false;
		std::vector<Matrix4x4> bones;
		bool isParticle = false;
		Vector4 uvScaleOffset;
		float reflectivity = 0.5f;
		bool useCubemap = false;
		bool useSkinnedVb = false; // ComputeShaderでスキン済みの場合
	};

	struct InstanceData {
		Matrix4x4 world;
		Vector4 color = {1, 1, 1, 1};
		Vector4 uvScaleOffset = {1, 1, 0, 0}; // UVスケール・オフセット用
	};

	struct InstancedDrawCall {
		MeshHandle mesh = 0;
		TextureHandle tex = 0;
		std::vector<TextureHandle> extraTex;
		std::string shaderName;
		std::vector<InstanceData> instances;
	};

private:
	struct Texture {
		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpuMaster{}; // 非ShaderVisible
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
	};

	struct UploadRing {
		Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
		uint8_t* mapped = nullptr;
		uint32_t sizeBytes = 0;
		uint32_t offset = 0;

		void Reset() {
			offset = 0;
		}
		uint32_t Allocate(uint32_t bytes, uint32_t alignment);
	};

private:
	uint32_t AllocateSrvIndex(uint32_t count = 1);
	uint32_t AllocateDynamicSrvIndex(uint32_t count = 1);
	Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* src, const char* entry, const char* target);
	Microsoft::WRL::ComPtr<ID3DBlob>
	    CompileShaderFromFile(const wchar_t* filePath, const char* entry, const char* target);

	bool InitPipelines();
	bool InitPostProcess_();
	bool CreatePSO(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob);
	bool CreatePSO(
	    const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob, const D3D12_INPUT_ELEMENT_DESC* layout,
	    UINT numElements);

	// Skybox描画
	void DrawSkybox();
	void InitSkyboxMesh();
	bool InitSkyboxPipeline();

private:
	static Renderer* instance_;

	WindowDX* window_ = nullptr;

	ID3D12Device* dev_ = nullptr;
	ID3D12GraphicsCommandList* list_ = nullptr;
	ID3D12CommandQueue* queue_ = nullptr;

	ID3D12DescriptorHeap* srvHeap_ = nullptr;
	uint32_t srvInc_ = 0;

	uint32_t srvCursor_ = 10;
	uint32_t srvDynamicCursor_ = 0;
	uint32_t srvDynamicEnd_ = 0;
	static constexpr uint32_t kSrvStaticMax = WindowDX::kSrvStaticCount;

	// RTV割り当て用
	uint32_t rtvCursor_ = 1; // 0番目は finalSceneColor_ 用
	uint32_t dsvCursor_ = 1; // 0番目は shadowMap_ 用

	UploadRing upload_[kFrameCount]{};
	UploadRing uiUpload_[kFrameCount]{};
	static thread_local UploadRing* tUiUpload_;
	struct FluidProfileFrame {
		bool pending = false;
		std::array<bool, kFluidProfileStageCount> used{};
		uint32_t particleSlots = 0;
		uint32_t substeps = 0;
		float simulatedDtMs = 0;
		float cpuSimulationMs = 0;
		float cpuVolumeMs = 0;
	};
	struct FluidProfileSample {
		std::array<float, kFluidProfileStageCount> ms{};
		float totalMs = 0;
	};
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> fluidProfilerQueries_;
	Microsoft::WRL::ComPtr<ID3D12Resource> fluidProfilerReadback_[kFrameCount];
	FluidProfileFrame fluidProfilerFrames_[kFrameCount]{};
	std::array<FluidProfileSample, 120> fluidProfilerHistory_{};
	uint32_t fluidProfilerHistoryCount_ = 0;
	uint64_t fluidProfilerFrequency_ = 0;
	bool fluidProfilerAvailable_ = false;
	bool fluidProfilerEnabled_ = false;
	FluidProfileStats fluidProfileStats_{};
	void InitFluidProfiler();
	void CollectFluidProfile(uint32_t frameIndex);
	void ResolveFluidProfile(uint32_t frameIndex);
	void BeginFluidProfile(FluidProfileStage stage);
	void EndFluidProfile(FluidProfileStage stage);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig3D_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigTerrain_;    // 地形用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigDistortion_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigPP_;

	// コンピュート用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigCompute_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigSkinningCS_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoSkinningCS_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoCollision_;
	Microsoft::WRL::ComPtr<ID3D12Resource> collisionResultBuffer_[kFrameCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> collisionReadbackBuffer_[kFrameCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> collisionRequestBuffer_[kFrameCount];
	Game::ContactInfo* collisionReadbackMapped_[kFrameCount]{};
	uint32_t collisionResultCount_[kFrameCount]{};
	uint32_t collisionDispatchCount_ = 0;
	uint32_t collisionMaxPairs_ = 0;
	std::vector<CollisionRequest> collisionRequests_; // バッチ用

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
	std::vector<std::string> shaderNames_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig2D_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso2D_;

	// テキスト描画用 (複数フォント対応)
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoText_;
	std::unordered_map<std::string, std::unique_ptr<DynamicGlyphCache>> glyphCaches_;
	struct TextVertex {
		float x, y;       // 位置 (NDC)
		float u, v;       // テクスチャ座標
		float r, g, b, a; // 色
	};
	// フォントパスごとに頂点を蓄積
	std::unordered_map<std::string, std::vector<TextVertex>> textVerticesMap_;

	// 3Dライン描画用
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoLine3D_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoLine3DXRay_;
	struct LineVertex {
		float x, y, z;    // 位置
		float r, g, b, a; // 色
	};
	static constexpr uint32_t kMaxLineVertices = 65536;
	std::vector<LineVertex> lineVertices_;
	std::vector<LineVertex> lineVerticesXRay_;

	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissor_{};

	bool ppEnabled_ = true;
	PostProcessParams ppParams_{};
	GraphicsSettings graphicsSettings_{};
	Matrix4x4 previousPostViewProjection_{};
	Vector3 previousPostCamera_{};
	bool postHistoryValid_ = false;
	TextureHandle cinematicLut_ = 0;
	static constexpr UINT kBloomLevels = 6;
	struct BloomTarget {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		D3D12_GPU_DESCRIPTOR_HANDLE srv{};
		UINT width = 0, height = 0;
	};
	BloomTarget bloomDown_[kBloomLevels], bloomUp_[kBloomLevels - 1];
	BloomTarget gtaoTarget_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> bloomRtvHeap_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoBloomDown_, psoBloomUp_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoGtao_;
	bool InitHdrBloom_();
	void RenderHdrBloom_(uint32_t frame);
	void RenderGtao_(uint32_t frame);
	bool RenderOcclusionComposite_(uint32_t frame, bool ambient, bool rtShadow);
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoOcclusionComposite_;
	void BindPbrTextures_(const std::vector<TextureHandle>& textures);
	TextureHandle neutralNormal_ = 0;
	UINT sceneWidth_ = WindowDX::kW, sceneHeight_ = WindowDX::kH;
	bool dlssActive_ = false;
	uint32_t temporalFrame_ = 0;
	uint32_t dlssEvaluatedFrames_ = 0;
	bool dlssHistoryValid_ = false;
	bool sceneCameraSet_ = false;
	uint32_t dlssHistoryResets_ = 0;
	Matrix4x4 unjitteredProjection_{}, unjitteredViewProjection_{}, previousTemporalViewProjection_{};
	Vector2 temporalJitter_{};
	Vector3 previousTemporalForward_{};
	float previousTemporalFov_ = 0;
	BloomTarget temporalMotion_, temporalDepth_, dlssOutput_, temporalExposure_;
	D3D12_GPU_DESCRIPTOR_HANDLE postColorSrv_{};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> temporalRtvHeap_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigTemporal_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoTemporalPrepare_, psoTemporalObjects_;
	std::unordered_map<uint64_t, Matrix4x4> temporalWorlds_;
	struct MotionJob {
		uint64_t id;
		MeshHandle mesh;
		TextureHandle texture;
		Matrix4x4 world;
	};
	std::vector<MotionJob> temporalJobs_;
	bool InitTemporalInputs_();
	bool EvaluateDlss_(uint32_t frame);
	bool InitRtShadows_();
	bool RenderRtShadows_(uint32_t frame);
	struct RtTriangle {
		Vector4 p[3], n[3];
	}; // 位置 / u と法線 / v、96 バイト
	struct RtGeometry {
		Microsoft::WRL::ComPtr<ID3D12Resource> blas, scratch;
		uint32_t triangleOffset = 0;
	};
	struct RtInstance {
		MeshHandle mesh;
		Matrix4x4 world;
		TextureHandle albedo, normal, roughness;
		Vector4 color;
		bool ground;
	};
	std::unordered_map<MeshHandle, RtGeometry> rtGeometry_;
	std::vector<RtInstance> rtInstances_;
	struct RtFrame {
		Microsoft::WRL::ComPtr<ID3D12Resource> tlas, scratch, instances, readback, triangles, triangleUpload, materials,
		    lightingReadback, lightingCounters;
		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> builtInstances;
		size_t triangleCount = 0;
		bool pending = false, lightingPending = false;
	};
	RtFrame rtFrames_[kFrameCount];
	BloomTarget rtShadowTarget_;
	D3D12_GPU_DESCRIPTOR_HANDLE rtShadowUav_{};
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigRtShadow_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoRtShadow_;
	uint32_t rtShadowFrames_ = 0, rtShadowPixels_ = 0, rtLitPixels_ = 0;
	bool rtShadowRendered_ = false;
	bool InitRtLighting_();
	bool RenderRtLighting_(uint32_t frame, const std::vector<RtInstance>& instances);
	void CompositeRtLighting_(uint32_t frame, float nearPlane, float farPlane, bool fluid);
	std::vector<RtTriangle> rtTriangles_;
	BloomTarget rtReflectionTarget_, rtIndirectTarget_;
	D3D12_GPU_DESCRIPTOR_HANDLE rtReflectionUav_{}, rtIndirectUav_{};
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigRtLighting_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoRtLighting_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoRtLightingComposite_;
	Microsoft::WRL::ComPtr<ID3D12Resource> rtLightingHdr_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtLightingRtvHeap_;
	uint32_t rtLightingFrames_ = 0, rtReflectionPixels_ = 0, rtIndirectPixels_ = 0;
	uint32_t rtReflectionHits_ = 0, rtIndirectHits_ = 0;
	bool rtLightingRendered_ = false;
	BloomTarget rtFilteredReflection_[2], rtFilteredIndirect_[2];
	D3D12_GPU_DESCRIPTOR_HANDLE rtFilteredReflectionUav_[2]{}, rtFilteredIndirectUav_[2]{};
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigRtDenoise_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoRtDenoise_;
	Matrix4x4 previousRtViewProjection_{};
	Vector3 previousRtCamera_{};
	uint32_t rtHistoryIndex_ = 0, rtHistoryMask_ = 0;
	bool rtLightingHistoryValid_ = false;
	void DenoiseRtLighting_(uint32_t frame, float nearPlane, float farPlane);

	Microsoft::WRL::ComPtr<ID3D12Resource> ppSceneColor_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ppRtvHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE ppRtv_{};
	D3D12_GPU_DESCRIPTOR_HANDLE ppSrvGpu_{};
	D3D12_RESOURCE_STATES ppSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// カスタムレンダーターゲットのトラッキング
	const CustomRenderTarget* currentCustomTarget_ = nullptr;

	// 最終描画先(エディタで表示、または画面にコピーする用)
	Microsoft::WRL::ComPtr<ID3D12Resource> finalSceneColor_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> finalRtvHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE finalRtv_{};
	D3D12_GPU_DESCRIPTOR_HANDLE finalSrvGpu_{};
	D3D12_RESOURCE_STATES finalSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	Microsoft::WRL::ComPtr<ID3D12Resource> backdropColor_;
	D3D12_CPU_DESCRIPTOR_HANDLE backdropRtv_{};
	D3D12_CPU_DESCRIPTOR_HANDLE backdropSrvCpu_{};
	D3D12_CPU_DESCRIPTOR_HANDLE backdropSrvCpuMaster_{};
	D3D12_GPU_DESCRIPTOR_HANDLE backdropSrv_{};

	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoPP_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoPPDefault_; // デフォルトPSOバックアップ

	// 最終テクスチャをバックバッファにそのままコピーして映すパイプライン
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoCopy_;

	// シャドウマップ用リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> shadowMap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv_{};
	D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv_{};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowSkinPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowInstancedPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowCutoutPso_, shadowInstancedCutoutPso_;

	// Skybox / 環境マップ
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigSkybox_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoSkybox_;
	Microsoft::WRL::ComPtr<ID3D12Resource> skyboxVB_;
	Microsoft::WRL::ComPtr<ID3D12Resource> skyboxIB_;
	D3D12_VERTEX_BUFFER_VIEW skyboxVBV_{};
	D3D12_INDEX_BUFFER_VIEW skyboxIBV_{};
	uint32_t skyboxIndexCount_ = 0;
	TextureHandle skyboxCubeMapHandle_ = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE envMapSrvGpu_{}; // 環境マップSRV (3D描画時バインド用)

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
	struct alignas(256) CBFrame {
		Matrix4x4 view;
		Matrix4x4 proj;
		Matrix4x4 viewProj;
		Vector3 cameraPos;
		float time = 0.0f;
		Vector4 windParams = {1.0f, 0.0f, 0.5f, 0.2f}; // x,y:方向, z:速度, w:強さ
		Vector3 playerPos = {0.0f, 0.0f, 0.0f};
		uint32_t useCubemapBackground = 1; // 背景も環境マップを使うか
	} cbFrame_{};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	LightCB lightCB_{};

	D3D12_GPU_VIRTUAL_ADDRESS cbFrameAddr_ = 0;
	D3D12_GPU_VIRTUAL_ADDRESS cbLightAddr_ = 0;

	bool framePPEnabled_ = false;
	bool backBufferBarrierState_ = false;

	std::vector<DrawCall> drawCalls_; // ドローコールバッファ
	// 高速化のためのキャッシュ
	int lastIDCIndex_ = -1;
	std::vector<InstancedDrawCall> instancedDrawCalls_;         // インスタンスドローコール
	std::vector<InstancedDrawCall> instancedParticleDrawCalls_; // パーティクル用
	std::vector<InstancedDrawCall> liquidParticleDrawCalls_;    // 液体パーティクル用
	std::vector<SpriteDrawCall> spriteDrawCalls_;               // スプライト用
	std::vector<SpriteDrawCall> overlaySprites_;
	std::unordered_map<std::string, std::vector<TextVertex>> overlayText_;
	bool overlayQueue_ = false;
	void FlushOverlayQueue();
	bool InitSpritePipeline_();
	bool InitUIFrame_();
	bool InitFinalTarget_();
	bool editorPresented_ = false, preserveEditorLoading_ = false;
	D3D12_VIEWPORT editorLoadingViewport_{};
	Microsoft::WRL::ComPtr<ID3D12Resource> editorLoadingBackground_;
	TextureHandle LoadTexture2DUncached_(const std::string& filePath, bool sRGB);
	TextureHandle LoadDDSTexture_(const std::string& unifiedPath, bool sRGB);
	struct TextureUpload;
	static bool DecodeTextureMips_(const std::string& unifiedPath, bool sRGB, TextureUpload& out);
	void UploadTextures_(std::vector<TextureUpload>& uploads);
	bool StageTextureUpload_(TextureUpload& upload) const;
	static bool DecodeTextureBase_(const std::string& unifiedPath, bool sRGB, TextureUpload& out);
	bool EnsureMipPipeline_();
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mipRoot_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mipPso_;
	bool mipPipelineTried_ = false;
	TextureHandle ReserveTexture_(const std::string& cacheKey);
	void SetTextureResource_(
	    TextureHandle handle, Microsoft::WRL::ComPtr<ID3D12Resource> tex, DXGI_FORMAT format, UINT mipCount);
	struct PendingTexture {
		std::string path;
		bool sRGB;
		TextureHandle handle;
	};
	std::vector<PendingTexture> pendingTextures_;
	int assetBatchDepth_ = 0;
	// アセットのまとめ読み中に記録したメッシュのアップロード: EndAssetBatch で 1 回 GPU を待つまで保持する
	// (メッシュごとに待つと、ロード画面の表示中は 1 回あたり約 1 フレームかかる)。
	std::vector<
	    std::pair<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>>>
	    pendingMeshUploads_;
	void FlushTextureBatch_();
	// 作成済みのテクスチャへ SRV を作り、キャッシュに登録する
	TextureHandle RegisterTexture_(
	    Microsoft::WRL::ComPtr<ID3D12Resource> tex, DXGI_FORMAT format, UINT mipCount, const std::string& cacheKey);
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uiAlloc_[2];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uiList_;
	std::atomic<bool> uiThreadActive_{false};
	bool recordUITextures_ = false;
	bool ready3D_ = false;
	std::unordered_map<std::string, TextureHandle> uiTextures_;
	std::recursive_mutex textMutex_;
	std::mutex srvMutex_;
	static thread_local ID3D12GraphicsCommandList* tUiList_;
	static thread_local bool tUiThread_;

	// モデルは共有ポインタで保持し、描画やキャッシュと寿命を共有する。
	std::vector<std::shared_ptr<Model>> models_;
	struct SceneryLods {
		std::array<uint32_t, 3> meshes{};
		Vector3 center{};
		float radius = 1;
		bool foliage = false;
	};
	std::unordered_map<uint32_t, SceneryLods> sceneryLods_;
	struct LodJob;
	struct PendingLod {
		uint32_t mesh;
		bool grassCards, authoredLeaves;
	};
	std::vector<PendingLod> pendingLods_;
	bool BuildDistanceLods_(LodJob& job) const;
	void FinishDistanceLods_(std::vector<LodJob>& jobs);
	bool distanceLodEnabled_ = true;
	SceneryLodStats sceneryLodStats_{}, lastSceneryLodStats_{};
	uint32_t SelectDistanceLod(uint32_t mesh, const Matrix4x4& world);
	bool TooSmallOnScreen(uint32_t mesh, const Matrix4x4& world) const;
	bool SceneryVisible(uint32_t mesh, const Matrix4x4& world, const Matrix4x4& viewProjection) const;
	bool sceneryOptimized_ = true;
	uint32_t rtTlasBuilds_ = 0;
	std::vector<Texture> textures_;

	std::unordered_map<std::string, TextureHandle> textureCache_;
	std::unordered_map<std::string, MeshHandle> meshCache_;

	// コリジョン同期用
	TextureHandle sumiEPaperTex_ = 0;
	TextureHandle sumiEVignetteTex_ = 0;

	// ポストプロセス用G-Buffer
	Microsoft::WRL::ComPtr<ID3D12Resource> ppSceneDepth_;
	D3D12_GPU_DESCRIPTOR_HANDLE ppDepthSrvGpu_{};
	D3D12_CPU_DESCRIPTOR_HANDLE ppDepthDsv_{};
	D3D12_RESOURCE_STATES ppDepthState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ppDepthDsvHeap_;
};

} // namespace Engine
