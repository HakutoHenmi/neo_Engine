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
#include "Matrix4x4.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/PathUtils.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

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
enum class FluidResetShape {
    Blob,
    Puddle
};
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
    bool Initialize(ID3D12Device* device) {
        // デバッグ用のログに初期化を記録する
        FILE* fp = nullptr;
        fopen_s(&fp, "fluid_debug.txt", "a");
        if (fp) {
            fprintf(fp, "[FluidSystem] Initialize called\n");
            fclose(fp);
        }
        // デバイスが無ければ失敗
        if (!device) return false;
        if (isInitialized_) return true; // 二重に初期化してリソースが解放され、デバイスが失われる (TDR) のを防ぐ
        device_ = device;

        // SSFR用テクスチャとヒープの生成 (Phase 1)
        // 画面の大きさで SSFR のテクスチャを作る
        screenWidth_ = Engine::WindowDX::kW;
        screenHeight_ = Engine::WindowDX::kH;
        CreateSSFRResources();

        // 1. ルートシグネチャ (コンピュート)
        CD3DX12_ROOT_PARAMETER computeParams[2]{};
        // b0: 定数
        computeParams[0].InitAsConstantBufferView(0);
        // u0: 粒子の UAV
        computeParams[1].InitAsUnorderedAccessView(0);

        // ルートシグネチャを作る
        CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
        rsDesc.Init(_countof(computeParams), computeParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        Microsoft::WRL::ComPtr<ID3DBlob> signature, error;
        if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
            if (error) OutputDebugStringA((char*)error->GetBufferPointer());
            return false;
        }
        if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSig_)))) {
            return false;
        }

        // 1.5 ルートシグネチャ (描画)
        CD3DX12_ROOT_PARAMETER renderParams[2]{};
        // t0: 粒子の SRV
        renderParams[0].InitAsShaderResourceView(0);
        // b0: ビュー・プロジェクションの定数
        renderParams[1].InitAsConstantBufferView(0);
        
        // サンプラー (ポイント・クランプ)
        // ピクセルシェーダー用のポイントのサンプラー
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // 描画用のルートシグネチャを作る
        CD3DX12_ROOT_SIGNATURE_DESC rsRenderDesc;
        rsRenderDesc.Init(_countof(renderParams), renderParams, 1, &sampler, 
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        if (FAILED(D3D12SerializeRootSignature(&rsRenderDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) return false;
        if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSigRender_)))) return false;

        // 2. コンピュートシェーダーの読み込み
        // FluidSimCS.hlsl の Emit を cs_5_0 でコンパイルする
        std::wstring shaderPath = Engine::PathUtils::GetUnifiedPathW(L"Resources/Shaders/FluidSimCS.hlsl");
        Microsoft::WRL::ComPtr<ID3DBlob> csBlob, errBlob;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        if (FAILED(D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "Emit", "cs_5_0", flags, 0, &csBlob, &errBlob))) {
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
            return false;
        }

        // 3. パイプラインの作成 (コンピュート)
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSig_.Get();
        psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
        if (FAILED(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso_)))) {
            return false;
        }

        // 3.5 パイプラインの作成 (描画)
        // 球の頂点シェーダーとピクセルシェーダー
        std::wstring vsPath = Engine::PathUtils::GetUnifiedPathW(L"Resources/Shaders/FluidSphereVS.hlsl");
        std::wstring psPath = Engine::PathUtils::GetUnifiedPathW(L"Resources/Shaders/FluidSpherePS.hlsl");
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob;
        D3DCompileFromFile(vsPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, &vsBlob, &errBlob);
        D3DCompileFromFile(psPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, &psBlob, &errBlob);

        // 粒子の描画用パイプライン (アルファブレンド・深度テストあり)
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoRenderDesc{};
        psoRenderDesc.pRootSignature = rootSigRender_.Get();
        psoRenderDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoRenderDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        // カリングなし
        psoRenderDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoRenderDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // カリングなし
        
        // アルファブレンド
        psoRenderDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoRenderDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        psoRenderDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        psoRenderDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        psoRenderDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoRenderDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoRenderDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        psoRenderDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        psoRenderDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        
        // 深度のテストと書き込みを行う
        psoRenderDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoRenderDesc.DepthStencilState.DepthEnable = TRUE;
        psoRenderDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoRenderDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        // 三角形・RGBA8 の描画先・D32 の深度
        psoRenderDesc.SampleMask = UINT_MAX;
        psoRenderDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoRenderDesc.NumRenderTargets = 1;
        psoRenderDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoRenderDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoRenderDesc.SampleDesc.Count = 1;

        if (FAILED(device->CreateGraphicsPipelineState(&psoRenderDesc, IID_PPV_ARGS(&psoRender_)))) {
            return false;
        }

        // 4.5 合成用パイプラインの初期化
        {
            // 合成用のルートシグネチャ
            // SRV 2 つの表と CBV
            CD3DX12_DESCRIPTOR_RANGE compSrvRange;
            compSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
            
            CD3DX12_ROOT_PARAMETER compParams[2]{};
            compParams[0].InitAsDescriptorTable(1, &compSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);
            compParams[1].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
            
            // ポイントとリニアのサンプラー
            CD3DX12_STATIC_SAMPLER_DESC compSamplers[2]{};
            compSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
            compSamplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
            
            CD3DX12_ROOT_SIGNATURE_DESC compSigDesc(2, compParams, 2, compSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
            if (FAILED(D3D12SerializeRootSignature(&compSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
                if (error) OutputDebugStringA((char*)error->GetBufferPointer());
                return false;
            }
            if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSigComposite_)))) {
                return false;
            }
            
            // 合成用のシェーダーをコンパイルする
            Microsoft::WRL::ComPtr<ID3DBlob> compVSBlob, compPSBlob, errBlob2;
            std::wstring compVsPath = Engine::PathUtils::GetUnifiedPathW(L"Resources/Shaders/FluidCompositeVS.hlsl");
            if (FAILED(D3DCompileFromFile(compVsPath.c_str(), nullptr, nullptr, "main", "vs_5_0", flags, 0, &compVSBlob, &errBlob2))) {
                if (errBlob2) OutputDebugStringA((char*)errBlob2->GetBufferPointer());
                return false;
            }
            std::wstring compPsPath = Engine::PathUtils::GetUnifiedPathW(L"Resources/Shaders/FluidCompositePS.hlsl");
            if (FAILED(D3DCompileFromFile(compPsPath.c_str(), nullptr, nullptr, "main", "ps_5_0", flags, 0, &compPSBlob, &errBlob2))) {
                if (errBlob2) OutputDebugStringA((char*)errBlob2->GetBufferPointer());
                return false;
            }
            
            // 合成用のパイプライン (アルファブレンド・深度なし)
            D3D12_GRAPHICS_PIPELINE_STATE_DESC compDesc{};
            compDesc.pRootSignature = rootSigComposite_.Get();
            compDesc.VS = CD3DX12_SHADER_BYTECODE(compVSBlob.Get());
            compDesc.PS = CD3DX12_SHADER_BYTECODE(compPSBlob.Get());
            compDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            compDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            compDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            compDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            compDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            compDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            compDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            compDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            compDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            
            compDesc.SampleMask = UINT_MAX;
            compDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            compDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            compDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            compDesc.DepthStencilState.DepthEnable = FALSE; // フルスクリーン合成なので深度テストは不要
            compDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            compDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            compDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            compDesc.NumRenderTargets = 1;
            compDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // メイン画面のフォーマット
            compDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            compDesc.SampleDesc.Count = 1;
            
            if (FAILED(device->CreateGraphicsPipelineState(&compDesc, IID_PPV_ARGS(&psoComposite_)))) {
                return false;
            }
        }

        // 4. 粒子のバッファの作成
        // 粒子の数の分のバッファ (UAV)
        uint32_t bufferSize = sizeof(FluidParticle) * numParticles_;
        auto heapPropsDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto descUAV = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        if (FAILED(device->CreateCommittedResource(
            &heapPropsDefault, D3D12_HEAP_FLAG_NONE,
            &descUAV, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&particleBuffer_)))) return false;

        // 最初の粒子の配置を作る
        std::vector<FluidParticle> initData(numParticles_);
        FillLocalBlobParticles(initData, { 0.8f, 0.8f, 0.8f }); // 球状に密集配置

        // アップロード用のバッファを作って初期の配置を書く
        auto heapPropsUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto descUpload = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        device->CreateCommittedResource(
            &heapPropsUpload, D3D12_HEAP_FLAG_NONE,
            &descUpload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&particleUploadBuffer_));

        void* mapped = nullptr;
        particleUploadBuffer_->Map(0, nullptr, &mapped);
        memcpy(mapped, initData.data(), bufferSize);
        particleUploadBuffer_->Unmap(0, nullptr);

        // 初期化コピーはUpdateの初回フレームに行うためここでは行わない
        // 5. 定数バッファの作成 (パス0用とパス1用で2つ分の領域を確保)
        auto descCB = CD3DX12_RESOURCE_DESC::Buffer(sizeof(FluidConstants) * 2);
        device->CreateCommittedResource(
            &heapPropsUpload, D3D12_HEAP_FLAG_NONE,
            &descCB, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&cbUploadBuffer_));
        // 定数を書き込めるよう対応付けておく
        cbUploadBuffer_->Map(0, nullptr, (void**)&cbMapped_);

        // 6. 描画用の定数バッファ
        auto descRenderCB = CD3DX12_RESOURCE_DESC::Buffer(sizeof(FluidRenderConstants));
        device->CreateCommittedResource(
            &heapPropsUpload, D3D12_HEAP_FLAG_NONE,
            &descRenderCB, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&cbRenderUploadBuffer_));
        cbRenderUploadBuffer_->Map(0, nullptr, (void**)&cbRenderMapped_);

        isInitialized_ = true;
        return true;
    }

    // スライムの形 (広く平らな裾野と小さな頭) の中に粒子を無作為に置く
    void FillLocalBlobParticles(std::vector<FluidParticle>& out, DirectX::XMFLOAT3 radii) {
        // 決まった乱数の種で毎回同じ形にする
        std::mt19937 mt(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        float heightScale = 1.0f; // 高さの倍率 (見本の絵に合わせて調整する)
        // 形の中に入るまで点を引き直す
        for (uint32_t i = 0; i < numParticles_; i++) {
            DirectX::XMFLOAT3 p{};
            for (int attempt = 0; attempt < 1000; ++attempt) {
                // x, z を広げるため生成範囲を 2.5 に拡大
                float lx = dist(mt) * 2.5f;
                float ly = dist(mt) * 0.5f + 0.5f; // 0.0f 〜 1.0f
                float lz = dist(mt) * 2.5f;
                
                float y = ly;
                // 絵のような広く平らな裾野と小さな頭のスライム形状
                float R = std::exp(-y * 5.0f) * 0.75f + std::sqrt(std::max(0.0f, 1.0f - y * y)) * 0.25f;
                
                // x, z が広がる分、判定用の半径も 2.5 倍する
                float scaledR = R * 2.5f;
                
                if ((lx * lx + lz * lz) <= scaledR * scaledR) {
                    p = { lx * radii.x, ly * radii.y * heightScale, lz * radii.z };
                    break;
                }
            }
            // 粒子の初期値を設定する
            out[i].position = p;
            out[i].velocity = { 0, 0, 0 };
            out[i].density = 0.0f;
            out[i].pressure = 0.0f;
            out[i].force = { 0, 0, 0 };
            out[i].pad1 = 0.0f;
            out[i].restPosition = p; // これが形を覚える元の位置になる
            out[i].pad2 = 0.0f;
        }
    }

    // 水たまりの形 (平たい円) に粒子を置き、少し下向きの速度を付ける
    void FillLocalPuddleParticles(std::vector<FluidParticle>& out, DirectX::XMFLOAT3 radii) {
        std::mt19937 mt(1337);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        // 円盤の中に偏りなく置き、少し下向きに動かす
        for (uint32_t i = 0; i < numParticles_; i++) {
            float angle = dist(mt) * 3.14159f;
            float r = std::sqrt(std::abs(dist(mt))) * radii.x * 1.8f;
            out[i].position = { std::cos(angle) * r, dist(mt) * 0.08f, std::sin(angle) * r };
            out[i].velocity = { dist(mt) * 0.5f, -std::abs(dist(mt)) * 1.5f, dist(mt) * 0.5f };
            out[i].density = 0.0f;
            out[i].pressure = 0.0f;
            out[i].force = { 0, 0, 0 };
            out[i].pad1 = 0.0f;
            out[i].restPosition = out[i].position;
            out[i].pad2 = 0.0f;
        }
    }

    // 指定の形で粒子を作り直し、アップロード用バッファへ書く
    void RebuildParticles(FluidResetShape shape, DirectX::XMFLOAT3 radii) {
        // アップロード用のバッファが無ければ何もしない
        if (!particleUploadBuffer_) return;

        // デバッグ用のログ
        FILE* fp = nullptr;
        fopen_s(&fp, "fluid_debug.txt", "a");
        if (fp) {
            fprintf(fp, "[RebuildParticles] shape=%d, radii=(%.2f, %.2f, %.2f)\n", (int)shape, radii.x, radii.y, radii.z);
            fclose(fp);
        }

        // 形を選んで粒子を作り、アップロード用のバッファへ書く
        std::vector<FluidParticle> initData(numParticles_);
        if (shape == FluidResetShape::Puddle) {
            FillLocalPuddleParticles(initData, radii);
        } else {
            FillLocalBlobParticles(initData, radii);
        }
        void* mapped = nullptr;
        particleUploadBuffer_->Map(0, nullptr, &mapped);
        memcpy(mapped, initData.data(), sizeof(FluidParticle) * numParticles_);
        particleUploadBuffer_->Unmap(0, nullptr);
    }

    // 毎フレームのシミュレーション: 必要なら粒子を作り直し、密度と圧力 → 力と移動の2回に分けて計算する
    void Update(ID3D12GraphicsCommandList* list, float dt, const DirectX::XMFLOAT3& corePos,
                DirectX::XMFLOAT3 blobRadii, bool isLiquidated, DirectX::XMFLOAT3 inputForce = {0.0f, 0.0f, 0.0f}) {
        // 最初の更新をログに残す
        static bool firstUpdate = true;
        if (firstUpdate) {
            firstUpdate = false;
            FILE* fp = nullptr;
            fopen_s(&fp, "fluid_debug.txt", "a");
            if (fp) {
                fprintf(fp, "[FluidSystem] First Update: isLiquidated=%d\n", isLiquidated);
                fclose(fp);
            }
        }
        // 初期化前なら何もしない
        if (!isInitialized_ || !pso_ || !particleBuffer_) return;

        // 作り直しの依頼があれば、アップロード用のバッファから粒子のバッファへコピーする
        if (resetRequested_) {
            FILE* fp = nullptr;
            fopen_s(&fp, "fluid_debug.txt", "a");
            if (fp) {
                fprintf(fp, "[UpdateReset] executing: shape=%d, radii=(%.2f, %.2f, %.2f)\n", (int)resetShape_, resetRadii_.x, resetRadii_.y, resetRadii_.z);
                fclose(fp);
            }

            RebuildParticles(resetShape_, resetRadii_);
            // 2 回目以降は UAV からコピー先へ切り替える
            if (!firstFrame_) {
                auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
                    particleBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
                list->ResourceBarrier(1, &toCopyDest);
            }
            list->CopyResource(particleBuffer_.Get(), particleUploadBuffer_.Get());
            auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
                particleBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->ResourceBarrier(1, &toUav);
            resetRequested_ = false;
            firstFrame_ = false;
        // 最初のフレームは初期の配置をコピーする
        } else if (firstFrame_) {
            list->CopyResource(particleBuffer_.Get(), particleUploadBuffer_.Get());
            auto transition = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffer_.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->ResourceBarrier(1, &transition);
            firstFrame_ = false;
        }

        // シミュレーションの定数 (液状化中は柔らかく、スライムの形のときは硬く粘る)
        FluidConstants constants{};
        // 核の位置・粒子数・時間 (上限 0.05 秒)・各定数
        constants.corePosition = corePos;
        constants.numParticles = numParticles_;
        constants.deltaTime = dt > 0.05f ? 0.05f : dt;
        constants.smoothingLength = 0.30f;
        constants.particleMass = 0.1f;
        constants.restDensity = 150.0f;
        // スライム時: 重力ほぼゼロ。地面衝突で底が平らになり自然なドーム型になる
        constants.gravity = isLiquidated ? -2.0f : -0.5f;
        constants.floorWorldY = 0.0f;
        constants.blobRadii = blobRadii;
        constants.simMode = isLiquidated ? 1u : 0u;
        constants.playerInputForce = inputForce;

        if (isLiquidated) {
            constants.gasStiffness = 60.0f;
            constants.viscosity = 15.0f;
            constants.damping = 0.990f;
        } else {
            // ドーム型スライム: 強い圧力 + 高粘度 + 表面張力
            constants.gasStiffness = 600.0f;
            constants.viscosity = 25.0f; // 粘度を上げて重みを出す
            constants.damping = 0.985f;
        }

        // 計算パイプラインと粒子のバッファを設定する
        list->SetPipelineState(pso_.Get());
        list->SetComputeRootSignature(rootSig_.Get());
        list->SetComputeRootUnorderedAccessView(1, particleBuffer_->GetGPUVirtualAddress());

        // 256 個ずつのグループ
        uint32_t numGroups = (numParticles_ + 255) / 256;

        // パス 0 (密度と圧力)
        constants.passType = 0;
        memcpy(cbMapped_, &constants, sizeof(FluidConstants));
        list->SetComputeRootConstantBufferView(0, cbUploadBuffer_->GetGPUVirtualAddress());
        list->Dispatch(numGroups, 1, 1);

        // パス 0 の結果を書き終えるまで待つ
        auto barrier1 = CD3DX12_RESOURCE_BARRIER::UAV(particleBuffer_.Get());
        list->ResourceBarrier(1, &barrier1);

        // パス 1 (力と移動)
        constants.passType = 1;
        memcpy((uint8_t*)cbMapped_ + sizeof(FluidConstants), &constants, sizeof(FluidConstants));
        list->SetComputeRootConstantBufferView(0, cbUploadBuffer_->GetGPUVirtualAddress() + sizeof(FluidConstants));
        list->Dispatch(numGroups, 1, 1);

        // パス 1 の結果を書き終えるまで待つ
        auto barrier2 = CD3DX12_RESOURCE_BARRIER::UAV(particleBuffer_.Get());
        list->ResourceBarrier(1, &barrier2);
    }

    // 粒子を板ポリゴンとしてメイン画面に描く
    void Draw(ID3D12GraphicsCommandList* list, Engine::Camera* camera, float time,
              DirectX::XMFLOAT3 corePos, DirectX::XMFLOAT3 blobRadii, bool isLiquidated,
              D3D12_CPU_DESCRIPTOR_HANDLE mainRtv, D3D12_CPU_DESCRIPTOR_HANDLE mainDsv) {
        (void)blobRadii; // 未使用変数の警告(C4100)を回避
        // 初期化前なら描かない
        if (!isInitialized_ || !psoRender_) return;

        // カメラの行列と逆行列を描画の定数に入れる
        FluidRenderConstants rcb{};
        DirectX::XMMATRIX viewMat = camera->View();
        DirectX::XMMATRIX projMat = camera->Proj();
        DirectX::XMMATRIX viewProjMat = DirectX::XMMatrixMultiply(viewMat, projMat);
        DirectX::XMMATRIX invProjMat = DirectX::XMMatrixInverse(nullptr, projMat);
        DirectX::XMMATRIX invViewMat = DirectX::XMMatrixInverse(nullptr, viewMat);
        
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.view), viewMat);
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.projection), projMat);
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.viewProj), viewProjMat);
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.invProjection), invProjMat);
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.invView), invViewMat);
        
        // カメラの位置・時刻・核・液状化・色
        Engine::Vector3 camPos = camera->GetPosition();
        rcb.cameraPos = { camPos.x, camPos.y, camPos.z };
        rcb.time = time;
        rcb.corePosition = corePos;
        rcb.isLiquidated = isLiquidated ? 1.0f : 0.0f;
        rcb.blobColor = { 0.15f, 0.85f, 0.4f };
        // 定数をアップロードする
        memcpy(cbRenderMapped_, &rcb, sizeof(FluidRenderConstants));

        // 描画先をメイン画面にする
        list->OMSetRenderTargets(1, &mainRtv, FALSE, &mainDsv);

        // ビューポートとシザーの設定
        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)screenWidth_, (float)screenHeight_, 0.0f, 1.0f };
        D3D12_RECT scissorRect = { 0, 0, (LONG)screenWidth_, (LONG)screenHeight_ };
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissorRect);

        // 描画のパイプラインを設定する
        list->SetPipelineState(psoRender_.Get());
        list->SetGraphicsRootSignature(rootSigRender_.Get());
        
        // 粒子のバッファを UAV から SRV へ切り替える
        auto transitionToSrv = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffer_.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &transitionToSrv);
        
        // 粒子のバッファと定数を渡す
        list->SetGraphicsRootShaderResourceView(0, particleBuffer_->GetGPUVirtualAddress());
        list->SetGraphicsRootConstantBufferView(1, cbRenderUploadBuffer_->GetGPUVirtualAddress());

        // インスタンス描画 (粒子の数だけ、1 個につき 4 頂点の四角形)
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        list->DrawInstanced(4, numParticles_, 0, 0);

        // 粒子のバッファを SRV から UAV へ戻す (次フレームのコンピュート用)
        auto transitionToUav = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffer_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        
        list->ResourceBarrier(1, &transitionToUav);
    }

    // 次の更新で粒子を指定の形に作り直す
    void RequestParticleReset(FluidResetShape shape, DirectX::XMFLOAT3 radii) {
        // 形と半径を記録し、作り直しの依頼を出す
        resetShape_ = shape;
        resetRadii_ = radii;
        resetRequested_ = true;

        // デバッグ用のログ
        FILE* fp = nullptr;
        fopen_s(&fp, "fluid_debug.txt", "a");
        if (fp) {
            fprintf(fp, "[RequestReset] shape=%d, radii=(%.2f, %.2f, %.2f)\n", (int)shape, radii.x, radii.y, radii.z);
            fclose(fp);
        }
    }

    // 粒子のバッファ・粒子の数・初期化済みか
    ID3D12Resource* GetParticleBuffer() const { return particleBuffer_.Get(); }
    uint32_t GetNumParticles() const { return numParticles_; }
    bool IsInitialized() const { return isInitialized_; }

private:
    FluidSystem() = default;

    // デバイス・シミュレーションと描画のパイプライン
    ID3D12Device* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
    
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigRender_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoRender_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigComposite_; // 合成用のルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoComposite_; // 合成用のパイプライン

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
    DirectX::XMFLOAT3 resetRadii_ = { 1.2f, 0.85f, 1.2f };

    // SSFR (画面空間での流体描画) 用のテクスチャとビューを作る
    void CreateSSFRResources() {
        // 1. ヒープの作成 (RTV x3: Depth, Thickness, SmoothedDepth / SRV x3)
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        // RTV と SRV のヒープ (3 つずつ)
        rtvHeapDesc.NumDescriptors = 3;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&ssfrRtvHeap_));
        rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
        srvHeapDesc.NumDescriptors = 3;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&ssfrSrvHeap_));
        srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // DSV のヒープ
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&ssfrDsvHeap_));

        // 2. テクスチャリソースの作成 (R32_FLOAT)
        // R32_FLOAT のテクスチャ
        D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_FLOAT, screenWidth_, screenHeight_, 1, 1, 1, 0, 
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        // 深度の初期値は遠く
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_R32_FLOAT;
        clearValue.Color[0] = 10000.0f; // 深度の初期値（遠く）
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        // 深度 (パス1用)
        device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, 
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&ssfrDepthTex_));

        // 厚み (パス1用)
        clearValue.Color[0] = 0.0f; // 厚みの初期値
        device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, 
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&ssfrThicknessTex_));

        // なめらかにした深度 (パス2用)
        clearValue.Color[0] = 10000.0f;
        device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, 
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&ssfrSmoothedDepthTex_));

        // 深度テスト用のテクスチャを作る
        D3D12_RESOURCE_DESC dsvDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT, screenWidth_, screenHeight_, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE dsvClear{};
        dsvClear.Format = DXGI_FORMAT_D32_FLOAT;
        dsvClear.DepthStencil.Depth = 1.0f;
        dsvClear.DepthStencil.Stencil = 0;
        device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &dsvDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dsvClear, IID_PPV_ARGS(&ssfrDsvTex_));

        // 3. View (RTV/SRV/DSV) の作成
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(ssfrRtvHeap_->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(ssfrSrvHeap_->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(ssfrDsvHeap_->GetCPUDescriptorHandleForHeapStart());

        // 深度テスト用のビュー
        device_->CreateDepthStencilView(ssfrDsvTex_.Get(), nullptr, dsvHandle);

        // 深度のビュー
        device_->CreateRenderTargetView(ssfrDepthTex_.Get(), nullptr, rtvHandle);
        device_->CreateShaderResourceView(ssfrDepthTex_.Get(), nullptr, srvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize_);
        srvHandle.Offset(1, srvDescriptorSize_);

        // 厚みのビュー
        device_->CreateRenderTargetView(ssfrThicknessTex_.Get(), nullptr, rtvHandle);
        device_->CreateShaderResourceView(ssfrThicknessTex_.Get(), nullptr, srvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize_);
        srvHandle.Offset(1, srvDescriptorSize_);

        // なめらかにした深度のビュー
        device_->CreateRenderTargetView(ssfrSmoothedDepthTex_.Get(), nullptr, rtvHandle);
        device_->CreateShaderResourceView(ssfrSmoothedDepthTex_.Get(), nullptr, srvHandle);
    }
};

} // namespace Game
