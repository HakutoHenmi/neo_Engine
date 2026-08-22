struct PSIn {
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float type : TEXCOORD1;
};

void main(PSIn input) {
    float2 centerOffset = input.uv - float2(0.5f, 0.5f);
    float distSq = dot(centerOffset, centerOffset);
    if (distSq > 0.25f) { discard; }

    bool isPlayerSlime = (input.type < 0.5f);
    bool isDecoySlime = (input.type > 1.5f && input.type < 2.5f);
    bool isLostPlayerSlime = (input.type > 2.5f && input.type < 3.5f);
    
    // スライム本体以外の「はぐれた水滴」は、メタボールの閾値未満で描画されない（透明になる）のに
    // 影だけが落ちてしまう不自然な見た目になるため、影を描画しない。
    bool isSlime = (isPlayerSlime || isDecoySlime);
    
    if (!isSlime) { 
        discard; 
    }
}