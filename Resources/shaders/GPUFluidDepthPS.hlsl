struct PSOut {
    float depth : SV_TARGET0;
};

uint GetFluidPhase(float type) {
    if (type < 0.5f || (type > 2.5f && type < 3.5f)) return 0U;
    if (type > 1.5f && type < 2.5f) return 1U;
    return 2U;
}

float EncodeSurfaceKey(float viewDepth, float type) {
    // Quantized view depth is the major key and material phase is the minor
    // key. MIN blending therefore resolves one coherent front material.
    float quantizedDepth = floor(max(viewDepth, 0.0f) * 1024.0f);
    return quantizedDepth * 4.0f + float(GetFluidPhase(type));
}

PSOut RenderPhase(float4 svpos,
                  float2 uv,
                  float viewZ,
                  float4 color,
                  float type,
                  uint targetPhase) {
    if (GetFluidPhase(type) != targetPhase) {
        discard;
    }
    float2 centerOffset = uv - float2(0.5f, 0.5f);
    float distSq = dot(centerOffset, centerOffset);
    if (distSq > 0.25f || color.a < 0.01f) {
        discard;
    }

    // Do not let the visually transparent fringe of a billboard become an
    // occluder.  That fringe used to win the MIN depth test and erase water
    // around the player even though the player contributed almost no density.
    float radial = saturate(1.0f - sqrt(distSq) * 2.0f);
    float kernel = radial * radial * (3.0f - 2.0f * radial);
    if (kernel * color.a < 0.035f) {
        discard;
    }

    // This pass stores the nearest view-space surface.  Keeping this separate
    // from thickness/density accumulation prevents unrelated particles at a
    // different depth from contributing to the same reconstructed surface.
    float sphereRadius = 0.90f;
    if (type > 2.5f && type < 3.5f) {
        sphereRadius = 0.65f;
    } else if (type >= 0.5f) {
        sphereRadius = 0.72f;
    }

    float normalizedDistSq = distSq * 4.0f;
    float zOffset = sqrt(max(0.0f, 1.0f - normalizedDistSq)) * sphereRadius;

    PSOut output;
    output.depth = EncodeSurfaceKey(viewZ - zOffset, type);
    return output;
}

PSOut mainPhase0(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0,
                 float viewZ : TEXCOORD1, float4 color : COLOR0,
                 float type : TEXCOORD2) {
    return RenderPhase(svpos, uv, viewZ, color, type, 0U);
}

PSOut mainPhase1(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0,
                 float viewZ : TEXCOORD1, float4 color : COLOR0,
                 float type : TEXCOORD2) {
    return RenderPhase(svpos, uv, viewZ, color, type, 1U);
}

PSOut mainPhase2(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0,
                 float viewZ : TEXCOORD1, float4 color : COLOR0,
                 float type : TEXCOORD2) {
    return RenderPhase(svpos, uv, viewZ, color, type, 2U);
}
