// LoadingUI.h
// ロード画面・起動スプラッシュ・シーン切り替えの演出。
#pragma once
// 共通の UI 描画 (Canvas・色・ボタン)
#include "GameUI.h"
// ロード画面の状態とシーン管理 (エンジン側)
#include "../../Engine/LoadingScreen.h"
#include "../../Engine/SceneManager.h"
#include "../Data/GameData.h"
#include <array>
#include <chrono>
#include <cmath>

// ロード画面とシーン切り替えはすべてスライムの意匠で統一する:
// 切り替えでは暗いスライムが画面に流れ込んで覆い、抜けるように流れ落ちる。
// ロード画面はそのスライムの「中」にあり、右下のマークは輪を描いて動くスライム
namespace Game::UI {
namespace Loading {
// スライムの画像と、塗りつぶしに使うスライムの色
inline constexpr const char* Circle = "Resources/Textures/UI/slime_circle.png";
inline constexpr const char* Glow = "Resources/Textures/UI/slime_glow.png";
inline const Engine::Vector4 Slime{.022f,.045f,.035f,1}, SlimeEdge{.09f,.19f,.12f,1};

// 起動からの経過秒数
inline float Seconds() {
    // 最初に呼ばれた時刻を覚えておく
    static const auto start=std::chrono::steady_clock::now();
    // そこからの経過秒数
    return std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
}
// 整数から 0〜1 の疑似乱数 / 滑らかな 0〜1 (smoothstep) / ゆっくり始まりゆっくり終わる曲線 / 不透明度を掛ける
inline float Hash(int i) { float v=std::sin(float(i)*12.9898f+78.233f)*43758.5453f; return v-std::floor(v); }
inline float Smooth(float a,float b,float x) { float t=std::clamp((x-a)/(b-a),0.f,1.f); return t*t*(3-2*t); }
inline float EaseInOut(float t) { t=std::clamp(t,0.f,1.f); return t<.5f?4*t*t*t:1-std::pow(-2*t+2,3.f)*.5f; }
inline Engine::Vector4 Alpha(Engine::Vector4 c,float a) { c.w*=a; return c; }
// 円の画像・光のぼかし画像を描く
inline void Dot(Canvas& ui,float x,float y,float r,Engine::Vector4 c) { ui.Image(Circle,{x-r,y-r,r*2,r*2},c); }
inline void Halo(Canvas& ui,float x,float y,float r,Engine::Vector4 c) { ui.Image(Glow,{x-r,y-r,r*2,r*2},c); }

// スライムのワイプ
// 細い帯の先端に重なる丸を付け、波打ちながら垂れる1本の縁にする。
// 3層 (つやのあるライム色の縁・緑の帯・暗い本体) で厚みを出す。
// 覆うときは上から流れ込み、抜けるときは上端が下へ落ちていく
inline void SlimeWipe(Canvas& ui,float t,bool draining) {
    // 画面を 28 本の縦の帯に分け、帯ごとに少しずつ遅らせて流す
    constexpr int columns=28;
    // 帯の幅・先端の丸の半径・端から端まで流れる距離
    const float width=1280.f/columns,radius=width*.68f,span=720+radius*2.4f;
    // 今の時刻 (揺れに使う)
    const float time=Seconds();
    // 層ごとの先行量・丸の大きさの倍率・色
    struct Layer { float lead,cap; Engine::Vector4 color; };
    // 覆うときは縁が本体より先に進み、抜けるときは後ろに残って常に上に見えるようにする
    const float dir=draining?-1.f:1.f;
    // 下の層ほど丸を小さくして、つやのある縁が1本につながって見えるようにする
    const Layer layers[]={{.04f*dir,1.f,{.36f,.66f,.21f,1}},{.02f*dir,.86f,SlimeEdge},{0,.72f,Slime}};
    // 3層 (縁・帯・本体) × 各帯
    for(int layer=0;layer<3;++layer){
        for(int i=0;i<columns;++i){
            // 帯ごとの遅れ (波のような揺らぎと乱数)
            const float delay=.13f*(.5f+.5f*std::sin(float(i)*.55f+1.3f))+.07f*Hash(i*7+3);
            // この帯の進み具合 (0〜1)
            const float u=EaseInOut((t*1.06f+layers[layer].lead-delay)/.8f);
            // 流れている途中だけ先端を左右に揺らす
            const float wobble=std::sin(time*3.1f+float(i)*.9f)*5*(1-std::abs(u*2-1));
            // 帯の左端・中心・先端の高さ
            const float x=float(i)*width,cx=x+width*.5f,edge=u*span-radius*1.2f+wobble;
            const auto color=layers[layer].color;
            // 覆う: 上から下へ伸びる
            if(!draining){
                // まだ画面に入っていなければ描かない
                if(edge<=-radius)continue;
                // 上から先端までを塗る
                ui.Fill({x-.5f,-4,width+1,std::max(0.f,edge)+4},color);
                // 先端の丸
                Dot(ui,cx,edge,radius*layers[layer].cap,color);
                // 流れている途中は縁から細い糸が伸び、着地すると戻る
                if(layer==0&&i%3==1){
                    // 糸の長さ (流れの中ほどで最も長い)
                    const float strand=(30+60*Hash(i*5))*std::sin(3.14159f*std::clamp(u,0.f,1.f));
                    // 糸と、その先のしずく
                    if(strand>2){ui.Fill({cx-3,edge,6,strand},color);Dot(ui,cx,edge+strand,6.5f,color);}
                }
            // 抜ける: 上端が下へ落ちていく
            }else{
                // もう画面の下へ抜けていれば描かない
                if(edge>=720+radius)continue;
                // 上端 (画面より上には出さない)
                const float top=std::max(-4.f,edge);
                // 上端から画面の下までを塗る
                ui.Fill({x-.5f,top,width+1,724-top},color);
                // 上端の丸
                Dot(ui,cx,edge,radius*layers[layer].cap,color);
            }
        }
    }
}
// 円周を線分で描く
inline void RingLine(Canvas& ui,float x,float y,float radius,float width,Engine::Vector4 color) {
    // 96 分割の折れ線で円を描く
    constexpr int segments=96;
    for(int i=0;i<segments;++i){
        // 線分の始点と終点の角度
        const float a0=6.2831853f*float(i)/segments,a1=6.2831853f*float(i+1)/segments;
        ui.Line(x+std::cos(a0)*radius,y+std::sin(a0)*radius,x+std::cos(a1)*radius,y+std::sin(a1)*radius,width,color);
    }
}
// ゲームの「輪」の意匠: ライム色の輪が閉じて外側へはじける
inline void LoopBurst(Canvas& ui,float t) {
    // 始まる前と終わった後は描かない
    if(t<=0||t>=1)return;
    // 広がり方と消え方
    const float grow=EaseInOut(t),fade=1-Smooth(.45f,1.f,t);
    // ライム色の大きな輪
    RingLine(ui,640,360,40+700*grow,3+6*(1-grow),Alpha(Lime,fade*.9f));
    // 金色の内側の輪
    RingLine(ui,640,360,20+520*grow,2,Alpha(Gold,fade*.45f));
}

// 右下のマーク: 無限記号を描いて動くスライム
inline void LogoMark(Canvas& ui,float cx,float cy,float alpha) {
    // ほぼ透明なら描かない
    if(alpha<=.01f)return;
    // マーク全体を alpha で薄くする
    Canvas mark=ui;mark.MultiplyAlpha(alpha);
    const float time=Seconds();
    // 後ろの光 (ゆっくり明滅する)
    Halo(mark,cx,cy,74,Alpha(Lime,.18f+.06f*std::sin(time*2)));
    // 無限記号の軌跡の位置
    auto loop=[&](float s){return std::array<float,2>{cx+std::cos(s)*48,cy+std::sin(s*2)*20};};
    // 残像の数
    constexpr int trail=30;
    for(int k=trail;k>=0;--k){
        // 古い残像ほど小さく薄く、先頭は白
        const float s=time*2.4f-float(k)*.075f,fade=1-float(k)/trail;
        auto p=loop(s);
        Dot(mark,p[0],p[1],1.2f+4.2f*fade,k==0?Paper:Alpha(Lime,.15f+.75f*fade));
    }
    // 伸び縮みする体 (底の位置は固定)
    const float wobble=std::sin(time*3.4f),w=21*(1+.09f*wobble),h=21*(1-.09f*wobble),base=cy+19;
    // 外側の体
    mark.Image(Circle,{cx-w,base-h*2,w*2,h*2},Engine::Vector4{.47f,.78f,.25f,1});
    // 内側の明るい体
    mark.Image(Circle,{cx-w*.86f,base-h*1.9f,w*1.72f,h*1.72f},Engine::Vector4{.62f,.90f,.33f,1});
    // つやの光
    Dot(mark,cx-w*.38f,base-h*1.45f,4.5f,Alpha(Paper,.75f));
    // ときどき瞬きする (3.3 秒ごとに少しの間)
    const bool blink=std::fmod(time,3.3f)>3.15f;
    const float eye=blink?1.2f:5.f;
    // 左右の目
    for(float side:{-1.f,1.f})mark.Image(Circle,{cx+side*7.5f-3,base-h-4-eye*.5f,6,eye},Ink);
}

// チュートリアルの1ページ (見出し・本文4行・キーボード/パッドの操作アイコンと名前)
struct Page { const char* title; std::array<const char*,4> lines; const char *key,*keyLabel,*pad,*padLabel; };
// loading.json の中身 (ページの文章・ロード中の文・自動でページを送る時間・起動画面の説明文)
struct LoadingData {
    struct PageText { std::string title; std::array<std::string,4> lines; std::string key,keyLabel,pad,padLabel; };
    std::array<PageText,6> pages;
    std::string pageHintPad,pageHintKeys,ready,preparingEngine,generatingField,loading,splashTagline;
    float autoPageSeconds=5;
};
// loading.json を読み込む (最初に使われたときに 1 回だけ。ロード用のスレッドからも呼ばれる)
inline const LoadingData& LoadingTexts() {
    static const LoadingData data=[]{
        const auto& json=Data::Load("loading.json");
        LoadingData out;
        const auto& pages=Data::Require(json,"pages","loading.json");
        // 絵はページの順番でコードが描くので、ページ数は 6 に固定する
        if(pages.size()!=out.pages.size())throw std::runtime_error("loading.json: pages must have 6 entries");
        for(size_t i=0;i<out.pages.size();++i){
            auto& page=out.pages[i];
            page.title=pages[i].at("title").get<std::string>();
            const auto lines=Data::ReadStrings(pages[i].at("lines"));
            for(size_t k=0;k<lines.size()&&k<page.lines.size();++k)page.lines[k]=lines[k];
            if(pages[i].contains("control")){
                const auto& control=pages[i].at("control");
                page.key=control.at("key").get<std::string>();page.keyLabel=control.at("keyLabel").get<std::string>();
                page.pad=control.at("pad").get<std::string>();page.padLabel=control.at("padLabel").get<std::string>();
            }
        }
        const auto& texts=Data::Require(json,"texts","loading.json");
        out.pageHintPad=texts.at("pageHintPad").get<std::string>();out.pageHintKeys=texts.at("pageHintKeys").get<std::string>();
        out.ready=texts.at("ready").get<std::string>();out.preparingEngine=texts.at("preparingEngine").get<std::string>();
        out.generatingField=texts.at("generatingField").get<std::string>();out.loading=texts.at("loading").get<std::string>();
        out.autoPageSeconds=Data::Require(json,"autoPageSeconds","loading.json").get<float>();
        out.splashTagline=Data::Require(json,"splashTagline","loading.json").get<std::string>();
        return out;
    }();
    return data;
}
// チュートリアルのページ一覧 (loading.json の文章を指す。操作が無いページは nullptr)
inline const std::array<Page,6>& Pages() {
    static const std::array<Page,6> pages=[]{
        std::array<Page,6> out{};
        const auto& texts=LoadingTexts().pages;
        for(size_t i=0;i<out.size();++i){
            const auto& t=texts[i];auto& page=out[i];
            page.title=t.title.c_str();
            for(size_t k=0;k<page.lines.size();++k)page.lines[k]=t.lines[k].c_str();
            const bool control=!t.key.empty();
            page.key=control?t.key.c_str():nullptr;page.keyLabel=control?t.keyLabel.c_str():nullptr;
            page.pad=control?t.pad.c_str():nullptr;page.padLabel=control?t.padLabel.c_str():nullptr;
        }
        return out;
    }();
    return pages;
}

// 点の列を線でつなぐ
inline void Trail(Canvas& ui,const std::vector<std::array<float,2>>& points,float width,Engine::Vector4 color) {
    // 隣どうしを線でつなぐ
    for(size_t i=1;i<points.size();++i)ui.Line(points[i-1][0],points[i-1][1],points[i][0],points[i][1],width,color);
    // つなぎ目を丸くする
    for(const auto& p:points)Dot(ui,p[0],p[1],width*.5f,color);
}
// 小さなスライムの絵
inline void MiniSlime(Canvas& ui,float x,float y,float r) {
    // 体
    Dot(ui,x,y,r,Engine::Vector4{.55f,.86f,.30f,1});
    // つやの光
    Dot(ui,x-r*.35f,y-r*.35f,r*.25f,Alpha(Paper,.7f));
    // 左右の目
    for(float side:{-1.f,1.f})Dot(ui,x+side*r*.33f,y-r*.05f,r*.14f,Ink);
}
// 各ページは 400x300 の枠に入る、ループする小さな図
inline void Illustration(Canvas& ui,int page,float ox,float oy,float time) {
    // ゆっくりした脈動 (0〜1)
    const float pulse=.5f+.5f*std::sin(time*2.8f);
    // 0: 3 色の素材が落ちてスライムになる
    if(page==0){
        for(int i=0;i<3;++i){auto c=i==0?Teal:i==1?Lime:Gold;Dot(ui,ox+100+float(i)*100,oy+65,22,c);
            Dot(ui,ox+100+float(i)*100,oy+100+std::fmod(time*70+float(i)*35,85.f),7,Alpha(c,.7f));}
        MiniSlime(ui,ox+200,oy+225,55);ui.LabelCenter("MIX YOUR SLIME",ox+200,oy+280,16,Lime);
    }else if(page==1){
        // 1: 左右へ腕を伸ばして取り込む
        MiniSlime(ui,ox+200,oy+215,58);float open=Smooth(0,.5f,pulse);
        for(float sign:{-1.f,1.f}){float x=ox+200+sign*(24+75*open);Dot(ui,x,oy+150,25,Lime);
            ui.Line(ox+200+sign*25,oy+205,x,oy+150,24,Lime);}
        Dot(ui,ox+200,oy+142,14,Gold);ui.LabelCenter("OPEN / WRAP / ABSORB",ox+200,oy+280,16,Lime);
    }else if(page==2){
        // 2: 攻撃を受けて左右へ分裂する
        MiniSlime(ui,ox+200,oy+180,48);float spread=pulse*115;
        for(float sign:{-1.f,1.f})MiniSlime(ui,ox+200+sign*spread,oy+215,23);
        ui.LabelCenter("HIT / SPLIT / AUTO FIGHT",ox+200,oy+280,16,Lime);
    }else if(page==3){
        // 3: 本体の HP のバーが減っていく
        MiniSlime(ui,ox+200,oy+180,42);for(float sign:{-1.f,1.f})MiniSlime(ui,ox+200+sign*115,oy+215,23);
        ui.Bar({ox+120,oy+245,160,10},1-pulse,Gold);
        ui.LabelCenter("PROTECT YOUR BODY HP",ox+200,oy+280,16,Gold);
    }else if(page==4){
        // 4: 体を伸ばしてハンマーの形になる
        MiniSlime(ui,ox+200,oy+235,42);ui.Line(ox+200,oy+210,ox+200,oy+115,22,Lime);
        ui.Rounded({ox+130,oy+64,140,65},Lime,22);ui.LabelCenter("BECOME THE HAMMER",ox+200,oy+280,16,Lime);
    }else{
        // 5: 子と一緒に育つ
        MiniSlime(ui,ox+200,oy+210,52);for(float sign:{-1.f,1.f})MiniSlime(ui,ox+200+sign*100,oy+235,22);
        ui.LabelCenter("GROW TOGETHER",ox+200,oy+280,16,Lime);
    }
}

// ページめくりの状態 (表示中のページ・放置時間・スライド量・表示中の進捗)
struct PagerState { int page=0; float idle=0,slide=0,shown=0,lastElapsed=0,last=0; };
// ページめくりの状態 (起動中ずっと残る)
inline PagerState& Pager() { static PagerState state; return state; }


// ロード画面全体。alpha で中身をフェードさせる (右下のマークは常に表示)
// ロード用のスレッドで動くので、入力と使用中の機器はすべて state から読む
inline void LoadingContent(Canvas& ui,const Engine::LoadingScreen::State& state,float alpha,bool interactive) {
    // ページの送り (クリック・左右キー・5 秒放置で次へ)
    auto& pager=Pager();
    // 前のフレームからの時間 (最大 0.1 秒)
    const float now=Seconds(),dt=std::clamp(now-pager.last,0.f,.1f);pager.last=now;
    // ページ数
    const int count=int(Pages().size());
    // ロードのたびに次のヒントのページから始める
    if(state.elapsed<pager.lastElapsed){pager.shown=0;pager.page=(pager.page+1)%count;pager.idle=0;}
    // 経過時間を覚える (次のロードの始まりを見分けるため)
    pager.lastElapsed=state.elapsed;
    // 操作を受け付けるときだけページを送る
    if(interactive){
        // キー・パッドでの送り
        int step=state.step;
        if(state.clicked){
            // 左の矢印のクリックで前へ、右の矢印かカードのクリックで次へ
            if(ui.HitPixel({126,296,50,64},state.clickX,state.clickY))step=-1;
            else if(ui.HitPixel({1104,296,50,64},state.clickX,state.clickY)||ui.HitPixel({190,112,900,430},state.clickX,state.clickY))step=1;
        }
        // 何もしない時間を数え、5 秒経ったら次へ
        pager.idle+=dt;
        if(!step&&pager.idle>LoadingTexts().autoPageSeconds)step=1;
        // 送ったらスライドの量を付け、放置時間を戻す
        if(step){pager.page=(pager.page+step+count)%count;pager.slide=float(step);pager.idle=0;}
    }
    // スライドはすぐに 0 へ戻す
    pager.slide*=std::exp(-12*dt);
    // ほぼ透明なら描かない
    if(alpha<=.01f)return;
    // 中身全体を alpha で薄くする
    Canvas c=ui;c.MultiplyAlpha(alpha);

    // 上部の見出し (他の画面と同じ形)
    c.SlimeMark(64,64,14);c.Text(GameTitle(),90,46,22,Paper,JapaneseFont);
    c.Label(state.finished?"READY":"NOW LOADING",92,76,12,WithAlpha(Lime,.85f),2.8f);

    // チュートリアルのカード (めくった方向から滑り込む)
    // 今のページ
    const Page& page=Pages()[size_t(pager.page)];
    // スライド中は薄くする
    Canvas card=c;card.MultiplyAlpha(1-std::abs(pager.slide)*.8f);
    // カードの左上 (スライドの分ずらす)
    const float ox=190+pager.slide*90,oy=112;
    // カードの台と縁
    card.Rounded({ox,oy,900,430},{.06f,.1f,.08f,.97f},24);card.RoundedRing({ox,oy,900,430},WithAlpha(Paper,.08f),24);
    // 左上の小見出し
    card.Label("HOW TO PLAY",ox+26,oy+22,13,Lime,3.2f);
    // 右上のページ番号
    const std::string counter="0"+std::to_string(pager.page+1)+"  /  0"+std::to_string(count);
    card.Label(counter,ox+874-card.SpacedWidth(counter,13,2.4f),oy+22,13,Muted,2.4f);
    // ページの見出し
    card.Text(page.title,ox+24,oy+44,34,Paper,JapaneseFont);
    // 図の枠と、その中の格子線
    card.Fill({ox+24,oy+96,400,300},{.03f,.06f,.045f,1});
    for(int i=1;i<4;++i){card.Fill({ox+24+float(i)*100,oy+96,1,300},{.06f,.11f,.08f,1});card.Fill({ox+24,oy+96+float(i)*75,400,1},{.06f,.11f,.08f,1});}
    // ページの図
    Illustration(card,pager.page,ox+24,oy+96,now);
    // 本文 (空の行は飛ばす)
    for(size_t i=0;i<page.lines.size();++i)if(page.lines[i]&&*page.lines[i])
        card.Text(page.lines[i],ox+452,oy+110+float(i)*42,20,Paper,JapaneseFont);
    // 操作があれば、パッドかキーボードの印を出す
    if(page.key){
        if(state.padMode)card.PadPrompt(page.pad,page.padLabel,ox+452,oy+260,40,24);
        else card.Prompt(page.key,page.keyLabel,ox+452,oy+260,40,24);
    }

    // ページの矢印と点
    // 左右の矢印 (マウスが乗っていると明るく)
    for(int side=0;side<2;++side){Rect b{side?1104.f:126.f,296,50,64};
        const bool hot=!state.padMode&&c.HitPixel(b,state.mouseX,state.mouseY);
        c.Panel(b,hot?Engine::Vector4{.22f,.35f,.27f,1}:Ink);c.Center(side?">":"<",b.x+25,b.y+12,36,hot?Paper:Lime);}
    // ページの点 (今のページは大きく)
    for(int i=0;i<count;++i){const bool on=i==pager.page;Dot(c,640+(float(i)-(count-1)*.5f)*22,560,on?6.f:4.f,on?Lime:Engine::Vector4{.22f,.32f,.25f,1});}
    // 操作の案内
    if(state.padMode){float x=c.PadPrompt("xbox_dpad",LoadingTexts().pageHintPad,530,576,22,15,Muted,JapaneseFont);
        c.PadPrompt("xbox_lb","",x+14,576,22,15);c.PadPrompt("xbox_rb","",x+42,576,22,15);}
    else c.Center(LoadingTexts().pageHintKeys,640,578,15,Muted,JapaneseFont);

    // 進捗: 見積もりが飛んでも止まって見えないよう滑らかに追従させる
    pager.shown+=(state.progress-pager.shown)*(1-std::exp(-(state.finished?20.f:6.f)*dt));
    // 読み込みが終わったらほぼ 100% にする
    const float shown=state.finished?std::max(pager.shown,.995f):pager.shown;
    // エンジンの準備中かどうか
    const bool engine=state.scene.find("+Engine")!=std::string::npos;
    // 状態の文 (準備完了・エンジン準備中・フィールド生成中・読み込み中)
    const auto& texts=LoadingTexts();
    const std::string& status=state.finished?texts.ready:engine&&state.progress<.62f?texts.preparingEngine:
        state.scene.rfind("Game",0)==0?texts.generatingField:texts.loading;
    // 末尾の点を 0〜3 個でアニメーションさせる
    std::string dots(state.finished?0:size_t(std::fmod(now*2.5f,4.f)),'.');
    c.Text(std::string(status)+dots,190,612,18,Paper,JapaneseFont);
    // 割合 (%) を右寄せで書く
    const std::string percent=std::to_string(int(std::round(shown*100)))+"%";
    c.Text(percent,1090-c.Width(percent,28),604,28,Lime);
    // 進捗のバー
    const Rect bar{190,646,900,10};
    c.Fill(bar,{.08f,.15f,.11f,1});
    // 埋まる長さ
    const float fill=bar.w*std::clamp(shown,0.f,1.f);
    if(fill>0){
        c.Fill({bar.x,bar.y,fill,bar.h},Lime);
        // 埋まった部分を光が流れる
        const float sweep=std::fmod(now*.7f,1.f)*(fill+120)-60;
        const float s0=std::clamp(sweep,0.f,fill),s1=std::clamp(sweep+60,0.f,fill);
        if(s1>s0)c.Fill({bar.x+s0,bar.y,s1-s0,bar.h},Alpha(Paper,.35f));
        // 先端の光と、上下に揺れる小さなスライム
        Halo(c,bar.x+fill,bar.y+bar.h*.5f,26,Alpha(Lime,.6f));
        Dot(c,bar.x+fill,bar.y+bar.h*.5f+std::sin(now*6)*1.5f,8,Engine::Vector4{.62f,.90f,.33f,1});
    }
}

// 起動スプラッシュ: neoEngine のロゴ
// 字間を空けた文字の幅 / 字間を空けて描く
inline float SpacedWidth(Canvas& ui,const std::string& text,float size,float spacing) {
    // 1 文字ずつの幅と字間を足す (最後の字間は除く)
    float width=0;for(char ch:text)width+=ui.Width(std::string(1,ch),size)+spacing;
    return text.empty()?0:width-spacing;
}
inline void SpacedText(Canvas& ui,const std::string& text,float x,float y,float size,float spacing,Engine::Vector4 color) {
    // 1 文字ずつ書き、字間を空けて進める
    for(char ch:text){const std::string glyph(1,ch);ui.Text(glyph,x,y,size,color);x+=ui.Width(glyph,size)+spacing;}
}
// スプラッシュの下に出す説明文
inline const std::string& SplashTagline(){return LoadingTexts().splashTagline;}

// ロゴの周りに無限記号を描き、閉じた瞬間に光ってから文字が下から現れる。
// ロード中は輪の上を光が流れ続ける
inline void SplashContent(Canvas& ui,const Engine::LoadingScreen::State& state,float alpha) {
    // ほぼ透明なら描かない
    if(alpha<=.01f)return;
    Canvas c=ui;c.MultiplyAlpha(alpha);
    // 無限記号の軌跡 (中心 640,318)
    const float t=state.elapsed,now=Seconds(),cx=640,cy=318;
    // 軌跡上の位置 (s: 0〜1)
    auto at=[&](float s){const float a=1.5707963f+6.2831853f*s;return std::array<float,2>{cx+std::cos(a)*330,cy+std::sin(a)*std::cos(a)*150};};
    // 描き進める割合と、描き終わって落ち着く割合
    const float draw=EaseInOut(t/1.1f),settled=Smooth(1.1f,1.6f,t);
    constexpr int segments=160;
    // 描き進んだところまでを線分でつなぐ (描き終わると薄くなる)
    for(int i=0;i<segments;++i){
        const float s0=float(i)/segments;if(s0>=draw)break;
        const float s1=std::min(float(i+1)/segments,draw);
        auto p0=at(s0),p1=at(s1);
        c.Line(p0[0],p0[1],p1[0],p1[1],5,Alpha(Lime,.3f+.7f*(1-settled)));
    }
    // 描いている途中は先端に光と白い点
    if(draw<1){auto head=at(draw);Halo(c,head[0],head[1],42,Alpha(Lime,.7f));Dot(c,head[0],head[1],7,Paper);}
    // 描き終わったら軌跡の上を光が流れ続ける
    else for(int k=0;k<20;++k){
        float s=std::fmod(now*.3f-float(k)*.006f,1.f);if(s<0)s+=1;
        auto p=at(s);const float fade=1-float(k)/20;
        Dot(c,p[0],p[1],1+4*fade,Alpha(Paper,.85f*fade*settled));
    }
    // 描き終わった瞬間の光と広がる輪
    const float flash=Smooth(1.f,1.15f,t)*(1-Smooth(1.15f,1.9f,t));
    if(flash>0){Halo(c,cx,cy,280,Alpha(Lime,.3f*flash));RingLine(c,cx,cy,60+320*Smooth(1.05f,1.9f,t),3,Alpha(Lime,flash));}

    // ロゴ: "neo" はクリーム色、"Engine" はライム色で、1文字ずつ浮かび上がる
    // 文字の大きさ・字間・2 語の間
    const float size=104,spacing=4,gap=18;
    // 2 語の幅
    const float wNeo=SpacedWidth(c,"neo",size,spacing),wEngine=SpacedWidth(c,"Engine",size,spacing);
    // 中央にそろえた書き始めの位置
    float x=cx-(wNeo+gap+wEngine)*.5f;
    const float y=cy-size*.62f;
    int index=0;
    auto word=[&](const std::string& text,Engine::Vector4 color){
        for(char ch:text){
            const std::string glyph(1,ch);
            // 1 文字ずつ少し遅れて、下から浮かび上がる
            const float appear=Smooth(.75f+float(index)*.06f,1.05f+float(index)*.06f,t);
            if(appear>0){Canvas letter=c;letter.MultiplyAlpha(appear);letter.Text(glyph,x,y+22*(1-appear),size,color);}
            x+=c.Width(glyph,size)+spacing;++index;
        }
    };
    // neo と Engine を続けて書く
    word("neo",Paper);x+=gap-spacing;word("Engine",Lime);
    // 下線が中央から左右へ伸びる
    const float underline=EaseInOut(Smooth(1.3f,1.8f,t));
    if(underline>0)c.Fill({cx-260*underline,cy+54,520*underline,2},Alpha(Lime,.9f));
    // 説明文がフェードインする
    const float tagline=Smooth(1.6f,2.1f,t);
    if(tagline>0){Canvas g=c;g.MultiplyAlpha(tagline);
        SpacedText(g,SplashTagline(),cx-SpacedWidth(g,SplashTagline(),17,3)*.5f,cy+72,17,3,Muted);}

    // 控えめな進捗の線。読み込みが終わったらスキップできる
    const float barWidth=220;
    // 線の下地と、進んだ分
    c.Fill({cx-barWidth*.5f,640,barWidth,2},Alpha(Paper,.12f));
    c.Fill({cx-barWidth*.5f,640,barWidth*std::clamp(state.progress,0.f,1.f),2},Alpha(Lime,.85f));
    // 読み込み済みで 2.4 秒経ったら押すように案内する
    if(state.finished&&t>=2.4f)c.Center(state.padMode?"PRESS ANY BUTTON":"PRESS ANY KEY",cx,654,15,Alpha(Muted,.45f+.25f*std::sin(now*4)));
}

// ロード用スレッドが始まる前にメインスレッドで (完全に透明で) 一度描き、
// ロード用スレッドで使うテクスチャと文字を先にキャッシュしておく
inline void Warmup(Canvas& ui) {
    // 完全に透明にして描く
    Canvas w=ui;w.MultiplyAlpha(0);
    // 使う図形・画像を一通り描く
    Dot(w,0,0,4,Lime);Halo(w,0,0,4,Lime);w.Panel({0,0,40,40},Ink);w.Bar({0,0,40,8},.5f);w.Line(0,0,10,10,2,Lime);
    w.RoundedRing({0,0,40,40},Lime);w.Grain();w.Brush({0,0,40,10},Lime);w.Splat(0,0,4,Lime);w.SlimeMark(0,0,4);
    // 各ページの図と文字
    for(int page=0;page<int(Pages().size());++page){
        const Page& p=Pages()[size_t(page)];
        Illustration(w,page,0,0,0);
        w.Text(p.title,0,0,34,Paper,JapaneseFont);
        for(const char* line:p.lines)if(line&&*line)w.Text(line,0,0,20,Paper,JapaneseFont);
        if(p.key){w.Prompt(p.key,p.keyLabel,0.f,0.f,40.f,24.f);w.PadPrompt(p.pad,p.padLabel,0.f,0.f,40.f,24.f);}
    }
    // パッドのアイコン
    for(const char* pad:{"xbox_dpad","xbox_lb","xbox_rb"})w.PadPrompt(pad,"",0,0,22,15);
    // ロード中に出す日本語の文字
    const auto& texts=LoadingTexts();
    for(const std::string& text:{texts.pageHintPad,texts.pageHintKeys,texts.ready,texts.preparingEngine,
        texts.generatingField,texts.loading,GameTitle()})w.Text(text,0,0,20,Paper,JapaneseFont);
    // 英字と数字
    for(const std::string& text:{std::string("HOW TO PLAY NOW LOADING READY 0123456789%/<>."),std::string("PRESS ANY KEY BUTTON"),std::string("neoEngine"),SplashTagline()})w.Text(text,0,0,20,Paper);
}

// ロード画面の中身の不透明度
inline float ContentAlpha(const Engine::LoadingScreen::State& state) {
    // 短いロードではスライムとマークだけを出し、長く待つときにチュートリアルを出す
    return state.startup?Smooth(0,.35f,state.elapsed):Smooth(.45f,.8f,state.elapsed);
}

// ロード画面の1フレームを描く (起動直後はスプラッシュ、それ以外はチュートリアル付きのロード画面)
inline void DrawLoadingFrame(const Engine::LoadingScreen::State& state) {
    // このフレームの描画先
    Canvas ui(Engine::Renderer::GetInstance());
    // 最初のフレームは先にキャッシュを作る
    if(state.warmup)Warmup(ui);
    // 画面全体をスライムの色で塗り、ざらつきを足す
    ui.Fill({-200,-200,1680,1120},Slime);
    ui.Grain(.04f);
    // 起動直後はスプラッシュだけを出す
    if(state.scene=="Startup"){SplashContent(ui,state,1);return;}
    // ロード画面の中身と右下のマーク
    LoadingContent(ui,state,ContentAlpha(state),!state.warmup);
    LogoMark(ui,1196,646,1);
}

// シーン切り替えの演出 (覆う → ロード → 抜ける)
inline void DrawTransition(const Engine::SceneManager::Transition& transition) {
    // このフレームの描画先
    Canvas ui(Engine::Renderer::GetInstance());
    using Phase=Engine::SceneManager::Transition::Phase;
    // 演出の進み具合 (0〜1)
    const float t=transition.progress;
    // 覆う: スライムを流し込み、終わり際にマークを出す
    if(transition.phase==Phase::Cover){
        SlimeWipe(ui,t,false);
        LogoMark(ui,1196,646,Smooth(.7f,1.f,t));
        return;
    }
    // 抜ける: 読み込みの終わったロード画面 (またはスプラッシュ) をフェードさせてからスライムを流し落とす
    // 直前のロード画面の状態
    const auto& last=Engine::LoadingScreen::Last();
    const bool splash=last.scene=="Startup";
    // ロード画面の中身が出ていたなら、少し止めてからフェードさせる
    const bool content=transition.loadingShown&&(splash||ContentAlpha(last)>.5f);
    const float hold=content?.22f:0.f;
    // スライムが抜ける進み具合
    const float drain=std::clamp((t-hold)/(1-hold),0.f,1.f);
    SlimeWipe(ui,drain,true);
    // ロード画面の中身を薄くしながら描く
    if(content){
        const float fade=1-Smooth(0,hold,t);
        if(splash)SplashContent(ui,last,fade);
        else LoadingContent(ui,last,fade*ContentAlpha(last),false);
    }
    // マークを消し、最後に輪をはじけさせる
    if(!splash)LogoMark(ui,1196,646,1-Smooth(0,.35f,drain));
    LoopBurst(ui,Smooth(0,.9f,drain));
}

// エンジンの起動前に1回呼び、起動時のロードも覆う
inline void InstallPresentation() {
    Engine::LoadingScreen::SetDrawer(DrawLoadingFrame);
}
// シーン切り替えの演出を登録する
inline void InstallTransitions(Engine::SceneManager& scenes) {
    scenes.SetTransitionDrawer(DrawTransition);
}
}
}
