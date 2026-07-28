#include "Model.h"
#include "Renderer.h"
#include "WindowDX.h"
#include "PathUtils.h"
#include <fstream>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "d3dx12.h"
#include <DirectXTex.h>
#include "NetworkProfiler.h"

// Assimp Includes
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <ufbx.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace Engine {

// ToWide function replaced by PathUtils::FromUTF8

static void SplitPath(const std::string& full, std::string& dir, std::string& file) {
	size_t p = full.find_last_of("/\\");
	if (p == std::string::npos) {
		dir = ".";
		file = full;
	} else {
		dir = full.substr(0, p);
		file = full.substr(p + 1);
	}
}

static Matrix4x4 XMToM4(const DirectX::XMMATRIX& xm) {
	Matrix4x4 out{};
	DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&out), xm);
	return out;
}

// ★修正: Assimp(Column) -> DirectX(Row) 転置 + X軸反転
static Matrix4x4 AiToMat4(const aiMatrix4x4& m) {
	Matrix4x4 out;
	// 転置しつつ、0列目と0行目（0,0以外）を反転させてX軸ミラーリングを行う
	out.m[0][0] = m.a1;
	out.m[0][1] = -m.b1;
	out.m[0][2] = -m.c1;
	out.m[0][3] = -m.d1;
	out.m[1][0] = -m.a2;
	out.m[1][1] = m.b2;
	out.m[1][2] = m.c2;
	out.m[1][3] = m.d2;
	out.m[2][0] = -m.a3;
	out.m[2][1] = m.b3;
	out.m[2][2] = m.c3;
	out.m[2][3] = m.d3;
	out.m[3][0] = -m.a4;
	out.m[3][1] = m.b4;
	out.m[3][2] = m.c4;
	out.m[3][3] = m.d4;
	return out;
}

static void ReadNodeHierarchy(Node& node, const aiNode* src) {
	node.name = src->mName.C_Str();

	aiVector3D scale, pos;
	aiQuaternion rot;
	src->mTransformation.Decompose(scale, rot, pos);
	node.transform.scale = {scale.x, scale.y, scale.z};
	node.transform.rotate = {rot.x, -rot.y, -rot.z, rot.w};
	node.transform.translate = {-pos.x, pos.y, pos.z};

	node.localMatrix = AiToMat4(src->mTransformation);

	node.children.resize(src->mNumChildren);
	for (unsigned int i = 0; i < src->mNumChildren; ++i) {
		ReadNodeHierarchy(node.children[i], src->mChildren[i]);
	}
}

static void ReadAnimation(ModelData& modelData, const aiScene* scene) {
	for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
		aiAnimation* srcAnim = scene->mAnimations[i];
		Animation dstAnim;
		dstAnim.name = srcAnim->mName.C_Str();
		dstAnim.duration = (float)srcAnim->mDuration;
		dstAnim.ticksPerSecond = (srcAnim->mTicksPerSecond != 0) ? (float)srcAnim->mTicksPerSecond : 25.0f;

		for (unsigned int j = 0; j < srcAnim->mNumChannels; ++j) {
			aiNodeAnim* channel = srcAnim->mChannels[j];
			NodeAnimation nodeAnim;
			// Translation: X反転
			for (unsigned int k = 0; k < channel->mNumPositionKeys; ++k) {
				aiVector3D v = channel->mPositionKeys[k].mValue;
				nodeAnim.translations.push_back({
				    (float)channel->mPositionKeys[k].mTime, {-v.x, v.y, v.z}
                });
			}
			// Rotation: Y, Z反転 (X軸ミラー)
			for (unsigned int k = 0; k < channel->mNumRotationKeys; ++k) {
				aiQuaternion q = channel->mRotationKeys[k].mValue;
				nodeAnim.rotations.push_back({
				    (float)channel->mRotationKeys[k].mTime, {q.x, -q.y, -q.z, q.w}
                });
			}
			// Scale: そのまま
			for (unsigned int k = 0; k < channel->mNumScalingKeys; ++k) {
				aiVector3D v = channel->mScalingKeys[k].mValue;
				nodeAnim.scales.push_back({
				    (float)channel->mScalingKeys[k].mTime, {v.x, v.y, v.z}
                });
			}
			dstAnim.nodeAnimations[channel->mNodeName.C_Str()] = nodeAnim;
		}
		modelData.animations.push_back(dstAnim);
	}
}

static Vector3 CalculateTranslation(const std::vector<Keyframe<XMFLOAT3>>& keys, float time, const Vector3& fallback) {
	if (keys.empty())
		return fallback;
	if (keys.size() == 1 || time <= keys.front().time)
		return {keys.front().value.x, keys.front().value.y, keys.front().value.z};
	for (size_t i = 0; i < keys.size() - 1; ++i) {
		if (time >= keys[i].time && time <= keys[i + 1].time) {
			float t = (time - keys[i].time) / (keys[i + 1].time - keys[i].time);
			XMVECTOR p = XMVectorLerp(XMLoadFloat3(&keys[i].value), XMLoadFloat3(&keys[i + 1].value), t);
			Vector3 res;
			XMStoreFloat3((XMFLOAT3*)&res, p);
			return res;
		}
	}
	return {keys.back().value.x, keys.back().value.y, keys.back().value.z};
}

static Vector3 CalculateScale(const std::vector<Keyframe<XMFLOAT3>>& keys, float time, const Vector3& fallback) {
	if (keys.empty())
		return fallback;
	if (keys.size() == 1 || time <= keys.front().time)
		return {keys.front().value.x, keys.front().value.y, keys.front().value.z};
	for (size_t i = 0; i < keys.size() - 1; ++i) {
		if (time >= keys[i].time && time <= keys[i + 1].time) {
			float t = (time - keys[i].time) / (keys[i + 1].time - keys[i].time);
			XMVECTOR s = XMVectorLerp(XMLoadFloat3(&keys[i].value), XMLoadFloat3(&keys[i + 1].value), t);
			Vector3 res;
			XMStoreFloat3((XMFLOAT3*)&res, s);
			return res;
		}
	}
	return {keys.back().value.x, keys.back().value.y, keys.back().value.z};
}

static XMFLOAT4 CalculateRotation(const std::vector<Keyframe<XMFLOAT4>>& keys, float time, const XMFLOAT4& fallback) {
	if (keys.empty())
		return fallback;
	if (keys.size() == 1 || time <= keys.front().time)
		return keys.front().value;
	for (size_t i = 0; i < keys.size() - 1; ++i) {
		if (time >= keys[i].time && time <= keys[i + 1].time) {
			float t = (time - keys[i].time) / (keys[i + 1].time - keys[i].time);
			XMVECTOR q = XMQuaternionSlerp(XMLoadFloat4(&keys[i].value), XMLoadFloat4(&keys[i + 1].value), t);
			XMFLOAT4 res;
			XMStoreFloat4(&res, q);
			return res;
		}
	}
	return keys.back().value;
}

ComPtr<ID3D12Resource> Model::CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
	D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
	D3D12_RESOURCE_DESC rd{
	    D3D12_RESOURCE_DIMENSION_BUFFER, 0, (UINT64)sizeInBytes, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
               D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE
    };
	ComPtr<ID3D12Resource> res;
	// 引数のdeviceがnullptrの場合はシステム全体で共有されているRendererのデバイスを借りる（簡易実装）
	ID3D12Device* pDev = device;
	if (!pDev && Renderer::GetInstance()) pDev = Renderer::GetInstance()->GetDevice(); 
	
	if (pDev) {
		pDev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
	}
	return res;
}

ComPtr<ID3D12Resource> Model::CreateUAVBufferResource(ID3D12Device* device, size_t sizeInBytes) {
	D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
	D3D12_RESOURCE_DESC rd{
	    D3D12_RESOURCE_DIMENSION_BUFFER, 0, (UINT64)sizeInBytes, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
               D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    };
	ComPtr<ID3D12Resource> res;
	ID3D12Device* pDev = device;
	if (!pDev && Renderer::GetInstance()) pDev = Renderer::GetInstance()->GetDevice(); 
	
	if (pDev) {
		pDev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&res));
	}
	return res;
}

ComPtr<ID3D12Resource> Model::CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& m) {
	D3D12_RESOURCE_DESC rd{
	    D3D12_RESOURCE_DIMENSION(m.dimension), 0, (UINT64)m.width, (UINT)m.height, (UINT16)m.arraySize, (UINT16)m.mipLevels, m.format, {1, 0},
               D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE
    };
	D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
	ComPtr<ID3D12Resource> tex;
	device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
	return tex;
}

ComPtr<ID3D12Resource> Model::UploadTextureData(ID3D12Resource* tex, const DirectX::ScratchImage& mip, ID3D12Device* dev, ID3D12GraphicsCommandList* cmd) {
	std::vector<D3D12_SUBRESOURCE_DATA> subs;
	const DirectX::Image* imgs = mip.GetImages();
	for (size_t i = 0; i < mip.GetImageCount(); ++i)
		subs.push_back({imgs[i].pixels, (LONG_PTR)imgs[i].rowPitch, (LONG_PTR)imgs[i].slicePitch});
	ComPtr<ID3D12Resource> inter = CreateBufferResource(dev, GetRequiredIntermediateSize(tex, 0, (UINT)subs.size()));
	UpdateSubresources(cmd, tex, inter.Get(), 0, 0, (UINT)subs.size(), subs.data());
	D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmd->ResourceBarrier(1, &b);
	return inter;
}

static void NormalizeSkinWeights(ModelData& modelData);

bool Model::Load(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const std::string& objPath) {
	if (objPath.length() >= 4) {
		std::string ext = objPath.substr(objPath.length() - 4);
		for (char& c : ext) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (ext == ".fbx") {
			return LoadWithUFBX(device, cmd, objPath);
		}
	}

	Assimp::Importer importer;
	const unsigned int flags = aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate | aiProcess_LimitBoneWeights | aiProcess_PreTransformVertices;
	
	// ReadFile supports external files (.bin, .mtl) unlike ReadFileFromMemory without an IO handler
	const aiScene* scene = importer.ReadFile(objPath.c_str(), flags);

	if (!scene || !scene->mRootNode) {
		OutputDebugStringA(("[Model::Load] Failed to load model: " + objPath + "\n").c_str());
		if (importer.GetErrorString()) {
			OutputDebugStringA((std::string("Assimp Error: ") + importer.GetErrorString() + "\n").c_str());
		}
		return false;
	}

	ReadNodeHierarchy(data_.rootNode, scene->mRootNode);
	if (scene->HasAnimations())
		ReadAnimation(data_, scene);

	uint32_t vertexOffset = 0;
	for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
		aiMesh* mesh = scene->mMeshes[m];
		
		MeshSubset subset;
		subset.indexStart = (uint32_t)data_.indices.size();
		subset.materialIndex = mesh->mMaterialIndex;
		
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
			VertexData v{};
			// ★修正: 手動X反転 (World.cppと一致させる)
			v.position = {mesh->mVertices[i].x * -1.0f, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f};
			if (mesh->HasNormals())
				v.normal = {mesh->mNormals[i].x * -1.0f, mesh->mNormals[i].y, mesh->mNormals[i].z};
			if (mesh->HasTextureCoords(0))
				v.texcoord = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
			data_.vertices.push_back(v);
		}
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
			aiFace& face = mesh->mFaces[f];
			data_.indices.push_back(vertexOffset + face.mIndices[0]);
			data_.indices.push_back(vertexOffset + face.mIndices[1]);
			data_.indices.push_back(vertexOffset + face.mIndices[2]);
		}
		if (mesh->HasBones()) {
			for (unsigned int i = 0; i < mesh->mNumBones; ++i) {
				aiBone* bone = mesh->mBones[i];
				std::string bName = bone->mName.C_Str();
				int bIdx = 0;
				if (data_.boneMapping.find(bName) == data_.boneMapping.end()) {
					if (data_.bones.size() >= kMaxBones) {
						continue;
					}
					bIdx = (int)data_.bones.size();
					// AiToMat4でX反転処理済み
					data_.bones.push_back({bName, AiToMat4(bone->mOffsetMatrix), bIdx});
					data_.boneMapping[bName] = bIdx;
				} else
					bIdx = data_.boneMapping[bName];
				if (bIdx < 0 || bIdx >= kMaxBones) {
					continue;
				}
				for (unsigned int j = 0; j < bone->mNumWeights; ++j) {
					VertexData& v = data_.vertices[vertexOffset + bone->mWeights[j].mVertexId];
					for (int k = 0; k < 4; ++k) {
						if (v.boneWeights[k] == 0.0f) {
							v.boneWeights[k] = bone->mWeights[j].mWeight;
							v.boneIndices[k] = bIdx;
							break;
						}
					}
				}
			}
		}
		
		subset.indexCount = (uint32_t)data_.indices.size() - subset.indexStart;
		data_.subsets.push_back(subset);
		
		vertexOffset += mesh->mNumVertices;
	}

	// Calculate AABB
	if (!data_.vertices.empty()) {
		data_.min = {FLT_MAX, FLT_MAX, FLT_MAX};
		data_.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		for (const auto& v : data_.vertices) {
			data_.min.x = (std::min)(data_.min.x, v.position.x);
			data_.min.y = (std::min)(data_.min.y, v.position.y);
			data_.min.z = (std::min)(data_.min.z, v.position.z);
			data_.max.x = (std::max)(data_.max.x, v.position.x);
			data_.max.y = (std::max)(data_.max.y, v.position.y);
			data_.max.z = (std::max)(data_.max.z, v.position.z);
		}
	}

	// ★★★ 修正箇所: テクスチャ読み込み部分 ★★★
	std::vector<int> materialToTexIdx(scene->mNumMaterials, -1);

	for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
		aiMaterial* material = scene->mMaterials[i];
		aiString str;
		if (material->GetTexture(aiTextureType_DIFFUSE, 0, &str) != aiReturn_SUCCESS) {
			material->GetTexture(aiTextureType_BASE_COLOR, 0, &str);
		}

		if (str.length > 0) {
			ScratchImage mip;
			bool textureReady = false;

			const aiTexture* embeddedTex = scene->GetEmbeddedTexture(str.C_Str());
			if (embeddedTex) {
				if (embeddedTex->mHeight == 0) {
					if (SUCCEEDED(LoadFromWICMemory(embeddedTex->pcData, embeddedTex->mWidth, WIC_FLAGS_FORCE_SRGB, nullptr, mip))) {
						textureReady = true;
					}
				}
			} else {
				std::filesystem::path fullPath(PathUtils::FromUTF8(objPath));
				std::filesystem::path dir = fullPath.parent_path();
				std::filesystem::path texPath = dir / str.C_Str();
				std::wstring widePath = PathUtils::GetUnifiedPathW(texPath.wstring());
				if (SUCCEEDED(LoadFromWICFile(widePath.c_str(), WIC_FLAGS_FORCE_SRGB, nullptr, mip))) {
					textureReady = true;
				}
			}

			if (textureReady) {
				auto tex = CreateTextureResource(device, mip.GetMetadata());
				auto upload = UploadTextureData(tex.Get(), mip, device, cmd);
				
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
				srvDesc.Format = mip.GetMetadata().format;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Texture2D.MipLevels = (UINT)mip.GetMetadata().mipLevels;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.PlaneSlice = 0;
				srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
				
				texs_.push_back(tex);
				uploads_.push_back(upload);
				srvDescs_.push_back(srvDesc);
				
				materialToTexIdx[i] = (int)texs_.size() - 1;
			}
		}
	}
	
	// サブメッシュのマテリアルIDをテクスチャIDに変換
	for (auto& sub : data_.subsets) {
		if (sub.materialIndex >= 0 && sub.materialIndex < materialToTexIdx.size()) {
			sub.materialIndex = materialToTexIdx[sub.materialIndex];
		}
		// Do not set to -1, allow manual SRV injection
	}
	// ★★★ 修正終わり ★★★

	bool hasBones = !data_.bones.empty();
	NormalizeSkinWeights(data_);
	vb_ = CreateBufferResource(device, sizeof(VertexData) * data_.vertices.size());
	if (!vb_) return false;

	if (hasBones) {
		skinnedVb_ = CreateUAVBufferResource(device, sizeof(VertexData) * data_.vertices.size());
		if (!skinnedVb_) return false;
	}

	void* vmap = nullptr;
	if (FAILED(vb_->Map(0, nullptr, &vmap))) return false;
	std::memcpy(vmap, data_.vertices.data(), sizeof(VertexData) * data_.vertices.size());
	vb_->Unmap(0, nullptr);
	vbv_ = {vb_->GetGPUVirtualAddress(), (UINT)(sizeof(VertexData) * data_.vertices.size()), sizeof(VertexData)};

	if (hasBones && skinnedVb_) {
		skinnedVbv_ = {skinnedVb_->GetGPUVirtualAddress(), (UINT)(sizeof(VertexData) * data_.vertices.size()), sizeof(VertexData)};
	}

	if (data_.indices.size() > 0) {
		ib_ = CreateBufferResource(device, sizeof(uint32_t) * data_.indices.size());
		if (!ib_) return false;

		void* imap = nullptr;
		if (FAILED(ib_->Map(0, nullptr, &imap))) return false;
		std::memcpy(imap, data_.indices.data(), sizeof(uint32_t) * data_.indices.size());
		ib_->Unmap(0, nullptr);
		ibv_ = {ib_->GetGPUVirtualAddress(), (UINT)(sizeof(uint32_t) * data_.indices.size()), DXGI_FORMAT_R32_UINT};
	}
	indexCount_ = (uint32_t)data_.indices.size();

	// BVH構築
	BuildBVH();

	float sizeMB = (sizeof(VertexData) * data_.vertices.size() + sizeof(uint32_t) * data_.indices.size()) / (1024.0f * 1024.0f);
	std::string detailsStr = std::to_string(data_.vertices.size() / 3) + " Triangles / " + std::to_string(data_.materials.size()) + " Mats";
	NetworkProfiler::GetInstance().RegisterAsset(objPath, "Mesh", sizeMB, detailsStr);

	return true;
}

static Matrix4x4 UfbxToMat4(const ufbx_matrix& m) {
	Matrix4x4 out;
	out.m[0][0] = (float)m.cols[0].x; out.m[0][1] = (float)-m.cols[0].y; out.m[0][2] = (float)-m.cols[0].z; out.m[0][3] = 0.0f;
	out.m[1][0] = (float)-m.cols[1].x; out.m[1][1] = (float)m.cols[1].y; out.m[1][2] = (float)m.cols[1].z; out.m[1][3] = 0.0f;
	out.m[2][0] = (float)-m.cols[2].x; out.m[2][1] = (float)m.cols[2].y; out.m[2][2] = (float)m.cols[2].z; out.m[2][3] = 0.0f;
	out.m[3][0] = (float)-m.cols[3].x; out.m[3][1] = (float)m.cols[3].y; out.m[3][2] = (float)m.cols[3].z; out.m[3][3] = 1.0f;
	return out;
}

static std::string SanitizeNodeName(const std::string& name) {
    std::string res = name;
    size_t colonPos = res.find(':');
    if (colonPos != std::string::npos) {
        res = res.substr(colonPos + 1);
    }
    // Mixamo FBX sometimes uses mixamorig_ instead of mixamorig:
    size_t mixaPos = res.find("mixamorig_");
    if (mixaPos == 0) {
        res = res.substr(10);
    }
    return res;
}

static void NormalizeSkinWeights(ModelData& modelData) {
	for (auto& v : modelData.vertices) {
		float sum = 0.0f;
		for (int i = 0; i < 4; ++i) {
			if (v.boneIndices[i] >= kMaxBones) {
				v.boneIndices[i] = 0;
				v.boneWeights[i] = 0.0f;
			}
			sum += v.boneWeights[i];
		}
		if (sum > 0.0001f) {
			const float inv = 1.0f / sum;
			for (float& weight : v.boneWeights) {
				weight *= inv;
			}
		}
	}
}

static void ReadNodeHierarchyUfbx(Node& node, const ufbx_node* src) {
	node.name = SanitizeNodeName(src->name.data);

	ufbx_vec3 scale = src->local_transform.scale;
	ufbx_quat rot = src->local_transform.rotation;
	ufbx_vec3 pos = src->local_transform.translation;

	node.transform.scale = {(float)scale.x, (float)scale.y, (float)scale.z};
	node.transform.rotate = {(float)rot.x, (float)-rot.y, (float)-rot.z, (float)rot.w};
	node.transform.translate = {(float)-pos.x, (float)pos.y, (float)pos.z};

	node.localMatrix = UfbxToMat4(src->node_to_parent);

	node.children.resize(src->children.count);
	for (size_t i = 0; i < src->children.count; ++i) {
		ReadNodeHierarchyUfbx(node.children[i], src->children.data[i]);
	}
}

static void ReadAnimationUfbx(ModelData& modelData, const ufbx_scene* scene, const std::string& namePrefix = "") {
	for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
		ufbx_anim_stack* srcAnim = scene->anim_stacks.data[i];
		Animation dstAnim;
		std::string baseName = srcAnim->name.data;
		dstAnim.name = namePrefix.empty() ? baseName : namePrefix + "_" + baseName;
		dstAnim.duration = (float)srcAnim->time_end * 25.0f; // Assumes 25fps if no ticks per sec
		dstAnim.ticksPerSecond = 25.0f;

        float durationSecs = (float)(srcAnim->time_end - srcAnim->time_begin);
        
        char debugMsg[256];
        sprintf_s(debugMsg, "[Model] Anim Stack '%s': duration=%f (begin=%f, end=%f)\n", dstAnim.name.c_str(), durationSecs, srcAnim->time_begin, srcAnim->time_end);
        OutputDebugStringA(debugMsg);

        if (durationSecs <= 0) {
            OutputDebugStringA(("[Model] Skipping anim '" + dstAnim.name + "' due to zero or negative duration.\n").c_str());
            continue;
        }
        
        int numFrames = (int)(durationSecs * 25.0f) + 1;
        
        for (size_t n = 0; n < scene->nodes.count; ++n) {
            ufbx_node* node = scene->nodes.data[n];
            if (node->is_root) continue;
            
            NodeAnimation nodeAnim;
            std::string sanitizedName = SanitizeNodeName(node->name.data);
            for (int f = 0; f < numFrames; ++f) {
                float time = (float)srcAnim->time_begin + (float)f / 25.0f;
                ufbx_transform transform = ufbx_evaluate_transform(srcAnim->anim, node, time);
                
                float frameTime = (float)f;
                
                ufbx_vec3 v = transform.translation;
                float tx = (float)-v.x;
                float ty = (float)v.y;
                float tz = (float)v.z;

                // Hipsのルートモーション（XZ平面の移動）を無効化し、すべてのアニメーションで原点に揃える（ブレンド時のワープ防止）
                if (sanitizedName == "Hips") {
                    tx = 0.0f;
                    tz = 0.0f;
                }
                
                nodeAnim.translations.push_back({frameTime, {tx, ty, tz}});
                
                ufbx_quat q = transform.rotation;
                nodeAnim.rotations.push_back({frameTime, {(float)q.x, (float)-q.y, (float)-q.z, (float)q.w}});
                
                ufbx_vec3 s = transform.scale;
                nodeAnim.scales.push_back({frameTime, {(float)s.x, (float)s.y, (float)s.z}});
            }
            dstAnim.nodeAnimations[SanitizeNodeName(node->name.data)] = nodeAnim;
        }
        
        if (dstAnim.nodeAnimations.empty()) {
            OutputDebugStringA(("[Model] Skipping anim '" + dstAnim.name + "' because it has no node animations.\n").c_str());
            continue;
        }
        
        sprintf_s(debugMsg, "[Model] Successfully loaded anim '%s' with %zu animated nodes.\n", dstAnim.name.c_str(), dstAnim.nodeAnimations.size());
        OutputDebugStringA(debugMsg);
        
		modelData.animations.push_back(dstAnim);
	}
}

bool Model::LoadWithUFBX(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const std::string& objPath) {
    ufbx_load_opts opts = {0};
    opts.ignore_missing_external_files = true;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.generate_missing_normals = true;
    
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(objPath.c_str(), &opts, &error);
    if (!scene) {
        OutputDebugStringA(("[Model::LoadWithUFBX] Failed to load model: " + objPath + "\n").c_str());
        return false;
    }

    if (scene->root_node) {
        ReadNodeHierarchyUfbx(data_.rootNode, scene->root_node);
    }
    
    if (scene->anim_stacks.count > 0) {
        size_t slash = objPath.find_last_of("/\\");
        std::string filename = (slash != std::string::npos) ? objPath.substr(slash + 1) : objPath;
        size_t dot = filename.find_last_of('.');
        if (dot != std::string::npos) filename = filename.substr(0, dot);
        ReadAnimationUfbx(data_, scene, filename);
    }

    uint32_t vertexOffset = 0;
    for (size_t m = 0; m < scene->meshes.count; ++m) {
        ufbx_mesh* mesh = scene->meshes.data[m];
        
        bool is_visible = false;
        for (size_t inst = 0; inst < mesh->instances.count; ++inst) {
            if (mesh->instances.data[inst]->visible) {
                is_visible = true;
                break;
            }
        }
        if (!is_visible) continue;
        
        // Split mesh into materials
        for (size_t part_idx = 0; part_idx < mesh->material_parts.count; ++part_idx) {
            ufbx_mesh_part part = mesh->material_parts.data[part_idx];
            if (part.num_faces == 0) continue;
            
            MeshSubset subset;
            subset.indexStart = (uint32_t)data_.indices.size();
            subset.materialIndex = -1;
            
            if (part_idx < mesh->materials.count && mesh->materials.data[part_idx] != nullptr) {
                ufbx_material* mat = mesh->materials.data[part_idx];
                for (size_t mat_i = 0; mat_i < scene->materials.count; ++mat_i) {
                    if (scene->materials.data[mat_i] == mat) {
                        subset.materialIndex = (int)mat_i;
                        break;
                    }
                }
            }
            
            // Extract triangles
            uint32_t max_tri_indices = (uint32_t)mesh->max_face_triangles * 3;
            std::vector<uint32_t> tri_indices(max_tri_indices);
            
            for (size_t i = 0; i < part.num_faces; ++i) {
                uint32_t face_idx = part.face_indices.data[i];
                ufbx_face face = mesh->faces.data[face_idx];
                
                uint32_t num_tris = ufbx_triangulate_face(tri_indices.data(), max_tri_indices, mesh, face);
                
                for (uint32_t j = 0; j < num_tris; ++j) {
                    for (uint32_t k = 0; k < 3; ++k) {
                        // X軸を反転させるため、カリング(Winding Order)が逆になるのを防ぐべく、
                        // 1番目と2番目のインデックスを入れ替える (0, 1, 2 -> 0, 2, 1)
                        uint32_t corner = (k == 1) ? 2 : ((k == 2) ? 1 : 0);
                        uint32_t index = tri_indices[j * 3 + corner];
                        uint32_t vertex_id = mesh->vertex_indices.data[index];
                        
                        VertexData v{};
                        ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                        v.position = {(float)-pos.x, (float)pos.y, (float)pos.z, 1.0f};
                        
                        if (mesh->vertex_normal.exists) {
                            ufbx_vec3 norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                            v.normal = {(float)-norm.x, (float)norm.y, (float)norm.z};
                        }
                        if (mesh->vertex_uv.exists) {
                            ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                            v.texcoord = {(float)uv.x, (float)uv.y};
                        }
                        
                        // Skinning
                        if (mesh->skin_deformers.count > 0) {
                            ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];
                            ufbx_skin_vertex skin_vert = skin->vertices.data[vertex_id];
                            
                            int weightCount = 0;
                            for (size_t w = 0; w < skin_vert.num_weights && weightCount < 4; ++w) {
                                ufbx_skin_weight skin_weight = skin->weights.data[skin_vert.weight_begin + w];
                                ufbx_skin_cluster* cluster = skin->clusters.data[skin_weight.cluster_index];
                                
                                std::string bName = cluster->bone_node ? cluster->bone_node->name.data : "unnamed";
                                bName = SanitizeNodeName(bName);
                                int bIdx = 0;
                                if (data_.boneMapping.find(bName) == data_.boneMapping.end()) {
                                    if (data_.bones.size() >= kMaxBones) {
                                        continue;
                                    }
                                    bIdx = (int)data_.bones.size();
                                    data_.bones.push_back({bName, UfbxToMat4(cluster->geometry_to_bone), bIdx});
                                    data_.boneMapping[bName] = bIdx;
                                } else {
                                    bIdx = data_.boneMapping[bName];
                                }
                                if (bIdx < 0 || bIdx >= kMaxBones) {
                                    continue;
                                }
                                
                                v.boneWeights[weightCount] = (float)skin_weight.weight;
                                v.boneIndices[weightCount] = bIdx;
                                weightCount++;
                            }
                        }
                        
                        data_.vertices.push_back(v);
                        data_.indices.push_back(vertexOffset++);
                    }
                }
            }
            
            subset.indexCount = (uint32_t)data_.indices.size() - subset.indexStart;
            data_.subsets.push_back(subset);
        }
    }

    if (!data_.vertices.empty()) {
        data_.min = {FLT_MAX, FLT_MAX, FLT_MAX};
        data_.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (const auto& v : data_.vertices) {
            data_.min.x = (std::min)(data_.min.x, v.position.x);
            data_.min.y = (std::min)(data_.min.y, v.position.y);
            data_.min.z = (std::min)(data_.min.z, v.position.z);
            data_.max.x = (std::max)(data_.max.x, v.position.x);
            data_.max.y = (std::max)(data_.max.y, v.position.y);
            data_.max.z = (std::max)(data_.max.z, v.position.z);
        }
    }

    std::vector<int> materialToTexIdx(scene->materials.count, -1);
    for (size_t i = 0; i < scene->materials.count; ++i) {
        ufbx_material* mat = scene->materials.data[i];
        ufbx_texture* tex = mat->pbr.base_color.texture;
        if (!tex && mat->fbx.diffuse_color.texture) tex = mat->fbx.diffuse_color.texture;
        if (!tex) continue;
        
        std::string str = tex->relative_filename.data;
        if (str.empty()) str = tex->filename.data;
        
        if (!str.empty()) {
            DirectX::ScratchImage mip;
            bool textureReady = false;

            if (tex->content.size > 0) {
                if (SUCCEEDED(DirectX::LoadFromWICMemory(tex->content.data, tex->content.size, DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, mip))) {
                    textureReady = true;
                }
            } else {
                std::filesystem::path fullPath(PathUtils::FromUTF8(objPath));
                std::filesystem::path dir = fullPath.parent_path();
                std::filesystem::path texPath = dir / str;
                std::wstring widePath = PathUtils::GetUnifiedPathW(texPath.wstring());
                if (SUCCEEDED(DirectX::LoadFromWICFile(widePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, mip))) {
                    textureReady = true;
                }
            }

            if (textureReady) {
                auto t = CreateTextureResource(device, mip.GetMetadata());
                auto upload = UploadTextureData(t.Get(), mip, device, cmd);
                
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = mip.GetMetadata().format;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels = (UINT)mip.GetMetadata().mipLevels;
                
                texs_.push_back(t);
                uploads_.push_back(upload);
                srvDescs_.push_back(srvDesc);
                
                materialToTexIdx[i] = (int)texs_.size() - 1;
            }
        }
    }
    
    for (auto& sub : data_.subsets) {
        if (sub.materialIndex >= 0 && sub.materialIndex < materialToTexIdx.size()) {
            sub.materialIndex = materialToTexIdx[sub.materialIndex];
        }
        // Do not set to -1, allow manual SRV injection
    }

    NormalizeSkinWeights(data_);

    ufbx_free_scene(scene);

    vb_ = CreateBufferResource(device, sizeof(VertexData) * data_.vertices.size());
    skinnedVb_ = CreateUAVBufferResource(device, sizeof(VertexData) * data_.vertices.size());
    if (!vb_ || !skinnedVb_) return false;

    void* vmap = nullptr;
    if (FAILED(vb_->Map(0, nullptr, &vmap))) return false;
    std::memcpy(vmap, data_.vertices.data(), sizeof(VertexData) * data_.vertices.size());
    vb_->Unmap(0, nullptr);
    vbv_ = {vb_->GetGPUVirtualAddress(), (UINT)(sizeof(VertexData) * data_.vertices.size()), sizeof(VertexData)};
    skinnedVbv_ = {skinnedVb_->GetGPUVirtualAddress(), (UINT)(sizeof(VertexData) * data_.vertices.size()), sizeof(VertexData)};

    if (data_.indices.size() > 0) {
        ib_ = CreateBufferResource(device, sizeof(uint32_t) * data_.indices.size());
        if (!ib_) return false;

        void* imap = nullptr;
        if (FAILED(ib_->Map(0, nullptr, &imap))) return false;
        std::memcpy(imap, data_.indices.data(), sizeof(uint32_t) * data_.indices.size());
        ib_->Unmap(0, nullptr);
        ibv_ = {ib_->GetGPUVirtualAddress(), (UINT)(sizeof(uint32_t) * data_.indices.size()), DXGI_FORMAT_R32_UINT};
    }
    indexCount_ = (uint32_t)data_.indices.size();

    BuildBVH();

    float sizeMB = (sizeof(VertexData) * data_.vertices.size() + sizeof(uint32_t) * data_.indices.size()) / (1024.0f * 1024.0f);
    std::string detailsStr = std::to_string(data_.vertices.size() / 3) + " Triangles / " + std::to_string(data_.materials.size()) + " Mats";
    NetworkProfiler::GetInstance().RegisterAsset(objPath, "Mesh", sizeMB, detailsStr);

    return true;
}

bool Model::LoadAdditionalAnimation(const std::string& animPath) {
    ufbx_load_opts opts = {0};
    opts.ignore_missing_external_files = true;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.generate_missing_normals = true;
    
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(animPath.c_str(), &opts, &error);
    if (!scene) {
        OutputDebugStringA(("[Model::LoadAdditionalAnimation] Failed to load animation: " + animPath + "\n").c_str());
        return false;
    }

    if (scene->anim_stacks.count > 0) {
        size_t slash = animPath.find_last_of("/\\");
        std::string filename = (slash != std::string::npos) ? animPath.substr(slash + 1) : animPath;
        size_t dot = filename.find_last_of('.');
        if (dot != std::string::npos) filename = filename.substr(0, dot);
        ReadAnimationUfbx(data_, scene, filename);
    }
    
    ufbx_free_scene(scene);
    return true;
}

void Model::InitializeDynamic(ID3D12Device* device, const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices) {
	data_.vertices = vertices;
	data_.indices = indices;

	vb_ = CreateBufferResource(device, sizeof(VertexData) * vertices.size());
	skinnedVb_ = CreateUAVBufferResource(device, sizeof(VertexData) * vertices.size());
	if (vb_ && skinnedVb_) {
		UpdateVertices(vertices);
		vbv_ = {vb_->GetGPUVirtualAddress(), (UINT)(sizeof(VertexData) * vertices.size()), sizeof(VertexData)};
		skinnedVbv_ = {skinnedVb_->GetGPUVirtualAddress(), (UINT)(sizeof(VertexData) * vertices.size()), sizeof(VertexData)};
	}

	if (indices.size() > 0) {
		ib_ = CreateBufferResource(device, sizeof(uint32_t) * indices.size());
		if (ib_) {
			void* imap = nullptr;
			if (SUCCEEDED(ib_->Map(0, nullptr, &imap))) {
				std::memcpy(imap, indices.data(), sizeof(uint32_t) * indices.size());
				ib_->Unmap(0, nullptr);
			}
			ibv_ = {ib_->GetGPUVirtualAddress(), (UINT)(sizeof(uint32_t) * indices.size()), DXGI_FORMAT_R32_UINT};
		}
	}
	indexCount_ = (uint32_t)indices.size();
	
	// 動的メッシュにおけるBVH構築（地形追従などの必要性があれば追加可能）
	BuildBVH();
}

void Model::UpdateVertices(const std::vector<VertexData>& vertices) {
	if (vertices.size() != data_.vertices.size() || !vb_) return; // 頂点数は固定前提
	data_.vertices = vertices;
	void* vmap = nullptr;
	if (SUCCEEDED(vb_->Map(0, nullptr, &vmap))) {
		std::memcpy(vmap, vertices.data(), sizeof(VertexData) * vertices.size());
		vb_->Unmap(0, nullptr);
	}
}

void Model::CreateSrvs(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, ID3D12DescriptorHeap* srvHeapMaster, UINT descriptorSize, const std::vector<uint32_t>& heapIndices) {
	if (texs_.empty() || heapIndices.size() != texs_.size()) return;
	
	srvGpus_.resize(texs_.size());
	for (size_t i = 0; i < texs_.size(); ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
		cpu.ptr += (SIZE_T)descriptorSize * heapIndices[i];
		
		if (srvHeapMaster) {
			D3D12_CPU_DESCRIPTOR_HANDLE cpuMaster = srvHeapMaster->GetCPUDescriptorHandleForHeapStart();
			cpuMaster.ptr += (SIZE_T)descriptorSize * heapIndices[i];
			device->CreateShaderResourceView(texs_[i].Get(), &srvDescs_[i], cpuMaster);
		}
		
		srvGpus_[i] = srvHeap->GetGPUDescriptorHandleForHeapStart();
		srvGpus_[i].ptr += (UINT64)descriptorSize * heapIndices[i];
		device->CreateShaderResourceView(texs_[i].Get(), &srvDescs_[i], cpu);
	}
}

void Model::Draw(ID3D12GraphicsCommandList* cmd, UINT rootSrvParamIndex, bool useModelTextures, bool useSkinnedVb) {
	cmd->IASetVertexBuffers(0, 1, useSkinnedVb && skinnedVb_ ? &skinnedVbv_ : &vbv_);
	cmd->IASetIndexBuffer(&ibv_);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	if (data_.subsets.empty()) {
		cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
	} else {
		for (const auto& sub : data_.subsets) {
			if (useModelTextures && sub.materialIndex >= 0 && sub.materialIndex < (int)srvGpus_.size()) {
				cmd->SetGraphicsRootDescriptorTable(rootSrvParamIndex, srvGpus_[sub.materialIndex]);
			}
			cmd->DrawIndexedInstanced(sub.indexCount, 1, sub.indexStart, 0, 0);
		}
	}
}

void Model::DrawInstanced(ID3D12GraphicsCommandList* cmd, UINT instanceCount, UINT rootSrvParamIndex, bool useModelTextures, bool useSkinnedVb) {
	cmd->IASetVertexBuffers(0, 1, useSkinnedVb && skinnedVb_ ? &skinnedVbv_ : &vbv_);
	cmd->IASetIndexBuffer(&ibv_);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	if (data_.subsets.empty()) {
		cmd->DrawIndexedInstanced(indexCount_, instanceCount, 0, 0, 0);
	} else {
		for (const auto& sub : data_.subsets) {
			if (useModelTextures && sub.materialIndex >= 0 && sub.materialIndex < (int)srvGpus_.size()) {
				cmd->SetGraphicsRootDescriptorTable(rootSrvParamIndex, srvGpus_[sub.materialIndex]);
			}
			cmd->DrawIndexedInstanced(sub.indexCount, instanceCount, sub.indexStart, 0, 0);
		}
	}
}

void Model::BuildBVH() {
	if (data_.indices.empty()) return;

	data_.bvhNodes.clear();
	data_.bvhIndices.clear();

	uint32_t numTriangles = (uint32_t)data_.indices.size() / 3;
	data_.bvhIndices.reserve(numTriangles);
	for (uint32_t i = 0; i < numTriangles; ++i) data_.bvhIndices.push_back(i);

	// Root node
	BVHNode root{};
	root.firstTriangle = 0;
	root.triangleCount = numTriangles;
	data_.bvhNodes.push_back(root);

	UpdateNodeBounds(0);
	SubdivideBVH(0);

	// ★追加: GPU用バッファの構築と転送
	if (!data_.bvhNodes.empty()) {
		// ※本来は専用のUPLOAD→DEFAULT遷移が必要だが、このエンジンのCreateBufferResourceは
		// UPLOADヒープで作られているため、そのままMapしてコピーする。
		vbBvhNodes_ = CreateBufferResource(nullptr, data_.bvhNodes.size() * sizeof(BVHNode));
		if (vbBvhNodes_) {
			void* nodePtr = nullptr;
			if (SUCCEEDED(vbBvhNodes_->Map(0, nullptr, &nodePtr))) {
				std::memcpy(nodePtr, data_.bvhNodes.data(), data_.bvhNodes.size() * sizeof(BVHNode));
				vbBvhNodes_->Unmap(0, nullptr);
			}
		}

		vbBvhIndices_ = CreateBufferResource(nullptr, data_.bvhIndices.size() * sizeof(uint32_t));
		if (vbBvhIndices_) {
			void* indexPtr = nullptr;
			if (SUCCEEDED(vbBvhIndices_->Map(0, nullptr, &indexPtr))) {
				std::memcpy(indexPtr, data_.bvhIndices.data(), data_.bvhIndices.size() * sizeof(uint32_t));
				vbBvhIndices_->Unmap(0, nullptr);
			}
		}
	}
}

void Model::UpdateNodeBounds(uint32_t nodeIdx) {
	BVHNode& node = data_.bvhNodes[nodeIdx];
	node.min = {FLT_MAX, FLT_MAX, FLT_MAX};
	node.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

	for (uint32_t i = 0; i < node.triangleCount; ++i) {
		uint32_t triIdx = data_.bvhIndices[node.firstTriangle + i];
		for (int v = 0; v < 3; ++v) {
			const auto& pos = data_.vertices[data_.indices[triIdx * 3 + v]].position;
			node.min.x = (std::min)(node.min.x, pos.x);
			node.min.y = (std::min)(node.min.y, pos.y);
			node.min.z = (std::min)(node.min.z, pos.z);
			node.max.x = (std::max)(node.max.x, pos.x);
			node.max.y = (std::max)(node.max.y, pos.y);
			node.max.z = (std::max)(node.max.z, pos.z);
		}
	}
}

void Model::SubdivideBVH(uint32_t nodeIdx) {
	// ノードの参照を取得（注意：bvhNodesの再確保により無効になる可能性があるため、分割後に再取得する）
	if (data_.bvhNodes[nodeIdx].triangleCount <= 4) return;

	// 分割軸の決定 (最も長い軸を選択)
	Vector3 extent = data_.bvhNodes[nodeIdx].max - data_.bvhNodes[nodeIdx].min;
	int axis = 0;
	if (extent.y > extent.x) axis = 1;
	if (extent.z > (axis == 0 ? extent.x : extent.y)) axis = 2;

	float splitPos = ((axis == 0 ? data_.bvhNodes[nodeIdx].min.x : (axis == 1 ? data_.bvhNodes[nodeIdx].min.y : data_.bvhNodes[nodeIdx].min.z)) +
					  (axis == 0 ? data_.bvhNodes[nodeIdx].max.x : (axis == 1 ? data_.bvhNodes[nodeIdx].max.y : data_.bvhNodes[nodeIdx].max.z))) * 0.5f;

	// 三角形を左右に分ける (in-place partitioning)
	int i = data_.bvhNodes[nodeIdx].firstTriangle;
	int j = data_.bvhNodes[nodeIdx].firstTriangle + data_.bvhNodes[nodeIdx].triangleCount - 1;
	while (i <= j) {
		uint32_t triIdx = data_.bvhIndices[i];
		Vector3 center = (Vector3{data_.vertices[data_.indices[triIdx * 3]].position.x, data_.vertices[data_.indices[triIdx * 3]].position.y, data_.vertices[data_.indices[triIdx * 3]].position.z} +
						  Vector3{data_.vertices[data_.indices[triIdx * 3 + 1]].position.x, data_.vertices[data_.indices[triIdx * 3 + 1]].position.y, data_.vertices[data_.indices[triIdx * 3 + 1]].position.z} +
						  Vector3{data_.vertices[data_.indices[triIdx * 3 + 2]].position.x, data_.vertices[data_.indices[triIdx * 3 + 2]].position.y, data_.vertices[data_.indices[triIdx * 3 + 2]].position.z}) / 3.0f;
		
		float val = (axis == 0 ? center.x : (axis == 1 ? center.y : center.z));
		if (val < splitPos) i++;
		else {
			std::swap(data_.bvhIndices[i], data_.bvhIndices[j]);
			j--;
		}
	}

	uint32_t leftCount = i - data_.bvhNodes[nodeIdx].firstTriangle;
	if (leftCount == 0 || leftCount == data_.bvhNodes[nodeIdx].triangleCount) return;

	uint32_t firstTri = data_.bvhNodes[nodeIdx].firstTriangle;
	uint32_t triCount = data_.bvhNodes[nodeIdx].triangleCount;

	// 子ノードを作成
	int leftIdx = (int)data_.bvhNodes.size();
	BVHNode left{};
	left.firstTriangle = firstTri;
	left.triangleCount = leftCount;
	data_.bvhNodes.push_back(left);

	int rightIdx = (int)data_.bvhNodes.size();
	BVHNode right{};
	right.firstTriangle = i;
	right.triangleCount = triCount - leftCount;
	data_.bvhNodes.push_back(right);

	// 親ノードを更新
	data_.bvhNodes[nodeIdx].leftChild = leftIdx;
	data_.bvhNodes[nodeIdx].rightChild = rightIdx;
	data_.bvhNodes[nodeIdx].triangleCount = 0;

	UpdateNodeBounds(leftIdx);
	UpdateNodeBounds(rightIdx);

	SubdivideBVH(leftIdx);
	SubdivideBVH(rightIdx);
}

bool Model::RayIntersectsAABB(const DirectX::XMVECTOR& rayOrig, const DirectX::XMVECTOR& rayDir, const Vector3& bmin, const Vector3& bmax, float& tOut) {
	XMFLOAT3 orig; XMStoreFloat3(&orig, rayOrig);
	XMFLOAT3 dir; XMStoreFloat3(&dir, rayDir);
	float tmin = -FLT_MAX, tmax = FLT_MAX;
	float mn[3] = {bmin.x, bmin.y, bmin.z};
	float mx[3] = {bmax.x, bmax.y, bmax.z};
	float o[3] = {orig.x, orig.y, orig.z};
	float d[3] = {dir.x, dir.y, dir.z};
	for (int i = 0; i < 3; ++i) {
		if (std::fabs(d[i]) < 1e-8f) {
			if (o[i] < mn[i] || o[i] > mx[i]) return false;
		} else {
			float t1 = (mn[i] - o[i]) / d[i];
			float t2 = (mx[i] - o[i]) / d[i];
			if (t1 > t2) std::swap(t1, t2);
			if (t1 > tmin) tmin = t1;
			if (t2 < tmax) tmax = t2;
			if (tmin > tmax) return false;
		}
	}
	if (tmax < 0) return false;
	tOut = tmin > 0 ? tmin : tmax;
	return true;
}

bool Model::RayCast(const DirectX::XMVECTOR& rayOrig, const DirectX::XMVECTOR& rayDir, const Matrix4x4& worldTransform, float& outDist, Vector3& outHitPoint) const {
	if (data_.bvhNodes.empty() || data_.indices.empty()) return false;

	// XMMATRIXに変換
	DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&worldTransform));
	DirectX::XMMATRIX invWorld = DirectX::XMMatrixInverse(nullptr, worldMat);

	// ワールド空間のRayをローカル空間に変換
	DirectX::XMVECTOR localOrig = DirectX::XMVector3TransformCoord(rayOrig, invWorld);
	DirectX::XMVECTOR localDir = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(rayDir, invWorld));

	float minDistLocal = FLT_MAX;
	bool hit = false;
	
	std::vector<uint32_t> stack;
	stack.push_back(0);

	while (!stack.empty()) {
		uint32_t nodeIdx = stack.back();
		stack.pop_back();

		const BVHNode& node = data_.bvhNodes[nodeIdx];
		float tBox;
		if (!RayIntersectsAABB(localOrig, localDir, node.min, node.max, tBox)) {
			continue;
		}
		if (tBox >= minDistLocal) {
			continue; // これ以上近い結果は望めない
		}

		if (node.leftChild == -1) {
			// 葉ノード: 三角形と交差判定
			for (uint32_t i = 0; i < node.triangleCount; ++i) {
				uint32_t triIdx = data_.bvhIndices[node.firstTriangle + i];
				
				DirectX::XMVECTOR v0 = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data_.vertices[data_.indices[triIdx * 3]].position));
				DirectX::XMVECTOR v1 = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data_.vertices[data_.indices[triIdx * 3 + 1]].position));
				DirectX::XMVECTOR v2 = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data_.vertices[data_.indices[triIdx * 3 + 2]].position));

				// DirectXMathのTriangleTests機能を使う (bvhで代用)
				// Möller–Trumbore intersection algorithm
				DirectX::XMVECTOR edge1 = DirectX::XMVectorSubtract(v1, v0);
				DirectX::XMVECTOR edge2 = DirectX::XMVectorSubtract(v2, v0);
				DirectX::XMVECTOR h = DirectX::XMVector3Cross(localDir, edge2);
				float a = DirectX::XMVectorGetX(DirectX::XMVector3Dot(edge1, h));

				if (a > -1e-6f && a < 1e-6f) continue; // 平行

				float f = 1.0f / a;
				DirectX::XMVECTOR s = DirectX::XMVectorSubtract(localOrig, v0);
				float u = f * DirectX::XMVectorGetX(DirectX::XMVector3Dot(s, h));
				if (u < 0.0f || u > 1.0f) continue;

				DirectX::XMVECTOR q = DirectX::XMVector3Cross(s, edge1);
				float v = f * DirectX::XMVectorGetX(DirectX::XMVector3Dot(localDir, q));
				if (v < 0.0f || u + v > 1.0f) continue;

				float t = f * DirectX::XMVectorGetX(DirectX::XMVector3Dot(edge2, q));
				if (t > 1e-6f && t < minDistLocal) {
					minDistLocal = t;
					hit = true;
				}
			}
		} else {
			// 子ノードへ（近い方から処理するとより効率的だが、今回はスタックにそのまま積む）
			stack.push_back(node.leftChild);
			stack.push_back(node.rightChild);
		}
	}

	if (hit) {
		// ローカル上の交差座標を計算
		DirectX::XMVECTOR hitLocal = DirectX::XMVectorAdd(localOrig, DirectX::XMVectorScale(localDir, minDistLocal));
		// ワールド座標に戻す
		DirectX::XMVECTOR hitWorld = DirectX::XMVector3TransformCoord(hitLocal, worldMat);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&outHitPoint), hitWorld);
		// 距離を計算 (ワールド空間上)
		outDist = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(hitWorld, rayOrig)));
		return true;
	}

	return false;
}

void Model::UpdateSkeleton(const Node& node, const Matrix4x4& parentMatrix, const Animation& animation, float time, const Animation* prevAnimation, float prevTime, float blendFactor, std::vector<Matrix4x4>& skeletonParams, std::vector<DebugBone>* debugBones) {
	Matrix4x4 localTransform = node.localMatrix;

	Vector3 trans = node.transform.translate;
	XMFLOAT4 rot = node.transform.rotate;
	Vector3 scale = node.transform.scale;

	auto it = animation.nodeAnimations.find(node.name);
	if (it != animation.nodeAnimations.end()) {
		const NodeAnimation& nodeAnim = it->second;
		trans = CalculateTranslation(nodeAnim.translations, time, node.transform.translate);
		rot = CalculateRotation(nodeAnim.rotations, time, node.transform.rotate);
		scale = CalculateScale(nodeAnim.scales, time, node.transform.scale);
	}

	if (prevAnimation && blendFactor > 0.0f) {
		Vector3 pTrans = node.transform.translate;
		XMFLOAT4 pRot = node.transform.rotate;
		Vector3 pScale = node.transform.scale;

		auto prevIt = prevAnimation->nodeAnimations.find(node.name);
		if (prevIt != prevAnimation->nodeAnimations.end()) {
			const NodeAnimation& prevNodeAnim = prevIt->second;
			pTrans = CalculateTranslation(prevNodeAnim.translations, prevTime, node.transform.translate);
			pRot = CalculateRotation(prevNodeAnim.rotations, prevTime, node.transform.rotate);
			pScale = CalculateScale(prevNodeAnim.scales, prevTime, node.transform.scale);
		}

		// Blend
		DirectX::XMVECTOR t1 = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&pTrans));
		DirectX::XMVECTOR t2 = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&trans));
		DirectX::XMVECTOR tBlend = DirectX::XMVectorLerp(t1, t2, blendFactor);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&trans), tBlend);

		DirectX::XMVECTOR r1 = DirectX::XMLoadFloat4(&pRot);
		DirectX::XMVECTOR r2 = DirectX::XMLoadFloat4(&rot);
		DirectX::XMVECTOR rBlend = DirectX::XMQuaternionSlerp(r1, r2, blendFactor);
		DirectX::XMStoreFloat4(&rot, rBlend);

		DirectX::XMVECTOR s1 = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&pScale));
		DirectX::XMVECTOR s2 = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&scale));
		DirectX::XMVECTOR sBlend = DirectX::XMVectorLerp(s1, s2, blendFactor);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&scale), sBlend);
	}

	// 少なくともどちらか一方にアニメーション、あるいはブレンドがあれば行列を更新
	// 全くアニメーションがない場合は localMatrix をそのまま使う
	if (it != animation.nodeAnimations.end() || (prevAnimation && blendFactor > 0.0f)) {
		DirectX::XMMATRIX S = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z);
		DirectX::XMMATRIX R = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&rot));
		DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(trans.x, trans.y, trans.z);
		localTransform = XMToM4(S * R * T);
	}

	DirectX::XMMATRIX localMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&localTransform));
	DirectX::XMMATRIX parentMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&parentMatrix));
	Matrix4x4 globalTransform = XMToM4(localMat * parentMat);

	if (debugBones) {
		Vector3 pParent = { parentMatrix.m[3][0], parentMatrix.m[3][1], parentMatrix.m[3][2] };
		if (node.name != "RootNode") {
			debugBones->push_back({ node.name, globalTransform, pParent });
		}
	}

	auto boneIt = data_.boneMapping.find(node.name);
	if (boneIt != data_.boneMapping.end()) {
		int boneIndex = boneIt->second;
		if (boneIndex < (int)skeletonParams.size()) {
			DirectX::XMMATRIX offsetMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&data_.bones[boneIndex].offsetMatrix));
			DirectX::XMMATRIX globalMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&globalTransform));
			skeletonParams[boneIndex] = XMToM4(offsetMat * globalMat);
		}
	}

	for (const Node& child : node.children) {
		UpdateSkeleton(child, globalTransform, animation, time, prevAnimation, prevTime, blendFactor, skeletonParams, debugBones);
	}
}

void Skeleton::Update() {
	for (Joint& joint : joints) {
		joint.localMatrix = joint.transform.ToMatrix();
		if (joint.parent) {
			joint.skeletonSpaceMatrix = Matrix4x4::Multiply(joint.localMatrix, joints[*joint.parent].skeletonSpaceMatrix);
		} else {
			joint.skeletonSpaceMatrix = joint.localMatrix;
		}
	}
}

} // namespace Engine
