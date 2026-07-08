struct VSOut {
    float4 svpos : SV_POSITION;
    float4 color : COLOR0;
};
float4 main(VSOut i) : SV_TARGET {
    return i.color;
}
