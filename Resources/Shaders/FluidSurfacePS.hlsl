// 相 (phase) ごとに単独で再構成する。x = 視点からの深度, y = 光学的な密度。
Texture2D<float4> rawColor : register(t0);
Texture2D<float> rawDepth : register(t1);
Texture2D<float2> previousSurface : register(t2);
cbuffer CBFrame : register(b0) {
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float3 gCamPos; float gTime;
};
cbuffer FilterSettings : register(b1) {
    float2 axis; float stride; float initialize;
};

float2 ReadSurface(int2 pixel, int2 size) {
    if (any(pixel < 0) || any(pixel >= size)) return 0;
    if (initialize > 0.5f) {
        // 2x2 のまとまりごとに一貫して縮小する。手前の深度を、離れた奥の面や
        // 消去した深度の目印と平均してはいけない。
        uint rawWidth, rawHeight;
        rawDepth.GetDimensions(rawWidth, rawHeight);
        float depths[4]; float densities[4];
        float nearest = 1.0e20f;
        float pixelCount = 0;
        [unroll] for (int i = 0; i < 4; ++i) {
            int2 p = pixel * 2 + int2(i & 1, i >> 1);
            depths[i] = 1.0e20f; densities[i] = 0;
            if (any(p >= int2(rawWidth, rawHeight))) continue;
            pixelCount += 1;
            float key = rawDepth.Load(int3(p, 0));
            if (key >= 1.0e19f) continue;
            depths[i] = floor(key * 0.25f) / 1024.0f;
            densities[i] = rawColor.Load(int3(p, 0)).a;
            nearest = min(nearest, depths[i]);
        }
        if (nearest >= 1.0e19f) return 0;
        float density = 0;
        [unroll] for (int j = 0; j < 4; ++j) {
            if (abs(depths[j] - nearest) <= 0.85f) density += densities[j];
        }
        return float2(nearest, density / max(pixelCount, 1.0f));
    }
    return previousSurface.Load(int3(pixel, 0));
}

float2 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    uint width, height;
    rawDepth.GetDimensions(width, height);
    width = (width + 1) / 2;
    height = (height + 1) / 2;
    int2 size = int2(width, height);
    int2 pixel = int2(position.xy);
    float2 center = ReadSurface(pixel, size);
    // 内側の小さな隙間は閉じるが、同じ深度で両側に支えがあることを条件にする。
    // 孤立したしずくを、関係の無い空きへ膨らませてはいけない。
    if (center.x <= 0.0f) {
        float2 left = 0, right = 0;
        int leftDistance = 0, rightDistance = 0;
        [unroll] for (int gap = 1; gap <= 4; ++gap) {
            if (left.x <= 0) { left = ReadSurface(pixel - int2(axis) * gap, size); leftDistance = gap; }
            if (right.x <= 0) { right = ReadSurface(pixel + int2(axis) * gap, size); rightDistance = gap; }
        }
        if (left.x <= 0 || right.x <= 0 || abs(left.x - right.x) > 0.5f) return 0;
        float gapWorld = (leftDistance + rightDistance) * 2.0f * max(left.x, right.x) /
                         (abs(gProj[1][1]) * height);
        if (gapWorld > 0.35f) return 0;
        center = float2(lerp(left.x, right.x, float(leftDistance) / (leftDistance + rightDistance)), 0);
    }
    float radiusPixels = 0.95f * abs(gProj[1][1]) * height / (2.0f * max(center.x, 0.95f));
    // 整数の決まったずれと、世界空間の連続的な重み: 深度に応じた間隔を丸めると、
    // 間隔が 1 変わる所ごとに輪ができる。
    int stepPixels = (int)stride;
    float depthSum = 0, depthWeights = 0, densitySum = 0, densityWeights = 0;
    [unroll] for (int tap = -4; tap <= 4; ++tap) {
        int2 samplePixel = pixel + int2(axis) * tap * stepPixels;
        if (any(samplePixel < 0) || any(samplePixel >= size)) continue;
        float2 sampleValue = ReadSurface(samplePixel, size);
        float relativeOffset = tap * stride / max(radiusPixels, 1.0f);
        float spatial = exp(-float(tap * tap) / 8.0f - relativeOffset * relativeOffset * 0.5f);
        if (sampleValue.x <= 0) {
            // 空きは覆う割合を下げる。1 個だけ残った粒子を正規化して、
            // 輪郭で完全な面にしてはいけない。
            densityWeights += spatial;
            continue;
        }
        float delta = abs(sampleValue.x - center.x);
        if (delta > 0.85f) continue;
        // しきい値に近づくほど重みを連続的に 0 にし、
        // 深度差がしきい値をまたいだときの輪状の境界を防ぐ。
        float range = saturate(1.0f - delta * delta / (0.85f * 0.85f));
        float weight = spatial * range * range;
        depthSum += sampleValue.x * weight;
        depthWeights += weight;
        densitySum += sampleValue.y * weight;
        densityWeights += weight;
    }
    if (depthWeights <= 0.0001f) return 0;
    return float2(depthSum / depthWeights, densitySum / max(densityWeights, 0.0001f));
}
