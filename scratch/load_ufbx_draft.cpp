#include "../Engine/Model.h"
#include <ufbx.h>
#include "../Engine/PathUtils.h"
#include "../Engine/Renderer.h"
#include "../Engine/WindowDX.h"

namespace Engine {

static Matrix4x4 UfbxToMat4(const ufbx_matrix& m) {
	Matrix4x4 out;
	out.m[0][0] = (float)m.cols[0].x; out.m[0][1] = (float)-m.cols[0].y; out.m[0][2] = (float)-m.cols[0].z; out.m[0][3] = 0.0f;
	out.m[1][0] = (float)-m.cols[1].x; out.m[1][1] = (float)m.cols[1].y; out.m[1][2] = (float)m.cols[1].z; out.m[1][3] = 0.0f;
	out.m[2][0] = (float)-m.cols[2].x; out.m[2][1] = (float)m.cols[2].y; out.m[2][2] = (float)m.cols[2].z; out.m[2][3] = 0.0f;
	out.m[3][0] = (float)-m.cols[3].x; out.m[3][1] = (float)m.cols[3].y; out.m[3][2] = (float)m.cols[3].z; out.m[3][3] = 1.0f;
	return out;
}

static void ReadNodeHierarchyUfbx(Node& node, const ufbx_node* src) {
	node.name = src->name.data;

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

static void ReadAnimationUfbx(ModelData& modelData, const ufbx_scene* scene) {
	for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
		ufbx_anim_stack* srcAnim = scene->anim_stacks.data[i];
		Animation dstAnim;
		dstAnim.name = srcAnim->name.data;
		dstAnim.duration = (float)srcAnim->time_end * 25.0f; // Assumes 25fps if no ticks per sec
		dstAnim.ticksPerSecond = 25.0f;

        // UFBX bakes animations into layers/tracks. We can evaluate them or copy keys.
        // For simplicity, we sample at 25fps.
        float durationSecs = (float)(srcAnim->time_end - srcAnim->time_begin);
        if (durationSecs <= 0) continue;
        
        int numFrames = (int)(durationSecs * 25.0f) + 1;
        
        for (size_t n = 0; n < scene->nodes.count; ++n) {
            ufbx_node* node = scene->nodes.data[n];
            if (node->is_root) continue;
            
            NodeAnimation nodeAnim;
            for (int f = 0; f < numFrames; ++f) {
                float time = (float)f / 25.0f;
                ufbx_transform transform = ufbx_evaluate_transform(&scene->anim_evaluator, srcAnim->anim, node, time);
                
                float frameTime = (float)f;
                
                ufbx_vec3 v = transform.translation;
                nodeAnim.translations.push_back({frameTime, {(float)-v.x, (float)v.y, (float)v.z}});
                
                ufbx_quat q = transform.rotation;
                nodeAnim.rotations.push_back({frameTime, {(float)q.x, (float)-q.y, (float)-q.z, (float)q.w}});
                
                ufbx_vec3 s = transform.scale;
                nodeAnim.scales.push_back({frameTime, {(float)s.x, (float)s.y, (float)s.z}});
            }
            dstAnim.nodeAnimations[node->name.data] = nodeAnim;
        }
        
		modelData.animations.push_back(dstAnim);
	}
}

bool Model::LoadWithUFBX(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const std::string& objPath) {
    ufbx_load_opts opts = {0};
    opts.ignore_missing_external_files = true;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    
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
        ReadAnimationUfbx(data_, scene);
    }

    uint32_t vertexOffset = 0;
    for (size_t m = 0; m < scene->meshes.count; ++m) {
        ufbx_mesh* mesh = scene->meshes.data[m];
        
        // Split mesh into materials
        for (size_t part_idx = 0; part_idx < mesh->materials.count; ++part_idx) {
            ufbx_mesh_material part = mesh->materials.data[part_idx];
            if (part.num_triangles == 0) continue;
            
            MeshSubset subset;
            subset.indexStart = (uint32_t)data_.indices.size();
            // ufbx provides a material mapping but we will use part.material
            subset.materialIndex = -1;
            for (size_t mat_i = 0; mat_i < scene->materials.count; ++mat_i) {
                if (scene->materials.data[mat_i] == part.material) {
                    subset.materialIndex = (int)mat_i;
                    break;
                }
            }
            
            // Extract triangles
            for (size_t i = 0; i < part.num_triangles; ++i) {
                uint32_t tri_idx = part.num_triangles == 0 ? 0 : mesh->material_parts.data[part_idx].triangles.data[i];
                for (size_t j = 0; j < 3; ++j) {
                    uint32_t face_idx = tri_idx * 3 + j;
                    uint32_t index = mesh->vertex_indices.data[face_idx];
                    
                    VertexData v{};
                    ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, face_idx);
                    v.position = {(float)-pos.x, (float)pos.y, (float)pos.z, 1.0f};
                    
                    if (mesh->vertex_normal.exists) {
                        ufbx_vec3 norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, face_idx);
                        v.normal = {(float)-norm.x, (float)norm.y, (float)norm.z};
                    }
                    if (mesh->vertex_uv.exists) {
                        ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, face_idx);
                        v.texcoord = {(float)uv.x, (float)uv.y};
                    }
                    
                    // Skinning
                    if (mesh->skin_deformers.count > 0) {
                        ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];
                        ufbx_skin_vertex skin_vert = skin->vertices.data[index];
                        
                        int weightCount = 0;
                        for (size_t w = 0; w < skin_vert.num_weights && weightCount < 4; ++w) {
                            ufbx_skin_weight skin_weight = skin->weights.data[skin_vert.weight_begin + w];
                            ufbx_bone* bone = skin->bones.data[skin_weight.bone_index];
                            
                            std::string bName = bone->instances.count > 0 ? bone->instances.data[0]->name.data : "unnamed";
                            int bIdx = 0;
                            if (data_.boneMapping.find(bName) == data_.boneMapping.end()) {
                                bIdx = (int)data_.bones.size();
                                data_.bones.push_back({bName, UfbxToMat4(bone->geometry_to_bone), bIdx});
                                data_.boneMapping[bName] = bIdx;
                            } else {
                                bIdx = data_.boneMapping[bName];
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
        if (!tex && mat->features.diffuse.texture) tex = mat->features.diffuse.texture;
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
        } else {
            sub.materialIndex = -1;
        }
    }

    ufbx_free_scene(scene);

    vb_ = CreateBufferResource(device, sizeof(VertexData) * data_.vertices.size());
    void* vmap;
    vb_->Map(0, nullptr, &vmap);
    std::memcpy(vmap, data_.vertices.data(), sizeof(VertexData) * data_.vertices.size());
    vb_->Unmap(0, nullptr);
    vbv_ = {vb_->GetGPUVirtualAddress(), (UINT)(sizeof(VertexData) * data_.vertices.size()), sizeof(VertexData)};
    
    ib_ = CreateBufferResource(device, sizeof(uint32_t) * data_.indices.size());
    void* imap;
    ib_->Map(0, nullptr, &imap);
    std::memcpy(imap, data_.indices.data(), sizeof(uint32_t) * data_.indices.size());
    ib_->Unmap(0, nullptr);
    ibv_ = {ib_->GetGPUVirtualAddress(), (UINT)(sizeof(uint32_t) * data_.indices.size()), DXGI_FORMAT_R32_UINT};
    indexCount_ = (uint32_t)data_.indices.size();

    BuildBVH();
    return true;
}

} // namespace Engine
