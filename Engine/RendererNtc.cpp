// NTC (Neural Texture Compression) のテクスチャセットを、ロード時に GPU で BCn へ復元する。
// 流れ: .ntc 読込 → 潜在テクスチャ・INT8 重み・BC7 モードの転送 → 推論 (全ミップ) → BCn 化 → 最終テクスチャへコピー。
// 復元した画素を CPU に戻したり、CLI を起動したりはしない。libntc.dll (NVIDIA RTX SDK) は実行時に読み込む。
// This software contains source code provided by NVIDIA Corporation.
#define NTC_STATIC 1
#include "Renderer.h"
#include "PathUtils.h"
#include "LoadProfile.h"
#include <libntc/ntc.h>
#include <d3dx12.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace Engine {
namespace {

void Check(HRESULT hr, const char* what) { if (FAILED(hr)) throw std::runtime_error(what); }
void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

// libntc.dll は実行ファイルの隣の NTC フォルダ (配布版) か、externals/RTXNTC/bin (開発時) から読む
std::filesystem::path FindNtcLibrary() {
	wchar_t exe[MAX_PATH]{};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	for (auto path : {std::filesystem::path(exe).parent_path() / L"NTC/libntc.dll", std::filesystem::absolute(L"externals/RTXNTC/bin/libntc.dll")})
		if (std::filesystem::exists(path)) return path;
	return {};
}

ComPtr<ID3D12Resource> MakeResource(ID3D12Device* dev, const D3D12_RESOURCE_DESC& desc, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
	ComPtr<ID3D12Resource> r; CD3DX12_HEAP_PROPERTIES hp(heap);
	Check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&r)), "NTC: resource creation failed");
	return r;
}
ComPtr<ID3D12Resource> MakeBuffer(ID3D12Device* dev, UINT64 size, D3D12_HEAP_TYPE heap) {
	// DEFAULT のバッファは COMMON で作り、コピー先として暗黙に昇格させる
	return MakeResource(dev, CD3DX12_RESOURCE_DESC::Buffer(((std::max<UINT64>)(size, 4) + 3) & ~3ull), heap,
		heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON);
}
void WriteBuffer(ID3D12Resource* r, const void* data, size_t size, UINT64 offset = 0) {
	void* p = nullptr; D3D12_RANGE empty{0, 0};
	Check(r->Map(0, &empty, &p), "NTC: map failed"); memcpy(static_cast<char*>(p) + offset, data, size); r->Unmap(0, nullptr);
}
DXGI_FORMAT FinalFormat(ntc::BlockCompressedFormat bc, bool srgb) {
	switch (bc) {
	case ntc::BlockCompressedFormat::BC7: return srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
	case ntc::BlockCompressedFormat::BC5: return DXGI_FORMAT_BC5_UNORM;
	case ntc::BlockCompressedFormat::BC4: return DXGI_FORMAT_BC4_UNORM;
	default: throw std::runtime_error("NTC: unsupported BCn format (BC7/BC5/BC4 only)");
	}
}

} // namespace

std::vector<Renderer::TextureHandle> Renderer::LoadNtcTextureSet(const std::string& filePath, const std::vector<std::string>& names) {
	const std::string unifiedPath = PathUtils::GetUnifiedPath(filePath);
	auto key = [&](const std::string& name) { return unifiedPath + "#ntc:" + name; };
	// 一度復元したセットはキャッシュから返す (推論はプロセス内で 1 回だけ)
	std::vector<TextureHandle> result;
	for (const auto& name : names) { auto it = textureCache_.find(key(name)); if (it == textureCache_.end()) { result.clear(); break; } result.push_back(it->second); }
	if (!result.empty()) return result;

	LoadProfile::Scope profile("ntc", unifiedPath);
	HMODULE dll = nullptr;
	ntc::IContext* context = nullptr;
	decltype(&ntc::DestroyContext) destroy = nullptr;
	try {
		const auto library = FindNtcLibrary();
		Require(!library.empty(), "NTC: libntc.dll not found");
		dll = LoadLibraryExW(library.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		Require(dll != nullptr, "NTC: cannot load libntc.dll");
		auto create = reinterpret_cast<decltype(&ntc::CreateContext)>(GetProcAddress(dll, "CreateContext"));
		destroy = reinterpret_cast<decltype(destroy)>(GetProcAddress(dll, "DestroyContext"));
		auto error = reinterpret_cast<decltype(&ntc::GetLastErrorMessage)>(GetProcAddress(dll, "GetLastErrorMessage"));
		Require(create && destroy && error, "NTC: missing libntc entry points");
		auto ok = [&](ntc::Status s) { if (s != ntc::Status::Ok) throw std::runtime_error(std::string("NTC: ") + error()); };
		// CUDA は使わず、D3D12 の非 CoopVec (汎用 INT8) 推論だけを使う
		ntc::ContextParameters cp; cp.cudaDevice = ntc::DisableCudaDevice; cp.graphicsApi = ntc::GraphicsAPI::D3D12; cp.d3d12Device = dev_; cp.enableCooperativeVector = false;
		const auto created = create(&context, cp);
		if (created != ntc::Status::CudaUnavailable) ok(created);
		Require(context != nullptr, "NTC: context creation failed");

		// 推論 (space1 の UAV 配列) と BCn 化 (space0 の u3) が共有するルートシグネチャ
		ComPtr<ID3D12RootSignature> root;
		{
			CD3DX12_DESCRIPTOR_RANGE ranges[3]; ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1); ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 48, 0, 1); ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
			CD3DX12_ROOT_PARAMETER params[4]; params[0].InitAsConstantBufferView(0); for (int i = 0; i < 3; ++i) params[i + 1].InitAsDescriptorTable(1, &ranges[i]);
			CD3DX12_STATIC_SAMPLER_DESC sampler(3, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
			CD3DX12_ROOT_SIGNATURE_DESC desc(4, params, 1, &sampler); ComPtr<ID3DBlob> blob, errors;
			Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "NTC: root signature serialization failed");
			Check(dev_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "NTC: root signature creation failed");
		}
		std::map<const void*, ComPtr<ID3D12PipelineState>> psos;
		auto pipeline = [&](const ntc::ComputePassDesc& pass) {
			auto& p = psos[pass.computeShader];
			if (!p) { D3D12_COMPUTE_PIPELINE_STATE_DESC d{}; d.pRootSignature = root.Get(); d.CS = {pass.computeShader, pass.computeShaderSize}; Check(dev_->CreateComputePipelineState(&d, IID_PPV_ARGS(&p)), "NTC: PSO creation failed"); }
			return p.Get();
		};

		// ファイルを読み、テクスチャセットの情報を得る
		std::ifstream file(std::filesystem::path(PathUtils::FromUTF8(unifiedPath)), std::ios::binary | std::ios::ate);
		Require(bool(file), "NTC: cannot open file");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))), "NTC: read failed");
		ntc::IStream* stream = nullptr; ok(context->OpenReadOnlyMemory(bytes.data(), bytes.size(), &stream));
		struct StreamGuard { ntc::IContext* c; ntc::IStream* p; ~StreamGuard() { c->CloseMemory(p); } } streamGuard{context, stream};
		ntc::ITextureSetMetadata* meta = nullptr; ok(context->CreateTextureSetMetadataFromStream(stream, &meta));
		struct MetaGuard { ntc::IContext* c; ntc::ITextureSetMetadata* p; ~MetaGuard() { c->DestroyTextureSetMetadata(p); } } metaGuard{context, meta};
		const auto md = meta->GetDesc();
		const int textureCount = meta->GetTextureCount();
		Require(textureCount >= 1 && textureCount <= 4 && md.mips >= 1 && md.mips <= 12, "NTC: unexpected texture set layout");
		Require(meta->GetBestSupportedWeightType() == ntc::InferenceWeightType::GenericInt8, "NTC: GPU does not support INT8 inference");
		const int mips = md.mips;
		auto textureName = [&](int i) { std::string n = meta->GetTexture(i)->GetName(); return n.substr(0, n.find('|')); };

		ComPtr<ID3D12CommandAllocator> alloc; ComPtr<ID3D12GraphicsCommandList> list;
		Check(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "NTC: allocator creation failed");
		Check(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)), "NTC: command list creation failed");
		auto barrier = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) { auto b = CD3DX12_RESOURCE_BARRIER::Transition(r, before, after); list->ResourceBarrier(1, &b); };
		// GPU の完了まで保持する一時リソース
		std::vector<ComPtr<ID3D12Resource>> keep;
		auto hold = [&](ComPtr<ID3D12Resource> r) { keep.push_back(r); return r; };

		// 潜在テクスチャ (全ミップ・全レイヤー)
		const auto ld = meta->GetLatentTextureDesc();
		auto latent = hold(MakeResource(dev_, CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_B4G4R4A4_UNORM, ld.width, ld.height, UINT16(ld.arraySize), UINT16(ld.mipLevels)), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST));
		{
			std::vector<D3D12_SUBRESOURCE_DATA> subs;
			for (int layer = 0; layer < ld.arraySize; ++layer) for (int mip = 0; mip < ld.mipLevels; ++mip) {
				ntc::LatentTextureFootprint fp; ok(meta->GetLatentTextureFootprint(mip, layer, fp));
				Require(fp.buffer.compressionType == ntc::CompressionType::None, "NTC: GDeflate-compressed latents are not supported");
				Require(fp.buffer.rangeInStream.offset + fp.buffer.rangeInStream.size <= bytes.size(), "NTC: invalid latent footprint");
				subs.push_back({bytes.data() + fp.buffer.rangeInStream.offset, LONG_PTR(fp.rowPitch), LONG_PTR(fp.rowPitch * fp.height)});
			}
			auto up = hold(MakeBuffer(dev_, GetRequiredIntermediateSize(latent.Get(), 0, UINT(subs.size())), D3D12_HEAP_TYPE_UPLOAD));
			Require(UpdateSubresources(list.Get(), latent.Get(), up.Get(), 0, 0, UINT(subs.size()), subs.data()) != 0, "NTC: latent upload failed");
			barrier(latent.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
		// INT8 の重み
		const void* weightData = nullptr; size_t weightSize = 0, converted = 0;
		ok(meta->GetInferenceWeights(ntc::InferenceWeightType::GenericInt8, &weightData, &weightSize, &converted));
		auto weights = hold(MakeBuffer(dev_, weightSize, D3D12_HEAP_TYPE_DEFAULT));
		{
			auto up = hold(MakeBuffer(dev_, weightSize, D3D12_HEAP_TYPE_UPLOAD)); WriteBuffer(up.Get(), weightData, weightSize);
			list->CopyBufferRegion(weights.Get(), 0, up.Get(), 0, weightSize);
			barrier(weights.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
		// BC7 のモードバッファ (ファイルに入っている。ロード時に作り直さない)
		std::vector<std::vector<ComPtr<ID3D12Resource>>> modes(textureCount, std::vector<ComPtr<ID3D12Resource>>(mips));
		std::vector<std::vector<size_t>> modeSizes(textureCount, std::vector<size_t>(mips));
		for (int i = 0; i < textureCount; ++i) {
			if (meta->GetTexture(i)->GetBlockCompressedFormat() != ntc::BlockCompressedFormat::BC7) continue;
			for (int mip = 0; mip < mips; ++mip) {
				auto fp = meta->GetTexture(i)->GetBC7ModeBufferFootprint(mip);
				if (fp.rangeInStream.size == 0) continue;
				Require(fp.compressionType == ntc::CompressionType::None && fp.rangeInStream.offset + fp.rangeInStream.size <= bytes.size(), "NTC: invalid BC7 mode footprint");
				modeSizes[i][mip] = size_t(fp.rangeInStream.size);
				modes[i][mip] = hold(MakeBuffer(dev_, fp.rangeInStream.size, D3D12_HEAP_TYPE_DEFAULT));
				auto up = hold(MakeBuffer(dev_, fp.rangeInStream.size, D3D12_HEAP_TYPE_UPLOAD)); WriteBuffer(up.Get(), bytes.data() + fp.rangeInStream.offset, modeSizes[i][mip]);
				list->CopyBufferRegion(modes[i][mip].Get(), 0, up.Get(), 0, fp.rangeInStream.size);
				barrier(modes[i][mip].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			}
		}

		// この復元専用のディスクリプタヒープ: 0=潜在, 1=重み, 2..49=推論出力 (mip*4+テクスチャ, 最大 12 ミップ), 50..=BCn 化
		const UINT bcDescriptorBase = 50;
		D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, bcDescriptorBase + UINT(textureCount * mips) * 3 + 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
		ComPtr<ID3D12DescriptorHeap> heap; Check(dev_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "NTC: descriptor heap creation failed");
		const UINT inc = dev_->GetDescriptorHandleIncrementSize(hd.Type);
		auto cpu = [&](UINT i) { return CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(), int(i), inc); };
		auto gpu = [&](UINT i) { return CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(), int(i), inc); };
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Format = DXGI_FORMAT_B4G4R4A4_UNORM; d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			d.Texture2DArray.MipLevels = ld.mipLevels; d.Texture2DArray.ArraySize = ld.arraySize; dev_->CreateShaderResourceView(latent.Get(), &d, cpu(0));
		}
		auto rawSrv = [&](ID3D12Resource* r, size_t size, UINT index) {
			D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Format = DXGI_FORMAT_R32_TYPELESS; d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			d.Buffer.NumElements = UINT((size + 3) / 4); d.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW; dev_->CreateShaderResourceView(r, &d, cpu(index));
		};
		rawSrv(weights.Get(), weightSize, 1);
		// 推論の出力先 (RGBA8 の中間画像。全ミップ)
		std::vector<ComPtr<ID3D12Resource>> decoded(textureCount);
		for (int i = 0; i < textureCount; ++i) {
			decoded[i] = hold(MakeResource(dev_, CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, md.width, md.height, 1, UINT16(mips), 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
			for (int mip = 0; mip < mips; ++mip) {
				D3D12_UNORDERED_ACCESS_VIEW_DESC d{}; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; d.Texture2D.MipSlice = UINT(mip);
				dev_->CreateUnorderedAccessView(decoded[i].Get(), nullptr, &d, cpu(2 + UINT(mip) * 4 + UINT(i)));
			}
		}
		// 定数はディスパッチごとに別の領域へ書く (GPU が実行し終えるまで上書きしない)
		const UINT dispatchCount = UINT(mips + textureCount * mips);
		auto cb = hold(MakeBuffer(dev_, UINT64(dispatchCount) * 4096, D3D12_HEAP_TYPE_UPLOAD)); UINT cbSlot = 0;
		auto dispatch = [&](const ntc::ComputePassDesc& pass, UINT srvIndex, UINT bcUav) {
			Require(pass.constantBufferSize <= 4096 && cbSlot < dispatchCount, "NTC: constant data exceeds slot");
			WriteBuffer(cb.Get(), pass.constantBufferData, pass.constantBufferSize, UINT64(cbSlot) * 4096);
			list->SetComputeRootSignature(root.Get()); list->SetPipelineState(pipeline(pass));
			list->SetComputeRootConstantBufferView(0, cb->GetGPUVirtualAddress() + UINT64(cbSlot++) * 4096);
			ID3D12DescriptorHeap* h = heap.Get(); list->SetDescriptorHeaps(1, &h);
			list->SetComputeRootDescriptorTable(1, gpu(srvIndex)); list->SetComputeRootDescriptorTable(2, gpu(2)); list->SetComputeRootDescriptorTable(3, gpu(bcUav));
			list->Dispatch(pass.dispatchWidth, pass.dispatchHeight, 1);
		};
		// 推論: ミップごとに全テクスチャを出力する
		for (int mip = 0; mip < mips; ++mip) {
			std::array<ntc::OutputTextureDesc, 4> output;
			for (int i = 0; i < textureCount; ++i) {
				auto* t = meta->GetTexture(i);
				output[i].descriptorIndex = i; t->GetChannels(output[i].firstChannel, output[i].numChannels);
				output[i].rgbColorSpace = t->GetRgbColorSpace(); output[i].alphaColorSpace = t->GetAlphaColorSpace(); output[i].ditherScale = 1.f / 255.f;
			}
			ntc::MakeDecompressionComputePassParameters p; p.textureSetMetadata = meta; p.weightType = ntc::InferenceWeightType::GenericInt8; p.mipLevel = mip;
			p.firstOutputDescriptorIndex = mip * 4; p.pOutputTextures = output.data(); p.numOutputTextures = textureCount;
			ntc::ComputePassDesc pass; ok(context->MakeDecompressionComputePass(p, &pass)); dispatch(pass, 0, hd.NumDescriptors - 1);
		}
		for (auto& t : decoded) barrier(t.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		// BCn 化: ミップごとにブロックを作り、まとめたバッファから最終テクスチャへコピーする
		std::vector<ComPtr<ID3D12Resource>> finals(textureCount);
		std::vector<DXGI_FORMAT> formats(textureCount);
		UINT descriptor = bcDescriptorBase;
		for (int i = 0; i < textureCount; ++i) {
			auto* t = meta->GetTexture(i);
			const auto bc = t->GetBlockCompressedFormat();
			formats[i] = FinalFormat(bc, t->GetRgbColorSpace() == ntc::ColorSpace::sRGB);
			auto desc = CD3DX12_RESOURCE_DESC::Tex2D(formats[i], md.width, md.height, 1, UINT16(mips));
			finals[i] = MakeResource(dev_, desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
			std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fps(mips); UINT64 packedSize = 0;
			dev_->GetCopyableFootprints(&desc, 0, UINT(mips), 0, fps.data(), nullptr, nullptr, &packedSize);
			auto packed = hold(MakeBuffer(dev_, packedSize, D3D12_HEAP_TYPE_DEFAULT));
			const auto blockFormat = bc == ntc::BlockCompressedFormat::BC4 ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_UINT;
			for (int mip = 0; mip < mips; ++mip) {
				const UINT w = (std::max)(1u, UINT(md.width) >> mip), h = (std::max)(1u, UINT(md.height) >> mip);
				auto blocks = hold(MakeResource(dev_, CD3DX12_RESOURCE_DESC::Tex2D(blockFormat, (w + 3) / 4, (h + 3) / 4, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
				D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				sd.Texture2D.MostDetailedMip = UINT(mip); sd.Texture2D.MipLevels = 1; dev_->CreateShaderResourceView(decoded[i].Get(), &sd, cpu(descriptor));
				if (modes[i][mip]) rawSrv(modes[i][mip].Get(), modeSizes[i][mip], descriptor + 1);
				D3D12_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = blockFormat; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; dev_->CreateUnorderedAccessView(blocks.Get(), nullptr, &ud, cpu(descriptor + 2));
				ntc::MakeBlockCompressionComputePassParameters p; p.srcRect.width = int(w); p.srcRect.height = int(h); p.dstFormat = bc;
				if (modes[i][mip]) { p.modeBufferSource = ntc::BlockCompressionModeBufferSource::TextureSet; p.modeBufferInfo.textureSet.texture = t; p.modeBufferInfo.textureSet.mipLevel = mip; }
				ntc::ComputePassDesc pass; ok(context->MakeBlockCompressionComputePass(p, &pass)); dispatch(pass, descriptor, descriptor + 2); descriptor += 3;
				barrier(blocks.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
				auto rawFp = fps[mip]; rawFp.Footprint.Format = blockFormat; rawFp.Footprint.Width = (w + 3) / 4; rawFp.Footprint.Height = (h + 3) / 4;
				auto dst = CD3DX12_TEXTURE_COPY_LOCATION(packed.Get(), rawFp), src = CD3DX12_TEXTURE_COPY_LOCATION(blocks.Get(), 0);
				list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}
			barrier(packed.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
			for (int mip = 0; mip < mips; ++mip) { auto dst = CD3DX12_TEXTURE_COPY_LOCATION(finals[i].Get(), UINT(mip)), src = CD3DX12_TEXTURE_COPY_LOCATION(packed.Get(), fps[mip]); list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); }
			barrier(finals[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		Check(list->Close(), "NTC: command list close failed");
		ID3D12CommandList* lists[] = {list.Get()};
		queue_->ExecuteCommandLists(1, lists);
		WaitGPU();
		Check(dev_->GetDeviceRemovedReason(), "NTC: device removed during decode");

		// 復元したテクスチャを登録し、要求された名前の順に返す
		std::map<std::string, TextureHandle> byName;
		for (int i = 0; i < textureCount; ++i) byName[textureName(i)] = RegisterTexture_(finals[i], formats[i], UINT(mips), key(textureName(i)));
		for (const auto& name : names) { auto it = byName.find(name); Require(it != byName.end(), "NTC: requested texture missing from set"); result.push_back(it->second); }
	} catch (const std::exception& e) {
		OutputDebugStringA(("[Renderer] " + std::string(e.what()) + " (" + unifiedPath + ")\n").c_str());
		result.clear();
	}
	if (context && destroy) destroy(context);
	if (dll) FreeLibrary(dll);
	return result;
}

} // namespace Engine
