// GraphicsUI.h
// ポーズ中に開くグラフィック設定画面 (ブルームなどの効果の ON/OFF・DLSS・レイトレーシング・露出・プリセット)。
// 文字は ui_text.json の graphics、プリセットと既定の強さは graphics.json に置く。
#pragma once
// 共通の UI 描画 (Canvas・色・ボタン)
#include "GameUI.h"
#include <array>
// UI 関連はすべて Game::UI 名前空間に置く
namespace Game::UI {
// ボタンの位置 (1280x720 の UI 座標)
inline constexpr Rect GraphicsButton{1046,38,190,46},GraphicsBack{984,600,226,58};
// DLSS の ON / OFF
inline constexpr Rect DlssQualityButton{760,238,222,44},DlssOffButton{986,238,206,44};
// RT の影・反射・間接光の ON / OFF
inline constexpr Rect RtOnButton{1010,357,92,38},RtOffButton{1106,357,92,38};
inline constexpr Rect RtReflectionOn{1010,415,92,38},RtReflectionOff{1106,415,92,38};
inline constexpr Rect RtIndirectOn{1010,473,92,38},RtIndirectOff{1106,473,92,38};
// 露出の増減
inline constexpr Rect ExposureDown{1010,531,92,38},ExposureUp{1106,531,92,38};
// プリセット (低・標準・高)
inline constexpr Rect PresetLow{96,604,170,50},PresetBalanced{278,604,206,50},PresetHigh{496,604,170,50};
// 効果の i 行目の位置
inline Rect GraphicsRow(int i){return {72,196+float(i)*58,612,50};}
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
// 2つで1組の切り替えボタンの片側。選ばれている側はライム色の楕円にする
inline void Segment(Canvas& ui,Rect b,const std::string& label,bool active,Engine::Vector4 accent=Lime){
    // マウスが乗っているか (フォーカス枠も描く)
    const bool hover=ui.Hover(b);ui.FocusFrame(b,b.h*.5f);
    // 選ばれている側は色付きの楕円、乗っているときは少し明るい楕円
    if(active)ui.Rounded(b,accent,b.h*.5f);
    else if(hover)ui.Rounded(b,{.14f,.23f,.18f,1},b.h*.5f);
    // ラベルを中央に書く
    ui.LabelCenter(label,b.x+b.w*.5f,b.y+(b.h-17)*.5f-1,17,active?Ink:hover?Paper:Muted,1.6f);
}
// 2つのボタンの後ろの溝
inline void SegmentTrack(Canvas& ui,Rect a,Rect b){
    ui.Rounded({a.x-4,a.y-4,b.x+b.w-a.x+8,a.h+8},{.05f,.085f,.07f,1},(a.h+8)*.5f);
}
// つまみが滑るトグルスイッチ
inline void Switch(Canvas& ui,float x,float y,bool on){
    // 台 (ON はライム色)
    ui.Rounded({x,y,62,30},on?Lime:Engine::Vector4{.16f,.24f,.2f,1},15);
    // つまみの位置 (ON は右、OFF は左)
    const float knob=on?x+47:x+15;
    // つまみと、その上の小さな光
    ui.Dot(knob,y+15,11,on?Ink:Muted);ui.Dot(knob-3,y+11,3,WithAlpha(Paper,on?.35f:.25f));
}
// 画面の描画
inline void DrawGraphics(Canvas& ui,const Engine::Renderer& renderer){
    // 背景と見出し
    ui.Background(Str("graphics.section").c_str());const auto& s=renderer.GetGraphicsSettings();
    ui.Brush({62,92,340,94},WithAlpha(Lime,.92f),-.025f);
    ui.Text(Str("graphics.heading"),96,116,40,Ink,JapaneseFont);
    // 操作の案内 (ゲームパッドとキーボードで出し分ける)
    if(UsingPad()){float x=ui.PadPrompt("xbox_dpad",Str("graphics.padSelect"),424,134,28,18,Muted,JapaneseFont);
        x=ui.PadPrompt("xbox_button_a",Str("graphics.padToggle"),x+22,134,28,18,Muted,JapaneseFont);ui.PadPrompt("xbox_button_b",Str("graphics.padBack"),x+22,134,28,18,Muted,JapaneseFont);}
    else ui.Text(Str("graphics.keyHint"),424,138,18,Muted,JapaneseFont);

    // 効果の一覧 (番号・名前・スイッチ)
    float values[]={s.bloom,s.lensFlare,s.grading,s.ambientOcclusion,s.motionBlur,s.dof};
    const char* names[]={"graphics.effect1","graphics.effect2","graphics.effect3","graphics.effect4","graphics.effect5","graphics.effect6"};
    for(int i=0;i<6;++i){
        // 行の位置・マウスが乗っているか・ON か
        const auto row=GraphicsRow(i);const bool hover=ui.Hover(row),on=values[i]>.001f;
        ui.FocusFrame(row,16);
        // 行の台 (乗っていると少し明るく)
        ui.Rounded(row,hover?Engine::Vector4{.12f,.2f,.16f,.98f}:Engine::Vector4{.07f,.115f,.09f,.94f},16);
        // 番号・名前・スイッチ
        ui.Label("0"+std::to_string(i+1),row.x+20,row.y+17,14,WithAlpha(on?Lime:Muted,.9f),2);
        ui.Text(Str(names[i]),row.x+62,row.y+12,21,on?Paper:Muted,JapaneseFont);
        Switch(ui,row.x+row.w-84,row.y+10,on);
    }

    // DLSS の欄 (使えない場合は理由を表示する)
    const Rect dlss{740,186,470,148};
    ui.Rounded(dlss,{.07f,.115f,.09f,.94f},20);
    ui.Label(Str("graphics.dlssTitle"),dlss.x+22,dlss.y+20,13,Lime,2.6f);
    SegmentTrack(ui,DlssQualityButton,DlssOffButton);
    // ON / OFF の切り替えボタン (DLSS が使えなければ ON にならない)
    Segment(ui,DlssQualityButton,Str("graphics.dlssOn"),s.dlssQuality&&renderer.SupportsDlss());Segment(ui,DlssOffButton,Str("graphics.off"),!s.dlssQuality);
    // 今適用されているかどうか
    std::string availability=Str(renderer.DlssActive()?"graphics.dlssActive":"graphics.dlssInactive");
    // 使えないときは、状態の文字列から理由を選んで表示する
    if(!renderer.SupportsDlss()){
        const auto reason=renderer.DlssStatus();availability=Str("graphics.dlssFailed");
        if(reason=="SDK DLLs missing")availability=Str("graphics.dlssMissing");
        else if(reason.find("signature")!=std::string::npos)availability=Str("graphics.dlssSignature");
        else if(reason.find("unsupported")!=std::string::npos)availability=Str("graphics.dlssUnsupported");
        else if(reason.find("evaluation failed")!=std::string::npos)availability=Str("graphics.dlssEvaluation");
    }
    // 再起動が必要な注意と、使える状態かの表示
    ui.Text(Str("graphics.restartNote"),dlss.x+22,dlss.y+104,16,Gold,JapaneseFont);
    ui.Text(availability,dlss.x+250,dlss.y+105,14,Muted,JapaneseFont);

    // RT の欄 (影・反射・間接光)
    auto rtRow=[&](float y,const std::string& name,Rect on,Rect off,bool enabled,bool available){
        // 行の台と名前 (使えないときは薄く)
        ui.Rounded({740,y,470,52},{.07f,.115f,.09f,.94f},16);
        ui.Text(name,762,y+13,20,available?Paper:Muted,JapaneseFont);
        // ON / OFF の切り替え
        SegmentTrack(ui,on,off);Segment(ui,on,Str("graphics.on"),enabled&&available,Teal);Segment(ui,off,Str("graphics.off"),!enabled,Teal);
    };
    // 影・反射・間接光の行
    rtRow(350,Str("graphics.rtShadows"),RtOnButton,RtOffButton,s.rtShadows,renderer.RtShadowsAvailable());
    rtRow(408,Str("graphics.rtReflections"),RtReflectionOn,RtReflectionOff,s.rtReflections,renderer.RtLightingAvailable());
    rtRow(466,Str("graphics.rtIndirect"),RtIndirectOn,RtIndirectOff,s.rtIndirect,renderer.RtLightingAvailable());
    // RT が使えないときの注意
    if(!renderer.RtLightingAvailable())ui.Text(Str("graphics.rtUnavailable"),900,358,14,Muted,JapaneseFont);
    // 露出の欄
    ui.Rounded({740,524,470,52},{.07f,.115f,.09f,.94f},16);
    ui.Text(Str("graphics.exposure"),762,537,20,Paper,JapaneseFont);
    // 今の露出 (%)
    ui.Text(std::to_string(int(std::round(s.exposure*100)))+"%",830,534,26,Lime);
    // -10% / +10% のボタン
    SegmentTrack(ui,ExposureDown,ExposureUp);Segment(ui,ExposureDown,Str("graphics.exposureDown"),false);Segment(ui,ExposureUp,Str("graphics.exposureUp"),false);

    // プリセットと戻るボタン
    ui.Text(Str("graphics.autoSave"),98,562,16,Muted,JapaneseFont);
    ui.Button(PresetLow,Str("graphics.presetLow"));ui.Button(PresetBalanced,Str("graphics.presetBalanced"));ui.Button(PresetHigh,Str("graphics.presetHigh"));
    ui.Button(GraphicsBack,Str(UsingPad()?"graphics.back":"graphics.backKey"),true,"xbox_button_b");
}
}
