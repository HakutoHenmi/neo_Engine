// ScaleUI.h
// 「物差し」の UI: タイトル・セレクト・リザルトで共通の巻尺・小道具・スライム・ロゴ・保存した記録。
// 巻尺は対数の目盛りで、5cm から 126m までを 1 本に載せる。画像は Resources/Textures/UI/Scale/
// (Docs/UIDesign/chiritsumo/bake_sprites.py でゲームのモデルから書き出した物)。
#pragma once
#include "GameUI.h"
#include "../Chrono/DevourStageRules.h"
#include "../../externals/nlohmann/json.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <Windows.h>

namespace Game::UI::Scale {
// 配色: 巻尺の黄色・墨・目標の赤・影
inline const Engine::Vector4 Yellow{.961f,.769f,0,1},Black{.067f,.067f,.067f,1},Red{.78f,.141f,.118f,1},
    Shadow{.23f,.16f,0,.3f},Silhouette{.72f,.56f,.02f,1},White{1,1,1,1},SlimeFill{.5f,.76f,.2f,1};
// フォント: 数字は Anton、日本語は Zen Kaku Gothic New。"#" の後ろは大きな文字用の解像度
inline constexpr const char* Num="Resources/Fonts/Anton/Anton-Regular.ttf";
inline constexpr const char* Jp="Resources/Fonts/ZenKakuGothicNew/ZenKakuGothicNew-Black.ttf";
inline constexpr const char* NumBig="Resources/Fonts/Anton/Anton-Regular.ttf#256";
inline constexpr const char* JpBig="Resources/Fonts/ZenKakuGothicNew/ZenKakuGothicNew-Black.ttf#192";
inline constexpr float NumBigBase=256,JpBigBase=192;
// 巻尺の決まった位置と長さ (設計座標)
inline constexpr float TapeTop=620,TapeLength=1120,SmallestCm=5,LargestCm=12631;
// 大きさ (cm) を巻尺の横の位置へ変える
inline float TapeX(float cm){return TapeLength*std::log((std::max)(cm,SmallestCm)/SmallestCm)/std::log(LargestCm/SmallestCm);}
// 文字の大きさの表記 (1 m からは m で出す)。Chrono::DevourSizeText と同じ
inline std::string SizeText(float cm){return Chrono::DevourSizeText(cm);}
// 整数の分:秒
inline std::string Clock(float seconds){int s=int(seconds+.5f);char text[16];snprintf(text,sizeof(text),"%d:%02d",s/60,s%60);return text;}

// 動きの曲線: 0〜1 を受け取る
inline float Clamp01(float t){return std::clamp(t,0.f,1.f);}
inline float EaseOut(float t){t=1-Clamp01(t);return 1-t*t*t;}
inline float EaseIn(float t){t=Clamp01(t);return t*t*t;}
// 少し行き過ぎて戻る
inline float Overshoot(float t){t=Clamp01(t)-1;return 1+2.7f*t*t*t+1.7f*t*t;}
// 上から落ちて弾む高さ (1 → 0)
inline float Drop(float t){t=Clamp01(t);if(t<.6f){float u=t/.6f;return 1-u*u;}float u=(t-.8f)/.2f;return .1f*(1-u*u);}

// 検証の撮影では、登場の動きを飛ばして完成の画面を撮る
inline bool SkipIntro(){return wcsstr(GetCommandLineW(),L"--ui-smoke")!=nullptr;}

// 画像の種類: 名前と縦横比 (幅 / 高さ)
struct Sprite{const char* name;float aspect;};
inline constexpr Sprite Rat{"rat",2.09f},Jerrycan{"jerrycan",.838f},Tyre{"tyre",.94f},Barrel{"barrel",.644f},Hydrant{"hydrant",1.044f},
    Box{"box",1.59f},Cat{"cat",.687f},Car{"car",2.306f},Horse{"horse",.701f},Barrier{"barrier",1.93f},Block{"block",1.086f},Ball{"ball",.994f};
inline std::string SpritePath(const char* name,bool silhouette=false){return std::string("Resources/Textures/UI/Scale/")+name+(silhouette?"_sil.png":".png");}

// 画面全体の黄色の地
inline void Backdrop(Canvas& ui){ui.Clear(Yellow);}
// 目盛りの文字: 小数を付けない大きさの表記
inline std::string TickText(float cm){return cm>=100?std::to_string(int(cm/100+.5f))+"m":std::to_string(int(cm+.5f))+"cm";}
// 目盛りの文字: 大きさ (cm) と赤くするか
struct Mark{float cm;bool red=false;};
// 巻尺: reveal は左から伸びる割合 (0〜1)。下の余りを地の色で隠し、物の影が目盛りにかからないようにする
inline void Tape(Canvas& ui,const std::vector<Mark>& marks,float top=TapeTop,float reveal=1,float height=80){
    const float end=1280*Clamp01(reveal);
    ui.Fill({-200,top,200+(reveal>=1?1480:end),300},Yellow);
    ui.Fill({0,top,end,2},Black);
    for(float x=0;x<end;x+=8){int i=int(x/8+.5f);bool big=i%10==0,mid=i%5==0;ui.Fill({x,top,big?2.6f:1.4f,height*(big?1:mid?.7f:.42f)},Black);}
    for(const auto& mark:marks){const float x=TapeX(mark.cm);if(x<=end)ui.Text(TickText(mark.cm),x+6,top+height*.5f,27,mark.red?Red:Black,Num);}
}
// 物を床の上に置く: cx は中央、base は床の高さ、height は高さ。squash は着地のつぶれ (1 で変形なし)
inline void Prop(Canvas& ui,const Sprite& sprite,float cx,float base,float height,float alpha=1,float lift=0,float squash=1){
    const float h=height*squash,w=height*sprite.aspect*(2-squash);const Rect b{cx-w*.5f,base-h-lift,w,h};
    ui.Image(SpritePath(sprite.name,true),{b.x+height*.1f,base-h*.98f,w,h*.98f},WithAlpha(Shadow,alpha*(lift>0?.5f:1)));
    ui.Photo(SpritePath(sprite.name),b,WithAlpha(White,alpha));
}
// 形だけの物 (未解放のステージ)
inline void PropShape(Canvas& ui,const Sprite& sprite,float cx,float base,float height,Engine::Vector4 color=Silhouette){
    const float w=height*sprite.aspect;
    ui.Image(SpritePath(sprite.name,true),{cx-w*.5f+height*.1f,base-height*.98f,w,height*.98f},Shadow);
    ui.Image(SpritePath(sprite.name,true),{cx-w*.5f,base-height,w,height},color);
}
// スライム: ゲームの GPU 流体を撮影した連番 (Docs/UIDesign/chiritsumo/matte_slime.py)。透明度があるので後ろの物が透ける
inline constexpr int IdleFrames=12,RollFrames=10;
inline constexpr float IdleAspect=2.241f,RollAspect=2.159f;
inline std::string SlimeFrame(bool rolling,int index){char name[48];snprintf(name,sizeof(name),"Resources/Textures/UI/Scale/slime_%s_%02d.png",rolling?"roll":"idle",index);return name;}
// 再生の途中で読み込みが起きないよう、先に全部のコマを読む
inline void PreloadSlime(Engine::Renderer* renderer){if(!renderer)return;for(int i=0;i<IdleFrames;++i)renderer->LoadTexture2D(SlimeFrame(false,i),false);for(int i=0;i<RollFrames;++i)renderer->LoadTexture2D(SlimeFrame(true,i),false);}
// 床に座るスライムを描く。cx は中央、width は体の幅。待機は往復、転がるときは繰り返しで再生する
inline void Slime(Canvas& ui,float cx,float base,float width,bool rolling=false){
    const float t=Seconds(),h=width/(rolling?RollAspect:IdleAspect);
    int frame=int(t*(rolling?14.f:9.f));
    if(rolling)frame%=RollFrames;else{frame%=IdleFrames*2-2;if(frame>=IdleFrames)frame=IdleFrames*2-2-frame;}
    ui.Image("Resources/Textures/UI/Scale/slime_shadow.png",{cx-width*.5f+width*.03f,base-h*.72f,width,h*.72f},WithAlpha(Shadow,.75f));
    ui.Photo(SlimeFrame(rolling,frame),{cx-width*.5f,base-h,width,h});
}
// ロゴの 1 文字: 100 の升の左上と大きさ。画像には周りに 8 の余白がある
inline void LogoGlyph(Canvas& ui,int index,float x,float y,float size,Engine::Vector4 color=Black,float rotation=0){
    const float pad=size*.08f;ui.ImageRot("Resources/Textures/UI/Scale/logo"+std::to_string(index)+".png",{x-pad,y-pad,size+pad*2,size+pad*2},rotation,color);
}
// タイトルとロードに並ぶ物 (小さい順) と、ロゴの大きさと位置
struct LineupProp{const Sprite* sprite;float cx,height;};
inline const LineupProp Lineup[]={{&Rat,73,16},{&Jerrycan,120,50},{&Tyre,180,73},{&Barrel,280,100},{&Hydrant,387,96},{&Box,493,62},{&Cat,627,147},{&Car,867,131},{&Horse,1133,293}};
inline constexpr int LineupCount=9;
inline constexpr float LogoBase=346,LogoLeft=427,LogoSizes[4]={80,127,193,293};
inline float LogoX(int index){float x=LogoLeft;for(int i=0;i<index;++i)x+=LogoSizes[i]*1.04f;return x;}
// ゲームパッドでフォーカスされた矩形の周りの赤い枠
inline void Focus(Canvas& ui,Rect b){if(!ui.Focused(b))return;const float t=3;ui.Fill({b.x-5,b.y-5,b.w+10,t},Red);ui.Fill({b.x-5,b.y+b.h+2,b.w+10,t},Red);ui.Fill({b.x-5,b.y-5,t,b.h+10},Red);ui.Fill({b.x+b.w+2,b.y-5,t,b.h+10},Red);}
// 操作の案内: 黒い台にキー (またはパッドのボタン) の画像と文字。文字の後ろの X を返す。
// ロード用のスレッドは入力を読めないので、pad に使用中の機器を渡す (-1 なら入力から調べる)
inline float Hint(Canvas& ui,const char* key,const char* pad,const std::string& label,float x,float y,Engine::Vector4 color=Black,float size=20,int padMode=-1){
    const float chip=size*1.7f;ui.Fill({x,y,chip+8,chip},Black);
    const std::string base="Resources/Textures/UI/kenney_input-prompts_1.5/";const bool usePad=padMode<0?UsingPad():padMode>0;
    ui.Image(usePad?base+"Xbox Series/Default/"+pad+".png":base+"Keyboard & Mouse/Default/"+key+".png",{x+4+chip*.08f,y+chip*.08f,chip*.84f,chip*.84f});
    ui.Text(label,x+chip+12,y+(chip-size)*.5f+size*.1f,size,color,Jp);
    return x+chip+12+ui.Width(label,size,Jp);
}
// 右向きの三角 (メニューの選択の印)
inline void Arrow(Canvas& ui,float x,float y,float size,Engine::Vector4 color=Red){const int rows=int(size);for(int i=0;i<rows;++i){const float f=1-std::abs(float(i)/float(rows-1)*2-1);ui.Fill({x,y+float(i),size*.8f*f,1.2f},color);}}

// 保存した記録: %LOCALAPPDATA%/LiquidTime/slime-lab-v1.json (ChronoSystem が書く)
inline std::filesystem::path LabPath(){
    wchar_t buffer[32768]{};const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
    return n>0&&n<32768?std::filesystem::path(buffer)/L"LiquidTime"/L"slime-lab-v1.json":std::filesystem::path{};
}
// 1 ステージの表示用の情報
struct StageView{std::string name;float startCm=0,goalCm=0,lastCm=0,bestCm=0,time=0;std::string rank;bool played=false,unlocked=true;};
// ステージの一覧と保存した記録を読む。前のステージをクリアすると次が開く
inline std::vector<StageView> LoadStages(int* lastStage=nullptr){
    std::array<float,8> bests{};int stage=0;
    try{std::ifstream file(LabPath());nlohmann::json data;if(file){file>>data;stage=data.value("devourStage",0);
        if(data.contains("devourBests")&&data["devourBests"].is_array())for(size_t i=0;i<bests.size()&&i<data["devourBests"].size();++i)bests[i]=data["devourBests"][i].get<float>();}}catch(...){}
    std::vector<StageView> out;bool open=true;const auto& stages=Chrono::DevourStages();
    for(size_t i=0;i<stages.size();++i){const auto& s=stages[i];StageView v;v.name=s.name;v.startCm=s.start;v.goalCm=s.goals.front();v.lastCm=s.goals.back();v.time=s.time;v.unlocked=open;
        const int rank=bests[i]>0?Chrono::DevourRankAt(s,bests[i]):0;v.played=bests[i]>0;if(v.played){v.bestCm=Chrono::DevourSizeCm(s,bests[i]);v.rank=Chrono::DevourRankName(s,rank);}
        open=open&&rank>0;out.push_back(std::move(v));}
    if(lastStage)*lastStage=std::clamp(stage,0,int(out.size())-1);
    return out;
}
// 選んだステージを保存する。本編は開始時にこの値を読む
inline void SaveStage(int stage){
    try{const auto path=LabPath();if(path.empty())return;nlohmann::json data=nlohmann::json::object();
        {std::ifstream file(path);if(file)file>>data;}
        data["devourStage"]=stage;if(!data.contains("version"))data["version"]=2;
        std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<data.dump();}catch(...){OutputDebugStringA("ScaleUI: stage could not be saved.\n");}
}

// 操作音より重い「飲む」音
inline void PlayGulp(bool big){
    auto* audio=Engine::Audio::GetInstance();if(!audio)return;
    static const uint32_t light=audio->Load("Resources/Sound/Devour/gulp-small-0.wav"),heavy=audio->Load("Resources/Sound/Devour/gulp-big.wav");
    audio->Play(big?heavy:light,false,.7f*audio->GetMasterSEVolume());
}

// ポーズ: 止まったゲームの左に黄色の面を出し、巻尺に今の大きさとランクの目標を出す
struct PauseView{std::string stage;float cm=0,timeLeft=0;std::vector<std::pair<std::string,float>> goals;};
inline PauseView MakePauseView(const Chrono::DevourStage& stage,float mass,float timeLeft){
    PauseView view;view.stage=stage.name;view.cm=Chrono::DevourSizeCm(stage,mass);view.timeLeft=timeLeft;
    for(size_t i=0;i<stage.goals.size();++i)view.goals.push_back({Chrono::DevourRankName(stage,int(i)+1),stage.goals[i]});
    return view;
}
inline constexpr Rect PauseMenu[3]={{48,196,390,70},{48,278,300,44},{48,326,300,44}};
// メニューの操作。選んだ項目 (0: 再開 / 1: 映像設定 / 2: タイトルへ) を返し、無ければ -1
inline int UpdatePause(Canvas& ui,int& menu){
    const int step=(Pressed(DIK_DOWN)||Pressed(DIK_S)?1:0)-(Pressed(DIK_UP)||Pressed(DIK_W)?1:0);
    if(step){menu=(menu+step+3)%3;PlayUISound(UISound::Move);}
    static float lastX=-1,lastY=-1;const auto pointer=ui.Pointer();const bool moved=lastX>=0&&std::abs(pointer[0]-lastX)+std::abs(pointer[1]-lastY)>.5f;lastX=pointer[0];lastY=pointer[1];
    int chosen=Pressed(DIK_RETURN)?menu:-1;
    for(int i=0;i<3;++i){ui.Track(PauseMenu[i],i==0);if(ui.Focused(PauseMenu[i])||(moved&&ui.Hover(PauseMenu[i])))menu=i;if(ui.Click(PauseMenu[i]))chosen=i;}
    return chosen;
}
inline void Pause(Canvas& ui,const PauseView& view,int menu,float age){
    if(SkipIntro())age=9;
    const float enter=EaseOut(age/.22f),dx=-480*(1-enter);
    ui.Fill({-200,-200,670+dx,1120},Yellow);
    ui.Big(Str("scale.pauseHeading"),48+dx,20,72,Black,JpBig,JpBigBase);
    ui.Text(view.stage,50+dx,126,28,Black,Jp);
    const char* labels[3]={"scale.pauseResume","scale.pauseGraphics","scale.pauseTitle"};float y=196;
    for(int i=0;i<3;++i){const bool on=i==menu;
        if(on){Arrow(ui,48+dx,y+16,30);ui.Big(Str(labels[i]),84+dx,y,52,Black,JpBig,JpBigBase);y+=82;}
        else{ui.Text(Str(labels[i]),50+dx,y+6,30,Black,Jp);y+=48;}}
    ui.Text(Str("scale.left"),50+dx,392,22,Black,Jp);ui.Text(Clock(view.timeLeft),50+dx,412,62,Black,Num);
    ui.Text(Str("scale.pauseSize"),210+dx,392,22,Black,Jp);const auto size=SizeText(view.cm);ui.Text(size,210+dx,412,62,Red,Num);
    Hint(ui,"keyboard_escape","xbox_button_b",Str("scale.pauseResumeShort"),50+dx,508);
    const float top=TapeTop+100*(1-enter);
    Tape(ui,{{5},{30},{100},{500},{5000},{12600}},top);
    // 巻尺の上の印: ランクの目標と今の大きさ
    for(const auto& [rank,cm]:view.goals){const float x=TapeX(cm);ui.Fill({x-1,top-16,2,16},Black);ui.Fill({x-11,top-40,22,24},Black);ui.Text(rank,x-6,top-40,20,Yellow,Num);}
    const float now=TapeX(view.cm);ui.Fill({now-2,top-34,5,34},Red);ui.Fill({now-74,top-62,76,28},Red);ui.Text(Str("scale.pauseNow"),now-64,top-58,19,White,Jp);
}

// 結果画面のボタン
// ゲーム中の表示。読む物は黄色や黒の面に載せ、景色の明るさに左右されないようにする
inline constexpr float HudTapeTop=662,HudTapeHeight=58,HudReadX=250,HudZoom=700;
// 巻尺の上の旗: 目標は赤、壁と最高記録は黒
struct HudFlag{float cm;std::string label;bool red=false;};
struct HudView{
    std::string stage,rank,next,gate,itemIcon,itemName,itemState;
    float cm=0,shownCm=0,time=0,pulse=0,flash=0,age=9,comboKick=0;
    int combo=0,items=0,percent=0;
    bool bestBroken=false,item=false,itemLit=false;
    std::vector<HudFlag> flags;
};
// 文字を黒い札に載せて中央に置く
inline void Notice(Canvas& ui,const std::string& text,float cx,float y,float size=26,Engine::Vector4 ink=Yellow,float alpha=1){
    const float w=ui.Width(text,size,Jp)+36;ui.Fill({cx-w*.5f,y,w,size*1.7f},WithAlpha(Black,.92f*alpha));ui.Text(text,cx-w*.5f+18,y+size*.2f,size,WithAlpha(ink,alpha),Jp);
}
// 巻尺の目盛りの文字: 小数は必要なときだけ付ける
inline std::string HudTick(float cm){char text[16];if(cm>=100)snprintf(text,sizeof(text),"%gm",std::round(cm)/100);else snprintf(text,sizeof(text),"%gcm",std::round(cm*10)/10);return text;}
// 今の大きさが読み取りの線に来るように流れる巻尺。大きくなるほど左へ流れる
inline float HudTapeX(float cm,float now){return HudReadX+HudZoom*std::log(cm/now);}
inline void HudTape(Canvas& ui,float now,float reveal){
    const float top=HudTapeTop,height=HudTapeHeight,end=1280*Clamp01(reveal);
    ui.Fill({-200,top,200+(reveal>=1?1480:end),300},Yellow);ui.Fill({0,top,end,2},Black);
    const float low=now,high=now*std::exp((1340-HudReadX)/HudZoom);
    for(float decade=std::pow(10.f,std::floor(std::log10(low)));decade<high;decade*=10)
        // 1〜2 は 0.1、2〜5 は 0.2、5〜10 は 0.5 刻み
        for(int i=0;i<45;++i){const float m=i<10?1+i*.1f:i<25?2+(i-10)*.2f:i<35?5+(i-25)*.5f:99;if(m>=10)break;
            const float cm=decade*m,x=HudTapeX(cm,now);if(x<HudReadX||x>end)continue;
            const bool major=i==0||i==5||i==10||i==15||i==25||i==29,mid=i%5==0;
            ui.Fill({x,top,major?2.6f:1.4f,height*(major?1:mid?.7f:.42f)},Black);
            if(major)ui.Text(HudTick(cm),x+7,top+18,26,Black,Num);}
}
// ゲーム中の表示全体
inline void Hud(Canvas& ui,const HudView& view){
    const float in=EaseOut(view.age/.7f),dx=(1-in);
    HudTape(ui,view.shownCm,in);
    // 旗: 近い順に立て、右へはみ出す物は見える端に寄せる。赤い目標を先に置き、他の旗と重なる旗は出さない
    {const float rightEdge=view.item?1030.f:1266.f,y=HudTapeTop-40;std::vector<const HudFlag*> order;for(const auto& flag:view.flags)order.push_back(&flag);
        std::stable_sort(order.begin(),order.end(),[](const HudFlag* a,const HudFlag* b){return a->red!=b->red?a->red:a->cm<b->cm;});
        std::vector<std::pair<float,float>> placed;
        for(const HudFlag* flag:order){const float x=HudTapeX(flag->cm,view.shownCm);if(x<HudReadX)continue;
            // 目盛りの書体には数字しかないので、漢字を含む黒い旗は日本語の書体にする
            const char* font=flag->red?Num:Jp;const float size=flag->red?28.f:24.f,w=ui.Width(flag->label,size,font)+22,at=(std::min)(x,rightEdge-w);
            if(std::any_of(placed.begin(),placed.end(),[&](const auto& other){return at<other.second+6&&at+w+6>other.first;}))continue;
            placed.push_back({at,at+w});const auto color=flag->red?Red:Black;
            if(at==x)ui.Fill({x-1.5f,y,3,HudTapeHeight+40},color);
            ui.Fill({at,y,w,38},color);ui.Text(flag->label,at+11,y+(flag->red?1.f:3.f),size,flag->red?White:Yellow,font);}}
    // 左下: 大きさの札。右の端が巻尺の読み取りの線になる
    const Rect block{-300*dx,606,HudReadX,114};
    ui.Fill({block.x-200,block.y,block.w+200,HudTapeTop+HudTapeHeight-block.y+10},Yellow);
    ui.Text(Str("scale.hudSize"),block.x+18,block.y+3,22,Black,Jp);
    {const std::string items=std::to_string(view.items)+Str("scale.itemsUnit")+"  "+std::to_string(view.percent)+"%";ui.Text(items,block.x+block.w-14-ui.Width(items,22,Jp),block.y+3,22,Black,Jp);}
    {const std::string value=SizeText(view.cm);const auto space=value.find(' ');const std::string number=value.substr(0,space),unit=space==std::string::npos?"":value.substr(space+1);
        // 文字の見た目の下端を札の下に揃える。長い数字は札に収める
        const float room=block.w-40-ui.Width(unit,56,Num),fit=(std::min)(1.f,room/(std::max)(1.f,ui.Width(number,128,Num))),size=128*fit*(1+.1f*view.pulse);
        ui.Text(number,block.x+18,712-size*.8f,size,view.pulse>0?Red:Black,Num);ui.Text(unit,block.x+26+ui.Width(number,128*fit,Num),712-56*.8f,56,Black,Num);}
    ui.Fill({HudReadX-2.5f,block.y,5,HudTapeTop+HudTapeHeight-block.y},Red);
    // 札の上: 最高記録の更新とコンボ
    float y=block.y;
    if(view.bestBroken){y-=38;ui.Fill({0,y,ui.Width(Str("scale.hudBest"),24,Jp)+40,36},Black);ui.Text(Str("scale.hudBest"),20,y+2,24,Yellow,Jp);}
    if(view.combo>1){const float kick=1+.22f*Clamp01(view.comboKick),size=72*kick;const std::string count=std::to_string(view.combo);y-=92;
        const float w=ui.Width(count,size,Num);ui.Fill({0,y,w+142,86},Black);ui.Text(count,20,y-4-(size-72)*.5f,size,Yellow,Num);ui.Text(Str("scale.hudCombo"),32+w,y+30,28,White,Jp);}
    // 左上: ステージ名とランク、次の目標、次の壁。面は文字の大きさに合わせる
    {const float top=18,x=-420*dx,w=ui.Width(view.stage,34,Jp);
        ui.Fill({x-20,top,w+(view.rank.empty()?64.f:112.f),52},Yellow);ui.Text(view.stage,x+24,top+4,34,Black,Jp);
        if(!view.rank.empty()){ui.Dot(x+w+56,top+26,21,Red);const float rw=ui.Width(view.rank,36,Num);ui.Text(view.rank,x+w+56-rw*.5f,top+7,36,White,Num);}
        float line=top+52;
        if(!view.next.empty()){const float tw=ui.Width(view.next,26,Jp)+44;ui.Fill({x-20,line,tw+20,42},Black);ui.Text(view.next,x+24,line+5,26,Yellow,Jp);line+=42;}
        if(!view.gate.empty()){const float tw=ui.Width(view.gate,22,Jp)+44;ui.Fill({x-20,line,tw+20,36},WithAlpha(Black,.82f));ui.Text(view.gate,x+24,line+4,22,White,Jp);}}
    // 右上: 残り時間 (残り 30 秒で赤く脈打つ)。面は文字の幅に合わせる
    {const bool hurry=view.time<=30;const float beat=hurry?.5f+.5f*std::sin(view.age*10):0,size=84*(1+.06f*beat);const std::string clock=Clock(view.time),&label=Str("scale.left");
        const float clockW=ui.Width(clock,84,Num),labelW=ui.Width(label,24,Jp),tabW=clockW+labelW+46,x=1280-tabW+300*dx;const auto ink=hurry?White:Black;
        ui.Fill({x,18,tabW+200,64},hurry?Red:Yellow);ui.Text(label,x+16,36,24,ink,Jp);ui.Text(clock,x+labelW+28,50-.52f*size,size,ink,Num);}
    // 右下: このステージの変形アイテム
    if(view.item){const Rect tab{1040+300*dx,566,240+200,96};ui.Fill(tab,view.itemLit?Yellow:WithAlpha(Black,.86f));const auto ink=view.itemLit?Black:White;
        ui.Image(std::string("Resources/Textures/UI/HUD/")+view.itemIcon+".png",{tab.x+14,tab.y+14,68,68},ink);
        ui.Text(view.itemName,tab.x+92,tab.y+10,30,ink,Jp);ui.Text(view.itemState,tab.x+92,tab.y+54,20,view.itemLit?Black:Yellow,Jp);}
    // ランクが上がった瞬間の知らせ
    if(view.flash>0&&!view.rank.empty()){const float pop=Overshoot((1-Clamp01(view.flash))*5),fade=Clamp01(view.flash*3);const std::string text=Str("scale.hudRankUp")+" "+view.rank;
        const float w=ui.Width(text,56*pop,Jp)+60;ui.Fill({640-w*.5f,134,w,92*pop},WithAlpha(Red,fade));ui.Text(text,640-w*.5f+30,146,56*pop,WithAlpha(White,fade),Jp);}
}

inline constexpr Rect ResultNext{48,574,410,50},ResultRetry{48,650,196,50},ResultTitle{254,650,204,50},ResultRetryOnly{48,650,196,50},ResultTitleOnly{254,650,204,50};
// 結果画面に出す内容。live ならゲームの本物のスライムの上に重ね、体の左右と上の端 (UI 座標) を測る
struct ResultView{std::string stage;float cm=0,goalCm=0,timeLeft=0;std::string rank;bool won=false,newBest=false,hasNext=false;int items=0,percent=0;bool live=false;float bodyLeft=520,bodyRight=1240,bodyTop=330;};
// 比べる物: 文字の名前と長さの基準 (cm)
struct Reference{const char* key;float length;};
inline const Reference* PickReference(float cm){
    static const Reference items[]={{"Block",5},{"Rat",20},{"Tyre",65},{"Barrel",90},{"Car",450}};
    const Reference* pick=&items[0];for(const auto& item:items)if(cm/item.length>=2)pick=&item;return pick;
}
// 結果画面の 1 つのボタン
inline void ResultButton(Canvas& ui,Rect b,const char* key,const char* pad,const std::string& label,bool primary){
    ui.Track(b,primary);const bool hover=ui.Hover(b);const float lift=hover?3.f:0.f;
    ui.Fill({b.x,b.y-lift,b.w,b.h},primary?Red:Black);if(hover)ui.Fill({b.x,b.y+b.h-lift,b.w,3},primary?Black:Red);
    const std::string base="Resources/Textures/UI/kenney_input-prompts_1.5/";
    ui.Image(UsingPad()?base+"Xbox Series/Default/"+pad+".png":base+"Keyboard & Mouse/Default/"+key+".png",{b.x+8,b.y+7-lift,36,36});
    ui.Text(label,b.x+54,b.y+13-lift,24,White,Jp);
}
// 結果画面: 巻尺が流れ込み、本物のスライムの体の端に寸法線を合わせて測り、記録の面が左から入る
inline void Result(Canvas& ui,const ResultView& view,float age){
    if(SkipIntro())age=9;
    static float lastAge=0;
    auto crossed=[&](float at){return lastAge<at&&age>=at;};
    const float base=633;
    // 検証の撮影では本物のスライムが無いので、黄色の地に連番を置く
    if(!view.live){Backdrop(ui);Slime(ui,(view.bodyLeft+view.bodyRight)*.5f,base,view.bodyRight-view.bodyLeft);}
    // 寸法線: 体の左端から右端へ伸び、両端から巻尺へ線を下ろす
    const float measure=EaseOut((age-.95f)/.75f),lineY=(std::max)(view.bodyTop-24,36.f),lineEnd=view.bodyLeft+(view.bodyRight-view.bodyLeft)*measure;
    if(age>.95f){
        ui.Fill({view.bodyLeft-1,lineY,3,base-lineY},WithAlpha(Red,.75f));ui.Fill({lineEnd-1,lineY,3,base-lineY},WithAlpha(Red,.75f));
        ui.Fill({view.bodyLeft,lineY-3,lineEnd-view.bodyLeft,6},Red);ui.Fill({view.bodyLeft-3,lineY-20,6,40},Red);ui.Fill({lineEnd-3,lineY-20,6,40},Red);
    }
    // 巻尺が左から流れ込む
    // 測り始めたら、目盛りは体の左端を 0 とした読みに変わる
    Tape(ui,age<.95f?std::vector<Mark>{{5},{30},{100},{500},{5000},{12600}}:std::vector<Mark>{},base,EaseOut((age-.1f)/.7f),70);
    if(age>.95f){const std::string reading=SizeText(view.cm*measure);ui.Text("0",view.bodyLeft+8,base+35,27,Red,Num);ui.Text(reading,lineEnd-ui.Width(reading,27,Num)-8,base+35,27,Red,Num);}
    // 終了の合図
    if(age<1.15f){const float in=EaseOut(age/.18f),out=1-Clamp01((age-.9f)/.25f);const std::string& label=Str("scale.finish");
        const float w=ui.BigWidth(label,84,JpBig,JpBigBase)+70;ui.Fill({640-w*.5f*in,264,w*in,118},WithAlpha(Black,out));
        if(in>.9f)ui.Big(label,640-w*.5f+35,274,84,WithAlpha(Yellow,out),JpBig,JpBigBase);}
    // 記録の面: 数字は寸法線と一緒に増える
    const float enter=EaseOut((age-.7f)/.32f),dx=-500*(1-enter);
    if(age>.7f){
        ui.Fill({-200,-200,680+dx,833+200},Yellow);
        ui.Text(view.stage,48+dx,22,22,Black,Jp);
        const std::string value=SizeText(view.cm*measure);const auto space=value.find(' ');const std::string number=value.substr(0,space),unit=space==std::string::npos?"":value.substr(space+1);
        ui.Big(number,46+dx,20,120,Black,NumBig,NumBigBase);ui.Big(unit,52+dx+ui.BigWidth(number,120,NumBig,NumBigBase),70,56,Black,NumBig,NumBigBase);
    }
    // 比べる物と記録
    if(age>1.6f){const float a=EaseOut((age-1.6f)/.3f);float y=206;const Reference* ref=PickReference(view.cm);const std::string key=ref->key;
        auto stat=[&](const std::string& head,const std::string& number2,const std::string& tail){float x=48+dx;ui.Text(head,x,y+20,24,WithAlpha(Black,a),Jp);x+=ui.Width(head,24,Jp)+8;
            ui.Text(number2,x,y,46,WithAlpha(Black,a),Num);x+=ui.Width(number2,46,Num)+8;ui.Text(tail,x,y+20,24,WithAlpha(Black,a),Jp);y+=60;};
        stat(Str(("scale.ref"+key).c_str()),std::to_string((std::max)(1,int(view.cm/ref->length))),Str(("scale.unit"+key).c_str())+Str("scale.compareTail"));
        stat(Str("scale.items"),std::to_string(view.items),Str("scale.itemsUnit"));
        stat(Str("scale.town"),std::to_string(view.percent),"%");
        stat(Str("scale.left"),Clock(view.timeLeft),"");
    }
    // ランク (届かなかったときは「時間切れ」の札) を貼る
    if(age>1.85f){const float pop=Overshoot((age-1.85f)/.28f),r=58*pop;
        if(!view.rank.empty()&&view.won){ui.Dot(388+dx,300,r,Red);ui.Big(view.rank,388+dx-ui.BigWidth(view.rank,80*pop,NumBig,NumBigBase)*.5f,300-40*pop,80*pop,White,NumBig,NumBigBase);}
        else{const std::string label=Str("scale.timeUp");const float w=ui.Width(label,28,Jp)+24;ui.Fill({450-w+dx,280,w*pop,48*pop},Red);ui.Text(label,462-w+dx,285,28,White,Jp);}
        if(crossed(1.85f))PlayGulp(view.won);
    }
    // 目標との差、または記録の更新
    if(age>2.05f){
        if(view.newBest){const std::string label=Str("scale.newBest");const float w=ui.Width(label,20,Jp)+20;ui.Fill({48+dx,164,w,30},Black);ui.Text(label,58+dx,166,20,Yellow,Jp);}
        if(!view.won&&view.goalCm>view.cm){const std::string text=Str("scale.goalLeft")+SizeText(view.goalCm-view.cm);const float w=ui.Width(text,20,Jp)+20;const float x=view.newBest?230.f:48.f;ui.Fill({x+dx,164,w,30},Red);ui.Text(text,x+10+dx,166,20,White,Jp);}
        // ボタン
        if(view.hasNext)ResultButton(ui,ResultNext,"keyboard_n","xbox_button_a",Str("scale.next"),true);
        ResultButton(ui,view.hasNext?ResultRetry:ResultRetryOnly,"keyboard_r","xbox_button_x",Str("scale.retry"),!view.hasNext);
        ResultButton(ui,view.hasNext?ResultTitle:ResultTitleOnly,"keyboard_tab","xbox_button_y",Str("scale.title"),false);
    }
    lastAge=age;
}
}
