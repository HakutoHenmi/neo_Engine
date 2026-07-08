import sys
import os

renderer_cpp = r'c:\Users\k024g\source\repos\neo_Engine\Engine\Renderer.cpp'
with open(renderer_cpp, 'r', encoding='utf-8') as f:
    text = f.read()

impl = '''
// ★追加: GPU流体パーティクルシステム
void Renderer::InitGPUFluid() {
	// 1. Root Signature (Compute)
	CD3DX12_ROOT_PARAMETER computeParams[2]{};
	computeParams[0].InitAsConstants(16, 0); // b0: CBCompute 
	computeParams[1].InitAsUnorderedAccessView(0); // u0: Particles

	CD3DX12_ROOT_SIGNATURE_DESC rsDescCompute;
	rsDescCompute.Init(_countof(computeParams), computeParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> sigCompute, errCompute;
	D3D12SerializeRootSignature(&rsDescCompute, D3D_ROOT_SIGNATURE_VERSION_1, &sigCompute, &errCompute);
	dev_->CreateRootSignature(0, sigCompute->GetBufferPointer(), sigCompute->GetBufferSize(), IID_PPV_ARGS(&rootSigFluid_));

	// 2. Compute PSO
	auto csBlob = CompileShaderFromFile(L"Resources/shaders/FluidSimCS.hlsl", "main", "cs_5_0");
	if (csBlob) {
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = rootSigFluid_.Get();
		psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
		dev_->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psoFluidSim_));
	}

	// 3. Render PSO (uses rootSig3D_)
	auto vsBlob = CompileShaderFromFile(L"Resources/shaders/GPUFluidVS.hlsl", "main", "vs_5_0");
	auto psBlob = CompileShaderFromFile(L"Resources/shaders/ParticlePS.hlsl", "main", "ps_5_0");
	if (vsBlob && psBlob) {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = rootSig3D_.Get();
		psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
		psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
		D3D12_INPUT_ELEMENT_DESC layout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		psoDesc.InputLayout = {layout, _countof(layout)};
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		auto& rt = psoDesc.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; 
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

		dev_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&psoFluidRender_));
	}

	// 4. GPU Buffer (UAV)
	uint32_t bufferSize = gpuFluidMaxParticles_ * sizeof(GPUFluidParticle);
	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto resDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	dev_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&gpuFluidBuffer_));

	isGPUFluidReady_ = true;
}

void Renderer::UpdateGPUFluid(float dt) {
	if (!isGPUFluidReady_ || !psoFluidSim_ || !gpuFluidBuffer_) return;

	list_->SetPipelineState(psoFluidSim_.Get());
	list_->SetComputeRootSignature(rootSigFluid_.Get());

	struct CB {
		float dt;
		uint32_t emitCursor;
		uint32_t emitCount;
		float pad;
		Vector3 emitPos; float pad1;
		Vector3 emitDir; float pad2;
		Vector4 emitColor;
	} cb;
	cb.dt = dt;
	cb.emitCursor = 0;
	cb.emitCount = 0;

	list_->SetComputeRoot32BitConstants(0, 16, &cb, 0);
	list_->SetComputeRootUnorderedAccessView(1, gpuFluidBuffer_->GetGPUVirtualAddress());

	uint32_t threadGroups = (gpuFluidMaxParticles_ + 255) / 256;
	list_->Dispatch(threadGroups, 1, 1);
}

void Renderer::EmitGPUFluid(const Vector3& pos, const Vector3& velocityDir, const Vector4& color, int count) {
	if (!isGPUFluidReady_ || !psoFluidSim_ || !gpuFluidBuffer_) return;

	list_->SetPipelineState(psoFluidSim_.Get());
	list_->SetComputeRootSignature(rootSigFluid_.Get());

	struct CB {
		float dt;
		uint32_t emitCursor;
		uint32_t emitCount;
		float pad;
		Vector3 emitPos; float pad1;
		Vector3 emitDir; float pad2;
		Vector4 emitColor;
	} cb;
	cb.dt = 0.0f;
	cb.emitCursor = gpuFluidEmitCursor_;
	cb.emitCount = count;
	cb.emitPos = pos;
	cb.emitDir = velocityDir;
	cb.emitColor = color;

	list_->SetComputeRoot32BitConstants(0, 16, &cb, 0);
	list_->SetComputeRootUnorderedAccessView(1, gpuFluidBuffer_->GetGPUVirtualAddress());

	uint32_t threadGroups = (gpuFluidMaxParticles_ + 255) / 256; // Issue 1 pass since we check boundaries inside CS
	list_->Dispatch(threadGroups, 1, 1);

	gpuFluidEmitCursor_ = (gpuFluidEmitCursor_ + count) % gpuFluidMaxParticles_;
}

void Renderer::DrawGPUFluid(TextureHandle texture) {
	if (!isGPUFluidReady_ || !psoFluidRender_ || !gpuFluidBuffer_) return;

	list_->SetPipelineState(psoFluidRender_.Get());
	list_->SetGraphicsRootSignature(rootSig3D_.Get());
	
	list_->SetGraphicsRootConstantBufferView(0, cbFrameAddr_);
	list_->SetGraphicsRootDescriptorTable(3, GetTextureSrvGpu(texture)); // t0 texture

	auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(gpuFluidBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	list_->ResourceBarrier(1, &b1);

	list_->SetGraphicsRootShaderResourceView(6, gpuFluidBuffer_->GetGPUVirtualAddress()); // t2

    // Cube Mesh
    auto* model = GetModel(LoadObjMesh("Resources/Models/cube/cube.obj"));
    if (model) {
        list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = model->GetVertexBufferAddr();
        vbv.SizeInBytes = model->GetVertexCount() * sizeof(VertexData);
        vbv.StrideInBytes = sizeof(VertexData);
        list_->IASetVertexBuffers(0, 1, &vbv);
        list_->DrawInstanced(model->GetVertexCount(), gpuFluidMaxParticles_, 0, 0);
    }

	auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(gpuFluidBuffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	list_->ResourceBarrier(1, &b2);
}
'''
if 'InitGPUFluid' not in text:
    text += impl
    text = text.replace('InitPostProcess_();', 'InitPostProcess_();\n\tInitGPUFluid();')
    text = text.replace('drawCalls_.clear();', 'drawCalls_.clear();\n\tUpdateGPUFluid(1.0f/60.0f);')

with open(renderer_cpp, 'w', encoding='utf-8') as f:
    f.write(text)

hit_effect_cpp = r'c:\Users\k024g\source\repos\neo_Engine\Game\Scripts\HitEffectScript.cpp'
with open(hit_effect_cpp, 'r', encoding='utf-8') as f:
    htext = f.read()

target = 'scene->GetRenderer()->DrawLiquidParticleInstanced(mesh, tex, t, c, {1,1,0,0}, "Particle");'
replace = '// ★フェーズ1：CPU液体の描画は無効化し、GPU放出に切り替える'
htext = htext.replace(target, replace)

emit_target = 'int particleCount = 20; // 飛び散る水滴の数'
emit_replace = '''
		// --- ★フェーズ1: GPUスプラッターエフェクト（一気に数千個放出） ---
		Engine::Vector3 ePos = {pos.x, pos.y + 0.5f, pos.z};
		Engine::Vector3 eDir = {attackDirX_, 1.0f, attackDirZ_};
		Engine::Vector4 eColor = (colorDist(mt) > 0.5f) ? Engine::Vector4{ 0.0f, 0.8f, 1.0f, 1.0f } : Engine::Vector4{ 1.0f, 0.0f, 1.0f, 1.0f };
		if (scene && scene->GetRenderer()) {
			scene->GetRenderer()->EmitGPUFluid(ePos, eDir, eColor, 400); // 400パーティクル一気に放出！
		}
'''
if 'GPUスプラッターエフェクト' not in htext:
    idx = htext.find(emit_target)
    idx2 = htext.find('} else if (isMelee_) {', idx)
    if idx != -1 and idx2 != -1:
        htext = htext[:idx] + emit_replace + htext[idx2:]

with open(hit_effect_cpp, 'w', encoding='utf-8') as f:
    f.write(htext)

print("success")
