// GameUI.h
// プレイヤーが目にする全画面 (タイトル・セレクト・ポーズ・結果・HUD) で共通の UI 部品。
// 1280x720 の設計座標で描き、実際の解像度へ拡大して表示する。ゲームパッドのフォーカス移動もここで扱う。
#pragma once
// Windows のヘッダーの min / max マクロを止める (std::min / std::max と衝突するため)
#ifndef NOMINMAX
#define NOMINMAX
#endif
// 描画・入力・ウィンドウの大きさ (エンジン側)
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include "../Data/GameData.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
// エディタのビルドでは ImGui の入力も見る
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

// プレイヤー向けの画面はすべて同じ 1280x720 の設計座標を使う。
// UI は Draw() の中で登録する (Renderer::EndFrame でスプライトと文字がまとめて描かれる前に)
namespace Game::UI {
// キーが押された瞬間か (DirectInput と ImGui の両方を見る。エディタの Game ビュー上でも反応させるため)
inline bool Pressed(BYTE key) {
    // 入力 (DirectInput) で押された瞬間なら true
    auto* input=Engine::Input::GetInstance();
    if(input && input->Trigger(key))return true;
#ifdef USE_IMGUI
    // ImGui があれば、対応するキーを ImGui のキーに置き換えて調べる
    if(ImGui::GetCurrentContext()) {
        ImGuiKey mapped=key==DIK_TAB?ImGuiKey_Tab:key==DIK_R?ImGuiKey_R:
            key==DIK_RETURN?ImGuiKey_Enter:key==DIK_ESCAPE?ImGuiKey_Escape:ImGuiKey_None;
        // 対応するキーがあれば ImGui の判定を返す
        if(mapped!=ImGuiKey_None)return ImGui::IsKeyPressed(mapped,false);
    }
#endif
    return false;
}
// ゲームパッド
inline bool UsingPad() {
    // 最後に使った機器がゲームパッドなら true
    auto* input=Engine::Input::GetInstance();
    return input && input->UsingGamepad();
}
// ゲームパッドのボタンが押された瞬間か
inline bool PadPressed(uint32_t button) {
    auto* input=Engine::Input::GetInstance();
    return input && input->PadTrigger(button);
}
// 戻る/キャンセル: キーボードは Esc (または指定のキー)、ゲームパッドは B
inline bool BackPressed(BYTE key=DIK_ESCAPE) { return Pressed(key)||PadPressed(XINPUT_GAMEPAD_B); }

// UI のフォント (英字用・日本語用) とゲームのタイトル
inline constexpr const char* Font = "Resources/Fonts/Rajdhani/Rajdhani-SemiBold.ttf";
inline constexpr const char* JapaneseFont = "Resources/Fonts/MPLUSRounded1c/MPLUSRounded1c-ExtraBold.ttf";
// 画面に出す文字 (Resources/Data/ui_text.json の "区切り.項目")
inline const std::string& Str(const char* key) { return Data::Text("ui_text.json", key); }
// 文字の中の {n} を数字に置き換える
inline std::string Format(const std::string& text, long long value) {
    std::string out = text; const auto at = out.find("{n}");
    if (at != std::string::npos) out.replace(at, 3, std::to_string(value));
    return out;
}
// ゲームのタイトル
inline const std::string& GameTitle() { return Str("common.gameTitle"); }
// 配色: 苔むした夜・つやのあるライム色のスライム・毒々しい青緑・琥珀色の警告・クリーム色の文字
inline const Engine::Vector4 Night{.032f,.052f,.043f,1}, Ink{.045f,.078f,.063f,1}, Moss{.085f,.135f,.105f,1},
    Paper{.94f,.96f,.86f,1}, Muted{.56f,.67f,.58f,1}, Lime{.74f,.95f,.33f,1}, Teal{.3f,.94f,.78f,1},
    Gold{1,.72f,.3f,1}, Coral{1,.43f,.35f,1};
// 色の不透明度を掛ける / 2色を混ぜる
inline Engine::Vector4 WithAlpha(Engine::Vector4 c,float a) { c.w*=a; return c; }
inline Engine::Vector4 Mix(Engine::Vector4 a,Engine::Vector4 b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t}; }
// 起動からの経過秒数 (UI のアニメーション用)
inline float Seconds() {
    static const auto start=std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
}
// 決まった値から 0〜1 の疑似乱数を作る / 文字列から種の値を作る
inline float Noise(float i) { float v=std::sin(i*12.9898f+78.233f)*43758.5453f; return v-std::floor(v); }
inline float SeedOf(const std::string& text) { float seed=0; for(char ch:text)seed=seed*1.37f+float(static_cast<unsigned char>(ch)); return std::fmod(seed,97.f); }
// UI で使うテクスチャのパス
namespace Art {
// 白 1x1 (塗りつぶし用)・角丸の板・角丸の枠・筆の塗り・しぶき・粒状感・端のぼかし・円・光
inline constexpr const char* White="Resources/Textures/white1x1.png";
inline constexpr const char* Round="Resources/Textures/UI/ui_round.png";
inline constexpr const char* RoundRing="Resources/Textures/UI/ui_round_ring.png";
inline constexpr const char* Brush="Resources/Textures/UI/ui_brush.png";
inline constexpr const char* Splat="Resources/Textures/UI/ui_splat.png";
inline constexpr const char* Grain="Resources/Textures/UI/ui_grain.png";
inline constexpr const char* Fade="Resources/Textures/UI/ui_fade.png";
inline constexpr const char* Circle="Resources/Textures/UI/slime_circle.png";
inline constexpr const char* Glow="Resources/Textures/UI/slime_glow.png";
}
// UI 座標の矩形 (左上・幅・高さ)
struct Rect { float x,y,w,h; };
// 画面の共通レイアウト (各シーンのクリック判定でも使う)
// リトライ・ステージ選択・再開・タイトルへ・ステージ・タイトルのスタート
inline constexpr Rect Retry{96,548,320,66}, Select{436,556,268,50},
    Resume{96,420,340,66}, Title{96,502,268,50}, Stage{96,486,470,76}, TitleStart{96,470,430,74};

// 全 Canvas で共有するゲームパッドのフォーカス移動。
// そのフレームで Click()/Track() に渡した矩形が次のフレームのフォーカス対象になるので、
// 各画面が自前のメニュー状態を持たなくても十字キー / 左スティックで移動できる
struct Navigator {
    // フォーカス対象 (矩形と、最初にフォーカスする主ボタンか)
    struct Item { Rect rect; bool primary=false; };
    // 前フレームの入力番号 / 今フレームと前フレームの対象一覧
    uint64_t frame=~0ull;
    std::vector<Item> current,previous;
    // フォーカス中の矩形と、A ボタンで決定されたか
    Rect focus{}; bool hasFocus=false,activate=false;
    // 十字キーを押しっぱなしにしたときのリピート
    int heldDirection=-1; float repeat=0;
    // 前回の時刻 (リピートの間隔を測る)
    std::chrono::steady_clock::time_point last=std::chrono::steady_clock::now();
    // 2つの矩形が同じか (誤差 0.5 まで)
    static bool Same(const Rect& a,const Rect& b) {
        return std::abs(a.x-b.x)<.5f&&std::abs(a.y-b.y)<.5f&&std::abs(a.w-b.w)<.5f&&std::abs(a.h-b.h)<.5f;
    }
    // 一覧から同じ矩形を探す
    static Item* Find(std::vector<Item>& items,const Rect& b) {
        for(auto& item:items)if(Same(item.rect,b))return &item;
        return nullptr;
    }
    // このフレームのフォーカス対象として登録する
    void Register(const Rect& b,bool primary=false) {
        // このフレームの準備をしておく
        Begin();
        // 同じ矩形が既にあれば主ボタンの印だけ足す
        if(auto* item=Find(current,b)){item->primary|=primary;return;}
        // 無ければ追加する
        current.push_back({b,primary});
    }
    // 指定方向で一番近い対象へフォーカスを移す (正面方向の距離と横ずれから点数を付ける)
    void Move(float dx,float dy) {
        // 今のフォーカスの中心
        const float fx=focus.x+focus.w*.5f,fy=focus.y+focus.h*.5f;
        // 一番良い点数と、その対象
        float best=1e9f;const Item* next=nullptr;
        // 前フレームの対象を順に調べる
        for(const auto& item:previous){
            // 今のフォーカス自身は飛ばす
            if(Same(item.rect,focus))continue;
            // 中心どうしの差
            float vx=item.rect.x+item.rect.w*.5f-fx,vy=item.rect.y+item.rect.h*.5f-fy;
            // 指定方向へ進んでいなければ飛ばす
            float along=vx*dx+vy*dy;if(along<=1)continue;
            // 正面方向の距離 + 横ずれの 2 倍 (小さいほど良い)
            float score=along+std::abs(vx*dy-vy*dx)*2;
            if(score<best){best=score;next=&item;}
        }
        // 見つかれば移す
        if(next)focus=next->rect;
    }
    // 入力の1フレームにつき1回、そのフレームで最初に Canvas を使う前に実行する
    void Begin() {
        auto* input=Engine::Input::GetInstance();
        // 入力が無いか、このフレームで既に済んでいれば何もしない
        if(!input||input->FrameIndex()==frame)return;
        // 古いフレーム (プレイ前など) の対象は、新しく開いたメニューへ持ち越さない
        const bool consecutive=input->FrameIndex()==frame+1;
        // フレーム番号を更新する
        frame=input->FrameIndex();
        // 今フレームの一覧を前フレームへ回し、空にする
        previous.swap(current);current.clear();activate=false;
        // 連続していなければ前の一覧とフォーカスを捨てる
        if(!consecutive){previous.clear();hasFocus=false;}
        // 前回からの時間 (最大 0.1 秒)
        auto now=std::chrono::steady_clock::now();
        float dt=std::clamp(std::chrono::duration<float>(now-last).count(),0.f,.1f);last=now;
        // 対象が無いか、ゲームパッドを使っていなければここで終わる
        if(previous.empty()||!input->UsingGamepad()){heldDirection=-1;return;}
        // フォーカスが無い・消えた場合は主ボタン (無ければ先頭) にフォーカスする
        if(!hasFocus||!Find(previous,focus)){
            // 主ボタンを探す (無ければ先頭)
            const Item* first=&previous.front();
            for(const auto& item:previous)if(item.primary){first=&item;break;}
            focus=first->rect;hasFocus=true;
        }
        // 十字キー / 左スティックで移動 (押した瞬間と、押しっぱなしのリピート)
        using E=Engine::Input;
        // 上下左右 (十字キーと左スティックの両方)
        const uint32_t up=XINPUT_GAMEPAD_DPAD_UP|E::kPadLsUp,down=XINPUT_GAMEPAD_DPAD_DOWN|E::kPadLsDown,
            left=XINPUT_GAMEPAD_DPAD_LEFT|E::kPadLsLeft,right=XINPUT_GAMEPAD_DPAD_RIGHT|E::kPadLsRight;
        // 押されている方向 (0: 上, 1: 下, 2: 左, 3: 右, -1: なし)
        int direction=input->PadDown(up)?0:input->PadDown(down)?1:input->PadDown(left)?2:input->PadDown(right)?3:-1;
        // このフレームで動かすか
        bool step=false;
        // 押す向きが変わったら 1 回動かし、次のリピートまで 0.38 秒
        if(direction!=heldDirection){heldDirection=direction;repeat=.38f;step=direction>=0;}
        // 押し続けていれば 0.11 秒ごとに動かす
        else if(direction>=0&&(repeat-=dt)<=0){repeat=.11f;step=true;}
        // 方向のベクトルで移動する
        if(step){const float dirs[4][2]={{0,-1},{0,1},{-1,0},{1,0}};Move(dirs[direction][0],dirs[direction][1]);}
        // A ボタンで決定
        activate=input->PadTrigger(XINPUT_GAMEPAD_A);
    }
};
// 全 Canvas で共有するフォーカス移動の状態
inline Navigator& Nav() { static Navigator nav; return nav; }

// UI を描く道具。1280x720 の座標で受け取り、画面の大きさに合わせて拡大・中央寄せして描く
class Canvas {
    // 描画先・拡大率・左右上下の余白・全体の不透明度
    Engine::Renderer* r_;
    float s_, ox_, oy_, alpha_;
    // マウス座標の上書き (エディタの Game ビュー用)
    bool pointerOverride_=false;
    float pointerX_=0,pointerY_=0;
    // 画像を1枚描く (UV と回転を指定できる)
    void Sprite(const char* path,Rect b,Engine::Vector4 color,Engine::Vector4 uv={1,1,0,0},float rotation=0) {
        // 設計座標から画面の座標へ変換する
        Engine::Renderer::SpriteDesc d; d.x=ox_+b.x*s_;d.y=oy_+b.y*s_;d.w=b.w*s_;d.h=b.h*s_;
        // 色 (全体の不透明度を掛ける)・UV・回転
        d.color=Tint(color);d.uvScaleOffset=uv;d.rotationRad=rotation;
        // テクスチャを読み込んで (読み込み済みならそのまま) 描く
        r_->DrawSprite(r_->LoadTexture2D(path),d);
    }
    // 3x3 分割: テクスチャの決まった角を、画面上の任意の角の丸さに割り当てて描く
    void Sliced(const char* path,Rect b,Engine::Vector4 color,float radius) {
        // テクスチャの角の割合 (96 ピクセル中 30 ピクセル)
        constexpr float corner=30.f/96.f;
        // 角の丸さ (矩形の半分を超えないように)
        const float r=(std::max)(.5f,(std::min)({radius,b.w*.5f,b.h*.5f}));
        // 画面上の分割位置
        const float xs[4]={b.x,b.x+r,b.x+b.w-r,b.x+b.w},ys[4]={b.y,b.y+r,b.y+b.h-r,b.y+b.h};
        // テクスチャ上の分割位置
        const float us[4]={0,corner,1-corner,1};
        // 9 つの区画を描く (大きさが 0 の区画は飛ばす)
        for(int row=0;row<3;++row)for(int col=0;col<3;++col){
            const float w=xs[col+1]-xs[col],h=ys[row+1]-ys[row];if(w<=.01f||h<=.01f)continue;
            Sprite(path,{xs[col],ys[row],w,h},color,{us[col+1]-us[col],us[row+1]-us[row],us[col],us[row]});
        }
    }
public:
    // 拡大率は 1280x720 が画面に収まる大きさにする
    Canvas(Engine::Renderer* r, float w=Engine::WindowDX::kW, float h=Engine::WindowDX::kH, float alpha=1)
        : r_(r), s_((std::min)(float(Engine::WindowDX::kW)/1280,float(Engine::WindowDX::kH)/720)),
          ox_((Engine::WindowDX::kW-1280*s_)*.5f), oy_((Engine::WindowDX::kH-720*s_)*.5f), alpha_(alpha) {(void)w;(void)h;}
    // Kenney の UI パックの画像のパス
    static std::string Asset(const char* name) { return std::string("Resources/Textures/UI/kenney_ui-pack/PNG/")+name; }
    // マウス座標は EditorUI が Game ビューから最終画像の座標へ換算済み。
    // スプライトと文字もその画像の座標で置くので、ImGui での拡大は1回だけになる
    void SetPointer(float x,float y) { pointerOverride_=true;pointerX_=x;pointerY_=y; }
    // マウスの位置を UI 座標で返す
    std::array<float,2> Pointer()const {
        // 入力からマウス座標を取り、上書きがあればそちらを使う
        float x=0,y=0;if(auto* input=Engine::Input::GetInstance())input->GetMousePos(x,y);
        // 画面の座標から設計座標へ戻す
        if(pointerOverride_){x=pointerX_;y=pointerY_;}return {(x-ox_)/s_,(y-oy_)/s_};
    }
    // 全体の不透明度を掛ける / さらに薄くする
    Engine::Vector4 Tint(Engine::Vector4 c) const { c.w*=alpha_; return c; }
    void MultiplyAlpha(float alpha) { alpha_*=std::clamp(alpha,0.f,1.f); }
    // 画像・回転した画像・塗りつぶし・線
    void Image(const std::string& path, Rect b, Engine::Vector4 color={1,1,1,1}) { Sprite(path.c_str(),b,color); }
    void ImageRot(const std::string& path,Rect b,float rotation,Engine::Vector4 color={1,1,1,1}) { Sprite(path.c_str(),b,color,{1,1,0,0},rotation); }
    void Fill(Rect b, Engine::Vector4 color) { Sprite(Art::White,b,color); }
    void Line(float x,float y,float tx,float ty,float width,Engine::Vector4 color){
        // 線の長さ (ほぼ 0 なら描かない)
        float dx=tx-x,dy=ty-y,length=std::sqrt(dx*dx+dy*dy);if(length<.01f)return;
        // 中点に置いた細長い板を、線の向きへ回して描く
        Sprite(Art::White,{(x+tx-length)*.5f,(y+ty-width)*.5f,length,width},color,{1,1,0,0},std::atan2(dy,dx));
    }

    // 図形
    void Rounded(Rect b,Engine::Vector4 color,float radius=14) { Sliced(Art::Round,b,color,radius); }
    void RoundedRing(Rect b,Engine::Vector4 color,float radius=14) { Sliced(Art::RoundRing,b,color,radius); }
    void Dot(float x,float y,float r,Engine::Vector4 color) { Sprite(Art::Circle,{x-r,y-r,r*2,r*2},color); }
    void Halo(float x,float y,float r,Engine::Vector4 color) { Sprite(Art::Glow,{x-r,y-r,r*2,r*2},color); }
    // かすれた筆の塗り。少し傾けると手描きらしく見える
    void Brush(Rect b,Engine::Vector4 color,float rotation=0,bool flip=false) {
        Sprite(Art::Brush,b,color,flip?Engine::Vector4{-1,1,1,0}:Engine::Vector4{1,1,0,0},rotation);
    }
    void Splat(float x,float y,float r,Engine::Vector4 color,float rotation=0) { Sprite(Art::Splat,{x-r,y-r,r*2,r*2},color,{1,1,0,0},rotation); }
    // 縁から垂れるスライムのしずく。ゆっくり伸び縮みさせて画面が止まって見えないようにする
    void Drips(float x,float y,float w,Engine::Vector4 color,float seed=0,float scale=1) {
        // 70 ピクセルにつき 1 本
        const int count=(std::max)(1,int(w/70));const float t=Seconds();
        for(int i=0;i<count;++i){
            // しずくの位置 (乱数で少しずらす)
            const float n=Noise(seed+float(i)*3.1f),dx=x+w*(float(i)+.25f+.5f*n)/float(count);
            // 長さ (ゆっくり伸び縮みする)
            const float length=(8+20*Noise(seed+float(i)*7.7f))*scale*(.88f+.12f*std::sin(t*1.6f+float(i)*1.9f+seed));
            // 太さ
            const float width=(5+3*n)*scale;
            // 付け根
            Dot(dx,y-1,width*1.15f,color);
            // 付け根から先まで点を重ねて、だんだん細くする
            const int steps=(std::max)(2,int(length/2.5f));
            for(int k=1;k<=steps;++k){const float f=float(k)/float(steps);
                Dot(dx+std::sin(t*1.3f+seed+f*2)*.6f,y+length*f,width*(.55f-.22f*f),color);}
            // 先のふくらみ
            Dot(dx,y+length+width*.15f,width*.72f,color);
        }
    }
    // 3D の画面の上でも読めるよう、箱ではなく柔らかい暗いぼかしを敷く
    void Shade(Rect b,float strength=.55f) { Halo(b.x+b.w*.5f,b.y+b.h*.5f,(std::max)(b.w,b.h)*.75f,WithAlpha(Night,strength)); }
    // 画面全体に粒状感の画像を薄く重ねる (画面の大きさに合わせて繰り返す)
    void Grain(float amount=.05f) {
        const float w=1280+2*ox_/s_,h=720+2*oy_/s_;
        Sprite(Art::Grain,{-ox_/s_,-oy_/s_,w,h},WithAlpha(Paper,amount),{w*s_/128,h*s_/128,0,0});
    }
    // 小さなスライムのマーク: つやのある体・2つの目・数秒ごとのまばたき (sleepy は寝ている顔)
    void SlimeMark(float x,float y,float r,float alpha=1,bool sleepy=false) {
        // 時刻と、伸び縮みの量
        const float t=Seconds(),wob=std::sin(t*3.2f)*.06f;
        // 外側の体
        Dot(x,y,r,WithAlpha({.47f,.78f,.25f,1},alpha));
        // 内側の明るい体 (伸び縮みする)
        Sprite(Art::Circle,{x-r*(1+wob),y-r*(1-wob)-r*.08f,r*2*(1+wob),r*2*(1-wob)},WithAlpha({.62f,.9f,.33f,1},alpha));
        // つやの光
        Dot(x-r*.38f,y-r*.42f,r*.22f,WithAlpha(Paper,.75f*alpha));
        // 寝ている顔: 線の目と、浮かんでいく z
        if(sleepy){
            for(float side:{-1.f,1.f})Line(x+side*r*.33f-r*.13f,y+r*.02f,x+side*r*.33f+r*.13f,y+r*.02f,(std::max)(2.f,r*.06f),WithAlpha(Ink,alpha));
            for(int k=0;k<3;++k){const float f=std::fmod(t*.35f+float(k)/3,1.f);
                Text("z",x+r*.7f+f*r*.5f,y-r*1.1f-f*r*.9f,r*(.25f+.2f*f),WithAlpha(Paper,alpha*(1-f)*.8f));}
            return;
        }
        // ときどき瞬きする
        const float eye=std::fmod(t,3.4f)>3.25f?r*.06f:r*.24f;
        // 左右の目
        for(float side:{-1.f,1.f})Sprite(Art::Circle,{x+side*r*.33f-r*.1f,y-eye*.5f,r*.2f,eye},WithAlpha(Ink,alpha));
    }

    // 文字
    // 文字列を描く (左揃え / 中央揃え)
    void Text(const std::string& label,float x,float y,float size=24,Engine::Vector4 color=Paper,const char* font=Font) {
        // 描画側の文字の描画へ (64 ピクセルを基準の大きさにする)
        r_->DrawString(label,ox_+x*s_,oy_+y*s_,size/64*s_,Tint(color),font);
    }
    void Center(const std::string& label,float x,float y,float size=24,Engine::Vector4 color=Paper,const char* font=Font) {
        // 幅の半分だけ左へずらして書く
        Text(label,x-r_->MeasureTextWidth(label,size/64,font)*.5f,y,size,color,font);
    }
    // ゲーム画面の上で読みやすいよう、少しずらした影を付けた文字
    void Bold(const std::string& label,float x,float y,float size,Engine::Vector4 color=Paper,const char* font=Font) {
        Text(label,x+size*.04f,y+size*.06f,size,{0,0,0,.55f*color.w},font);Text(label,x,y,size,color,font);
    }
    // 文字列の幅
    float Width(const std::string& label,float size=24,const char* font=Font) const {
        return r_->MeasureTextWidth(label,size/64,font);
    }
    // 字間を空けた小さな大文字: 各画面の「ラベル」の書体
    float SpacedWidth(const std::string& text,float size,float spacing) const {
        float width=0;for(char ch:text)width+=Width(std::string(1,ch),size)+spacing;
        return text.empty()?0:width-spacing;
    }
    float Spaced(const std::string& text,float x,float y,float size,float spacing,Engine::Vector4 color) {
        for(char ch:text){const std::string glyph(1,ch);Text(glyph,x,y,size,color);x+=Width(glyph,size)+spacing;}
        return x;
    }
    // 左揃え / 中央揃えのラベル
    void Label(const std::string& text,float x,float y,float size=14,Engine::Vector4 color=Muted,float spacing=2.4f) { Spaced(text,x,y,size,spacing,color); }
    void LabelCenter(const std::string& text,float x,float y,float size=14,Engine::Vector4 color=Muted,float spacing=2.4f) {
        Spaced(text,x-SpacedWidth(text,size,spacing)*.5f,y,size,spacing,color);
    }

    // 操作
    // ゲームパッドでフォーカスされているか (キーボード/マウスを使っている間は常に false)
    bool Focused(Rect b) const {
        auto& nav=Nav();nav.Begin();
        return UsingPad()&&nav.hasFocus&&Navigator::Same(nav.focus,b);
    }
    // ゲームパッドでフォーカスされている矩形の周りの枠 (ボタン以外のフォーカス対象用)
    void FocusFrame(Rect b,float radius=14) {
        if(!Focused(b))return;
        RoundedRing({b.x-5,b.y-5,b.w+10,b.h+10},WithAlpha(Paper,.9f),radius+5);
    }
    // クリックはさせずにフォーカス対象としてだけ登録する (情報カードなど)
    void Track(Rect b,bool primary=false) const { Nav().Register(b,primary); }
    // 内部解像度 (WindowDX::kW x kH) のピクセルの点が矩形に入っているか (保存したクリックなど)
    bool HitPixel(Rect b,float x,float y) const {
        // 画面の座標から設計座標へ戻す
        x=(x-ox_)/s_;y=(y-oy_)/s_;
        return x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;
    }
    // マウスが乗っているか (ゲームパッドならフォーカス中か)
    bool Hover(Rect b) const {
        auto* input=Engine::Input::GetInstance(); if(!input)return false;
        if(input->UsingGamepad())return Focused(b);
        // マウス座標 (上書きがあればそちら)
        float x,y;input->GetMousePos(x,y);
        if(pointerOverride_){x=pointerX_;y=pointerY_;}
        // 設計座標へ戻して、矩形に入っているか
        x=(x-ox_)/s_;y=(y-oy_)/s_;
        bool inside=x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;
        // マウスが最後にあった場所にパッドのフォーカスを置き、操作機器を切り替えても続けて操作できるようにする
        if(inside){auto& nav=Nav();nav.focus=b;nav.hasFocus=true;}
        return inside;
    }
    // クリックされたか (ゲームパッドならフォーカス中に A ボタン、マウスなら範囲内で左クリック)
    bool Click(Rect b) const {
        // フォーカス対象として登録する
        Track(b);
        if(UsingPad()){
            // フォーカス中に A ボタンが押されたら、同じ押下を他で使わないよう止める
            if(!Nav().activate||!Focused(b))return false;
            Engine::Input::GetInstance()->LatchPad();
            return true;
        }
        // マウスが乗っていなければ押されていない
        if(!Hover(b))return false;
        // 左クリックの瞬間か (ImGui のクリックも見る)
        bool clicked=Engine::Input::GetInstance()->IsMouseTrigger(0);
#ifdef USE_IMGUI
        if(ImGui::GetCurrentContext())clicked|=ImGui::IsMouseClicked(ImGuiMouseButton_Left);
#endif
        return clicked;
    }
    // Kenney の入力アイコンパックの Xbox Series のボタン画像
    // リリースでは scripts/SlimeResources.json に書いたファイルだけがコピーされるので、新しいアイコンはそこにも追加する
    void PadGlyph(const char* pad,float x,float y,float size) {
        Image(std::string("Resources/Textures/UI/kenney_input-prompts_1.5/Xbox Series/Default/")+pad+".png",{x,y,size,size});
    }
    // ボタン画像と文字を並べる。文字の後ろの X 座標を返すので、続けて並べられる
    float PadPrompt(const char* pad,const std::string& label,float x,float y,float size=26,float textSize=18,
        Engine::Vector4 color=Muted,const char* font=Font) {
        // ボタン画像
        PadGlyph(pad,x,y,size);
        // その右に文字
        Text(label,x+size+6,y+(size-textSize)*.5f-1,textSize,color,font);
        return x+size+6+Width(label,textSize,font);
    }
    // スライムのボタン: 主ボタンはしずくの垂れるつやのあるライム色、それ以外は青緑の縁の暗い楕円。
    // フォーカス・ホバーで少し浮き上がり、周りが光る。
    // ゲームパッド使用中はショートカットのボタン画像 (padGlyph) を、フォーカス中は A ボタンの画像を出す。
    // focusable=false ならパッドのフォーカス移動の対象にしない
    void Button(Rect b,const std::string& label,bool primary=false,const char* padGlyph=nullptr,bool focusable=true) {
        // フォーカス対象として登録する
        if(focusable)Track(b,primary);
        // マウスが乗っているか (フォーカスしない場合はパッドでは反応しない)
        const bool pad=UsingPad(),hover=focusable||!pad?Hover(b):false;
        // 乗っていると少し浮かせる
        const float lift=hover?2.5f:0.f,radius=b.h*.5f;
        const Rect r{b.x,b.y-lift,b.w,b.h};
        // 乗っていると周りを光らせる
        if(hover)Halo(r.x+r.w*.5f,r.y+r.h*.5f,r.w*.62f,WithAlpha(primary?Lime:Teal,.2f));
        if(primary){
            // 主ボタン: しずく・ライム色の板・上端のつや
            Drips(r.x+radius,r.y+r.h-4,r.w-radius*2,hover?Mix(Lime,Paper,.25f):Lime,SeedOf(label),.9f);
            Rounded(r,hover?Mix(Lime,Paper,.25f):Lime,radius);
            Rounded({r.x+radius*.9f,r.y+5,r.w-radius*1.8f,r.h*.16f},WithAlpha(Paper,.2f),r.h*.08f);
        }else{
            // それ以外: 暗い板と青緑の縁
            Rounded(r,hover?Engine::Vector4{.13f,.22f,.17f,.96f}:Engine::Vector4{.07f,.12f,.095f,.92f},radius);
            RoundedRing(r,hover?WithAlpha(Teal,.85f):WithAlpha(Paper,.16f),radius);
        }
        // パッドでフォーカス中は白い枠
        if(hover&&pad)RoundedRing({r.x-6,r.y-6,r.w+12,r.h+12},WithAlpha(Paper,.9f),radius+6);
        // 文字の大きさと字間
        const float size=(std::min)(26.f,b.h*.42f),spacing=size*.09f;
        // 出すボタン画像 (指定が無くフォーカス中なら A ボタン)
        const char* glyph=pad?(padGlyph?padGlyph:hover?"xbox_button_a":nullptr):nullptr;
        // 画像の大きさと、文字との間
        const float glyphSize=glyph?(std::min)(b.h-16,34.f):0.f,gap=glyph?10.f:0.f;
        // 画像と文字の合計の幅 (中央にそろえるため)
        const float total=glyphSize+gap+SpacedWidth(label,size,spacing);
        float x=r.x+(r.w-total)*.5f;
        // 画像を描き、その右へ進める
        if(glyph){PadGlyph(glyph,x,r.y+(r.h-glyphSize)*.5f,glyphSize);x+=glyphSize+gap;}
        // 文字 (主ボタンは濃い色)
        Spaced(label,x,r.y+(r.h-size)*.5f-1,size,spacing,primary?Ink:Paper);
    }
    // キーボード/マウスの操作の案内 (アイコンと文字)
    void Prompt(const char* key,const std::string& label,float x,float y,float size=32,float textSize=22) {
        Image(std::string("Resources/Textures/UI/kenney_input-prompts_1.5/Keyboard & Mouse/Default/")+key+".png",{x,y,size,size});
        Text(label,x+size+8,y+(size-textSize)*.5f-1,textSize,Muted);
    }
    // ゲームパッド使用中は Xbox のボタン画像と文字を出す。パッド用の画像が null なら何も出さない
    void Prompt(const char* key,const std::string& label,const char* pad,const std::string& padLabel,float x,float y,float size=32,float textSize=22) {
        // キーボード / マウスを使っていれば、そちらの案内を出す
        if(!UsingPad()){Prompt(key,label,x,y,size,textSize);return;}
        // パッド用の画像が無ければ何も出さない
        if(!pad)return;
        PadPrompt(pad,padLabel,x,y,size,textSize);
    }
    // パネルは角の丸い板。主パネルは上端につやを付ける
    void Panel(Rect b, Engine::Vector4 color=Moss, bool primary=false) {
        Rounded(b,color,(std::min)(16.f,(std::min)(b.w,b.h)*.3f));
        if(primary)Rounded({b.x+8,b.y+4,b.w-16,(std::min)(10.f,b.h*.2f)},WithAlpha(Paper,.12f),5);
    }
    // スライムの管のようなゲージ
    // ゲージ (棒)
    void Bar(Rect b,float ratio,Engine::Vector4 color=Lime) {
        // 管の下地
        Rounded(b,{.06f,.1f,.08f,.92f},b.h*.5f);
        // 埋まる長さ
        const float width=b.w*std::clamp(ratio,0.f,1.f);
        // 中身と、その上のつや
        if(width>1){Rounded({b.x,b.y,(std::max)(width,b.h),b.h},color,b.h*.5f);
            Rounded({b.x+b.h*.3f,b.y+b.h*.18f,(std::max)(0.f,width-b.h*.6f),b.h*.22f},WithAlpha(Paper,.35f),b.h*.11f);}
    }
    // 円形のゲージ (12時から時計回り)
    void Ring(float x,float y,float radius,float ratio,Engine::Vector4 color=Lime,float width=5) {
        // 円周の分割数
        constexpr int segments=72;
        // 12 時から時計回りに、割合の分だけ弧を描く
        auto arc=[&](float amount,Engine::Vector4 tint){
            float sweep=6.2831853f*std::clamp(amount,0.f,1.f);
            for(int i=0;i<segments;++i){float from=6.2831853f*float(i)/segments;if(from>=sweep)break;
                float to=(std::min)(6.2831853f*float(i+1)/segments,sweep);
                Line(x+std::sin(from)*radius,y-std::cos(from)*radius,
                    x+std::sin(to)*radius,y-std::cos(to)*radius,width,tint);}
        };
        // 下地の円と、割合の分の弧
        arc(1,{.12f,.19f,.15f,.9f});arc(ratio,color);
    }

    // 画面
    // 背景: 苔むした夜に2つの柔らかい光・薄い筆の塗り・フィルムの粒状感、上部にロゴと区画名
    void Background(const char* section) {
        const float t=Seconds();
        // 画面全体を夜の色で塗る
        Fill({-ox_/s_,-oy_/s_,1280+2*ox_/s_,720+2*oy_/s_},Night);
        // 左上と右下の柔らかい光 (ゆっくり動く)
        Halo(150+20*std::sin(t*.21f),90,760,WithAlpha(Lime,.075f));
        Halo(1190,720+20*std::cos(t*.17f),640,WithAlpha(Teal,.06f));
        // 下の筆の塗りと粒状感
        Brush({-120,610,980,150},WithAlpha(Moss,.55f),-.05f);
        Grain(.045f);
        // 左上のマーク・タイトル・区画名
        SlimeMark(64,64,14);
        Text(GameTitle(),90,46,22,Paper,JapaneseFont);
        Label(section,92,76,12,WithAlpha(Lime,.85f),2.8f);
        // 右下の小さな文字
        const std::string& footer=Str("common.footer");
        Label(footer,1232-SpacedWidth(footer,11,2.4f),690,11,WithAlpha(Muted,.6f));
    }
    // 結果画面 (成功/失敗の見出し・クリアタイム・リトライとステージ選択のボタン)
    void Result(bool won,const std::string& time,const std::string& detail) {
        Background(Str("result.section").c_str());
        // 成功はライム色、失敗は金色
        const auto accent=won?Lime:Gold;const float t=Seconds();
        // 右側の大きなスライムのマーク
        Splat(1010,330,250,WithAlpha(accent,.06f),.4f);
        SlimeMark(1010,330,92+3*std::sin(t*2));
        // 見出し (筆の塗りの上)
        Brush({52,150,820,140},WithAlpha(accent,.92f),-.025f);
        Text(Str(won?"result.winHeading":"result.loseHeading"),96,184,70,Ink);
        // ひとこと
        Text(Str(won?"result.winMessage":"result.loseMessage"),100,300,26,Paper,JapaneseFont);
        // クリアタイムと詳細
        Label(Str("result.clearTime"),102,358,13,WithAlpha(accent,.9f));
        Text(time,98,374,58,Paper);
        Text(detail,102,446,20,Muted);
        // ボタンと操作の案内
        const std::string& retry=Str("result.retry");const std::string& stageSelect=Str("result.stageSelect");
        Button(Retry,retry,true,"xbox_button_x");Button(Select,stageSelect,false,"xbox_button_y");
        Prompt("keyboard_r",retry,"xbox_button_x",retry,102,632,26,18);
        Prompt("keyboard_tab",stageSelect,"xbox_button_y",stageSelect,250,632,26,18);
    }
    // ポーズ画面 (再開とタイトルへ戻るボタン)
    void Pause() {
        Background(Str("pause.section").c_str());
        // 見出し (筆の塗りの上) とひとこと
        Brush({60,226,560,118},WithAlpha(Lime,.9f),-.03f);
        Text(Str("pause.heading"),96,250,58,Ink);
        Text(Str("pause.message"),100,360,24,Paper,JapaneseFont);
        // 右側の寝ているスライム
        Splat(960,380,240,WithAlpha(Lime,.05f),1.1f);
        Halo(960,500,170,WithAlpha(Night,.6f));
        SlimeMark(960,400,100,1,true);
        // ボタンと操作の案内
        const std::string& resume=Str("pause.resume");
        Button(Resume,resume,true,"xbox_button_b");Button(Title,Str("pause.backToTitle"));
        Prompt("keyboard_escape",resume,"xbox_button_b",resume,102,592,26,18);
        Prompt("keyboard_tab",Str("pause.promptTitle"),"xbox_button_a",Str("pause.promptSelect"),250,592,26,18);
    }
};
}
