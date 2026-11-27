// CalcDensity は粒子自身も含む。0.4 ワールド単位の SPH の影響範囲に同じ相 (phase) の
// 隣が無い粒子は、ちょうどこの密度になる。
// この式は FluidSimCS.hlsl の poly6 の定数と一致させておく。
float FluidRenderSupport(float density) {
    const float selfDensity = (315.0f / (64.0f * 3.1415926535f * 0.000262144f)) *
                              0.16f * 0.16f * 0.16f;
    // 隣が半径をまたいだときに急に出たり消えたりせず、弱い支えは薄くしていく。
    // 影響するのは描画だけで、孤立した粒子もシミュレーションは続ける。
    return smoothstep(0.001f, 0.5f, density - selfDensity);
}
