// GraphicsUI.h
// ポーズ中に開くグラフィック設定画面 (ブルームなどの効果の ON/OFF・DLSS・レイトレーシング・露出・プリセット)。
// 文字は ui_text.json の graphics、プリセットと既定の強さは graphics.json に置く。
#pragma once
// 共通の UI 描画 (Canvas・色・ボタン)
#include "ScaleUI.h"
#include <array>
// UI 関連はすべて Game::UI 名前空間に置く
namespace Game::UI {
// ボタンの位置 (1280x720 の UI 座標)
inline constexpr Rect GraphicsButton{1046,38,190,46},GraphicsBack{1104,36,136,42};
// DLSS の ON / OFF
inline constexpr Rect DlssQualityButton{650,208,288,40},DlssOffButton{942,208,288,40};
// RT の影・反射・間接光の ON / OFF
inline constexpr Rect RtOnButton{1042,286,92,38},RtOffButton{1138,286,92,38};
inline constexpr Rect RtReflectionOn{1042,336,92,38},RtReflectionOff{1138,336,92,38};
inline constexpr Rect RtIndirectOn{1042,386,92,38},RtIndirectOff{1138,386,92,38};
// 露出の増減
inline constexpr Rect ExposureDown{810,442,44,38},ExposureUp{1186,442,44,38};
// プリセット (低・標準・高): 巻尺の上の帯
inline constexpr Rect PresetLow{170,564,210,43},PresetBalanced{600,564,210,43},PresetHigh{1040,564,210,43};
// 効果の i 行目の位置
inline Rect GraphicsRow(int i){return {48,176+float(i)*54,520,50};}
// graphics.json の中身 (効果を ON にしたときの強さ・プリセット・露出の範囲)
struct GraphicsData {
    std::array<float,6> effectDefaults{};
    Engine::Renderer::GraphicsSettings low,balanced,high;
    float exposureMin=0,exposureMax=0,exposureStep=0;
};
// graphics.json を読み込む (最初に使われたときに 1 回だけ)
inline const GraphicsData& GraphicsPresets(){
    static const GraphicsData data=[]{
        const auto& json=Data::Load("graphics.json");
        GraphicsData out;
        out.effectDefaults=Data::ReadArray<float,6>(Data::Require(json,"effectDefaults","graphics.json"),"graphics.json effectDefaults");
        // プリセットに書かれた項目だけを、エンジンの既定値に上書きする
        auto preset=[&](const char* name){
            Engine::Renderer::GraphicsSettings settings{};
            const auto& p=Data::Require(Data::Require(json,"presets","graphics.json"),name,"graphics.json presets");
            settings.bloom=p.value("bloom",settings.bloom);settings.lensFlare=p.value("lensFlare",settings.lensFlare);
            settings.grading=p.value("grading",settings.grading);settings.ambientOcclusion=p.value("ambientOcclusion",settings.ambientOcclusion);
            settings.motionBlur=p.value("motionBlur",settings.motionBlur);settings.dof=p.value("dof",settings.dof);
            settings.exposure=p.value("exposure",settings.exposure);
            return settings;
        };
        out.low=preset("low");out.balanced=preset("balanced");out.high=preset("high");
        const auto& exposure=Data::Require(json,"exposure","graphics.json");
        out.exposureMin=exposure.at("min").get<float>();out.exposureMax=exposure.at("max").get<float>();out.exposureStep=exposure.at("step").get<float>();
        return out;
    }();
    return data;
}
// 入力の処理: 各行のクリックか 1〜6 キーで効果を切り替え、プリセット・露出・RT・DLSS のボタンを反映する。
// 戻る操作で true を返す (画面を閉じる)
inline bool UpdateGraphics(Canvas& ui,Engine::Renderer& renderer){
    // 今の設定を取り出し、6 つの効果の値をまとめて指す
    auto s=renderer.GetGraphicsSettings();float* values[]={&s.bloom,&s.lensFlare,&s.grading,&s.ambientOcclusion,&s.motionBlur,&s.dof};
    // 各効果を ON にしたときの既定値と、プリセットで変えたくない設定を覚える
    const auto& presets=GraphicsPresets();const auto& defaults=presets.effectDefaults;const bool quality=s.dlssQuality,shadows=s.rtShadows,reflection=s.rtReflections,indirect=s.rtIndirect;
    // 効果を ON なら 0、OFF なら既定値にする
    for(int i=0;i<6;++i)if(ui.Click(GraphicsRow(i))||Pressed(BYTE(DIK_1+i)))*values[i]=*values[i]>.001f?0:defaults[size_t(i)];
    // 低: 効果を弱めて重いものを切る
    if(ui.Click(PresetLow))s=presets.low;
    // 標準: 既定の設定
    if(ui.Click(PresetBalanced))s=presets.balanced;
    // 高: 効果を強める
    if(ui.Click(PresetHigh))s=presets.high;
    // プリセットを選んでも DLSS と RT の設定は変えない
    s.dlssQuality=quality;
    s.rtShadows=shadows;s.rtReflections=reflection;s.rtIndirect=indirect;
    // 露出は graphics.json の範囲と刻みで変える
    if(ui.Click(ExposureDown))s.exposure=std::max(presets.exposureMin,s.exposure-presets.exposureStep);
    if(ui.Click(ExposureUp))s.exposure=std::min(presets.exposureMax,s.exposure+presets.exposureStep);
    // ここまでの設定を描画に反映する
    renderer.ApplyGraphicsSettings(s);
    // RT と DLSS のボタンは描画側の関数で直接切り替える
    if(ui.Click(RtOnButton))renderer.SetRtShadows(true);
    if(ui.Click(RtOffButton))renderer.SetRtShadows(false);
    if(ui.Click(RtReflectionOn))renderer.SetRtReflections(true);
    if(ui.Click(RtReflectionOff))renderer.SetRtReflections(false);
    if(ui.Click(RtIndirectOn))renderer.SetRtIndirect(true);
    if(ui.Click(RtIndirectOff))renderer.SetRtIndirect(false);
    if(ui.Click(DlssQualityButton))renderer.SetDlssQuality(true);
    if(ui.Click(DlssOffButton))renderer.SetDlssQuality(false);
    // 戻るボタン・戻るキー・パッドの BACK・G・F7 で閉じる
    return ui.Click(GraphicsBack)||BackPressed()||PadPressed(XINPUT_GAMEPAD_BACK)||Pressed(DIK_G)||Pressed(DIK_F7);
}
// 切り替えの片側。選ばれている側は黒い台にする
inline void Segment(Canvas& ui,Rect b,const std::string& label,bool active){
    namespace S=Scale;const bool hover=ui.Hover(b);S::Focus(ui,b);
    ui.Fill(b,active?S::Black:WithAlpha(S::Black,hover?.26f:.14f));
    ui.Text(label,b.x+(b.w-ui.Width(label,24,S::Num))*.5f,b.y+(b.h-29)*.5f,24,active?S::Yellow:S::Black,S::Num);
}
// 今の設定に一致するプリセットの番号 (無ければ -1)
inline int ActivePreset(const Engine::Renderer::GraphicsSettings& s){
    const auto& p=GraphicsPresets();const Engine::Renderer::GraphicsSettings* list[]={&p.low,&p.balanced,&p.high};
    auto same=[](float a,float b){return std::abs(a-b)<.001f;};
    for(int i=0;i<3;++i){const auto& q=*list[i];if(same(s.bloom,q.bloom)&&same(s.lensFlare,q.lensFlare)&&same(s.grading,q.grading)&&same(s.ambientOcclusion,q.ambientOcclusion)&&same(s.motionBlur,q.motionBlur)&&same(s.dof,q.dof)&&same(s.exposure,q.exposure))return i;}
    return -1;
}
// 画面の描画
inline void DrawGraphics(Canvas& ui,const Engine::Renderer& renderer){
    namespace S=Scale;const auto& s=renderer.GetGraphicsSettings();
    S::Backdrop(ui);
    ui.Big(Str("graphics.heading"),48,18,86,S::Black,S::JpBig,S::JpBigBase);
    ui.Text(UsingPad()?Str("graphics.padHint"):Str("graphics.keyHint"),52,132,22,S::Black,S::Jp);
    ui.Track(GraphicsBack);S::Hint(ui,"keyboard_escape","xbox_button_b",Str("graphics.back"),1110,40);
    // 効果の一覧
    float values[]={s.bloom,s.lensFlare,s.grading,s.ambientOcclusion,s.motionBlur,s.dof};
    const char* names[]={"graphics.effect1","graphics.effect2","graphics.effect3","graphics.effect4","graphics.effect5","graphics.effect6"};
    for(int i=0;i<6;++i){const auto row=GraphicsRow(i);const bool on=values[i]>.001f,hover=ui.Hover(row);
        S::Focus(ui,row);if(hover)ui.Fill(row,WithAlpha(S::Black,.08f));
        ui.Fill({row.x,row.y+row.h-2,row.w,2},S::Black);
        ui.Text(std::to_string(i+1),row.x+2,row.y+2,30,S::Black,S::Num);ui.Text(Str(names[i]),row.x+44,row.y+10,26,S::Black,S::Jp);
        const Rect badge{row.x+row.w-92,row.y+6,92,34};ui.Fill(badge,on?S::Black:WithAlpha(S::Black,.14f));
        const std::string& label=Str(on?"graphics.on":"graphics.off");ui.Text(label,badge.x+(badge.w-ui.Width(label,24,S::Num))*.5f,badge.y+3,24,on?S::Yellow:S::Black,S::Num);}
    // DLSS の欄 (使えない場合は理由を表示する)
    ui.Text(Str("graphics.dlssTitle"),650,170,24,S::Black,S::Num);
    Segment(ui,DlssQualityButton,Str("graphics.dlssOn"),s.dlssQuality&&renderer.SupportsDlss());Segment(ui,DlssOffButton,Str("graphics.dlssOff"),!s.dlssQuality);
    std::string availability=Str(renderer.DlssUpscaling()?"graphics.dlssActive":renderer.DlssActive()?"graphics.dlaaActive":"graphics.dlssInactive");
    if(!renderer.SupportsDlss()){
        const auto reason=renderer.DlssStatus();availability=Str("graphics.dlssFailed");
        if(reason=="SDK DLLs missing")availability=Str("graphics.dlssMissing");
        else if(reason.find("signature")!=std::string::npos)availability=Str("graphics.dlssSignature");
        else if(reason.find("unsupported")!=std::string::npos)availability=Str("graphics.dlssUnsupported");
        else if(reason.find("evaluation failed")!=std::string::npos)availability=Str("graphics.dlssEvaluation");
    }
    ui.Text(Str("graphics.restartNote"),652,254,16,S::Red,S::Jp);ui.Text(availability,930,254,16,S::Black,S::Jp);
    // RT の欄 (影・反射・間接光)
    auto rtRow=[&](float y,const std::string& name,Rect on,Rect off,bool enabled,bool available){
        ui.Fill({650,y+44,580,2},S::Black);ui.Text(name,650,y+6,26,WithAlpha(S::Black,available?1.f:.45f),S::Jp);
        Segment(ui,on,Str("graphics.on"),enabled&&available);Segment(ui,off,Str("graphics.off"),!enabled);};
    rtRow(286,Str("graphics.rtShadows"),RtOnButton,RtOffButton,s.rtShadows,renderer.RtShadowsAvailable());
    rtRow(336,Str("graphics.rtReflections"),RtReflectionOn,RtReflectionOff,s.rtReflections,renderer.RtLightingAvailable());
    rtRow(386,Str("graphics.rtIndirect"),RtIndirectOn,RtIndirectOff,s.rtIndirect,renderer.RtLightingAvailable());
    if(!renderer.RtLightingAvailable())ui.Text(Str("graphics.rtUnavailable"),800,296,16,S::Black,S::Jp);
    // 露出: 小さな目盛りの上の赤い印
    ui.Text(Str("graphics.exposure"),650,448,26,S::Black,S::Jp);
    ui.Text(std::to_string(int(std::round(s.exposure*100)))+"%",712,434,38,S::Red,S::Num);
    Segment(ui,ExposureDown,"-",false);Segment(ui,ExposureUp,"+",false);
    const auto& presets=GraphicsPresets();const float left=866,width=306;
    for(int i=0;i<=40;++i){const bool big=i%10==0,mid=i%5==0;ui.Fill({left+float(i)*width/40,444,big?2.4f:1.4f,big?30.f:mid?22.f:12.f},S::Black);}
    const float ratio=std::clamp((s.exposure-presets.exposureMin)/(std::max)(presets.exposureMax-presets.exposureMin,.001f),0.f,1.f);
    ui.Fill({left+ratio*width-3,436,7,44},S::Red);
    ui.Text(Str("graphics.autoSave"),190,510,18,S::Black,S::Jp);
    // プリセット: 巻尺の上の帯。スライムが今の設定に座る
    const int active=ActivePreset(s);const Rect bars[3]={PresetLow,PresetBalanced,PresetHigh};const char* labels[3]={"graphics.presetLow","graphics.presetBalanced","graphics.presetHigh"};
    // どのプリセットとも違うときは、左の「カスタム」に座る
    S::Slime(ui,active>=0?bars[active].x+105:80,564,150);
    if(active<0){const Rect custom{12,564,136,43};ui.Fill(custom,S::Black);ui.Text(Str("graphics.presetCustom"),custom.x+14,custom.y+8,24,S::Yellow,S::Jp);}
    for(int i=0;i<3;++i){const bool on=i==active;S::Focus(ui,bars[i]);ui.Fill(bars[i],on?S::Black:WithAlpha(S::Black,ui.Hover(bars[i])?.26f:.14f));ui.Text(Str(labels[i]),bars[i].x+14,bars[i].y+8,24,on?S::Yellow:S::Black,S::Jp);}
    S::Tape(ui,{{5},{30},{100},{500},{5000},{12600}});
}
}
