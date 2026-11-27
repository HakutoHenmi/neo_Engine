// 有限のシャドウマップは、境界で影なしのライティングへなめらかに移らなければならない。
// 範囲の外ですぐに完全に明るくすると、まっすぐな帯ができる。
float ShadowFrustumWeight(float3 uvDepth) {
    float3 edge=min(uvDepth,1-uvDepth);
    return smoothstep(0,.08,min(edge.x,min(edge.y,edge.z)));
}
