struct PSOut {
    float depth : SV_TARGET0;
};

uint GetFluidPhase(float type) {
    if (type < 0.5f || (type > 2.5f && type < 3.5f)) return 0U;
    if (type > 1.5f && type < 2.5f) return 1U;
    return 2U;
}

float EncodeSurfaceKey(float viewDepth, float type) {
    // 量子化した視点からの深度を主のキー、材質の相 (phase) を従のキーにする。
    // そのため MIN の合成で、手前の材質が 1 つにまとまって決まる。
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

    // 透明なビルボードの縁が深度を書き込み、周囲の水を遮らないよう、
    // 密度カーネルへの寄与が小さいフラグメントを除外する。
    float radial = saturate(1.0f - sqrt(distSq) * 2.0f);
    float kernel = radial * radial * (3.0f - 2.0f * radial);
    if (kernel * color.a < 0.035f) {
        discard;
    }

    // このパスは一番近い視点空間の面を保存する。厚み / 密度の積み上げと
    // 分けておくことで、別の深度にある無関係な粒子が
    // 同じ再構成した面に寄与しないようにする。
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
