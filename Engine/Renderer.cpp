#include "Renderer.h"
#include "Model.h"
#include "PathUtils.h"
#include "../Game/ObjectTypes.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numeric>

#include <directxmath.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include "UnicodeUtils.h"
#include <d3dx12.h>
#ifdef _MSC_VER
#pragma warning(disable: 4865)
#endif

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace Engine {

Renderer* Renderer::instance_ = nullptr;

// クリア値の定数定義
static const float kPPSceneClearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
static const float kFinalSceneClearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
static const float kCustomTargetClearColor[] = { 0.1f, 0.1f, 0.12f, 1.0f };

static uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + (a - 1)) & ~(a - 1); }

uint32_t Renderer::UploadRing::Allocate(uint32_t bytes, uint32_t alignment) {
	const uint32_t aligned = AlignUp(offset, alignment);
	if (aligned + bytes > sizeBytes)
		return UINT32_MAX;
	offset = aligned + bytes;
	return aligned;
}

static Matrix4x4 XMToM4(const DirectX::XMMATRIX& xm) {
	Matrix4x4 out{};
	DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&out), xm);
	return out;
}

static XMMATRIX M4ToXM(const Matrix4x4& m) { return XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&m)); }

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Initialize(WindowDX* window) {
	instance_ = this;

	window_ = window;
	if (!window_)
		return false;

	dev_ = window_->Dev();
	list_ = window_->List();
	queue_ = window_->Queue();
	srvHeap_ = window_->SRV();
	srvInc_ = window_->SrvInc();

	if (!dev_ || !list_ || !queue_ || !srvHeap_)
		return false;

	srvDynamicCursor_ = kSrvStaticMax;

	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.Width = (float)Engine::WindowDX::kW;
	viewport_.Height = (float)Engine::WindowDX::kH;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	scissor_.left = 0;
	scissor_.top = 0;
	scissor_.right = (LONG)Engine::WindowDX::kW;
	scissor_.bottom = (LONG)Engine::WindowDX::kH;

	textures_.clear();
	models_.clear();

	// ★修正: Index 0 に「白い1x1テクスチャ」をプログラムで生成して登録する
	// これにより、テクスチャがない場合のフォールバック描画でクラッシュしなくなります
	{
		// 1. 一時的なコマンドリスト作成 (WindowDXのlist_は閉じられているため)
		ComPtr<ID3D12CommandAllocator> alloc;
		ComPtr<ID3D12GraphicsCommandList> cmd;
		dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
		dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd));

		// 2. リソース作成
		Texture t{};
		CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1);
		CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
		dev_->CreateCommittedResource(&heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&t.res));

		// 3. データ転送 (白: 0xFFFFFFFF)
		const uint32_t pixel = 0xFFFFFFFF;
		const UINT64 uploadSize = GetRequiredIntermediateSize(t.res.Get(), 0, 1);
		ComPtr<ID3D12Resource> uploadBuf;
		CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		dev_->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));

		D3D12_SUBRESOURCE_DATA subData{};
		subData.pData = &pixel;
		subData.RowPitch = 4;
		subData.SlicePitch = 4;
		UpdateSubresources(cmd.Get(), t.res.Get(), uploadBuf.Get(), 0, 0, 1, &subData);

		// 4. バリア変更
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(t.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &barrier);

		// 5. 実行と待機
		cmd->Close();
		ID3D12CommandList* ppLists[] = {cmd.Get()};
		queue_->ExecuteCommandLists(1, ppLists);
		WaitGPU(); // 完了待ち

		// 6. SRV作成
		const uint32_t srvIdx = AllocateSrvIndex();
		t.srvCpu = window_->SRV_CPU((int)srvIdx);
		t.srvCpuMaster = window_->SRV_CPU_Master((int)srvIdx); // ★追加
		t.srvGpu = window_->SRV_GPU((int)srvIdx);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		dev_->CreateShaderResourceView(t.res.Get(), &srvDesc, t.srvCpu);
		dev_->CreateShaderResourceView(t.res.Get(), &srvDesc, t.srvCpuMaster); // ★追加: 二重化

		// textures_[0] として登録
		textures_.push_back(t);
	}

	// ダミーのモデル (Index 0)
	models_.push_back(std::make_shared<Model>());

	for (uint32_t i = 0; i < kFrameCount; ++i) {
		// 8MB -> 32MB に増量 (大量のインスタンシング描画に対応)
		upload_[i].sizeBytes = 32 * 1024 * 1024;
		CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(upload_[i].sizeBytes);

		if (FAILED(dev_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload_[i].buffer)))) {
			return false;
		}
		if (FAILED(upload_[i].buffer->Map(0, nullptr, (void**)&upload_[i].mapped))) {
			return false;
		}
		upload_[i].offset = 0;
	}

	lightCB_.ambientColor = Vector3{0.1f, 0.1f, 0.1f};

	// ★追加: シャドウマップリソースの作成
	{
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = 2048;
		desc.Height = 2048;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R32_TYPELESS; 
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE clearVal{};
		clearVal.Format = DXGI_FORMAT_D32_FLOAT;
		clearVal.DepthStencil.Depth = 1.0f;
		clearVal.DepthStencil.Stencil = 0;

		CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);
		dev_->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal, IID_PPV_ARGS(&shadowMap_));

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
		dsvHeapDesc.NumDescriptors = 16; // ★修正: カスタムレンダーターゲット用に増やす
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dev_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&shadowDsvHeap_));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		shadowDsv_ = shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
		dev_->CreateDepthStencilView(shadowMap_.Get(), &dsvDesc, shadowDsv_);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		const uint32_t sIdx = AllocateSrvIndex();
		dev_->CreateShaderResourceView(shadowMap_.Get(), &srvDesc, window_->SRV_CPU((int)sIdx));
		dev_->CreateShaderResourceView(shadowMap_.Get(), &srvDesc, window_->SRV_CPU_Master((int)sIdx)); // ★追加
		shadowSrv_ = window_->SRV_GPU((int)sIdx);
	}

	if (!InitPipelines())
		return false;

	if (!InitPostProcess_())
		return false;

	ppEnabled_ = true;

	// ★追加: コリジョン同期用のコマンドリスト作成
	if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&collisionAlloc_)))) return false;
	if (FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, collisionAlloc_.Get(), nullptr, IID_PPV_ARGS(&collisionList_)))) return false;
	collisionList_->Close(); // 最初は閉じておく

	// テキストシステム初期化
	// ※"msgothic.ttc" はWindows環境依存ですがテスト用に使用
	InitTextSystem("C:\\Windows\\Fonts\\msgothic.ttc", 64.0f);

	// ★追加: デフォルトのプロシージャルSkybox生成（グラデーション空）
	{
		TextureHandle cubeHandle = LoadCubeMap("Resources/Textures/skybox.dds");
		if (cubeHandle == 0 || cubeHandle >= textures_.size()) {
			OutputDebugStringA("[Renderer] Proceeding without custom skybox.dds\n");
		} else {
			SetSkyboxTexture(cubeHandle);
			OutputDebugStringA(("[Renderer] Loaded skybox from DDS, handle=" + std::to_string(cubeHandle) + "\n").c_str());
		}
	}

	return true;
}

void Renderer::Shutdown() {
	WaitGPU();

	instance_ = nullptr;

	for (uint32_t i = 0; i < kFrameCount; ++i) {
		if (upload_[i].buffer)
			upload_[i].buffer->Unmap(0, nullptr);
		upload_[i] = UploadRing{};
	}

	rootSig3D_.Reset();
	pipelines_.clear();
	shaderNames_.clear();

	// ★追加: Skyboxリソース解放
	rootSigSkybox_.Reset();
	psoSkybox_.Reset();
	skyboxVB_.Reset();
	skyboxIB_.Reset();
	skyboxCubeMapHandle_ = 0;
	envMapSrvGpu_ = {};

	rootSig2D_.Reset();
	pso2D_.Reset();

	psoPP_.Reset();
	rootSigPP_.Reset();
	ppSceneColor_.Reset();
	ppRtvHeap_.Reset();
	ppSrvGpu_ = {};
	ppRtv_ = {};
	ppSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// ★追加: 最終描画先の解放
	finalSceneColor_.Reset();
	finalRtvHeap_.Reset();
	finalSrvGpu_ = {};
	finalRtv_ = {};
	finalSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	psoCopy_.Reset();

	if (collisionReadbackBuffer_ && collisionReadbackMapped_) {
		collisionReadbackBuffer_->Unmap(0, nullptr);
		collisionReadbackMapped_ = nullptr;
	}
	collisionResultBuffer_.Reset();
	collisionReadbackBuffer_.Reset();
	collisionAlloc_.Reset();
	collisionList_.Reset();

	ppEnabled_ = true;
	ppParams_ = PostProcessParams{};

	models_.clear();
	textures_.clear();
	textureCache_.clear();
	meshCache_.clear();

	window_ = nullptr;
	dev_ = nullptr;
	list_ = nullptr;
	queue_ = nullptr;
	srvHeap_ = nullptr;
	srvInc_ = 0;
	srvCursor_ = 10;

	cbFrameAddr_ = 0;
	cbLightAddr_ = 0;
}

void Renderer::WaitGPU() {
	if (window_)
		window_->WaitIdle();
}

void Renderer::BeginFrame(const float clearColorRGBA[4]) {
	ID3D12DescriptorHeap* heaps[] = {srvHeap_};
	list_->SetDescriptorHeaps(1, heaps);

	const uint32_t fi = window_->FrameIndex();
	upload_[fi].Reset();

	drawCalls_.clear(); // ★追加: ドローコールをクリア
	
	cbFrame_.time += 0.016f; // 固定値だが、本来はDeltaTimeを使うべき

	// インスタンス描画用のキューをクリア
	instancedDrawCalls_.clear();                    
	instancedParticleDrawCalls_.clear();            
	lastIDCIndex_ = -1;
	srvDynamicCursor_ = kSrvStaticMax; // 動的SRVカーソルをリセット
	spriteDrawCalls_.clear(); // ★スプライトもクリア

	cbFrameAddr_ = 0;
	backBufferBarrierState_ = false;

	framePPEnabled_ = ppEnabled_ && ppSceneColor_;

	cbFrame_.time += 1.0f / 60.0f;

	{
		const uint32_t off = upload_[fi].Allocate(sizeof(CBFrame), 256);
		if (off != UINT32_MAX) {
			std::memcpy(upload_[fi].mapped + off, &cbFrame_, sizeof(CBFrame));
			cbFrameAddr_ = upload_[fi].buffer->GetGPUVirtualAddress() + off;
		}
	}
	{
		const uint32_t off = upload_[fi].Allocate(sizeof(LightCB), 256);
		if (off != UINT32_MAX) {
			std::memcpy(upload_[fi].mapped + off, &lightCB_, sizeof(LightCB));
			cbLightAddr_ = upload_[fi].buffer->GetGPUVirtualAddress() + off;
		}
	}

	// ★修正: 常に ppSceneColor_ (Render To Texture) に描画するように変更する
	// これにより、後続のEndFrameでPostProcessやコピーパスを経て finalSceneColor_ に焼き付けられる
	if (ppSceneState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(ppSceneColor_.Get(), ppSceneState_, D3D12_RESOURCE_STATE_RENDER_TARGET);
		list_->ResourceBarrier(1, &b);
		ppSceneState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	auto rtv = ppRtv_;
	auto dsv = window_->DSV_CPU(0);
	list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	list_->ClearRenderTargetView(rtv, clearColorRGBA, 0, nullptr);
	list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// GameSceneの描画時は常に画面全体(1920x1080)のViewportを使用する
	D3D12_VIEWPORT fullVP = { 0.0f, 0.0f, static_cast<float>(window_->kW), static_cast<float>(window_->kH), 0.0f, 1.0f };
	D3D12_RECT fullScissor = { 0, 0, static_cast<LONG>(window_->kW), static_cast<LONG>(window_->kH) };
	list_->RSSetViewports(1, &fullVP);
	list_->RSSetScissorRects(1, &fullScissor);
}

void Renderer::SetGameViewport(float x, float y, float w, float h) {
	viewport_.TopLeftX = x;
	viewport_.TopLeftY = y;
	viewport_.Width = w;
	viewport_.Height = h;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	scissor_.left = static_cast<LONG>(x);
	scissor_.top = static_cast<LONG>(y);
	scissor_.right = static_cast<LONG>(x + w);
	scissor_.bottom = static_cast<LONG>(y + h);
}

void Renderer::ResetGameViewport() {
	if (!window_) return;
	float w = static_cast<float>(window_->kW);
	float h = static_cast<float>(window_->kH);

	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.Width = w;
	viewport_.Height = h;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	scissor_.left = 0;
	scissor_.top = 0;
	scissor_.right = static_cast<LONG>(w);
	scissor_.bottom = static_cast<LONG>(h);
}

void Renderer::FlushDrawCalls() {
	// オブジェクトが無くてもSkyboxは描画したいため、早期リターンをSkybox描画の後に移動します。

	const uint32_t fi = window_->FrameIndex();

	cbFrameAddr_ = upload_[fi].buffer->GetGPUVirtualAddress() + upload_[fi].Allocate(sizeof(CBFrame), 256);
	std::memcpy(upload_[fi].mapped + (cbFrameAddr_ - upload_[fi].buffer->GetGPUVirtualAddress()), &cbFrame_, sizeof(CBFrame));

	cbLightAddr_ = upload_[fi].buffer->GetGPUVirtualAddress() + upload_[fi].Allocate(sizeof(LightCB), 256);
	std::memcpy(upload_[fi].mapped + (cbLightAddr_ - upload_[fi].buffer->GetGPUVirtualAddress()), &lightCB_, sizeof(LightCB));

	list_->SetGraphicsRootSignature(rootSig3D_.Get());
	list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
	list_->SetGraphicsRootConstantBufferView(2, cbLightAddr_);
	if (shadowSrv_.ptr != 0) {
		list_->SetGraphicsRootDescriptorTable(5, shadowSrv_);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE defaultSrv = textures_[0].srvGpu;

	// ★追加: Skybox描画（最背面、全ドローコールの前）
	DrawSkybox();

	// ★RootSignatureを3D用に戻す（DrawSkyboxで変更されたため）
	list_->SetGraphicsRootSignature(rootSig3D_.Get());
	list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
	list_->SetGraphicsRootConstantBufferView(2, cbLightAddr_);
	if (shadowSrv_.ptr != 0) list_->SetGraphicsRootDescriptorTable(5, shadowSrv_);

	// ★追加: 環境マップ(t3)をバインド
	if (envMapSrvGpu_.ptr != 0) {
		list_->SetGraphicsRootDescriptorTable(7, envMapSrvGpu_);
	} else {
		list_->SetGraphicsRootDescriptorTable(7, textures_[0].srvGpu); // フォールバック (RootSig要件を満たすため)
	}

	// ★追加: Skybox描画後にオブジェクトがなければ早期リターン
	if (drawCalls_.empty() && instancedDrawCalls_.empty() && instancedParticleDrawCalls_.empty()) {
		FlushLines();
		return;
	}

	for (const auto& dc : drawCalls_) {
		if (dc.shaderName == "Distortion") continue; // 空間のゆがみは EndFrame で別途描画

		auto* model = GetModel(dc.mesh);
		if (!model) continue;

		ID3D12PipelineState* pso = pipelines_["Default"].Get();
		if (pipelines_.find(dc.shaderName) != pipelines_.end()) {
			pso = pipelines_[dc.shaderName].Get();
		}

		if (!pso) continue;
		list_->SetPipelineState(pso);

		list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
		list_->SetGraphicsRootConstantBufferView(2, cbLightAddr_);
		if (shadowSrv_.ptr != 0) {
			list_->SetGraphicsRootDescriptorTable(5, shadowSrv_);
		}

		if (dc.shaderName == "EnhancedTerrain") {
			list_->SetGraphicsRootSignature(rootSigTerrain_.Get());
			list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
			list_->SetGraphicsRootConstantBufferView(2, cbLightAddr_);
			
			// t0-t5: Descriptor Table
			uint32_t sIdx = AllocateDynamicSrvIndex(6);
			if (sIdx != UINT32_MAX) {
				D3D12_CPU_DESCRIPTOR_HANDLE dest = window_->SRV_CPU((int)sIdx);
				D3D12_GPU_DESCRIPTOR_HANDLE destGpu = window_->SRV_GPU((int)sIdx);
				for (int i = 0; i < 6; ++i) {
					Texture* texObj = &textures_[0];
					if (i == 0 && dc.tex < textures_.size()) texObj = &textures_[dc.tex];
					else if (i > 0 && (i - 1) < (int)dc.extraTex.size() && dc.extraTex[i - 1] < textures_.size())
						texObj = &textures_[dc.extraTex[i - 1]];

					if (texObj && texObj->res) {
						D3D12_RESOURCE_DESC resDesc = texObj->res->GetDesc();
						D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
						srvDesc.Format = resDesc.Format;
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
						srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						srvDesc.Texture2D.MipLevels = resDesc.MipLevels > 0 ? resDesc.MipLevels : 1;
						dev_->CreateShaderResourceView(texObj->res.Get(), &srvDesc, dest);
					}
					dest.ptr += srvInc_;
				}
				list_->SetGraphicsRootDescriptorTable(3, destGpu);
			}
			if (shadowSrv_.ptr != 0) list_->SetGraphicsRootDescriptorTable(4, shadowSrv_);
		} else {
			list_->SetGraphicsRootSignature(rootSig3D_.Get());
			list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
			list_->SetGraphicsRootConstantBufferView(2, cbLightAddr_);
			if (shadowSrv_.ptr != 0) list_->SetGraphicsRootDescriptorTable(5, shadowSrv_);
			
			// param 7 is no longer used by ObjPS since we use procedural space reflection.
			// Just bind dummy to prevent RootSignature uninitialized warnings.
			list_->SetGraphicsRootDescriptorTable(7, textures_[0].srvGpu);
			
			if (dc.tex != 0 && dc.tex < textures_.size()) {
				list_->SetGraphicsRootDescriptorTable(3, textures_[dc.tex].srvGpu);
			} else if (model->GetSrvGpu().ptr != 0) {
				list_->SetGraphicsRootDescriptorTable(3, model->GetSrvGpu());
			} else {
				list_->SetGraphicsRootDescriptorTable(3, textures_[0].srvGpu);
			}
		}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
		struct alignas(256) CBObj { Matrix4x4 world; float color[4]; float reflectivity; float pad[3]; };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
		CBObj ocb{}; ocb.world = dc.worldMatrix; 
		ocb.color[0]=dc.color.x; ocb.color[1]=dc.color.y; ocb.color[2]=dc.color.z; ocb.color[3]=dc.color.w;
		ocb.reflectivity = dc.reflectivity;
		uint32_t oOff = upload_[fi].Allocate(sizeof(CBObj), 256);
		if (oOff != UINT32_MAX) {
			std::memcpy(upload_[fi].mapped + oOff, &ocb, sizeof(CBObj));
			auto gpuAddr = upload_[fi].buffer->GetGPUVirtualAddress() + oOff;
			list_->SetGraphicsRootConstantBufferView(1, gpuAddr);
			
			// ★重要: EnhancedTerrain の場合は StructuredBuffer<InstanceData> (Slot 5) にもバインド
			if (dc.shaderName == "EnhancedTerrain") {
				list_->SetGraphicsRootShaderResourceView(5, gpuAddr);
			}
		}
		
		if (dc.shaderName != "EnhancedTerrain") {
			if (dc.isSkinned) {
				struct CBBone { Matrix4x4 bones[128]; };
				CBBone boneData{};
				size_t count = (std::min)(dc.bones.size(), size_t(128));
				std::memcpy(boneData.bones, dc.bones.data(), count * sizeof(Matrix4x4));
				uint32_t bOff = upload_[fi].Allocate(sizeof(CBBone), 256);
				std::memcpy(upload_[fi].mapped + bOff, &boneData, sizeof(CBBone));
				list_->SetGraphicsRootConstantBufferView(4, upload_[fi].buffer->GetGPUVirtualAddress() + bOff);
			} else {
				list_->SetGraphicsRootConstantBufferView(4, upload_[fi].buffer->GetGPUVirtualAddress() + oOff);
			}
		}

		// ★トゥーン: アウトライン先行描画パス
		// Toon/ToonSkinning のドローコールに対して、アウトラインPSOで先に描画し、その後にモデル本体を描画する
		if (dc.shaderName == "Toon" || dc.shaderName == "ToonSkinning") {
			// アウトラインPSOを選択
			std::string outlineName = dc.isSkinned ? "ToonSkinningOutline" : "ToonOutline";
			if (pipelines_.count(outlineName)) {
				list_->SetPipelineState(pipelines_[outlineName].Get());
				model->Draw(list_);
			}
			// 本体PSOに戻す
			list_->SetPipelineState(pso);
		}

		model->Draw(list_);
	}

	// ====== インスタンス描画の共通処理関数 (ラムダ) ======
	auto flushInstanced = [&](std::vector<InstancedDrawCall>& calls, const std::string& defaultShaderName) {
		if (calls.empty()) return;

		for (auto& idc : calls) {
			auto* model = GetModel(idc.mesh);
			if (!model || idc.instances.empty()) continue;

			// シェーダー設定
			std::string sName = idc.shaderName;
			if (sName == "Default") sName = defaultShaderName;
			// "Particle" -> "ParticleInstanced", "ParticleAdditive" -> "ParticleAdditiveInstanced" への自動マッピング
			if (defaultShaderName == "ParticleInstanced") {
				if (sName == "Particle") sName = "ParticleInstanced";
				else if (sName == "ParticleAdditive") sName = "ParticleAdditiveInstanced";
				else if (sName == "ProceduralSmoke") sName = "ProceduralSmokeInstanced";
				else if (sName == "ProceduralSmokeAdditive") sName = "ProceduralSmokeAdditiveInstanced";
			}
			// ★修正: Toon系や新しく追加したリッチシェーダーはインスタンス描画非対応のためデフォルトにフォールバック
			if (sName == "Toon" || sName == "ToonSkinning" || sName == "ToonOutline" || sName == "ToonSkinningOutline" ||
				sName == "Hologram" || sName == "EmissiveGlow" || sName == "ForceField" || sName == "Dissolve" || sName == "Distortion" || sName == "Reflection") {
				sName = defaultShaderName;
			}

			if (pipelines_.count(sName)) {
				list_->SetPipelineState(pipelines_[sName].Get());
			} else if (pipelines_.count(defaultShaderName)) {
				list_->SetPipelineState(pipelines_[defaultShaderName].Get());
			} else {
				continue; // 有効なPSOがない場合はスキップ
			}

			// テクスチャ設定
			// インスタンスデータの転送
			uint32_t dataSize = static_cast<uint32_t>(sizeof(InstanceData) * idc.instances.size());
			uint32_t offset = upload_[fi].Allocate(dataSize, 256);
			if (offset != UINT32_MAX) {
				std::memcpy(upload_[fi].mapped + offset, idc.instances.data(), dataSize);
				
				// ★修正: EnhancedTerrainの場合のセットアップ (RootSignatureを先に設定してからバインドする)
				if (sName == "EnhancedTerrain") {
					list_->SetGraphicsRootSignature(rootSigTerrain_.Get());
					list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
					list_->SetGraphicsRootConstantBufferView(2, cbLightAddr_);
					if (shadowSrv_.ptr != 0) list_->SetGraphicsRootDescriptorTable(4, shadowSrv_);

					// テクスチャバインド
					uint32_t sIdx = AllocateDynamicSrvIndex(6);
					if (sIdx != UINT32_MAX) {
						D3D12_CPU_DESCRIPTOR_HANDLE dest = window_->SRV_CPU((int)sIdx);
						D3D12_GPU_DESCRIPTOR_HANDLE destGpu = window_->SRV_GPU((int)sIdx);
						for (int i = 0; i < 6; ++i) {
							Texture* texObj = &textures_[0];
							if (i == 0 && idc.tex < textures_.size()) texObj = &textures_[idc.tex];
							else if (i > 0 && (i - 1) < (int)idc.extraTex.size() && idc.extraTex[i - 1] < textures_.size())
								texObj = &textures_[idc.extraTex[i - 1]];

							if (texObj->res) {
								D3D12_RESOURCE_DESC resDesc = texObj->res->GetDesc();
								D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
								srvDesc.Format = resDesc.Format;
								srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
								srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
								srvDesc.Texture2D.MipLevels = resDesc.MipLevels > 0 ? resDesc.MipLevels : 1;
								dev_->CreateShaderResourceView(texObj->res.Get(), &srvDesc, dest);
							}
							dest.ptr += srvInc_;
						}
						list_->SetGraphicsRootDescriptorTable(3, destGpu);
					}
					// インスタンスデータバインド (Slot 5)
					list_->SetGraphicsRootShaderResourceView(5, upload_[fi].buffer->GetGPUVirtualAddress() + offset);
				} else {
					list_->SetGraphicsRootSignature(rootSig3D_.Get());
					list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
					list_->SetGraphicsRootConstantBufferView(2, cbLightAddr_);
					if (shadowSrv_.ptr != 0) list_->SetGraphicsRootDescriptorTable(5, shadowSrv_);
					
					// テクスチャ
					if (idc.tex != 0 && idc.tex < textures_.size()) {
						list_->SetGraphicsRootDescriptorTable(3, textures_[idc.tex].srvGpu);
					} else {
						list_->SetGraphicsRootDescriptorTable(3, textures_[0].srvGpu);
					}
					// インスタンスデータバインド (Slot 6)
					list_->SetGraphicsRootShaderResourceView(6, upload_[fi].buffer->GetGPUVirtualAddress() + offset);
				}
			}

			model->DrawInstanced(list_, static_cast<uint32_t>(idc.instances.size()));
		}
		calls.clear();
	};

	// 通常オブジェクトのインスタンス描画
	flushInstanced(instancedDrawCalls_, "Instanced");
	
	// パーティクルのインスタンス描画
	flushInstanced(instancedParticleDrawCalls_, "ParticleInstanced");

	FlushLines();
	drawCalls_.clear();
}

void Renderer::EndFrame() {
	const uint32_t fi = window_->FrameIndex();

	// ====== 0. シャドウ行列の計算 ======
	Matrix4x4 lightVP = Matrix4x4::Identity();
	if (lightCB_.dirLights[0].enabled) {
		Vector3 ldir = lightCB_.dirLights[0].direction;
		Vector3 target = cbFrame_.cameraPos;
		Vector3 pos = { target.x - ldir.x * 50.0f, target.y - ldir.y * 50.0f, target.z - ldir.z * 50.0f };
		
		DirectX::XMVECTOR pObj = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&pos));
		DirectX::XMVECTOR tObj = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&target));
		DirectX::XMVECTOR uObj = DirectX::XMVectorSet(0, 1, 0, 0);
		if (std::abs(ldir.y) > 0.999f) uObj = DirectX::XMVectorSet(1, 0, 0, 0);
		DirectX::XMMATRIX vMat = DirectX::XMMatrixLookAtLH(pObj, tObj, uObj);
		DirectX::XMMATRIX pMat = DirectX::XMMatrixOrthographicLH(100.0f, 100.0f, 1.0f, 100.0f);
		lightVP = XMToM4(vMat * pMat);
		lightCB_.shadowMatrix = lightVP;
	}

	// ====== 1. シャドウパス ======
	if (shadowMap_) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(shadowMap_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		list_->ResourceBarrier(1, &b);
		
		list_->ClearDepthStencilView(shadowDsv_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		list_->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv_);

		D3D12_VIEWPORT svp = { 0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f };
		D3D12_RECT ssc = { 0, 0, 2048, 2048 };
		list_->RSSetViewports(1, &svp);
		list_->RSSetScissorRects(1, &ssc);

		CBFrame scb = cbFrame_;
		scb.viewProj = lightVP;
		uint32_t off = upload_[fi].Allocate(sizeof(CBFrame), 256);
		std::memcpy(upload_[fi].mapped + off, &scb, sizeof(CBFrame));
		D3D12_GPU_VIRTUAL_ADDRESS sCbAddr = upload_[fi].buffer->GetGPUVirtualAddress() + off;

		list_->SetGraphicsRootSignature(rootSig3D_.Get());
		
		// Normal Shadow Pass
		for (const auto& dc : drawCalls_) {
			if (dc.isParticle || dc.shaderName == "Particle" || dc.shaderName == "ParticleAdditive" || dc.shaderName == "ProceduralSmoke" || dc.shaderName == "ProceduralSmokeAdditive" || dc.shaderName == "2D" || dc.shaderName == "Distortion") continue;

			auto* model = GetModel(dc.mesh);
			if (!model) continue;

			list_->SetPipelineState(dc.isSkinned ? shadowSkinPso_.Get() : shadowPso_.Get());
			list_->SetGraphicsRootConstantBufferView(0, sCbAddr);

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
			struct alignas(256) CBObj { Matrix4x4 world; float color[4]; float reflectivity; float pad[3]; };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
			CBObj objCb{}; objCb.world = dc.worldMatrix; objCb.reflectivity = dc.reflectivity;
			uint32_t oOff = upload_[fi].Allocate(sizeof(CBObj), 256);
			std::memcpy(upload_[fi].mapped + oOff, &objCb, sizeof(CBObj));
			list_->SetGraphicsRootConstantBufferView(1, upload_[fi].buffer->GetGPUVirtualAddress() + oOff);

			if (dc.isSkinned) {
				struct CBBone { Matrix4x4 bones[128]; };
				CBBone boneData{};
				size_t count = (std::min)(dc.bones.size(), size_t(128));
				std::memcpy(boneData.bones, dc.bones.data(), count * sizeof(Matrix4x4));
				uint32_t bOff = upload_[fi].Allocate(sizeof(CBBone), 256);
				std::memcpy(upload_[fi].mapped + bOff, &boneData, sizeof(CBBone));
				list_->SetGraphicsRootConstantBufferView(4, upload_[fi].buffer->GetGPUVirtualAddress() + bOff);
			} else {
				list_->SetGraphicsRootConstantBufferView(4, upload_[fi].buffer->GetGPUVirtualAddress() + oOff);
			}

			model->Draw(list_, 3);
		}

		// Instanced Shadow Pass
		for (const auto& idc : instancedDrawCalls_) {
			if (idc.shaderName == "Particle" || idc.shaderName == "ParticleInstanced" || idc.shaderName == "ProceduralSmoke" || idc.shaderName == "ProceduralSmokeInstanced" || idc.shaderName == "2D" || idc.shaderName == "Distortion") continue;
			
			auto* model = GetModel(idc.mesh);
			if (!model || idc.instances.empty()) continue;

			list_->SetPipelineState(shadowInstancedPso_.Get());
			list_->SetGraphicsRootSignature(rootSig3D_.Get());
			list_->SetGraphicsRootConstantBufferView(0, sCbAddr);

			uint32_t dataSize = static_cast<uint32_t>(sizeof(InstanceData) * idc.instances.size());
			uint32_t offset = upload_[fi].Allocate(dataSize, 256);
			if (offset != UINT32_MAX) {
				std::memcpy(upload_[fi].mapped + offset, idc.instances.data(), dataSize);
				list_->SetGraphicsRootShaderResourceView(6, upload_[fi].buffer->GetGPUVirtualAddress() + offset);
				model->DrawInstanced(list_, static_cast<uint32_t>(idc.instances.size()));
			}
		}

		b = CD3DX12_RESOURCE_BARRIER::Transition(shadowMap_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		list_->ResourceBarrier(1, &b);
	}

	// ====== 2. メインパス ======
	auto rtv = ppRtv_;
	auto dsv = window_->DSV_CPU(0);
	list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	list_->RSSetViewports(1, &viewport_);
	list_->RSSetScissorRects(1, &scissor_);

	// Distortion オブジェクトの事前集約 (Mesh-Texture のペアごとに InstanceData をまとめる)
	struct DistortionKey {
		MeshHandle mesh;
		TextureHandle tex;
		bool operator<(const DistortionKey& o) const {
			if (mesh != o.mesh) return mesh < o.mesh;
			return tex < o.tex;
		}
	};
	std::map<DistortionKey, std::vector<InstanceData>> aggregatedJobs;

	for (const auto& dc : drawCalls_) {
		if (dc.shaderName == "Distortion") {
			InstanceData id; id.world = dc.worldMatrix; id.color = dc.color; id.uvScaleOffset = dc.uvScaleOffset;
			aggregatedJobs[{dc.mesh, dc.tex}].push_back(id);
		}
	}
	for (auto it = instancedDrawCalls_.begin(); it != instancedDrawCalls_.end(); ) {
		if (it->shaderName == "Distortion") {
			auto& target = aggregatedJobs[{it->mesh, it->tex}];
			target.insert(target.end(), it->instances.begin(), it->instances.end());
			it = instancedDrawCalls_.erase(it);
		} else ++it;
	}

	FlushDrawCalls();
	
	// ★完全最適化: 空間のゆがみ (Distortion) パス (一括インスタンス描画)
	if (!aggregatedJobs.empty()) {
		if (ppSceneColor_ && backdropColor_) {
			auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(ppSceneColor_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
			auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(backdropColor_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
			D3D12_RESOURCE_BARRIER bars[] = { b1, b2 };
			list_->ResourceBarrier(2, bars);
			list_->CopyResource(backdropColor_.Get(), ppSceneColor_.Get());
			auto b3 = CD3DX12_RESOURCE_BARRIER::Transition(ppSceneColor_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
			auto b4 = CD3DX12_RESOURCE_BARRIER::Transition(backdropColor_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			D3D12_RESOURCE_BARRIER bars2[] = { b3, b4 };
			list_->ResourceBarrier(2, bars2);
			ppSceneState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
		}

		if (ppSceneColor_ && backdropColor_) {
			list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
			list_->SetGraphicsRootSignature(rootSigDistortion_.Get()); 
			list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
			list_->SetGraphicsRootConstantBufferView(1, cbFrameAddr_);
			list_->RSSetViewports(1, &viewport_);
			
			uint32_t bIdx = AllocateDynamicSrvIndex(1);
			if (bIdx != UINT32_MAX) {
				D3D12_CPU_DESCRIPTOR_HANDLE bDest = window_->SRV_CPU((int)bIdx);
				dev_->CopyDescriptorsSimple(1, bDest, backdropSrvCpuMaster_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				list_->SetGraphicsRootDescriptorTable(2, window_->SRV_GPU((int)bIdx));
			}

			for (auto& pair : aggregatedJobs) {
				const auto& key = pair.first;
				const auto& instances = pair.second;
				auto* model = GetModel(key.mesh);
				if (!model || instances.empty()) continue;
				if (pipelines_.count("Distortion")) {
					list_->SetPipelineState(pipelines_["Distortion"].Get());
				} else continue;

				uint32_t sIdx = AllocateDynamicSrvIndex(1);
				if (sIdx != UINT32_MAX) {
					D3D12_CPU_DESCRIPTOR_HANDLE dest = window_->SRV_CPU((int)sIdx);
					// ★修正: マスター記述子をソースにする
					D3D12_CPU_DESCRIPTOR_HANDLE src = (key.tex < textures_.size() && textures_[key.tex].res) ? textures_[key.tex].srvCpuMaster : textures_[0].srvCpuMaster;
					dev_->CopyDescriptorsSimple(1, dest, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					list_->SetGraphicsRootDescriptorTable(3, window_->SRV_GPU((int)sIdx));

					uint32_t dataSize = static_cast<uint32_t>(sizeof(InstanceData) * instances.size());
					uint32_t offset = upload_[fi].Allocate(dataSize, 256);
					if (offset != UINT32_MAX) {
						std::memcpy(upload_[fi].mapped + offset, instances.data(), dataSize);
						list_->SetGraphicsRootShaderResourceView(4, upload_[fi].buffer->GetGPUVirtualAddress() + offset);
						auto vbv = model->GetVBV(); auto ibv = model->GetIBV();
						list_->IASetVertexBuffers(0, 1, &vbv);
						list_->IASetIndexBuffer(&ibv);
						list_->DrawIndexedInstanced(model->GetIndexCount(), (uint32_t)instances.size(), 0, 0, 0);
					}
				}
			}
		}
	}

	// --- 1. sceneBaseColor_ (ppSceneColor_) をShaderResourceStateに遷移 ---
	if (ppSceneState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(ppSceneColor_.Get(), ppSceneState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		list_->ResourceBarrier(1, &b);
		ppSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	// --- 2. finalSceneColor_ をRenderTargetStateに遷移し描画 ---
	if (finalSceneState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(finalSceneColor_.Get(), finalSceneState_, D3D12_RESOURCE_STATE_RENDER_TARGET);
		list_->ResourceBarrier(1, &b);
		finalSceneState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	auto rtvFinal = finalRtv_;
	list_->OMSetRenderTargets(1, &rtvFinal, FALSE, nullptr);
	list_->ClearRenderTargetView(rtvFinal, kFinalSceneClearColor, 0, nullptr);

	if (framePPEnabled_) {
		list_->SetPipelineState(psoPP_.Get());
	} else {
		list_->SetPipelineState(psoCopy_.Get());
	}
	list_->SetGraphicsRootSignature(rootSigPP_.Get());
	list_->RSSetViewports(1, &viewport_);
	list_->RSSetScissorRects(1, &scissor_);

	ID3D12DescriptorHeap* heaps[] = {srvHeap_};
	list_->SetDescriptorHeaps(1, heaps);

	if (framePPEnabled_) {
		ppParams_.time += 1.0f / 60.0f;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
		struct alignas(256) CBPost {
			float time;
			float noiseStrength;
			float distortion;
			float chromaShift;
			float vignette;
			float scanline;
			float san;
			float pad0;
			float pad[8];
		};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
		CBPost cb{};
		cb.time = ppParams_.time;
		cb.noiseStrength = ppParams_.noiseStrength;
		cb.distortion = ppParams_.distortion;
		cb.chromaShift = ppParams_.chromaShift;
		cb.vignette = ppParams_.vignette;
		cb.scanline = ppParams_.scanline;
		cb.san = ppParams_.san;

		const uint32_t off = upload_[fi].Allocate(sizeof(CBPost), 256);
		if (off != UINT32_MAX) {
			std::memcpy(upload_[fi].mapped + off, &cb, sizeof(CBPost));
			const D3D12_GPU_VIRTUAL_ADDRESS cbAddr = upload_[fi].buffer->GetGPUVirtualAddress() + off;
			list_->SetGraphicsRootConstantBufferView(0, cbAddr);
		}
	}

	list_->SetGraphicsRootDescriptorTable(1, ppSrvGpu_);
	list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	list_->DrawInstanced(3, 1, 0, 0);

	FlushSprites();
	FlushText(); // ★追加: メインUI描画後にテキスト描画を実行

	if (finalSceneState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(finalSceneColor_.Get(), finalSceneState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		list_->ResourceBarrier(1, &b);
		finalSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	ID3D12Resource* backBuffer = window_->GetCurrentBackBufferResource();
	auto br = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	list_->ResourceBarrier(1, &br);

	auto rtvBack = window_->GetCurrentRTV();
	list_->OMSetRenderTargets(1, &rtvBack, FALSE, nullptr);
	const float bgClearColor[] = { 0.06f, 0.06f, 0.06f, 1.0f };
	list_->ClearRenderTargetView(rtvBack, bgClearColor, 0, nullptr);

	D3D12_VIEWPORT fullVP = { 0.0f, 0.0f, static_cast<float>(window_->kW), static_cast<float>(window_->kH), 0.0f, 1.0f };
	D3D12_RECT fullScissor = { 0, 0, static_cast<LONG>(window_->kW), static_cast<LONG>(window_->kH) };
	list_->RSSetViewports(1, &fullVP);
	list_->RSSetScissorRects(1, &fullScissor);

	list_->SetPipelineState(psoCopy_.Get());
	list_->SetGraphicsRootSignature(rootSigPP_.Get());
	list_->SetGraphicsRootDescriptorTable(1, finalSrvGpu_);
	list_->DrawInstanced(3, 1, 0, 0);
}

void Renderer::SetCamera(const Camera& camera) {
	cbFrame_.view = XMToM4(camera.View());
	cbFrame_.proj = XMToM4(camera.Proj());
	cbFrame_.viewProj = Matrix4x4::Multiply(cbFrame_.view, cbFrame_.proj);
	auto p = camera.Position();
	cbFrame_.cameraPos = Vector3{p.x, p.y, p.z};
}

void Renderer::SetAmbientColor(const Vector3& color) { lightCB_.ambientColor = color; }
void Renderer::SetDirectionalLight(const Vector3& dir, const Vector3& color, bool enabled) {
	lightCB_.dirLights[0].direction = dir;
	lightCB_.dirLights[0].color = color;
	lightCB_.dirLights[0].enabled = enabled ? 1 : 0;
}
void Renderer::SetPointLight(int index, const Vector3& pos, const Vector3& color, float range, const Vector3& atten, bool enabled) {
	if (index < 0 || index >= kMaxPointLights)
		return;
	lightCB_.pointLights[index].position = pos;
	lightCB_.pointLights[index].color = color;
	lightCB_.pointLights[index].range = range;
	lightCB_.pointLights[index].atten = atten;
	lightCB_.pointLights[index].enabled = enabled ? 1 : 0;
}
void Renderer::SetSpotLight(int index, const Vector3& pos, const Vector3& dir, const Vector3& color, float range, float innerCos, float outerCos, const Vector3& atten, bool enabled) {
	if (index < 0 || index >= kMaxSpotLights)
		return;
	lightCB_.spotLights[index].position = pos;
	lightCB_.spotLights[index].direction = dir;
	lightCB_.spotLights[index].color = color;
	lightCB_.spotLights[index].range = range;
	lightCB_.spotLights[index].innerCos = innerCos;
	lightCB_.spotLights[index].outerCos = outerCos;
	lightCB_.spotLights[index].atten = atten;
	lightCB_.spotLights[index].enabled = enabled ? 1 : 0;
}
void Renderer::SetAreaLight(int index, const Vector3& pos, const Vector3& color, float range, const Vector3& right, const Vector3& up, float halfW, float halfH, const Vector3& atten, bool enabled) {
	if (index < 0 || index >= kMaxAreaLights)
		return;
	lightCB_.areaLights[index].position = pos;
	lightCB_.areaLights[index].color = color;
	lightCB_.areaLights[index].range = range;
	lightCB_.areaLights[index].right = right;
	lightCB_.areaLights[index].up = up;
	lightCB_.areaLights[index].halfWidth = halfW;
	lightCB_.areaLights[index].halfHeight = halfH;

	XMVECTOR R = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&right));
	XMVECTOR U = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&up));
	XMVECTOR D = XMVector3Normalize(XMVector3Cross(R, U));
	XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&lightCB_.areaLights[index].direction), D);

	lightCB_.areaLights[index].atten = atten;
	lightCB_.areaLights[index].enabled = enabled ? 1 : 0;
}

uint32_t Renderer::AllocateSrvIndex(uint32_t count) {
	uint32_t idx = srvCursor_;
	srvCursor_ += count;
	return idx;
}

uint32_t Renderer::AllocateDynamicSrvIndex(uint32_t count) {
	if (srvDynamicCursor_ + count > kSrvHeapTotal) {
		return UINT32_MAX; // 溢れた
	}
	uint32_t idx = srvDynamicCursor_;
	srvDynamicCursor_ += count;
	return idx;
}

ComPtr<ID3DBlob> Renderer::CompileShader(const char* src, const char* entry, const char* target) {
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	ComPtr<ID3DBlob> blob, err;
	HRESULT hr = D3DCompile(src, (UINT)std::strlen(src), nullptr, nullptr, nullptr, entry, target, flags, 0, &blob, &err);
	if (FAILED(hr)) {
		if (err)
			::MessageBoxA(nullptr, (const char*)err->GetBufferPointer(), "HLSL Compile Error", MB_OK);
		return nullptr;
	}
	return blob;
}

ComPtr<ID3DBlob> Renderer::CompileShaderFromFile(const wchar_t* filePath, const char* entry, const char* target) {
	std::wstring unifiedPath = PathUtils::GetUnifiedPathW(filePath);
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	ComPtr<ID3DBlob> blob, err;
	HRESULT hr = D3DCompileFromFile(unifiedPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, flags, 0, &blob, &err);
	if (FAILED(hr)) {
		if (err) {
			::MessageBoxA(nullptr, (const char*)err->GetBufferPointer(), "HLSL CompileFromFile Error", MB_OK);
		} else {
			wchar_t msg[512];
			wsprintfW(msg, L"File: %s\nHRESULT: 0x%08X", unifiedPath.c_str(), hr);
			::MessageBoxW(nullptr, msg, L"HLSL CompileFromFile Error (no message)", MB_OK);
		}
		return nullptr;
	}
	return blob;
}

// デフォルトPSO作成
bool Renderer::CreatePSO(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob) {
	// 標準レイアウト
	D3D12_INPUT_ELEMENT_DESC layout[] = {
	    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT4
	    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT2
	    {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT3
	    {"WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // Weights
	    {"BONES",    0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // Indices
	};
	return CreatePSO(name, vsBlob, psBlob, layout, _countof(layout));
}


// レイアウト指定PSO作成
bool Renderer::CreatePSO(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob, const D3D12_INPUT_ELEMENT_DESC* layout, UINT numElements) {
	if (!vsBlob || !psBlob)
		return false;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = rootSig3D_.Get();
	pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
	pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
	pso.InputLayout = {layout, numElements};
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pso.NumRenderTargets = 1;
	pso.SampleDesc.Count = 1;
	pso.SampleMask = UINT_MAX;

	auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rast.CullMode = D3D12_CULL_MODE_BACK;
	pso.RasterizerState = rast;

	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> newPipeline;
	if (FAILED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&newPipeline)))) {
		return false;
	}

	pipelines_[name] = newPipeline;
	if (std::find(shaderNames_.begin(), shaderNames_.end(), name) == shaderNames_.end()) {
		shaderNames_.push_back(name);
	}
	return true;
}

bool Renderer::CreateShaderPipeline(const std::string& shaderName, const std::wstring& vsPath, const std::wstring& psPath) {
	auto vs = CompileShaderFromFile(vsPath.c_str(), "main", "vs_5_0");
	auto ps = CompileShaderFromFile(psPath.c_str(), "main", "ps_5_0");
	if (!vs || !ps)
		return false;
	return CreatePSO(shaderName, vs.Get(), ps.Get());
}

// ★追加：透明/加算 用 PSO作成
// additive=false: 通常αブレンド
// additive=true : 加算（エネルギー向き）
bool Renderer::CreatePSO_Transparent(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob, bool additive) {
	if (!vsBlob || !psBlob)
		return false;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = rootSig3D_.Get();
	pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
	pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

	D3D12_INPUT_ELEMENT_DESC layout[] = {
	    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT4
	    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT2
	    {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT3
	};
	pso.InputLayout = {layout, _countof(layout)};
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pso.NumRenderTargets = 1;
	pso.SampleDesc.Count = 1;
	pso.SampleMask = UINT_MAX;

	// ラスタライザ（円柱が欠けるなら NONE に）
	auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rast.CullMode = D3D12_CULL_MODE_BACK;
	rast.FrontCounterClockwise = FALSE;
	pso.RasterizerState = rast;

	// ★ブレンドを有効化
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	{
		auto& rt = pso.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.LogicOpEnable = FALSE;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		if (!additive) {
			// 通常αブレンド
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		} else {
			// 加算（エネルギー向き）
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA; // ONE でもOK
			rt.DestBlend = D3D12_BLEND_ONE;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ONE;
		}
	}

	// 深度（透明は“深度書き込みOFF”が定番）
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> newPipeline;
	if (FAILED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&newPipeline)))) {
		return false;
	}

	pipelines_[name] = newPipeline;

	if (std::find(shaderNames_.begin(), shaderNames_.end(), name) == shaderNames_.end()) {
		shaderNames_.push_back(name);
	}

	return true;
}


// 追加：透明/加算 シェーダー登録
bool Renderer::CreateShaderPipelineTransparent(const std::string& shaderName, const std::wstring& vsPath, const std::wstring& psPath, bool additive) {
	auto vs = CompileShaderFromFile(vsPath.c_str(), "main", "vs_5_0");
	auto ps = CompileShaderFromFile(psPath.c_str(), "main", "ps_5_0");
	if (!vs || !ps)
		return false;

	return CreatePSO_Transparent(shaderName, vs.Get(), ps.Get(), additive);
}

bool Renderer::InitPipelines() {
	ComPtr<ID3DBlob> shaderBlob, shaderError;

	// ★共通 RootSignature (3D用)
	{
		CD3DX12_DESCRIPTOR_RANGE rangeSRV;
		rangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

		CD3DX12_DESCRIPTOR_RANGE rangeShadowSRV;
		rangeShadowSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1

		CD3DX12_DESCRIPTOR_RANGE rangeInstanceSRV;
		rangeInstanceSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t2 (Instance Data)

		// ★追加: 環境マップ用
		CD3DX12_DESCRIPTOR_RANGE rangeEnvMapSRV;
		rangeEnvMapSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3); // t3 (Environment CubeMap)

		CD3DX12_ROOT_PARAMETER params[8]{};
		params[0].InitAsConstantBufferView(0);                                        // b0: CBFrame
		params[1].InitAsConstantBufferView(1);                                        // b1: CBObj
		params[2].InitAsConstantBufferView(2);                                        // b2: CBLight
		params[3].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_PIXEL); // t0: Texture
		params[4].InitAsConstantBufferView(3);                                        // b3: CBBone (スキニング用)
		params[5].InitAsDescriptorTable(1, &rangeShadowSRV, D3D12_SHADER_VISIBILITY_PIXEL); // t1: ShadowMap
		params[6].InitAsShaderResourceView(2, 0, D3D12_SHADER_VISIBILITY_VERTEX);       // t2: InstanceData (SRV)
		params[7].InitAsDescriptorTable(1, &rangeEnvMapSRV, D3D12_SHADER_VISIBILITY_PIXEL); // t3: EnvMap (★追加)

		// s0: 通常のテクスチャサンプラー, s1: 影比較用サンプラー
		CD3DX12_STATIC_SAMPLER_DESC samp[2]{};
		samp[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
		samp[1].Init(1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
		samp[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samp[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;

		CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
		rsDesc.Init(_countof(params), params, 2, samp, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &shaderBlob, &shaderError))) {
			if (shaderError) OutputDebugStringA((const char*)shaderError->GetBufferPointer());
			return false;
		}
		if (FAILED(dev_->CreateRootSignature(0, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig3D_))))
			return false;
	}

	// ★追加: 地形用 RootSignature (複数テクスチャ対応)
	{
		CD3DX12_DESCRIPTOR_RANGE rangeSRVs;
		rangeSRVs.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0); // t0-t5: SplatMap, Layer0-3, Detail

		CD3DX12_DESCRIPTOR_RANGE rangeShadowSRV;
		rangeShadowSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6); // t6: ShadowMap

		CD3DX12_ROOT_PARAMETER params[6]{};
		params[0].InitAsConstantBufferView(0);                                        // b0: CBFrame
		params[1].InitAsConstantBufferView(1);                                        // b1: CBObj
		params[2].InitAsConstantBufferView(2);                                        // b2: CBLight
		params[3].InitAsDescriptorTable(1, &rangeSRVs, D3D12_SHADER_VISIBILITY_PIXEL); // t0-t5
		params[4].InitAsDescriptorTable(1, &rangeShadowSRV, D3D12_SHADER_VISIBILITY_PIXEL); // t6: ShadowMap
		params[5].InitAsShaderResourceView(2, 0, D3D12_SHADER_VISIBILITY_VERTEX);       // t2: InstanceData (SRV)

		CD3DX12_STATIC_SAMPLER_DESC samp[2]{};
		samp[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
		samp[1].Init(1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
		samp[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samp[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;

		CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
		rsDesc.Init(_countof(params), params, 2, samp, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &shaderBlob, &shaderError))) {
			if (shaderError) OutputDebugStringA((const char*)shaderError->GetBufferPointer());
			return false;
		}
		if (FAILED(dev_->CreateRootSignature(0, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), IID_PPV_ARGS(&rootSigTerrain_)))) {
			return false;
		}
	}

	// ★追加: 空間のゆがみ用 RootSignature (t0:Backdrop, t1:DistortionMap)
	{
		CD3DX12_DESCRIPTOR_RANGE rangeBackdrop;
		rangeBackdrop.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0: Backdrop

		CD3DX12_DESCRIPTOR_RANGE rangeDistMap;
		rangeDistMap.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1: DistortionMap

		CD3DX12_ROOT_PARAMETER params[5]{};
		params[0].InitAsConstantBufferView(0);                                        // b0: CBFrame
		params[1].InitAsConstantBufferView(1);                                        // b1: CBObj (互換用)
		params[2].InitAsDescriptorTable(1, &rangeBackdrop, D3D12_SHADER_VISIBILITY_PIXEL); // t0: Backdrop
		params[3].InitAsDescriptorTable(1, &rangeDistMap, D3D12_SHADER_VISIBILITY_PIXEL);  // t1: DistortionMap
		params[4].InitAsShaderResourceView(2, 0, D3D12_SHADER_VISIBILITY_VERTEX);       // t2: InstanceData (VS)

		CD3DX12_STATIC_SAMPLER_DESC samp(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
		CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
		rsDesc.Init(_countof(params), params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &shaderBlob, &shaderError))) {
			if (shaderError) OutputDebugStringA((const char*)shaderError->GetBufferPointer());
			return false;
		}
		if (FAILED(dev_->CreateRootSignature(0, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), IID_PPV_ARGS(&rootSigDistortion_)))) {
			return false;
		}
	}

	// ★追加: コンピュート RootSignature
	{
		CD3DX12_ROOT_PARAMETER computeParams[7]{};
		computeParams[0].InitAsConstants(1, 0);       // b0: { numRequests }
		computeParams[1].InitAsShaderResourceView(0); // t0: Requests
		computeParams[2].InitAsShaderResourceView(1); // t1: BvhNodes
		computeParams[3].InitAsShaderResourceView(2); // t2: BvhIndices
		computeParams[4].InitAsUnorderedAccessView(0); // u0: Results
		computeParams[5].InitAsShaderResourceView(3); // t3: ModelVertices
		computeParams[6].InitAsShaderResourceView(4); // t4: ModelIndices

		CD3DX12_ROOT_SIGNATURE_DESC rsDescCompute;
		rsDescCompute.Init(_countof(computeParams), computeParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ComPtr<ID3DBlob> sigCompute, errCompute;
		if (FAILED(D3D12SerializeRootSignature(&rsDescCompute, D3D_ROOT_SIGNATURE_VERSION_1, &sigCompute, &errCompute)))
			return false;
		if (FAILED(dev_->CreateRootSignature(0, sigCompute->GetBufferPointer(), sigCompute->GetBufferSize(), IID_PPV_ARGS(&rootSigCompute_))))
			return false;
	}

	// ---------------------------------------------------------
	// Default Shader
	// ---------------------------------------------------------
	static const char* kVS3D = R"(
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObj : register(b1) { row_major float4x4 gWorld; float4 gColor; };
// スキニング用の入力も受け取るが、ここでは使わない
struct VSIn { 
	float4 pos : POSITION; 
	float2 uv : TEXCOORD0; 
	float3 nrm : NORMAL; 
	float4 weights : WEIGHTS; 
	uint4 indices : BONES; 
};
struct VSOut { float4 svpos : SV_POSITION; float3 worldPos: TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; };
VSOut main(VSIn v) { 
    VSOut o; 
    float4 wp = mul(v.pos, gWorld); 
    o.worldPos = wp.xyz; 
    float3 wn = mul(float4(v.nrm, 0), gWorld).xyz; 
    o.normal = normalize(wn); 
    float4 vp = mul(wp, gView); 
    o.svpos = mul(vp, gProj); 
    o.uv = v.uv; 
    return o; 
}
)";

	static const char* kVSInstanced = R"(
struct InstanceData { row_major float4x4 world; float4 color; };
StructuredBuffer<InstanceData> gInstanceData : register(t2);

cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };

struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; float3 nrm : NORMAL; };
struct VSOut { float4 svpos : SV_POSITION; float3 worldPos: TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0; };

VSOut main(VSIn v, uint instanceID : SV_InstanceID) {
    VSOut o;
    InstanceData data = gInstanceData[instanceID];
    float4 wp = mul(v.pos, data.world);
    o.worldPos = wp.xyz;
    o.normal = normalize(mul(float4(v.nrm, 0), data.world).xyz);
    o.svpos = mul(mul(wp, gView), gProj);
    o.uv = v.uv;
    o.color = data.color;
    return o;
}
)";

	static const char* kPS3D = R"(
Texture2D gTex : register(t0); 
Texture2D gShadowMap : register(t1);
SamplerState gSmp : register(s0);
SamplerComparisonState gShadowSmp : register(s1);

cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObj : register(b1) { row_major float4x4 gWorld; float4 gColor; };

struct DirLight { float3 dir; float pad0; float3 color; float pad1; uint enabled; float3 pad2; };
struct PointLight { float3 pos; float pad0; float3 color; float range; float3 atten; float pad1; uint enabled; float3 pad2; };
struct SpotLight { float3 pos; float pad0; float3 dir; float range; float3 color; float inner; float3 atten; float outer; uint enabled; float3 pad2; };
struct AreaLight { float3 pos; float pad0; float3 color; float range; float3 right; float halfWidth; float3 up; float halfHeight; float3 dir; float pad1; float3 atten; float pad2; uint enabled; float3 pad3; };

#define MAX_DIR 1
#define MAX_POINT 4
#define MAX_SPOT 4
#define MAX_AREA 4
cbuffer CBLight : register(b2) { float3 gAmbientColor; float padA0; DirLight gDir[MAX_DIR]; PointLight gPoint[MAX_POINT]; SpotLight gSpot[MAX_SPOT]; AreaLight gArea[MAX_AREA]; row_major float4x4 gShadowMatrix; };

float GetAttenuation(float3 atten, float d) { return 1.0 / (atten.x + atten.y * d + atten.z * d * d); }
float3 BlinnPhong(float3 L, float3 V, float3 N, float3 C, float3 A) {
    float NdotL = max(dot(N, L), 0.0); float3 diff = A * C * NdotL;
    float3 H = normalize(L + V); float NdotH = max(dot(N, H), 0.0);
    float3 spec = C * pow(NdotH, 32.0) * 0.5; return diff + spec;
}
float3 CalcAreaLight(AreaLight L, float3 wPos, float3 N, float3 V, float3 A) {
	float3 Lvec = L.pos - wPos; float distPlane = dot(Lvec, L.dir); float3 planePoint = wPos + L.dir * distPlane; float3 dirFromCenter = planePoint - L.pos;
	float distRight = dot(dirFromCenter, L.right); float distUp = dot(dirFromCenter, L.up);
	float clampedRight = clamp(distRight, -L.halfWidth, L.halfWidth); float clampedUp = clamp(distUp, -L.halfHeight, L.halfHeight);
	float3 closest = L.pos + L.right * clampedRight + L.up * clampedUp;
	float3 lightDirVec = closest - wPos; float d = length(lightDirVec); if(d >= L.range) return float3(0,0,0);
	float3 lDir = normalize(lightDirVec); float att = GetAttenuation(L.atten, d);
	return BlinnPhong(lDir, V, N, L.color, A) * att;
}

float CalcShadow(float3 worldPos) {
    float4 shadowPos = mul(float4(worldPos, 1.0f), gShadowMatrix);
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    
    // NDC [-1, 1] to UV [0, 1]
    projCoords.x = projCoords.x * 0.5f + 0.5f;
    projCoords.y = -projCoords.y * 0.5f + 0.5f;
    
    // Check if outside shadow map
    if (projCoords.x < 0.0f || projCoords.x > 1.0f || projCoords.y < 0.0f || projCoords.y > 1.0f || projCoords.z < 0.0f || projCoords.z > 1.0f)
        return 1.0f;

    // PCF 3x3
    float shadow = 0.0f;
    float texelSize = 1.0f / 2048.0f;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            shadow += gShadowMap.SampleCmpLevelZero(gShadowSmp, projCoords.xy + float2(x, y) * texelSize, projCoords.z).r;
        }
    }
    return shadow / 9.0f;
}

float4 main(float4 svpos:SV_POSITION, float3 worldPos:TEXCOORD0, float3 normal:TEXCOORD1, float2 uv:TEXCOORD2) : SV_TARGET {
    float4 tex = gTex.Sample(gSmp, uv); 
    float3 albedo = tex.rgb * gColor.rgb; 
    float3 N = normalize(normal); 
    float3 V = normalize(gCamPos - worldPos);
    float3 finalColor = albedo * gAmbientColor;

    float shadowFactor = CalcShadow(worldPos);

    for(int i=0; i<MAX_DIR; ++i) if(gDir[i].enabled) finalColor += BlinnPhong(normalize(-gDir[i].dir), V, N, gDir[i].color, albedo) * shadowFactor;
    for(int i=0; i<MAX_POINT; ++i) if(gPoint[i].enabled) { float3 Lv = gPoint[i].pos - worldPos; float d = length(Lv); if(d < gPoint[i].range) finalColor += BlinnPhong(normalize(Lv), V, N, gPoint[i].color, albedo) * GetAttenuation(gPoint[i].atten, d); }
    for(int i=0; i<MAX_SPOT; ++i) if(gSpot[i].enabled) { float3 Lv = gSpot[i].pos - worldPos; float d = length(Lv); if(d < gSpot[i].range) { float3 L = normalize(Lv); float c = dot(L, normalize(-gSpot[i].dir)); float s = smoothstep(gSpot[i].outer, gSpot[i].inner, c); finalColor += BlinnPhong(L, V, N, gSpot[i].color, albedo) * GetAttenuation(gSpot[i].atten, d) * s; } }
    for(int i=0; i<MAX_AREA; ++i) if(gArea[i].enabled) finalColor += CalcAreaLight(gArea[i], worldPos, N, V, albedo);

    return float4(finalColor, tex.a * gColor.a);
}
)";

	// ---------------------------------------------------------
	// Skinning Vertex Shader
	// ---------------------------------------------------------
	static const char* kVSSkin = R"(
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObj : register(b1) { row_major float4x4 gWorld; float4 gColor; };
cbuffer CBBone : register(b3) { row_major float4x4 gBones[128]; };

struct VSIn { 
	float4 pos : POSITION; 
	float2 uv : TEXCOORD0; 
	float3 nrm : NORMAL; 
	float4 weights : WEIGHTS; 
	uint4 indices : BONES; 
};
struct VSOut { float4 svpos : SV_POSITION; float3 worldPos: TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; };

VSOut main(VSIn v) { 
    VSOut o; 
    
    // スキニング行列の合成
    float4x4 skinMat = 
        gBones[v.indices.x] * v.weights.x +
        gBones[v.indices.y] * v.weights.y +
        gBones[v.indices.z] * v.weights.z +
        gBones[v.indices.w] * v.weights.w;

    float4 localPos = v.pos;
    float4 skinnedPos = mul(localPos, skinMat);
    float4 localNrm = float4(v.nrm, 0.0f);
    float3 skinnedNrm = mul(localNrm, skinMat).xyz;

    float4 wp = mul(skinnedPos, gWorld); 
    o.worldPos = wp.xyz; 
    float3 wn = mul(float4(skinnedNrm, 0), gWorld).xyz; 
    o.normal = normalize(wn); 
    
    float4 vp = mul(wp, gView); 
    o.svpos = mul(vp, gProj); 
    o.uv = v.uv; 
    return o; 
}
)";

	auto vs3d = CompileShader(kVS3D, "main", "vs_5_0");
	auto ps3d = CompileShader(kPS3D, "main", "ps_5_0");
	auto vsSkin = CompileShader(kVSSkin, "main", "vs_5_0");

	if (!vs3d || !ps3d || !vsSkin)
		return false;

	if (!CreatePSO("Default", vs3d.Get(), ps3d.Get()))
		return false;

	// 通常オブジェクト インスタンス描画 (インラインコンパイルで環境非依存にする)
	static const char* kVSInstancedObj = R"(
struct InstanceData { row_major float4x4 world; float4 color; float4 uvScaleOffset; };
StructuredBuffer<InstanceData> gInstanceData : register(t2);
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; float3 nrm : NORMAL; };
struct VSOut { float4 svpos : SV_POSITION; float3 worldPos: TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float4 color : COLOR0; };
VSOut main(VSIn v, uint instanceID : SV_InstanceID) {
    VSOut o;
    InstanceData data = gInstanceData[instanceID];
    float4 wp = mul(v.pos, data.world);
    o.worldPos = wp.xyz;
    o.normal = normalize(mul(float4(v.nrm, 0), data.world).xyz);
    o.svpos = mul(mul(wp, gView), gProj);
    o.uv = v.uv;
    o.color = data.color;
    return o;
}
)";
	static const char* kPSInstancedObj = R"(
Texture2D gTex : register(t0);
Texture2D gShadowMap : register(t1);
SamplerState gSmp : register(s0);
SamplerComparisonState gShadowSmp : register(s1);
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
struct DirLight { float3 dir; float pad0; float3 color; float pad1; uint enabled; float3 pad2; };
struct PointLight { float3 pos; float pad0; float3 color; float range; float3 atten; float pad1; uint enabled; float3 pad2; };
struct SpotLight { float3 pos; float pad0; float3 dir; float range; float3 color; float inner; float3 atten; float outer; uint enabled; float3 pad2; };
struct AreaLight { float3 pos; float pad0; float3 color; float range; float3 right; float halfWidth; float3 up; float halfHeight; float3 dir; float pad1; float3 atten; float pad2; uint enabled; float3 pad3; };
#define MAX_DIR 1
#define MAX_POINT 4
#define MAX_SPOT 4
#define MAX_AREA 4
cbuffer CBLight : register(b2) { float3 gAmbientColor; float padA0; DirLight gDir[MAX_DIR]; PointLight gPoint[MAX_POINT]; SpotLight gSpot[MAX_SPOT]; AreaLight gArea[MAX_AREA]; row_major float4x4 gShadowMatrix; };
float GetAttenuation(float3 atten, float d) { return 1.0 / (atten.x + atten.y * d + atten.z * d * d); }
float3 BlinnPhong(float3 L, float3 V, float3 N, float3 C, float3 A) {
    float NdotL = max(dot(N, L), 0.0); float3 diff = A * C * NdotL;
    float3 H = normalize(L + V); float NdotH = max(dot(N, H), 0.0);
    float3 spec = C * pow(NdotH, 32.0) * 0.5; return diff + spec;
}
float CalcShadow(float3 worldPos) {
    float4 shadowPos = mul(float4(worldPos, 1.0f), gShadowMatrix);
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    projCoords.x = projCoords.x * 0.5f + 0.5f;
    projCoords.y = -projCoords.y * 0.5f + 0.5f;
    if (projCoords.x < 0.0f || projCoords.x > 1.0f || projCoords.y < 0.0f || projCoords.y > 1.0f || projCoords.z < 0.0f || projCoords.z > 1.0f)
        return 1.0f;
    float shadow = 0.0f;
    float texelSize = 1.0f / 2048.0f;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            shadow += gShadowMap.SampleCmpLevelZero(gShadowSmp, projCoords.xy + float2(x, y) * texelSize, projCoords.z).r;
        }
    }
    return shadow / 9.0f;
}
float4 main(float4 svpos:SV_POSITION, float3 worldPos:TEXCOORD0, float3 normal:TEXCOORD1, float2 uv:TEXCOORD2, float4 color:COLOR0) : SV_TARGET {
    float4 tex = gTex.Sample(gSmp, uv);
    float3 albedo = tex.rgb * color.rgb;
    float3 N = normalize(normal);
    float3 V = normalize(gCamPos - worldPos);
    float3 finalColor = albedo * gAmbientColor;
    float shadowFactor = CalcShadow(worldPos);
    for(int i=0; i<MAX_DIR; ++i) if(gDir[i].enabled) finalColor += BlinnPhong(normalize(-gDir[i].dir), V, N, gDir[i].color, albedo) * shadowFactor;
    for(int j=0; j<MAX_POINT; ++j) if(gPoint[j].enabled) { float3 Lv = gPoint[j].pos - worldPos; float d = length(Lv); if(d < gPoint[j].range) finalColor += BlinnPhong(normalize(Lv), V, N, gPoint[j].color, albedo) * GetAttenuation(gPoint[j].atten, d); }
    for(int k=0; k<MAX_SPOT; ++k) if(gSpot[k].enabled) { float3 Lv = gSpot[k].pos - worldPos; float d = length(Lv); if(d < gSpot[k].range) { float3 L = normalize(Lv); float c = dot(L, normalize(-gSpot[k].dir)); float s = smoothstep(gSpot[k].outer, gSpot[k].inner, c); finalColor += BlinnPhong(L, V, N, gSpot[k].color, albedo) * GetAttenuation(gSpot[k].atten, d) * s; } }
    return float4(finalColor, tex.a * color.a);
}
)";
	auto vsInst = CompileShader(kVSInstancedObj, "main", "vs_5_0");
	auto psInst = CompileShader(kPSInstancedObj, "main", "ps_5_0");
	if (!vsInst || !psInst || !CreatePSO("Instanced", vsInst.Get(), psInst.Get()))
		return false;

	// パーティクル インスタンス描画 (インラインコンパイルで環境非依存にする)
	static const char* kVSParticleInstanced = R"(
struct InstanceData { row_major float4x4 world; float4 color; float4 uvScaleOffset; };
StructuredBuffer<InstanceData> gInstanceData : register(t2);
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
struct VSOutput { float4 svpos : SV_POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
VSOutput main(float4 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD, uint instanceID : SV_InstanceID) {
    VSOutput output;
    InstanceData data = gInstanceData[instanceID];
    output.svpos = mul(pos, mul(data.world, mul(gView, gProj)));
    output.uv.x = uv.x * data.uvScaleOffset.x + data.uvScaleOffset.z;
    output.uv.y = uv.y * data.uvScaleOffset.y + data.uvScaleOffset.w;
    output.color = data.color;
    return output;
}
)";
	static const char* kPSParticle = R"(
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD, float4 color:COLOR) : SV_TARGET {
    float4 texColor = tex.Sample(smp, uv);
    texColor *= color;
    if (texColor.a <= 0.0f) { discard; }
    return texColor;
}
)";
	auto vsPartInst = CompileShader(kVSParticleInstanced, "main", "vs_5_0");
	auto psPart = CompileShader(kPSParticle, "main", "ps_5_0");
	if (!vsPartInst || !psPart || !CreatePSO("ParticleInstanced", vsPartInst.Get(), psPart.Get()))
		return false;

	// パーティクル 加算 インスタンス描画
	if (vsPartInst && psPart) {
		CreatePSO_Transparent("ParticleAdditiveInstanced", vsPartInst.Get(), psPart.Get(), true);
	}

	// ★追加: プロシージャル煙パーティクル (FBMノイズベース)
	static const char* kPSProceduralSmoke = R"(
cbuffer CBFrame : register(b0) {
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float3 gCamPos;
    float gTime;
};

struct VSOutput {
    float4 svpos : SV_POSITION;
    float2 uv    : TEXCOORD;
    float4 color : COLOR;
};

// --- ノイズ関数群 ---
float hash(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f); // smoothstep
    float a = hash(i);
    float b = hash(i + float2(1.0, 0.0));
    float c = hash(i + float2(0.0, 1.0));
    float d = hash(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// FBM (Fractal Brownian Motion) - 軽量版 (2オクターブ)
float fbm(float2 p) {
    float val = 0.5 * noise(p);
    p = float2(p.x * 0.866 - p.y * 0.5, p.x * 0.5 + p.y * 0.866) * 2.17;
    val += 0.235 * noise(p);
    return val;
}

float4 main(VSOutput input) : SV_TARGET {
    // カメラ近接フェード (近い場合は重いノイズ計算をスキップ)
    float camFade = smoothstep(3.0, 10.0, input.svpos.w);
    if (camFade <= 0.01) discard;

    float2 uv = input.uv;
    float2 center = uv - 0.5;
    float baseDist = length(center);

    // 画面外周は無条件で破棄 (最適化)
    if (baseDist >= 0.5) discard;

    // 時間によるスクロール (煙特有の湧き上がる動き)
    float t = gTime * 0.3;
    float2 noiseUV = uv * 3.0 + float2(-t * 0.3, -t * 1.2);

    // FBMで煙のシルエットとなるディティールを生成 (1回のみ)
    float n = fbm(noiseUV);

    // 原点からの距離にノイズを足して、円を「モクモクした形」に歪ませる
    float distortedDist = baseDist + (n * 0.4);

    // 歪んだ距離を使って、滑らかなモヤモヤしたマスクを作る
    float smokeDensity = 1.0 - smoothstep(0.15, 0.55, distortedDist);
    if (smokeDensity <= 0.01) discard;

    // --- スチームパンク風ライティング ---
    float3 lightDir = normalize(float3(0.3, 0.8, -0.5));
    // 煙のふくらみを擬似的に法線として扱う (中心から外に向かう法線 + 上向き成分)
    float3 fakeNormal = normalize(float3(center.x, -center.y, 0.4 - n * 0.3));
    float NdotL = saturate(dot(fakeNormal, lightDir));

    // 水蒸気(Steam)向けのスチームパンク風ライティング
    // 環境光や下からの反射光として、ほんのり暖かみのあるハイライト
    float3 warmLight = float3(1.15, 1.05, 0.95);
    
    // 基本色 (入力カラーを活用)
    float3 baseColor = input.color.rgb;
    
    // 水蒸気は黒くならず、影部分は少しグレー・青みがかった透かし色になる
    float3 shadowColor = baseColor * float3(0.65, 0.70, 0.75);
    float3 litColor = baseColor * warmLight * 1.25;

    // シャドウとハイライトのコントラストを合成
    float3 finalColor = lerp(shadowColor, litColor, NdotL);

    // アルファ値: 外枠が滑らかに透ける
    float alpha = smokeDensity * input.color.a * camFade;

    return float4(finalColor, alpha);
}
)";
	auto psProceduralSmoke = CompileShader(kPSProceduralSmoke, "main", "ps_5_0");
	if (vsPartInst && psProceduralSmoke) {
		CreatePSO_Transparent("ProceduralSmokeInstanced", vsPartInst.Get(), psProceduralSmoke.Get(), false);
		CreatePSO_Transparent("ProceduralSmokeAdditiveInstanced", vsPartInst.Get(), psProceduralSmoke.Get(), true);
	}

	// Skinning用レイアウト
	D3D12_INPUT_ELEMENT_DESC skinLayout[] = {
	    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT4
	    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT2
	    {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // XMFLOAT3
	    {"WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // Weights
	    {"BONES",    0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, // Indices
	};
	if (!CreatePSO("Skinning", vsSkin.Get(), ps3d.Get(), skinLayout, _countof(skinLayout)))
		return false;

	// ---------------------------------------------------------
	// ★追加: Shadow Depth Shaders
	// ---------------------------------------------------------
	static const char* kVSShadow = R"(
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObj : register(b1) { row_major float4x4 gWorld; float4 gColor; };
struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; float3 nrm : NORMAL; float4 weights : WEIGHTS; uint4 indices : BONES; };
float4 main(VSIn v) : SV_POSITION { 
    return mul(mul(v.pos, gWorld), gViewProj); 
}
)";
	static const char* kVSSkinShadow = R"(
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObj : register(b1) { row_major float4x4 gWorld; float4 gColor; };
cbuffer CBBone : register(b3) { row_major float4x4 gBones[128]; };
struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; float3 nrm : NORMAL; float4 weights : WEIGHTS; uint4 indices : BONES; };
float4 main(VSIn v) : SV_POSITION { 
    float4x4 skinMat = gBones[v.indices.x] * v.weights.x + gBones[v.indices.y] * v.weights.y + gBones[v.indices.z] * v.weights.z + gBones[v.indices.w] * v.weights.w;
    float4 skinnedPos = mul(v.pos, skinMat);
    return mul(mul(skinnedPos, gWorld), gViewProj); 
}
)";

	static const char* kVSInstancedShadow = R"(
struct InstanceData { row_major float4x4 world; float4 color; float4 uvScaleOffset; };
StructuredBuffer<InstanceData> gInstanceData : register(t2);
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
struct VSIn { float4 pos : POSITION; };
float4 main(VSIn v, uint instanceID : SV_InstanceID) : SV_POSITION {
    return mul(mul(v.pos, gInstanceData[instanceID].world), gViewProj);
}
)";
	auto vsShadow = CompileShader(kVSShadow, "main", "vs_5_0");
	auto vsSkinShadow = CompileShader(kVSSkinShadow, "main", "vs_5_0");
	auto vsInstShadow = CompileShader(kVSInstancedShadow, "main", "vs_5_0");
	if (vsShadow && vsSkinShadow && vsInstShadow) {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = rootSig3D_.Get();
		psoDesc.VS = {vsShadow->GetBufferPointer(), vsShadow->GetBufferSize()};
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.DepthBias = 10000;
		psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
		psoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 0; // 深度のみ
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		D3D12_INPUT_ELEMENT_DESC layout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"BONES",    0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		psoDesc.InputLayout = {layout, _countof(layout)};
		dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&shadowPso_));

		psoDesc.VS = {vsSkinShadow->GetBufferPointer(), vsSkinShadow->GetBufferSize()};
		psoDesc.InputLayout = {skinLayout, _countof(skinLayout)};
		dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&shadowSkinPso_));

		psoDesc.VS = {vsInstShadow->GetBufferPointer(), vsInstShadow->GetBufferSize()};
		D3D12_INPUT_ELEMENT_DESC instLayout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		psoDesc.InputLayout = {instLayout, _countof(instLayout)};
		dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&shadowInstancedPso_));
	}

	// 2D Shader (変更なし)
	{
		static const char* kVS2D =
		    R"(cbuffer CBSprite : register(b0) { float4x4 gMVP; float4 gColor; }; struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; }; struct VSOut { float4 svpos : SV_POSITION; float2 uv : TEXCOORD0; }; VSOut main(VSIn v) { VSOut o; o.svpos = mul(float4(v.pos, 0, 1), gMVP); o.uv = v.uv; return o; })";
		static const char* kPS2D =
		    R"(Texture2D gTex : register(t0); SamplerState gSmp : register(s0); cbuffer CBSprite : register(b0) { float4x4 gMVP; float4 gColor; }; float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET { return gTex.Sample(gSmp, uv) * gColor; })";
		auto vs2d = CompileShader(kVS2D, "main", "vs_5_0");
		auto ps2d = CompileShader(kPS2D, "main", "ps_5_0");
		if (!vs2d || !ps2d)
			return false;

		CD3DX12_DESCRIPTOR_RANGE rangeSRV;
		rangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_ROOT_PARAMETER params[2]{};
		params[0].InitAsConstantBufferView(0);
		params[1].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_PIXEL);
		CD3DX12_STATIC_SAMPLER_DESC samp(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
		CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
		rsDesc.Init(_countof(params), params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &shaderBlob, &shaderError)))
			return false;
		if (FAILED(dev_->CreateRootSignature(0, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig2D_))))
			return false;

		D3D12_INPUT_ELEMENT_DESC layout[] = {
		    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
		pso.pRootSignature = rootSig2D_.Get();
		pso.VS = {vs2d->GetBufferPointer(), vs2d->GetBufferSize()};
		pso.PS = {ps2d->GetBufferPointer(), ps2d->GetBufferSize()};
		pso.InputLayout = {layout, _countof(layout)};
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso.NumRenderTargets = 1;
		pso.SampleDesc.Count = 1;
		pso.SampleMask = UINT_MAX;
		pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		auto blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blend.RenderTarget[0].BlendEnable = TRUE;
		blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		// ★修正: アルファチャネルは常に最大値を維持し、スプライトのアルファ値でRTのアルファを破壊しない
		blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
		blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
		blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pso.BlendState = blend;
		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = FALSE;
		pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
		if (FAILED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pso2D_))))
			return false;
	}

	// ---------------------------------------------------------
	// ★追加: 3Dライン描画用パイプライン (Position + Color)
	// ---------------------------------------------------------
	{
		static const char* kVSLine = R"(
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
struct VSIn { float3 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 svpos : SV_POSITION; float4 color : COLOR; };
VSOut main(VSIn v) {
    VSOut o;
    float4 wp = float4(v.pos, 1.0f);
    float4 vp = mul(wp, gView);
    o.svpos = mul(vp, gProj);
    o.color = v.color;
    return o;
})";
		static const char* kPSLine = R"(
struct PSIn { float4 svpos : SV_POSITION; float4 color : COLOR; };
float4 main(PSIn i) : SV_TARGET { return i.color; }
)";
		auto vsBlob = CompileShader(kVSLine, "main", "vs_5_0");
		auto psBlob = CompileShader(kPSLine, "main", "ps_5_0");
		if (!vsBlob || !psBlob) return false;

		D3D12_INPUT_ELEMENT_DESC layout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
		pso.pRootSignature = rootSig3D_.Get(); // b0(CBFrame)を利用するため3DのRootSigを流用
		pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
		pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
		pso.InputLayout = {layout, _countof(layout)};
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso.NumRenderTargets = 1;
		pso.SampleDesc.Count = 1;
		pso.SampleMask = UINT_MAX;
		pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso.RasterizerState.AntialiasedLineEnable = TRUE;
		auto blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blend.RenderTarget[0].BlendEnable = TRUE;
		blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pso.BlendState = blend;
		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = TRUE; // Grid lines are depth tested
		pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Lines don't write to depth
		pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		if (FAILED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoLine3D_))))
			return false;
			
		// Create XRay pipeline state (disabled depth)
		pso.DepthStencilState.DepthEnable = FALSE;
		pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		if (FAILED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoLine3DXRay_))))
			return false;
	}

	// ★追加: コンピュート PSOの作成
	auto csBlob = CompileShaderFromFile(L"Resources/shaders/CollisionCompute.hlsl", "main", "cs_5_0");
	if (csBlob) {
		D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc{};
		computePsoDesc.pRootSignature = rootSigCompute_.Get();
		computePsoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};
		dev_->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&psoCollision_)); // 失敗しても続行
	}

	// ★追加: 高品位地形シェーダーの登録
	{
		auto vs = CompileShaderFromFile(L"Resources/shaders/EnhancedTerrainVS.hlsl", "main", "vs_5_0");
		auto ps = CompileShaderFromFile(L"Resources/shaders/EnhancedTerrainPS.hlsl", "main", "ps_5_0");
		if (vs && ps) {
			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
			psoDesc.pRootSignature = rootSigTerrain_.Get();
			psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
			psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			psoDesc.InputLayout = { skinLayout, _countof(skinLayout) }; // VertexData に合わせる
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets = 1;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			psoDesc.SampleDesc.Count = 1;

			Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
			if (SUCCEEDED(dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)))) {
				pipelines_["EnhancedTerrain"] = pso;
				shaderNames_.push_back("EnhancedTerrain");
			}
		}
	}

	// ★追加: 鳴潮風スタイライズド草シェーダーの登録
	{
		auto vs = CompileShaderFromFile(L"Resources/shaders/StylizedGrassVS.hlsl", "main", "vs_5_0");
		auto ps = CompileShaderFromFile(L"Resources/shaders/StylizedGrassPS.hlsl", "main", "ps_5_0");
		if (vs && ps) {
			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
			psoDesc.pRootSignature = rootSig3D_.Get();
			psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
			psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			psoDesc.InputLayout = { skinLayout, _countof(skinLayout) };
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets = 1;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			psoDesc.SampleDesc.Count = 1;

			Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
			if (SUCCEEDED(dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)))) {
				pipelines_["StylizedGrass"] = pso;
				shaderNames_.push_back("StylizedGrass");
			}
		}
	}

	// ---------------------------------------------------------
	// ★追加: トゥーンシェーダー・アウトライン用パイプライン
	// ---------------------------------------------------------
	{
		// --- Toon (通常メッシュ) ---
		auto vsToon = CompileShaderFromFile(L"Resources/shaders/ToonVS.hlsl", "main", "vs_5_0");
		auto psToon = CompileShaderFromFile(L"Resources/shaders/ToonPS.hlsl", "main", "ps_5_0");
		if (vsToon && psToon) {
			CreatePSO("Toon", vsToon.Get(), psToon.Get());
		}

		// --- ToonOutline (アウトライン用: 前面カリング) ---
		auto vsOutline = CompileShaderFromFile(L"Resources/shaders/ToonOutlineVS.hlsl", "main", "vs_5_0");
		auto psOutline = CompileShaderFromFile(L"Resources/shaders/ToonOutlinePS.hlsl", "main", "ps_5_0");
		if (vsOutline && psOutline) {
			// アウトラインPSOは前面カリング（裏面を描画 = 膨張した部分が見える）
			D3D12_INPUT_ELEMENT_DESC outlineLayout[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"BONES",    0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			};

			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
			psoDesc.pRootSignature = rootSig3D_.Get();
			psoDesc.VS = { vsOutline->GetBufferPointer(), vsOutline->GetBufferSize() };
			psoDesc.PS = { psOutline->GetBufferPointer(), psOutline->GetBufferSize() };
			psoDesc.InputLayout = { outlineLayout, _countof(outlineLayout) };
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.NumRenderTargets = 1;
			psoDesc.SampleDesc.Count = 1;
			psoDesc.SampleMask = UINT_MAX;

			auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			rast.CullMode = D3D12_CULL_MODE_FRONT; // ★前面カリング（輪郭のみ見える）
			psoDesc.RasterizerState = rast;

			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

			Microsoft::WRL::ComPtr<ID3D12PipelineState> outlinePso;
			if (SUCCEEDED(dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outlinePso)))) {
				pipelines_["ToonOutline"] = outlinePso;
			}
		}

		// --- ToonSkinning (スキニング対応トゥーン) ---
		auto vsToonSkin = CompileShaderFromFile(L"Resources/shaders/ToonSkinningVS.hlsl", "main", "vs_5_0");
		if (vsToonSkin && psToon) {
			CreatePSO("ToonSkinning", vsToonSkin.Get(), psToon.Get());
		}

		// --- ToonSkinningOutline (スキニング対応アウトライン) ---
		auto vsSkinOutline = CompileShaderFromFile(L"Resources/shaders/ToonSkinningOutlineVS.hlsl", "main", "vs_5_0");
		if (vsSkinOutline && psOutline) {
			D3D12_INPUT_ELEMENT_DESC outlineSkinLayout[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"BONES",    0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			};

			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
			psoDesc.pRootSignature = rootSig3D_.Get();
			psoDesc.VS = { vsSkinOutline->GetBufferPointer(), vsSkinOutline->GetBufferSize() };
			psoDesc.PS = { psOutline->GetBufferPointer(), psOutline->GetBufferSize() };
			psoDesc.InputLayout = { outlineSkinLayout, _countof(outlineSkinLayout) };
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.NumRenderTargets = 1;
			psoDesc.SampleDesc.Count = 1;
			psoDesc.SampleMask = UINT_MAX;

			auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			rast.CullMode = D3D12_CULL_MODE_FRONT;
			psoDesc.RasterizerState = rast;

			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

			Microsoft::WRL::ComPtr<ID3D12PipelineState> skinOutlinePso;
			if (SUCCEEDED(dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&skinOutlinePso)))) {
				pipelines_["ToonSkinningOutline"] = skinOutlinePso;
			}
		}

		// シェーダー名リストに追加 (エディタのドロップダウンに表示される)
		// ※CreatePSO や CreatePSO_Transparent 内部で重複チェック付きで push_back されるため、明示的な処理は不要です。

		// ★追加: 反射(映り込み)用シェーダー
		auto vsReflection = CompileShaderFromFile(L"Resources/shaders/ObjVS.hlsl", "main", "vs_5_0");
		auto psReflection = CompileShaderFromFile(L"Resources/shaders/ObjPS.hlsl", "main", "ps_5_0");
		if (vsReflection && psReflection) {
			CreatePSO("Reflection", vsReflection.Get(), psReflection.Get());
		}

		// ★追加: リッチシェーダー
		auto vsEmissive = CompileShaderFromFile(L"Resources/shaders/EmissiveGlowVS.hlsl", "main", "vs_5_0");
		auto psEmissive = CompileShaderFromFile(L"Resources/shaders/EmissiveGlowPS.hlsl", "main", "ps_5_0");
		if (vsEmissive && psEmissive) {
			CreatePSO("EmissiveGlow", vsEmissive.Get(), psEmissive.Get());
		}

		auto vsHologram = CompileShaderFromFile(L"Resources/shaders/HologramVS.hlsl", "main", "vs_5_0");
		auto psHologram = CompileShaderFromFile(L"Resources/shaders/HologramPS.hlsl", "main", "ps_5_0");
		if (vsHologram && psHologram) {
			CreatePSO_Transparent("Hologram", vsHologram.Get(), psHologram.Get(), false);
		}

		auto vsForceField = CompileShaderFromFile(L"Resources/shaders/ForceFieldVS.hlsl", "main", "vs_5_0");
		auto psForceField = CompileShaderFromFile(L"Resources/shaders/ForceFieldPS.hlsl", "main", "ps_5_0");
		if (vsForceField && psForceField) {
			CreatePSO_Transparent("ForceField", vsForceField.Get(), psForceField.Get(), true);
		}

		auto vsDissolve = CompileShaderFromFile(L"Resources/shaders/DissolveVS.hlsl", "main", "vs_5_0");
		auto psDissolve = CompileShaderFromFile(L"Resources/shaders/DissolvePS.hlsl", "main", "ps_5_0");
		if (vsDissolve && psDissolve) {
			CreatePSO("Dissolve", vsDissolve.Get(), psDissolve.Get());
		}
	}

	// ★追加: 川用シェーダー
	{
		auto vsRiver = CompileShaderFromFile(L"Resources/shaders/RiverVS.hlsl", "main", "vs_5_0");
		auto psRiver = CompileShaderFromFile(L"Resources/shaders/RiverPS.hlsl", "main", "ps_5_0");
		if (vsRiver && psRiver) {
			D3D12_INPUT_ELEMENT_DESC layout[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			};
			D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
			pso.pRootSignature = rootSig3D_.Get();
			pso.VS = {vsRiver->GetBufferPointer(), vsRiver->GetBufferSize()};
			pso.PS = {psRiver->GetBufferPointer(), psRiver->GetBufferSize()};
			pso.InputLayout = {layout, _countof(layout)};
			pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			pso.NumRenderTargets = 1;
			pso.SampleDesc.Count = 1;
			pso.SampleMask = UINT_MAX;
			auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			rast.CullMode = D3D12_CULL_MODE_NONE; // 両面描画
			rast.DepthBias = -100; // 地形より手前に描画
			rast.SlopeScaledDepthBias = -1.0f;
			pso.RasterizerState = rast;
			pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			Microsoft::WRL::ComPtr<ID3D12PipelineState> riverPso;
			if (SUCCEEDED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&riverPso)))) {
				pipelines_["River"] = riverPso;
				shaderNames_.push_back("River");
			}
		}
	}

	// ★追加: 空間のゆがみ (Distortion) シェーダーの登録
	{
		auto vs = CompileShaderFromFile(L"Resources/shaders/Distortion.hlsl", "main", "vs_5_0");
		auto ps = CompileShaderFromFile(L"Resources/shaders/Distortion.hlsl", "ps_main", "ps_5_0");
		if (vs && ps) {
			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
			psoDesc.pRootSignature = rootSigDistortion_.Get();
			psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
			psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			
			// 背景サンプリングのため不透明描画
			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度は書かない
			
			psoDesc.InputLayout = { skinLayout, _countof(skinLayout) };
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets = 1;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			psoDesc.SampleDesc.Count = 1;

			Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
			if (SUCCEEDED(dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)))) {
				pipelines_["Distortion"] = pso;
				shaderNames_.push_back("Distortion");
			}
		}
	}

	// ★追加: Skybox メッシュ＆パイプライン初期化
	InitSkyboxMesh();
	InitSkyboxPipeline();

	return true;
}

Renderer::TextureHandle Renderer::LoadTexture2D(const std::string& filePath, bool sRGB) {
	if (filePath.empty())
		return 0;
	
	std::string unifiedPath = PathUtils::GetUnifiedPath(filePath);
	auto it = textureCache_.find(unifiedPath);
	if (it != textureCache_.end())
		return it->second;

	DirectX::ScratchImage img;
	std::wstring wpath = PathUtils::FromUTF8(unifiedPath);
	HRESULT hr = DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, img);
	if (FAILED(hr))
		return 0;

	const DirectX::Image* src = img.GetImage(0, 0, 0);
	if (!src)
		return 0;

	DirectX::ScratchImage conv;
	if (src->format != DXGI_FORMAT_R8G8B8A8_UNORM && src->format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		hr = DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv);
		if (FAILED(hr))
			return 0;
		src = conv.GetImage(0, 0, 0);
	}

	const DXGI_FORMAT fmt = sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	ComPtr<ID3D12Resource> tex;
	{
		CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(fmt, (UINT64)src->width, (UINT)src->height, 1, 1);
		hr = dev_->CreateCommittedResource(&heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
		if (FAILED(hr))
			return 0;
	}

	ComPtr<ID3D12Resource> upload;
	UINT64 uploadSize = 0;
	{
		D3D12_RESOURCE_DESC texDesc = tex->GetDesc();
		dev_->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);
		CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC upDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		hr = dev_->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &upDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
		if (FAILED(hr))
			return 0;
	}

	{
		ComPtr<ID3D12CommandAllocator> alloc;
		ComPtr<ID3D12GraphicsCommandList> cl;
		dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
		dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cl));

		D3D12_SUBRESOURCE_DATA sub{};
		sub.pData = src->pixels;
		sub.RowPitch = (LONG_PTR)src->rowPitch;
		sub.SlicePitch = (LONG_PTR)src->slicePitch;
		UpdateSubresources(cl.Get(), tex.Get(), upload.Get(), 0, 0, 1, &sub);
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cl->ResourceBarrier(1, &barrier);
		cl->Close();

		ID3D12CommandList* lists[] = {cl.Get()};
		queue_->ExecuteCommandLists(1, lists);
		WaitGPU();
	}

	const uint32_t idx = AllocateSrvIndex();
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = window_->SRV_CPU((int)idx);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuMaster = window_->SRV_CPU_Master((int)idx); // ★追加
	D3D12_GPU_DESCRIPTOR_HANDLE gpu = window_->SRV_GPU((int)idx);
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Format = fmt;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	dev_->CreateShaderResourceView(tex.Get(), &srv, cpu);
	dev_->CreateShaderResourceView(tex.Get(), &srv, cpuMaster); // ★追加

	Texture t{};
	t.res = tex;
	t.srvCpu = cpu;
	t.srvCpuMaster = cpuMaster; // ★追加
	t.srvGpu = gpu;

	TextureHandle handle = (TextureHandle)textures_.size();
	textures_.push_back(t);
	textureCache_[unifiedPath] = handle;
	return handle;
}

void Renderer::DrawMeshInstanced(MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor, 
								 const std::string& shaderName, const std::vector<TextureHandle>& extraTex) {
	DrawMeshInstanced(mesh, texture, transform.ToMatrix(), mulColor, shaderName, extraTex);
}

void Renderer::DrawMeshInstanced(MeshHandle mesh, TextureHandle texture, const Matrix4x4& worldMatrix, const Vector4& mulColor, 
								 const std::string& shaderName, const std::vector<TextureHandle>& extraTex) {
	// キャッシュチェック (前回のドローコールと同じアセットなら検索をスキップ)
	if (lastIDCIndex_ != -1 && lastIDCIndex_ < (int)instancedDrawCalls_.size()) {
		auto& last = instancedDrawCalls_[lastIDCIndex_];
		if (last.mesh == mesh && last.tex == texture && last.shaderName == shaderName && last.extraTex == extraTex) {
			InstanceData data;
			data.world = worldMatrix;
			data.color = mulColor;
			data.uvScaleOffset = {1, 1, 0, 0};
			last.instances.push_back(data);
			return;
		}
	}

	// 既存のInstancedDrawCallを探す
	auto it = std::find_if(instancedDrawCalls_.begin(), instancedDrawCalls_.end(), [&](const InstancedDrawCall& idc) {
		return idc.mesh == mesh && idc.tex == texture && idc.shaderName == shaderName && idc.extraTex == extraTex;
	});

	if (it == instancedDrawCalls_.end()) {
		InstancedDrawCall newIdc;
		newIdc.mesh = mesh;
		newIdc.tex = texture;
		newIdc.extraTex = extraTex; 
		newIdc.shaderName = shaderName;
		instancedDrawCalls_.push_back(newIdc);
		it = instancedDrawCalls_.end() - 1;
	}

	lastIDCIndex_ = static_cast<int>(std::distance(instancedDrawCalls_.begin(), it));

	InstanceData data;
	data.world = worldMatrix;
	data.color = mulColor;
	data.uvScaleOffset = {1, 1, 0, 0}; // デフォルト
	it->instances.push_back(data);
}

void Renderer::DrawParticleInstanced(MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor, const Vector4& uvScaleOffset, const std::string& shaderName) {
	auto it = std::find_if(instancedParticleDrawCalls_.begin(), instancedParticleDrawCalls_.end(), [&](const InstancedDrawCall& idc) {
		return idc.mesh == mesh && idc.tex == texture && idc.shaderName == shaderName;
	});

	if (it == instancedParticleDrawCalls_.end()) {
		InstancedDrawCall newIdc;
		newIdc.mesh = mesh;
		newIdc.tex = texture;
		newIdc.shaderName = shaderName;
		instancedParticleDrawCalls_.push_back(newIdc);
		it = instancedParticleDrawCalls_.end() - 1;
	}

	InstanceData data;
	data.world = transform.ToMatrix();
	data.color = mulColor;
	data.uvScaleOffset = uvScaleOffset;
	it->instances.push_back(data);
}

Renderer::MeshHandle Renderer::LoadObjMesh(const std::string& objFilePath) {
	if (objFilePath.empty())
		return 0;

	std::string unifiedPath = PathUtils::GetUnifiedPath(objFilePath);
	auto it = meshCache_.find(unifiedPath);
	if (it != meshCache_.end())
		return (MeshHandle)it->second;

	// ★修正: 一時的なコマンドリストを作成して使用する
	ComPtr<ID3D12CommandAllocator> alloc;
	ComPtr<ID3D12GraphicsCommandList> cmd;
	if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) {
		return 0;
	}
	if (FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd)))) {
		return 0;
	}

	auto model = std::make_shared<Model>();

	// 作成した一時コマンドリストを渡す
	if (!model->Load(dev_, cmd.Get(), unifiedPath)) {
		return 0;
	}

	// コマンドリストを閉じて実行
	cmd->Close();
	ID3D12CommandList* ppCommandLists[] = {cmd.Get()};
	queue_->ExecuteCommandLists(1, ppCommandLists);

	// 完了を待機 (テクスチャ転送を確実にするため)
	WaitGPU();

	if (model->GetData().material.textureFilePath.size() > 0) {
		uint32_t idx = AllocateSrvIndex();
		model->CreateSrv(dev_, srvHeap_, window_->SRV_CPU_Heap(), srvInc_, idx);
	}

	MeshHandle handle = (MeshHandle)models_.size();
	models_.push_back(model);
	meshCache_[unifiedPath] = (int)handle;
	return handle;
}

Model* Renderer::GetModel(MeshHandle handle) {
	if (handle == 0 || handle >= models_.size())
		return nullptr;
	return models_[handle].get();
}

void Renderer::DrawMesh(MeshHandle meshH, TextureHandle texH, const Transform& tr, const Vector4& mulColor, const std::string& shaderName, float reflectivity) {
	DrawMesh(meshH, texH, tr.ToMatrix(), mulColor, shaderName, reflectivity);
}

void Renderer::DrawMesh(MeshHandle meshH, TextureHandle texH, const Matrix4x4& worldMatrix, const Vector4& mulColor, const std::string& shaderName, float reflectivity) {
	if (meshH == 0 || meshH >= models_.size())
		return;

	DrawCall dc{};
	dc.mesh = meshH;
	dc.tex = texH;
	dc.worldMatrix = worldMatrix;
	dc.color = mulColor;
	dc.shaderName = shaderName;
	dc.reflectivity = reflectivity;
	drawCalls_.push_back(dc);
}

// ★追加: UVスケール・オフセット付きパーティクル描画
void Renderer::DrawParticle(MeshHandle meshH, TextureHandle texH, const Transform& tr, 
							const Vector4& mulColor, const Vector4& uvScaleOffset, 
							const std::string& shaderName) {
	DrawParticle(meshH, texH, tr.ToMatrix(), mulColor, uvScaleOffset, shaderName);
}

void Renderer::DrawParticle(MeshHandle meshH, TextureHandle texH, const Matrix4x4& worldMatrix, 
							const Vector4& mulColor, const Vector4& uvScaleOffset, 
							const std::string& shaderName) {
	if (meshH == 0 || meshH >= models_.size())
		return;

	DrawCall dc{};
	dc.mesh = meshH;
	dc.tex = texH;
	dc.worldMatrix = worldMatrix;
	dc.color = mulColor;
	dc.shaderName = shaderName;
	dc.isParticle = true;
	dc.uvScaleOffset = uvScaleOffset;
	drawCalls_.push_back(dc);
}

void Renderer::DrawSkinnedMesh(MeshHandle meshH, TextureHandle texH, const Transform& tr, const std::vector<Matrix4x4>& bones, const Vector4& mulColor) {
	DrawSkinnedMesh(meshH, texH, tr.ToMatrix(), bones, mulColor);
}

void Renderer::DrawSkinnedMesh(MeshHandle meshH, TextureHandle texH, const Matrix4x4& worldMatrix, const std::vector<Matrix4x4>& bones, const Vector4& mulColor) {
	if (meshH == 0 || meshH >= models_.size())
		return;

	DrawCall dc{};
	dc.mesh = meshH;
	dc.tex = texH;
	dc.worldMatrix = worldMatrix;
	dc.color = mulColor;
	dc.shaderName = "Skinning";
	dc.isSkinned = true;
	dc.bones = bones;
	drawCalls_.push_back(dc);
}

void Renderer::DrawSprite(TextureHandle texH, const SpriteDesc& s) {
	if (texH == 0 || texH >= textures_.size()) return;
	spriteDrawCalls_.push_back({ texH, s });
}

void Renderer::DrawSprite9Slice(TextureHandle texH, const Sprite9SliceDesc& s) {
	if (texH >= textures_.size() || !textures_[texH].res) return;

	D3D12_RESOURCE_DESC texDesc = textures_[texH].res->GetDesc();
	float tw = (float)texDesc.Width;
	float th = (float)texDesc.Height;

	float x[4] = { s.x, s.x + s.left, s.x + s.w - s.right, s.x + s.w };
	float y[4] = { s.y, s.y + s.top, s.y + s.h - s.bottom, s.y + s.h };
	float u[4] = { 0, s.left / tw, (tw - s.right) / tw, 1 };
	float v[4] = { 0, s.top / th, (th - s.bottom) / th, 1 };

	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			float w = x[col + 1] - x[col];
			float h = y[row + 1] - y[row];
			if (w <= 0.01f || h <= 0.01f) continue;

			SpriteDesc sd;
			sd.x = x[col];
			sd.y = y[row];
			sd.w = w;
			sd.h = h;
			sd.color = s.color;
			sd.rotationRad = s.rotationRad;
			sd.uvScaleOffset = { u[col+1] - u[col], v[row+1] - v[row], u[col], v[row] };
			sd.layer = s.layer; // ★追加: レイヤー引き継ぎ
			DrawSprite(texH, sd);
		}
	}
}

void Renderer::FlushSprites() {
	if (spriteDrawCalls_.empty()) return;

	// ★追加: レイヤー値で安定ソート（小さい値が先 = 奥に描画）
	std::stable_sort(spriteDrawCalls_.begin(), spriteDrawCalls_.end(), [](const SpriteDrawCall& a, const SpriteDrawCall& b) {
		return a.desc.layer < b.desc.layer;
	});

	const float W = (float)Engine::WindowDX::kW;
	const float H = (float)Engine::WindowDX::kH;
	const uint32_t fi = window_->FrameIndex();

	ID3D12DescriptorHeap* heaps[] = { srvHeap_ };
	list_->SetDescriptorHeaps(1, heaps);
	list_->SetPipelineState(pso2D_.Get());
	list_->SetGraphicsRootSignature(rootSig2D_.Get());
	list_->RSSetViewports(1, &viewport_);
	list_->RSSetScissorRects(1, &scissor_);
	list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (const auto& dc : spriteDrawCalls_) {
		const auto& s = dc.desc;
		auto texH = dc.tex;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
		struct alignas(256) CBSprite {
			Matrix4x4 mvp;
			float color[4];
		};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

		CBSprite cb{};
		cb.mvp = Matrix4x4::Identity();
		cb.color[0] = s.color.x;
		cb.color[1] = s.color.y;
		cb.color[2] = s.color.z;
		cb.color[3] = s.color.w;

		const uint32_t cbOff = upload_[fi].Allocate(sizeof(CBSprite), 256);
		if (cbOff == UINT32_MAX)
			continue;
		std::memcpy(upload_[fi].mapped + cbOff, &cb, sizeof(CBSprite));
		list_->SetGraphicsRootConstantBufferView(0, upload_[fi].buffer->GetGPUVirtualAddress() + cbOff);

		auto ToNDCX = [&](float x) { return (x / W) * 2.0f - 1.0f; };
		auto ToNDCY = [&](float y) { return 1.0f - (y / H) * 2.0f; };
		const float cx = s.x + s.w * 0.5f;
		const float cy = s.y + s.h * 0.5f;

		struct V {
			float x, y;
			float u, v;
		};
		auto Rot = [&](float x, float y) {
			float px = x - cx;
			float py = y - cy;
			const float c = cosf(s.rotationRad);
			const float si = sinf(s.rotationRad);
			float rx = px * c - py * si;
			float ry = px * si + py * c;
			return std::pair<float, float>(cx + rx, cy + ry);
		};
		auto P = [&](float sx, float sy) {
			auto [rx, ry] = Rot(sx, sy);
			return std::pair<float, float>(ToNDCX(rx), ToNDCY(ry));
		};

		auto p00 = P(s.x, s.y);
		auto p10 = P(s.x + s.w, s.y);
		auto p01 = P(s.x, s.y + s.h);
		auto p11 = P(s.x + s.w, s.y + s.h);

		V vtx[] = {
			{p00.first, p00.second, 0 * s.uvScaleOffset.x + s.uvScaleOffset.z, 0 * s.uvScaleOffset.y + s.uvScaleOffset.w},
			{p10.first, p10.second, 1 * s.uvScaleOffset.x + s.uvScaleOffset.z, 0 * s.uvScaleOffset.y + s.uvScaleOffset.w},
			{p01.first, p01.second, 0 * s.uvScaleOffset.x + s.uvScaleOffset.z, 1 * s.uvScaleOffset.y + s.uvScaleOffset.w},
			{p01.first, p01.second, 0 * s.uvScaleOffset.x + s.uvScaleOffset.z, 1 * s.uvScaleOffset.y + s.uvScaleOffset.w},
			{p10.first, p10.second, 1 * s.uvScaleOffset.x + s.uvScaleOffset.z, 0 * s.uvScaleOffset.y + s.uvScaleOffset.w},
			{p11.first, p11.second, 1 * s.uvScaleOffset.x + s.uvScaleOffset.z, 1 * s.uvScaleOffset.y + s.uvScaleOffset.w},
		};

		const uint32_t vbOff = upload_[fi].Allocate(sizeof(vtx), 16);
		if (vbOff == UINT32_MAX)
			continue;
		std::memcpy(upload_[fi].mapped + vbOff, vtx, sizeof(vtx));

		D3D12_VERTEX_BUFFER_VIEW vbv{};
		vbv.BufferLocation = upload_[fi].buffer->GetGPUVirtualAddress() + vbOff;
		vbv.SizeInBytes = sizeof(vtx);
		vbv.StrideInBytes = sizeof(V);

		list_->IASetVertexBuffers(0, 1, &vbv);
		list_->SetGraphicsRootDescriptorTable(1, textures_[texH].srvGpu);
		list_->DrawInstanced(6, 1, 0, 0);
	}
}

// ★追加: テキスト描画システム

bool Renderer::InitTextSystem(const std::string& fontPath, float pixelHeight) {
	// 既にこのフォントがロード済みなら何もしない
	if (glyphCaches_.count(fontPath)) return true;

	auto cache = std::make_unique<DynamicGlyphCache>();
	if (!cache->Initialize(this, fontPath, pixelHeight)) {
		return false;
	}
	glyphCaches_[fontPath] = std::move(cache);

	// テキスト描画用 PSO が未作成なら作成 (初回のみ)
	if (!psoText_) {
		if (!rootSig2D_) return false;

		static const char* kVSText = R"(
struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
struct VSOut { float4 svpos : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
VSOut main(VSIn v) {
    VSOut o;
    o.svpos = float4(v.pos, 0, 1);
    o.uv = v.uv;
    o.color = v.color;
    return o;
})";

		static const char* kPSText = R"(
Texture2D gTex : register(t0);
SamplerState gSmp : register(s0);
struct PSIn { float4 svpos : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
float4 main(PSIn i) : SV_TARGET {
    float alpha = gTex.Sample(gSmp, i.uv).r;
    return float4(i.color.rgb, i.color.a * alpha);
})";

		auto vsBlob = CompileShader(kVSText, "main", "vs_5_0");
		auto psBlob = CompileShader(kPSText, "main", "ps_5_0");
		if (!vsBlob || !psBlob) return false;

		D3D12_INPUT_ELEMENT_DESC layout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
		pso.pRootSignature = rootSig2D_.Get();
		pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
		pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
		pso.InputLayout = {layout, _countof(layout)};
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso.NumRenderTargets = 1;
		pso.SampleDesc.Count = 1;
		pso.SampleMask = UINT_MAX;
		pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

		auto blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blend.RenderTarget[0].BlendEnable = TRUE;
		blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pso.BlendState = blend;

		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = FALSE;
		pso.DSVFormat = DXGI_FORMAT_UNKNOWN;

		if (FAILED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoText_)))) {
			return false;
		}
	}

	return true;
}

void Renderer::DrawString(const std::string& text, float x, float y, float scale, const Vector4& color, const std::string& fontPath) {
	// フォントが未ロードなら遅延初期化
	if (!glyphCaches_.count(fontPath)) {
		if (!InitTextSystem(fontPath, 64.0f)) return;
	}
	auto& cache = glyphCaches_[fontPath];
	if (!cache || !cache->IsInitialized()) return;

	const float W = static_cast<float>(Engine::WindowDX::kW);
	const float H = static_cast<float>(Engine::WindowDX::kH);

	auto codepoints = Utf8ToCodepoints(text);

	float cursorX = x;
	float cursorY = y;

	int ascent = cache->GetAscent();
	float scaledAscent = ascent * scale;

	auto& vertices = textVerticesMap_[fontPath];

	for (uint32_t cp : codepoints) {
		// 改行処理
		if (cp == '\n') {
			cursorX = x;
			cursorY += cache->GetLineHeight() * scale;
			continue;
		}
		// タブ → 4スペース分
		if (cp == '\t') {
			const CachedGlyph* spaceGlyph = cache->GetGlyph(' ');
			if (spaceGlyph) {
				cursorX += spaceGlyph->metrics.advance * scale * 4.0f;
			}
			continue;
		}

		const CachedGlyph* glyph = cache->GetGlyph(cp);
		if (!glyph) continue;

		if (glyph->hasBitmap) {
			// Quadの位置を計算 (ピクセル座標)
			float xPos = cursorX + glyph->metrics.bearingX * scale;
			float yPos = cursorY + (scaledAscent - glyph->metrics.bearingY * scale);
			float w = glyph->metrics.width * scale;
			float h = glyph->metrics.height * scale;

			// ピクセル座標 → NDC
			auto toNdcX = [W](float px) { return (px / W) * 2.0f - 1.0f; };
			auto toNdcY = [H](float py) { return 1.0f - (py / H) * 2.0f; };

			float nx0 = toNdcX(xPos);
			float ny0 = toNdcY(yPos);
			float nx1 = toNdcX(xPos + w);
			float ny1 = toNdcY(yPos + h);

			// 6頂点 (2三角形)
			TextVertex v0 = { nx0, ny0, glyph->u0, glyph->v0, color.x, color.y, color.z, color.w };
			TextVertex v1 = { nx1, ny0, glyph->u1, glyph->v0, color.x, color.y, color.z, color.w };
			TextVertex v2 = { nx0, ny1, glyph->u0, glyph->v1, color.x, color.y, color.z, color.w };
			TextVertex v3 = { nx0, ny1, glyph->u0, glyph->v1, color.x, color.y, color.z, color.w };
			TextVertex v4 = { nx1, ny0, glyph->u1, glyph->v0, color.x, color.y, color.z, color.w };
			TextVertex v5 = { nx1, ny1, glyph->u1, glyph->v1, color.x, color.y, color.z, color.w };

			vertices.push_back(v0);
			vertices.push_back(v1);
			vertices.push_back(v2);
			vertices.push_back(v3);
			vertices.push_back(v4);
			vertices.push_back(v5);
		}

		cursorX += glyph->metrics.advance * scale;
	}
}

void Renderer::FlushText() {
	if (textVerticesMap_.empty() || !psoText_) return;

	const uint32_t fi = window_->FrameIndex();

	ID3D12DescriptorHeap* heaps[] = { srvHeap_ };
	list_->SetDescriptorHeaps(1, heaps);
	list_->SetPipelineState(psoText_.Get());
	list_->SetGraphicsRootSignature(rootSig2D_.Get());
	list_->RSSetViewports(1, &viewport_);
	list_->RSSetScissorRects(1, &scissor_);
	list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// b0: ダミーのCBSprite
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
	struct alignas(256) CBSprite {
		Matrix4x4 mvp;
		float color[4];
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	CBSprite cb{};
	cb.mvp = Matrix4x4::Identity();
	cb.color[0] = cb.color[1] = cb.color[2] = cb.color[3] = 1.0f;
	const uint32_t cbOff = upload_[fi].Allocate(sizeof(CBSprite), 256);
	if (cbOff != UINT32_MAX) {
		std::memcpy(upload_[fi].mapped + cbOff, &cb, sizeof(CBSprite));
		list_->SetGraphicsRootConstantBufferView(0, upload_[fi].buffer->GetGPUVirtualAddress() + cbOff);
	}

	// フォントごとにアトラスSRVをバインドして描画
	for (auto& [fontKey, vertices] : textVerticesMap_) {
		if (vertices.empty()) continue;

		auto it = glyphCaches_.find(fontKey);
		if (it == glyphCaches_.end() || !it->second) continue;

		const uint32_t vertCount = static_cast<uint32_t>(vertices.size());
		const uint32_t bytesNeeded = vertCount * sizeof(TextVertex);

		const uint32_t vbOff = upload_[fi].Allocate(bytesNeeded, 16);
		if (vbOff == UINT32_MAX) continue;

		std::memcpy(upload_[fi].mapped + vbOff, vertices.data(), bytesNeeded);

		// このフォントのアトラスSRVをバインド
		uint32_t texIdx = AllocateDynamicSrvIndex(1);
		if (texIdx != UINT32_MAX) {
			D3D12_CPU_DESCRIPTOR_HANDLE dest = window_->SRV_CPU(static_cast<int>(texIdx));
			dev_->CopyDescriptorsSimple(1, dest, it->second->GetAtlasSrvCpuMaster(),
			                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			list_->SetGraphicsRootDescriptorTable(1, window_->SRV_GPU(static_cast<int>(texIdx)));
		}

		D3D12_VERTEX_BUFFER_VIEW vbv{};
		vbv.BufferLocation = upload_[fi].buffer->GetGPUVirtualAddress() + vbOff;
		vbv.SizeInBytes = bytesNeeded;
		vbv.StrideInBytes = sizeof(TextVertex);

		list_->IASetVertexBuffers(0, 1, &vbv);
		list_->DrawInstanced(vertCount, 1, 0, 0);
	}

	textVerticesMap_.clear();
}

float Renderer::MeasureTextWidth(const std::string& text, float scale, const std::string& fontPath) {
	// フォントが未ロードなら遅延初期化
	if (!glyphCaches_.count(fontPath)) {
		if (!InitTextSystem(fontPath, 64.0f)) return 0.0f;
	}
	auto it = glyphCaches_.find(fontPath);
	if (it == glyphCaches_.end() || !it->second || !it->second->IsInitialized()) return 0.0f;
	auto& cache = it->second;

	auto codepoints = Utf8ToCodepoints(text);
	float width = 0.0f;

	for (uint32_t cp : codepoints) {
		if (cp == '\n') break;
		if (cp == '\t') {
			const CachedGlyph* spaceGlyph = cache->GetGlyph(' ');
			if (spaceGlyph) width += spaceGlyph->metrics.advance * scale * 4.0f;
			continue;
		}
		const CachedGlyph* glyph = cache->GetGlyph(cp);
		if (glyph) {
			width += glyph->metrics.advance * scale;
		}
	}
	return width;
}

float Renderer::GetTextLineHeight(float scale, const std::string& fontPath) const {
	auto it = glyphCaches_.find(fontPath);
	if (it == glyphCaches_.end() || !it->second || !it->second->IsInitialized()) return 0.0f;
	return it->second->GetLineHeight() * scale;
}

// ★追加: 3Dライン描画（蓄積API）
void Renderer::DrawLine3D(const Vector3& p0, const Vector3& p1, const Vector4& color, bool xray) {
	if (xray) {
		if (lineVerticesXRay_.size() + 2 > kMaxLineVertices) return; // 溢れ防止
		lineVerticesXRay_.push_back({p0.x, p0.y, p0.z, color.x, color.y, color.z, color.w});
		lineVerticesXRay_.push_back({p1.x, p1.y, p1.z, color.x, color.y, color.z, color.w});
	} else {
		if (lineVertices_.size() + 2 > kMaxLineVertices) return; // 溢れ防止
		lineVertices_.push_back({p0.x, p0.y, p0.z, color.x, color.y, color.z, color.w});
		lineVertices_.push_back({p1.x, p1.y, p1.z, color.x, color.y, color.z, color.w});
	}
}

// ★追加: 蓄積した3Dラインを一括描画
void Renderer::FlushLines() {
	auto drawBuffer = [&](const std::vector<LineVertex>& vertices, ID3D12PipelineState* pipeline) {
		if (vertices.empty()) return;

		const uint32_t vertCount = static_cast<uint32_t>(vertices.size());
		const uint32_t bytesNeeded = vertCount * sizeof(LineVertex);

		const uint32_t fi = window_->FrameIndex();
		const uint32_t off = upload_[fi].Allocate(bytesNeeded, 16);
		if (off == UINT32_MAX) return;

		std::memcpy(upload_[fi].mapped + off, vertices.data(), bytesNeeded);

		ID3D12DescriptorHeap* heaps[] = { srvHeap_ };
		list_->SetDescriptorHeaps(1, heaps);

		list_->SetPipelineState(pipeline);
		list_->SetGraphicsRootSignature(rootSig3D_.Get());
		list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
		list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

		D3D12_VERTEX_BUFFER_VIEW vbv{};
		vbv.BufferLocation = upload_[fi].buffer->GetGPUVirtualAddress() + off;
		vbv.SizeInBytes = bytesNeeded;
		vbv.StrideInBytes = sizeof(LineVertex);
		list_->IASetVertexBuffers(0, 1, &vbv);

		list_->DrawInstanced(vertCount, 1, 0, 0);
	};

	drawBuffer(lineVertices_, psoLine3D_.Get());
	drawBuffer(lineVerticesXRay_, psoLine3DXRay_.Get());

	lineVertices_.clear();
	lineVerticesXRay_.clear();
}

bool Renderer::InitPostProcess_() {
	{
		const UINT W = Engine::WindowDX::kW;
		const UINT H = Engine::WindowDX::kH;
		D3D12_RESOURCE_DESC rd{};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rd.Width = W;
		rd.Height = H;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rd.SampleDesc.Count = 1;
		rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE cv{};
		cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		std::memcpy(cv.Color, kPPSceneClearColor, sizeof(float) * 4);

		CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		HRESULT hr = dev_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&ppSceneColor_));
		if (FAILED(hr)) return false;
		ppSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		hd.NumDescriptors = 1;
		hr = dev_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&ppRtvHeap_));
		if (FAILED(hr)) return false;
		ppRtv_ = ppRtvHeap_->GetCPUDescriptorHandleForHeapStart();
		dev_->CreateRenderTargetView(ppSceneColor_.Get(), nullptr, ppRtv_);

		const uint32_t idx = AllocateSrvIndex();
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = window_->SRV_CPU((int)idx);
		D3D12_CPU_DESCRIPTOR_HANDLE cpuMaster = window_->SRV_CPU_Master((int)idx); // ★追加
		ppSrvGpu_ = window_->SRV_GPU((int)idx);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		dev_->CreateShaderResourceView(ppSceneColor_.Get(), &srv, cpu);
		dev_->CreateShaderResourceView(ppSceneColor_.Get(), &srv, cpuMaster); // ★追加
	}
	{
		static const char* kVSPP = R"(
struct VSOut { float4 svpos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    float2 p = (vid == 0) ? float2(-1, -1) : ((vid == 1) ? float2(-1,  3) : float2( 3, -1));
    VSOut o; o.svpos = float4(p, 0, 1); o.uv = float2((p.x + 1) * 0.5, 1.0 - (p.y + 1) * 0.5); return o;
})";
		static const char* kPSPP = R"(
Texture2D gScene : register(t0); SamplerState gSmp : register(s0);
cbuffer CBPost : register(b0) { float gTime; float gNoiseStrength; float gDistortion; float gChromaShift; float gVignette; float gScanline; float2 pad; };
float hash(float2 p) { return frac(sin(dot(p, float2(12.9898,78.233))) * 43758.5453); }
float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
    float wave = sin(uv.y * 900.0 + gTime * 12.0) * gDistortion;
    float2 uvd = uv + float2(wave, 0);
    float3 col = float3(gScene.Sample(gSmp, uvd + float2(gChromaShift,0)).r, gScene.Sample(gSmp, uvd).g, gScene.Sample(gSmp, uvd - float2(gChromaShift,0)).b);
    col -= sin(uv.y * 900.0).xxx * gScanline;
    col += (hash(uv * 1000.0 + gTime) - 0.5).xxx * gNoiseStrength;
    float2 d = uv - 0.5;
    col *= saturate(1.0 - dot(d,d) * gVignette);
    return float4(col, 1);
})";
		auto vs = CompileShader(kVSPP, "main", "vs_5_0");
		auto ps = CompileShaderFromFile(L"Resources/shaders/CRTPost.hlsl", "main", "ps_5_0");
		if (!ps)
			ps = CompileShader(kPSPP, "main", "ps_5_0");
		if (!vs || !ps)
			return false;

		CD3DX12_DESCRIPTOR_RANGE rangeSRV;
		rangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_ROOT_PARAMETER params[2]{};
		params[0].InitAsConstantBufferView(0);
		params[1].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_PIXEL);
		CD3DX12_STATIC_SAMPLER_DESC samp(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
		CD3DX12_ROOT_SIGNATURE_DESC rs{};
		rs.Init(_countof(params), params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		ComPtr<ID3DBlob> sig, err;
		D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
		dev_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rootSigPP_));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
		pso.pRootSignature = rootSigPP_.Get();
		pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
		pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso.NumRenderTargets = 1;
		pso.SampleDesc.Count = 1;
		pso.SampleMask = UINT_MAX;
		pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = FALSE;
		pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
		if (FAILED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoPP_))))
			return false;

		// ★追加：PostProcessと同様、そのままテクスチャをコピーするだけのパイプライン
		static const char* kPSCopy = R"(
Texture2D gScene : register(t0); SamplerState gSmp : register(s0);
float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
    return float4(gScene.Sample(gSmp, uv).rgb, 1.0f);
})";
		auto psCopy = CompileShader(kPSCopy, "main", "ps_5_0");
		if (psCopy) {
			pso.PS = { psCopy->GetBufferPointer(), psCopy->GetBufferSize() };
			dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoCopy_));
		}

		// ★追加: Rich PostProcess パイプライン
		auto psRich = CompileShaderFromFile(L"Resources/shaders/RichPostProcess.hlsl", "main", "ps_5_0");
		if (psRich) {
			pso.PS = { psRich->GetBufferPointer(), psRich->GetBufferSize() };
			Microsoft::WRL::ComPtr<ID3D12PipelineState> psoRich;
			if (SUCCEEDED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoRich)))) {
				pipelines_["Rich"] = psoRich;
			}
		}
	}

	// ★追加: 最終描画用テクスチャの作成
	{
		const UINT W = Engine::WindowDX::kW;
		const UINT H = Engine::WindowDX::kH;
		D3D12_RESOURCE_DESC rd{};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rd.Width = W;
		rd.Height = H;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rd.SampleDesc.Count = 1;
		rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE cv{};
		cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		std::memcpy(cv.Color, kFinalSceneClearColor, sizeof(float) * 4);

		CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		HRESULT hr = dev_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&finalSceneColor_));
		if (FAILED(hr)) return false;
		finalSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		hd.NumDescriptors = 16; // ★修正: カスタムレンダーターゲット用に増やす
		hr = dev_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&finalRtvHeap_));
		if (FAILED(hr)) return false;
		finalRtv_ = finalRtvHeap_->GetCPUDescriptorHandleForHeapStart();
		dev_->CreateRenderTargetView(finalSceneColor_.Get(), nullptr, finalRtv_);

		const uint32_t idx = AllocateSrvIndex();
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = window_->SRV_CPU((int)idx);
		D3D12_CPU_DESCRIPTOR_HANDLE cpuMaster = window_->SRV_CPU_Master((int)idx); // ★追加
		finalSrvGpu_ = window_->SRV_GPU((int)idx);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		dev_->CreateShaderResourceView(finalSceneColor_.Get(), &srv, cpu);
		dev_->CreateShaderResourceView(finalSceneColor_.Get(), &srv, cpuMaster); // ★追加
	}

	// ★追加: Distortion用バックドロップテクスチャの作成
	{
		const UINT W = Engine::WindowDX::kW;
		const UINT H = Engine::WindowDX::kH;
		D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
		CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_CLEAR_VALUE cv = { DXGI_FORMAT_R8G8B8A8_UNORM, {0,0,0,1} };
		HRESULT hr = dev_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&backdropColor_));
		if (FAILED(hr)) {
			// 歪み用バックドロップの生成失敗は致命的ではないが警告を出す
			OutputDebugStringA("[Renderer] Failed to create backdropColor_ for Distortion pass.\n");
		}
		
		uint32_t sIdx = AllocateSrvIndex();
		backdropSrv_ = window_->SRV_GPU((int)sIdx);
		backdropSrvCpu_ = window_->SRV_CPU((int)sIdx);
		backdropSrvCpuMaster_ = window_->SRV_CPU_Master((int)sIdx); // ★追加
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;
		dev_->CreateShaderResourceView(backdropColor_.Get(), &srv, backdropSrvCpu_);
		dev_->CreateShaderResourceView(backdropColor_.Get(), &srv, backdropSrvCpuMaster_); // ★追加
	}

	// ★追加: Distortion PSOの作成
	{
		auto vs = CompileShaderFromFile(L"Resources/shaders/Distortion.hlsl", "main", "vs_5_0");
		auto ps = CompileShaderFromFile(L"Resources/shaders/Distortion.hlsl", "ps_main", "ps_5_0");
		if (vs && ps) {
			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
			psoDesc.pRootSignature = rootSigDistortion_.Get(); // 専用RootSig
			psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
			psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
			
			D3D12_INPUT_ELEMENT_DESC layout[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // XMFLOAT4
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // XMFLOAT2
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // XMFLOAT3
				{ "WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // Weights
				{ "BONES",    0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // Indices
			};
			psoDesc.InputLayout = { layout, _countof(layout) };
			psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			
			// 透明ブレンド設定
			auto& rt = psoDesc.BlendState.RenderTarget[0];
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 歪み自体は深度を書かない
			
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets = 1;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			psoDesc.SampleDesc.Count = 1;

			if (SUCCEEDED(dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelines_["Distortion"])))) {
				OutputDebugStringA("[Renderer] Distortion PSO created.\n");
			}
		}
	}

	return true;
}

void Renderer::SetPostEffect(const std::string& name) {
	// 空文字の場合は無効化
	if (name.empty()) {
		ppEnabled_ = false;
		return;
	}

	// パイプラインマップから検索
	auto it = pipelines_.find(name);
	if (it != pipelines_.end()) {
		psoPP_ = it->second; // パイプラインステートを切り替え
		ppEnabled_ = true;   // 有効化
	}
}

// ★追加: 外部用のカスタムレンダーターゲット生成
Renderer::CustomRenderTarget Renderer::CreateRenderTarget(uint32_t width, uint32_t height) {
	CustomRenderTarget target{};
	target.width = width;
	target.height = height;

	// Texture (RTV用)
	D3D12_RESOURCE_DESC rd{};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rd.Width = width;
	rd.Height = height;
	rd.DepthOrArraySize = 1;
	rd.MipLevels = 1;
	rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rd.SampleDesc.Count = 1;
	rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE cv{};
	cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	static const float kClearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
	std::memcpy(cv.Color, kClearColor, sizeof(float) * 4);

	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	dev_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&target.texture));

	// Depth (DSV用)
	rd.Format = DXGI_FORMAT_D32_FLOAT;
	rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE dcv{};
	dcv.Format = DXGI_FORMAT_D32_FLOAT;
	dcv.DepthStencil.Depth = 1.0f;
	dev_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv, IID_PPV_ARGS(&target.depth));

	// Descriptors
	uint32_t rtvIdx = rtvCursor_++;
	target.rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(finalRtvHeap_->GetCPUDescriptorHandleForHeapStart(), rtvIdx, dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
	dev_->CreateRenderTargetView(target.texture.Get(), nullptr, target.rtv);

	uint32_t dsvIdx = dsvCursor_++;
	target.dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart(), dsvIdx, dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
	dev_->CreateDepthStencilView(target.depth.Get(), nullptr, target.dsv);

	uint32_t srvIdx = AllocateSrvIndex();
	target.srvGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvHeap_->GetGPUDescriptorHandleForHeapStart(), srvIdx, srvInc_);
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	dev_->CreateShaderResourceView(target.texture.Get(), &srvDesc, window_->SRV_CPU((int)srvIdx));
	dev_->CreateShaderResourceView(target.texture.Get(), &srvDesc, window_->SRV_CPU_Master((int)srvIdx)); // ★追加

	return target;
}

void Renderer::BeginCustomRenderTarget(const CustomRenderTarget& target) {
	currentCustomTarget_ = &target;

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target.texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	list_->ResourceBarrier(1, &barrier);

	list_->OMSetRenderTargets(1, &target.rtv, FALSE, &target.dsv);
	
	static const float kClearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
	list_->ClearRenderTargetView(target.rtv, kClearColor, 0, nullptr);
	list_->ClearDepthStencilView(target.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_VIEWPORT vp{};
	vp.Width = (float)target.width;
	vp.Height = (float)target.height;
	vp.MaxDepth = 1.0f;

	D3D12_RECT sc{};
	sc.right = target.width;
	sc.bottom = target.height;

	list_->RSSetViewports(1, &vp);
	list_->RSSetScissorRects(1, &sc);
}

void Renderer::EndCustomRenderTarget() {
	if (!currentCustomTarget_) return;

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		currentCustomTarget_->texture.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	list_->ResourceBarrier(1, &barrier);

	currentCustomTarget_ = nullptr;

	auto rtvBack = window_->GetCurrentRTV();
	list_->OMSetRenderTargets(1, &rtvBack, FALSE, nullptr);

	ResetGameViewport();
	list_->RSSetViewports(1, &viewport_);
	list_->RSSetScissorRects(1, &scissor_);
}

void Renderer::BeginCollisionCheck(uint32_t maxPairs) {
	collisionRequests_.clear();

	if (collisionMaxPairs_ != maxPairs) {
		// Cleanup old resources
		if (collisionReadbackBuffer_ && collisionReadbackMapped_) {
			collisionReadbackBuffer_->Unmap(0, nullptr);
			collisionReadbackMapped_ = nullptr;
		}
		collisionResultBuffer_.Reset();
		collisionReadbackBuffer_.Reset();
		collisionRequestBuffer_.Reset();
		collisionMaxPairs_ = 0; // 一旦クリア

		if (maxPairs > 0) {
			uint32_t bufferSize = maxPairs * sizeof(Game::ContactInfo);
			uint32_t requestBufferSize = maxPairs * sizeof(CollisionRequest);
			
			CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
			CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			if (FAILED(dev_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&collisionResultBuffer_)))) {
				return;
			}
			
			CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
			CD3DX12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
			if (FAILED(dev_->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&collisionReadbackBuffer_)))) {
				return;
			}
			
			CD3DX12_RESOURCE_DESC reqDesc = CD3DX12_RESOURCE_DESC::Buffer(requestBufferSize);
			if (FAILED(dev_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &reqDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&collisionRequestBuffer_)))) {
				return;
			}

			if (FAILED(collisionReadbackBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&collisionReadbackMapped_)))) {
				collisionReadbackMapped_ = nullptr;
				return;
			}
			
			collisionMaxPairs_ = maxPairs; // 成功した時のみ更新
		}
	}

	// 以前のクリア処理は EndCollisionCheck へ統合
}

void Renderer::DispatchCollision(MeshHandle /*meshA*/, uint32_t meshBHandle, const Transform& trA, const Game::BoxColliderComponent& bcA, const Transform& trB, uint32_t resultIndex) {
	if (!psoCollision_ || !rootSigCompute_ || !collisionResultBuffer_) return;
	auto* modelB = GetModel(meshBHandle);
	if (!modelB) return;

	CollisionRequest req{};
	req.worldA = trA.ToMatrix();
	req.worldB = trB.ToMatrix();
	req.resultIndex = resultIndex;

	// --- 座標空間の変換 (重要: BVHはメッシュのモデル空間にあるため、OBBもそれに合わせる) ---
	XMMATRIX worldA = M4ToXM(req.worldA);
	XMMATRIX worldB = M4ToXM(req.worldB);
	XMMATRIX invWorldB = XMMatrixInverse(nullptr, worldB);

	// OBBの中心を B のモデル空間へ
	XMVECTOR cOrig = XMLoadFloat3(&bcA.center);
	XMVECTOR cWorld = XMVector3TransformCoord(cOrig, worldA);
	XMVECTOR cModelB = XMVector3TransformCoord(cWorld, invWorldB);
	XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&req.obbCenter), cModelB);

	// OBBの各軸（サイズ込み）を B のモデル空間へ
	float ex = bcA.size.x * 0.5f * std::abs(trA.scale.x);
	float ey = bcA.size.y * 0.5f * std::abs(trA.scale.y);
	float ez = bcA.size.z * 0.5f * std::abs(trA.scale.z);

	XMVECTOR scaledAxisX = XMVectorScale(XMVector3Normalize(worldA.r[0]), ex);
	XMVECTOR scaledAxisY = XMVectorScale(XMVector3Normalize(worldA.r[1]), ey);
	XMVECTOR scaledAxisZ = XMVectorScale(XMVector3Normalize(worldA.r[2]), ez);

	XMVECTOR axisXModelB = XMVector3TransformNormal(scaledAxisX, invWorldB);
	XMVECTOR axisYModelB = XMVector3TransformNormal(scaledAxisY, invWorldB);
	XMVECTOR axisZModelB = XMVector3TransformNormal(scaledAxisZ, invWorldB);

	// モデル空間での新しいサイズ（Extents）を取得
	req.obbExtents.x = XMVectorGetX(XMVector3Length(axisXModelB));
	req.obbExtents.y = XMVectorGetX(XMVector3Length(axisYModelB));
	req.obbExtents.z = XMVectorGetX(XMVector3Length(axisZModelB));

	// 正規化した軸を保存
	XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&req.obbAxisX), XMVector3Normalize(axisXModelB));
	XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&req.obbAxisY), XMVector3Normalize(axisYModelB));
	XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&req.obbAxisZ), XMVector3Normalize(axisZModelB));

	req.numBvhNodes = modelB->GetBvhNodeCount();
	req.meshB = meshBHandle;

	collisionRequests_.push_back(req);
}

void Renderer::DispatchCollision(MeshHandle meshA, const Transform& trA, const Game::BoxColliderComponent& bcA, uint32_t resultIndex) {
	DispatchCollision(meshA, 0, trA, bcA, Transform(), resultIndex);
}

void Renderer::DispatchCollision(MeshHandle meshA, const Transform& trA, MeshHandle meshB, const Transform& trB, uint32_t resultIndex) {
	// For backward compatibility: Treat MeshA's AABB as an OBB
	Game::BoxColliderComponent bcA;
	auto* modelA = GetModel(meshA);
	if (modelA) {
		const auto& data = modelA->GetData();
		bcA.center = {(data.min.x + data.max.x) * 0.5f, (data.min.y + data.max.y) * 0.5f, (data.min.z + data.max.z) * 0.5f};
		bcA.size = {data.max.x - data.min.x, data.max.y - data.min.y, data.max.z - data.min.z};
	}
	DispatchCollision(meshA, meshB, trA, bcA, trB, resultIndex);
}

void Renderer::EndCollisionCheck() {
	if (!collisionResultBuffer_ || !collisionReadbackBuffer_ || collisionMaxPairs_ == 0) return;
	if (collisionRequests_.empty()) return;

	// 1. ターゲットメッシュ(meshB)ごとに並び替え（グルーピングのため、効率向上のため）
	std::sort(collisionRequests_.begin(), collisionRequests_.end(), [](const CollisionRequest& a, const CollisionRequest& b) {
		return a.meshB < b.meshB;
	});

	const uint32_t fi = window_->FrameIndex();

	// 1. 専用コマンドリストのリセット
	collisionAlloc_->Reset();
	collisionList_->Reset(collisionAlloc_.Get(), nullptr);

	// 2. 結果バッファのクリア
	uint32_t bufferSize = collisionMaxPairs_ * sizeof(Game::ContactInfo);
	uint32_t clearOff = upload_[fi].Allocate(bufferSize, 256);
	if (clearOff != UINT32_MAX) {
		std::memset(upload_[fi].mapped + clearOff, 0, bufferSize);
		auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(collisionResultBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
		collisionList_->ResourceBarrier(1, &b1);
		collisionList_->CopyBufferRegion(collisionResultBuffer_.Get(), 0, upload_[fi].buffer.Get(), clearOff, bufferSize);
		auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(collisionResultBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		collisionList_->ResourceBarrier(1, &b2);
	}

	// 3. 全リクエストデータをGPUに転送
	uint32_t reqSize = (uint32_t)(collisionRequests_.size() * sizeof(CollisionRequest));
	uint32_t reqOff = upload_[fi].Allocate(reqSize, 256);
	if (reqOff != UINT32_MAX) {
		std::memcpy(upload_[fi].mapped + reqOff, collisionRequests_.data(), reqSize);
		auto barrierReq = CD3DX12_RESOURCE_BARRIER::Transition(collisionRequestBuffer_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
		collisionList_->ResourceBarrier(1, &barrierReq);
		collisionList_->CopyBufferRegion(collisionRequestBuffer_.Get(), 0, upload_[fi].buffer.Get(), reqOff, reqSize);
		auto barrierReq2 = CD3DX12_RESOURCE_BARRIER::Transition(collisionRequestBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
		collisionList_->ResourceBarrier(1, &barrierReq2);
	} else {
		collisionList_->Close();
		return;
	}

	// 4. パイプラインとルートシグネチャの設定
	ID3D12DescriptorHeap* heaps[] = {srvHeap_};
	collisionList_->SetDescriptorHeaps(1, heaps);
	collisionList_->SetPipelineState(psoCollision_.Get());
	collisionList_->SetComputeRootSignature(rootSigCompute_.Get());
	collisionList_->SetComputeRootUnorderedAccessView(4, collisionResultBuffer_->GetGPUVirtualAddress());

	// 5. メッシュごとにグループ化して Dispatch
	uint32_t currentStart = 0;
	while (currentStart < (uint32_t)collisionRequests_.size()) {
		uint32_t meshHandle = collisionRequests_[currentStart].meshB;
		uint32_t count = 0;
		while (currentStart + count < (uint32_t)collisionRequests_.size() && collisionRequests_[currentStart + count].meshB == meshHandle) {
			count++;
		}
		auto* model = GetModel(meshHandle);
		if (model && model->GetBvhNodeCount() > 0 && model->GetBvhNodeBufferAddr() != 0) {
			collisionList_->SetComputeRoot32BitConstant(0, count, 0);
			collisionList_->SetComputeRootShaderResourceView(1, collisionRequestBuffer_->GetGPUVirtualAddress() + currentStart * sizeof(CollisionRequest));
			collisionList_->SetComputeRootShaderResourceView(2, model->GetBvhNodeBufferAddr());
			collisionList_->SetComputeRootShaderResourceView(3, model->GetBvhIndexBufferAddr());
			collisionList_->SetComputeRootShaderResourceView(5, model->GetVertexBufferAddr());
			collisionList_->SetComputeRootShaderResourceView(6, model->GetIndexBufferAddr());
			collisionList_->Dispatch((count + 63) / 64, 1, 1);
		}
		currentStart += count;
	}

	// 6. 結果を読み戻し用バッファにコピー
	auto bCopy = CD3DX12_RESOURCE_BARRIER::Transition(collisionResultBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	collisionList_->ResourceBarrier(1, &bCopy);
	collisionList_->CopyBufferRegion(collisionReadbackBuffer_.Get(), 0, collisionResultBuffer_.Get(), 0, collisionMaxPairs_ * sizeof(Game::ContactInfo));
	auto bBack = CD3DX12_RESOURCE_BARRIER::Transition(collisionResultBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	collisionList_->ResourceBarrier(1, &bBack);

	// 7. 命令発行と完了待ちの解除 (非同期的実行)
	collisionList_->Close();
	ID3D12CommandList* ppLists[] = {collisionList_.Get()};
	queue_->ExecuteCommandLists(1, ppLists);
	// WaitGPU(); // ★削除: CPU停止によるTDRを回避。結果は次フレーム以降に整合することになるが、生存性は向上する。

	// 8. リクエストリストをクリア (重要: 漏れると毎フレーム蓄積する)
	collisionRequests_.clear();
}

bool Renderer::GetCollisionResult(uint32_t resultIndex, Game::ContactInfo& outInfo) const {
	if (resultIndex < collisionMaxPairs_ && collisionReadbackMapped_) {
		outInfo = collisionReadbackMapped_[resultIndex];
		return outInfo.intersected > 0;
	}
	return false;
}

bool Renderer::GetCollisionResult(uint32_t resultIndex) const {
	Game::ContactInfo ci;
	return GetCollisionResult(resultIndex, ci);
}

Renderer::MeshHandle Renderer::CreateDynamicMesh(const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices) {
	auto model = std::make_shared<Model>();
	model->InitializeDynamic(dev_, vertices, indices);
	models_.push_back(model);
	return (MeshHandle)(models_.size() - 1);
}

void Renderer::UpdateDynamicMesh(MeshHandle handle, const std::vector<VertexData>& vertices) {
	if (handle < models_.size() && models_[handle]) {
		models_[handle]->UpdateVertices(vertices);
	}
}


// ====================================================================
// ★★★ Skybox / 環境マップ 実装 (CG4 00. 環境マップ) ★★★
// ====================================================================

Renderer::TextureHandle Renderer::LoadCubeMap(const std::string& ddsPath) {
	if (ddsPath.empty()) return 0;

	std::string unifiedPath = PathUtils::GetUnifiedPath(ddsPath);
	auto it = textureCache_.find(unifiedPath);
	if (it != textureCache_.end()) return it->second;

	std::wstring wpath = PathUtils::FromUTF8(unifiedPath);
	DirectX::TexMetadata meta{};
	DirectX::ScratchImage img;
	HRESULT hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, &meta, img);
	if (FAILED(hr)) {
		OutputDebugStringA(("[Renderer] LoadCubeMap FAILED: " + unifiedPath + "\n").c_str());
		return 0;
	}

	// キューブマップであることを確認
	if (!meta.IsCubemap()) {
		OutputDebugStringA(("[Renderer] LoadCubeMap: Not a cubemap: " + unifiedPath + "\n").c_str());
		return 0;
	}

	// リソース作成
	ComPtr<ID3D12Resource> tex;
	{
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = (UINT64)meta.width;
		desc.Height = (UINT)meta.height;
		desc.DepthOrArraySize = (UINT16)meta.arraySize; // 6 for cubemap
		desc.MipLevels = (UINT16)meta.mipLevels;
		desc.Format = meta.format;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
		hr = dev_->CreateCommittedResource(&heapDefault, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
		if (FAILED(hr)) return 0;
	}

	// サブリソースデータの準備とアップロード
	std::vector<D3D12_SUBRESOURCE_DATA> subData;
	const DirectX::Image* imgs = img.GetImages();
	size_t nimages = img.GetImageCount();
	subData.reserve(nimages);
	for (size_t i = 0; i < nimages; ++i) {
		D3D12_SUBRESOURCE_DATA sd{};
		sd.pData = imgs[i].pixels;
		sd.RowPitch = (LONG_PTR)imgs[i].rowPitch;
		sd.SlicePitch = (LONG_PTR)imgs[i].slicePitch;
		subData.push_back(sd);
	}

	UINT64 uploadSize = GetRequiredIntermediateSize(tex.Get(), 0, (UINT)subData.size());
	ComPtr<ID3D12Resource> uploadBuf;
	{
		CD3DX12_HEAP_PROPERTIES heapUp(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		dev_->CreateCommittedResource(&heapUp, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));
	}

	// 一時コマンドリストで転送
	ComPtr<ID3D12CommandAllocator> alloc;
	ComPtr<ID3D12GraphicsCommandList> cmd;
	dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
	dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd));

	UpdateSubresources(cmd.Get(), tex.Get(), uploadBuf.Get(), 0, 0, (UINT)subData.size(), subData.data());

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &barrier);
	cmd->Close();
	ID3D12CommandList* ppLists[] = { cmd.Get() };
	queue_->ExecuteCommandLists(1, ppLists);
	WaitGPU();

	// SRV作成 (TextureCube)
	Texture t{};
	t.res = tex;
	const uint32_t srvIdx = AllocateSrvIndex();
	t.srvCpu = window_->SRV_CPU((int)srvIdx);
	t.srvCpuMaster = window_->SRV_CPU_Master((int)srvIdx);
	t.srvGpu = window_->SRV_GPU((int)srvIdx);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = meta.format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.TextureCube.MipLevels = (UINT)meta.mipLevels;
	srvDesc.TextureCube.MostDetailedMip = 0;
	dev_->CreateShaderResourceView(tex.Get(), &srvDesc, t.srvCpu);
	dev_->CreateShaderResourceView(tex.Get(), &srvDesc, t.srvCpuMaster);

	TextureHandle handle = (TextureHandle)textures_.size();
	textures_.push_back(t);
	textureCache_[unifiedPath] = handle;

	OutputDebugStringA(("[Renderer] LoadCubeMap OK: " + unifiedPath + " handle=" + std::to_string(handle) + "\n").c_str());
	return handle;
}

void Renderer::SetSkyboxTexture(TextureHandle cubeMap) {
	skyboxCubeMapHandle_ = cubeMap;
	if (cubeMap > 0 && cubeMap < textures_.size()) {
		envMapSrvGpu_ = textures_[cubeMap].srvGpu;
	}
}

void Renderer::InitSkyboxMesh() {
	// キューブの頂点（内側向き：天球として見る）
	struct SkyboxVertex { float x, y, z; };
	SkyboxVertex verts[] = {
		{-1, -1, -1}, {+1, -1, -1}, {+1, +1, -1}, {-1, +1, -1}, // 前面
		{-1, -1, +1}, {+1, -1, +1}, {+1, +1, +1}, {-1, +1, +1}, // 背面
	};

	// インデックス（内側向き＝表裏反転）
	uint32_t indices[] = {
		// 前面 (z-)
		0, 2, 1,  0, 3, 2,
		// 背面 (z+)
		4, 5, 6,  4, 6, 7,
		// 左面 (x-)
		0, 4, 7,  0, 7, 3,
		// 右面 (x+)
		1, 2, 6,  1, 6, 5,
		// 上面 (y+)
		3, 7, 6,  3, 6, 2,
		// 下面 (y-)
		0, 1, 5,  0, 5, 4,
	};

	skyboxIndexCount_ = _countof(indices);

	// VB作成
	{
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(verts));
		dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&skyboxVB_));
		void* mapped;
		skyboxVB_->Map(0, nullptr, &mapped);
		std::memcpy(mapped, verts, sizeof(verts));
		skyboxVB_->Unmap(0, nullptr);
		skyboxVBV_ = { skyboxVB_->GetGPUVirtualAddress(), sizeof(verts), sizeof(SkyboxVertex) };
	}
	// IB作成
	{
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices));
		dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&skyboxIB_));
		void* mapped;
		skyboxIB_->Map(0, nullptr, &mapped);
		std::memcpy(mapped, indices, sizeof(indices));
		skyboxIB_->Unmap(0, nullptr);
		skyboxIBV_ = { skyboxIB_->GetGPUVirtualAddress(), sizeof(indices), DXGI_FORMAT_R32_UINT };
	}
}

bool Renderer::InitSkyboxPipeline() {
	// Skybox用 RootSignature: b0(CBFrame), t0(TextureCube), s0(Sampler)
	{
		CD3DX12_DESCRIPTOR_RANGE rangeSRV;
		rangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0: TextureCube

		CD3DX12_ROOT_PARAMETER params[2]{};
		params[0].InitAsConstantBufferView(0); // b0: CBFrame
		params[1].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_PIXEL); // t0

		CD3DX12_STATIC_SAMPLER_DESC samp{};
		samp.Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

		CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
		rsDesc.Init(_countof(params), params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> blob, err;
		if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
			if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
			return false;
		}
		if (FAILED(dev_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSigSkybox_))))
			return false;
	}

	// シェーダーコンパイル
	auto vs = CompileShaderFromFile(L"Resources/shaders/SkyboxVS.hlsl", "main", "vs_5_0");
	auto ps = CompileShaderFromFile(L"Resources/shaders/SkyboxPS.hlsl", "main", "ps_5_0");
	if (!vs || !ps) {
		OutputDebugStringA("[Renderer] Skybox shader compile failed\n");
		return false;
	}

	// PSO作成
	D3D12_INPUT_ELEMENT_DESC layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.InputLayout = { layout, _countof(layout) };
	pso.pRootSignature = rootSigSkybox_.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 内側を描画
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // z=1.0でも描画
	pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度書き込みOFF
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc.Count = 1;

	if (FAILED(dev_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoSkybox_)))) {
		OutputDebugStringA("[Renderer] Skybox PSO creation failed\n");
		return false;
	}

	OutputDebugStringA("[Renderer] Skybox pipeline initialized OK\n");
	return true;
}

void Renderer::DrawSkybox() {
	if (!psoSkybox_ || skyboxCubeMapHandle_ == 0 || skyboxCubeMapHandle_ >= textures_.size()) return;
	if (skyboxIndexCount_ == 0) return;

	list_->SetPipelineState(psoSkybox_.Get());
	list_->SetGraphicsRootSignature(rootSigSkybox_.Get());

	// CBFrame バインド
	if (cbFrameAddr_ != 0)
		list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);

	// キューブマップテクスチャ バインド
	list_->SetGraphicsRootDescriptorTable(1, textures_[skyboxCubeMapHandle_].srvGpu);

	list_->IASetVertexBuffers(0, 1, &skyboxVBV_);
	list_->IASetIndexBuffer(&skyboxIBV_);
	list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	list_->DrawIndexedInstanced(skyboxIndexCount_, 1, 0, 0, 0);
}

} // namespace Engine