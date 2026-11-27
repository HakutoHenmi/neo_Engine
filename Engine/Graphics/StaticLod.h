#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace Engine {
struct LodSubset {
	uint32_t first = 0, count = 0;
	int material = -1;
};
template<class Vertex> struct ReducedStaticMesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<LodSubset> subsets;
};
} // namespace Engine
#include "TopologyLod.h"
namespace Engine {
// 固い面は形のつながりを保つ辺の縮約で減らし、草は交差した房ごとにだけ
// 間引く。どちらも残った面を勝手に消すことはない。
template<class Vertex>
ReducedStaticMesh<Vertex> ReduceStaticMesh(
    const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, std::vector<LodSubset> subsets, int grid,
    bool grassCards = false) {
	ReducedStaticMesh<Vertex> out;
	if (vertices.empty() || indices.size() < 3 || grid < 1)
		return out;
	if (subsets.empty())
		subsets.push_back({0, uint32_t(indices.size()), -1});
	if (!grassCards) {
		ReduceConnectedSurface(vertices, indices, subsets, grid, out);
		return out;
	}
	for (const auto& sub : subsets) {
		if (size_t(sub.first) + sub.count > indices.size() || sub.count % 3 != 0)
			continue;
		uint32_t start = uint32_t(out.indices.size());
		if (grassCards) {
			// インデックス 12 個が、生成した草パッチの交差した房 1 つ分になる。
			std::map<uint32_t, uint32_t> remap;
			uint32_t stride = uint32_t(grid);
			for (uint32_t i = 0; i < sub.count; ++i) {
				if ((i / 12) % stride != 0)
					continue;
				uint32_t source = indices[sub.first + i];
				if (source >= vertices.size())
					continue;
				auto it = remap.find(source);
				if (it == remap.end()) {
					uint32_t id = uint32_t(out.vertices.size());
					out.vertices.push_back(vertices[source]);
					it = remap.emplace(source, id).first;
				}
				out.indices.push_back(it->second);
			}
		}
		uint32_t count = uint32_t(out.indices.size()) - start;
		if (count)
			out.subsets.push_back({start, count, sub.material});
	}
	// 使われないクラスタを詰めて、GPU のバッファの頂点も少なくする。
	std::vector<uint32_t> compact(out.vertices.size(), UINT32_MAX);
	std::vector<Vertex> used;
	for (auto& id : out.indices) {
		if (compact[id] == UINT32_MAX) {
			compact[id] = uint32_t(used.size());
			used.push_back(out.vertices[id]);
		}
		id = compact[id];
	}
	out.vertices = std::move(used);
	for (auto& v : out.vertices) {
		float length = std::sqrt(v.normal.x * v.normal.x + v.normal.y * v.normal.y + v.normal.z * v.normal.z);
		if (length > .00001f) {
			v.normal.x /= length;
			v.normal.y /= length;
			v.normal.z /= length;
		}
	}
	return out;
}
// 小物は約 18 m より遠いと数十ピクセルの高さしかないので早めに切り替える。大きな物は大きさに従う
// 法線が主にどの軸方向 (6 方向のうちどれ、0..5) を向いているか
template<class Normal> uint32_t FacingOf(const Normal& n) {
	const float a[3] = {std::abs(n.x), std::abs(n.y), std::abs(n.z)};
	uint32_t axis = a[0] >= a[1] && a[0] >= a[2] ? 0 : a[1] >= a[2] ? 1 : 2;
	const float value = axis == 0 ? n.x : axis == 1 ? n.y : n.z;
	return axis * 2 + (value < 0 ? 1 : 0);
}
// 遠景用のレベル: 同じ格子セル・マテリアル・向きの頂点を 1 つの平均の頂点にまとめ、つぶれた
// 三角形を捨てる。継ぎ目や形のつながりは無視するので、数十ピクセルの高さの物にだけ使う
template<class Vertex>
ReducedStaticMesh<Vertex> ClusterStaticMesh(
    const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, std::vector<LodSubset> subsets,
    int grid) {
	ReducedStaticMesh<Vertex> out;
	if (vertices.empty() || indices.size() < 3 || grid < 1 || grid > 255)
		return out;
	if (subsets.empty())
		subsets.push_back({0, uint32_t(indices.size()), -1});
	float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
	for (const auto& v : vertices) {
		const float p[3] = {v.position.x, v.position.y, v.position.z};
		for (int a = 0; a < 3; ++a) {
			lo[a] = (std::min)(lo[a], p[a]);
			hi[a] = (std::max)(hi[a], p[a]);
		}
	}
	float extent = (std::max)({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1e-6f}), cell = extent / grid;
	std::unordered_map<uint64_t, uint32_t> clusters;
	std::vector<std::array<float, 7>> sums; // 位置 xyz、法線 xyz、個数
	auto clusterOf = [&](uint32_t source, uint32_t subset) {
		const auto& v = vertices[source];
		const float p[3] = {v.position.x, v.position.y, v.position.z};
		uint64_t key = (uint64_t(subset) << 27) | (uint64_t(FacingOf(v.normal)) << 24);
		for (int a = 0; a < 3; ++a)
			key |= uint64_t((std::min)(uint32_t((p[a] - lo[a]) / cell), uint32_t(grid - 1))) << (16 - 8 * a);
		auto it = clusters.find(key);
		if (it == clusters.end()) {
			it = clusters.emplace(key, uint32_t(out.vertices.size())).first;
			out.vertices.push_back(v);
			sums.push_back({});
		}
		auto& s = sums[it->second];
		s[0] += p[0];
		s[1] += p[1];
		s[2] += p[2];
		s[3] += v.normal.x;
		s[4] += v.normal.y;
		s[5] += v.normal.z;
		s[6] += 1;
		return it->second;
	};
	for (uint32_t si = 0; si < subsets.size(); ++si) {
		const auto& sub = subsets[si];
		if (size_t(sub.first) + sub.count > indices.size() || sub.count % 3 != 0)
			continue;
		uint32_t start = uint32_t(out.indices.size());
		for (uint32_t i = 0; i < sub.count; i += 3) {
			uint32_t t[3];
			bool valid = true;
			for (int k = 0; k < 3; ++k) {
				uint32_t source = indices[sub.first + i + k];
				if (source >= vertices.size()) {
					valid = false;
					break;
				}
				t[k] = clusterOf(source, si);
			}
			if (valid && t[0] != t[1] && t[1] != t[2] && t[0] != t[2])
				out.indices.insert(out.indices.end(), {t[0], t[1], t[2]});
		}
		uint32_t count = uint32_t(out.indices.size()) - start;
		if (count)
			out.subsets.push_back({start, count, sub.material});
	}
	for (size_t i = 0; i < out.vertices.size(); ++i) {
		auto& v = out.vertices[i];
		const auto& s = sums[i];
		v.position.x = s[0] / s[6];
		v.position.y = s[1] / s[6];
		v.position.z = s[2] / s[6];
		float length = std::sqrt(s[3] * s[3] + s[4] * s[4] + s[5] * s[5]);
		if (length > .00001f) {
			v.normal.x = s[3] / length;
			v.normal.y = s[4] / length;
			v.normal.z = s[5] / length;
		}
	}
	return out;
}
// 距離の約 1/200 より小さい小物は数ピクセルしか覆わないので、まったく描かない
inline bool TooSmallToDraw(float distance, float radius) {
	return distance > radius * 200;
}
inline int DistanceLodLevel(float distance, float radius) {
	return distance > (std::max)(45.f, radius * 6) ? 2 : distance > (std::max)(18.f, radius * 3) ? 1 : 0;
}

// 手作りの曲がった葉はつながった帯であり、12 インデックスで生成した交差カードではない。
// つながった葉ごと残すか消すかを決め、残った帯の UV・法線・三角形は
// すべて保つ。
template<class Vertex>
ReducedStaticMesh<Vertex> ReduceLeafCards(
    const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, std::vector<LodSubset> subsets,
    uint32_t stride) {
	ReducedStaticMesh<Vertex> out;
	if (vertices.empty() || indices.size() < 3 || stride < 1)
		return out;
	for (auto id : indices)
		if (id >= vertices.size())
			return out;
	if (subsets.empty())
		subsets.push_back({0, uint32_t(indices.size()), -1});
	std::vector<uint32_t> parent(vertices.size());
	for (uint32_t i = 0; i < parent.size(); ++i)
		parent[i] = i;
	auto root = [&](uint32_t id) {
		while (parent[id] != id) {
			parent[id] = parent[parent[id]];
			id = parent[id];
		}
		return id;
	};
	std::map<std::array<float, 5>, uint32_t> welded;
	for (uint32_t i = 0; i < vertices.size(); ++i) {
		const auto& v = vertices[i];
		auto inserted = welded.emplace(
		    std::array<float, 5>{v.position.x, v.position.y, v.position.z, v.texcoord.x, v.texcoord.y}, i);
		if (!inserted.second)
			parent[i] = inserted.first->second;
	}
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		uint32_t a = root(indices[i]);
		for (int k = 1; k < 3; ++k) {
			uint32_t b = root(indices[i + k]);
			if (a != b) {
				parent[b] = a;
			}
		}
	}
	std::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
	for (const auto& sub : subsets) {
		if (size_t(sub.first) + sub.count > indices.size() || sub.count % 3)
			continue;
		uint32_t start = uint32_t(out.indices.size());
		std::map<uint32_t, bool> kept;
		uint32_t ordinal = 0;
		for (uint32_t i = 0; i < sub.count; i += 3) {
			uint32_t component = root(indices[sub.first + i]);
			auto found = kept.find(component);
			if (found == kept.end()) {
				found = kept.emplace(component, ordinal % stride == 0).first;
				++ordinal;
			}
			if (!found->second)
				continue;
			for (int k = 0; k < 3; ++k) {
				uint32_t id = indices[sub.first + i + k];
				if (remap[id] == UINT32_MAX) {
					remap[id] = uint32_t(out.vertices.size());
					out.vertices.push_back(vertices[id]);
				}
				out.indices.push_back(remap[id]);
			}
		}
		uint32_t count = uint32_t(out.indices.size()) - start;
		if (count)
			out.subsets.push_back({start, count, sub.material});
	}
	return out;
}
} // namespace Engine
