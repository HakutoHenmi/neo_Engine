// A finite shadow map must blend to unshadowed lighting at its boundary.
// Returning fully lit immediately outside the volume creates a straight band.
float ShadowFrustumWeight(float3 uvDepth) {
    float3 edge=min(uvDepth,1-uvDepth);
    return smoothstep(0,.08,min(edge.x,min(edge.y,edge.z)));
}
