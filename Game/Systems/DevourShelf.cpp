// DevourShelf.cpp
// 結果画面の「おなかの中」(ChronoSystem の一部)。大きさを測ったあと、カメラが本物のスライムの体の中へ入り、
// 画面いっぱいの GPU 流体の中に、今回飲みこんだ物を種類ごとの升へ左上から並べる。
// 飲みこんだ数が多いほど並べる間隔が詰まっていく。升をクリックすると 1 つを大きく回して見せる。
// 集めた記録は図鑑 (Game/Data/Collection.h) に足し、タイトルの「ずかん」からも見られる。
#include "ChronoSystemInternal.h"
#include "../Data/Collection.h"

namespace Game {
namespace {
// 升を並べる範囲 (設計座標)。左は記録の面、下は巻尺の上まで
constexpr float kShelfLeft=500,kShelfRight=1256,kShelfTop=86,kShelfBottom=598,kShelfMaxCell=128;
// 大きく見せるときの中心と大きさ
constexpr float kDetailX=878,kDetailY=282,kDetailSize=300;
float Smooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
}

// 飲みこんだ物を記録する (図鑑に無い物と、光る球は数えない)
void ChronoSystem::DevourRecordEaten(const entt::registry& r,uint32_t visual){
    if(!devour_||visual==0xffffffffu)return;auto e=entt::entity(visual);if(!r.valid(e))return;
    const auto* name=r.try_get<NameComponent>(e);if(!name)return;
    DevourEaten eaten;eaten.id=Collection::KeyOfName(name->name);if(eaten.id.empty())return;eaten.visual=visual;
    if(const auto* mr=r.try_get<MeshRendererComponent>(e))eaten.tint={mr->color.x,mr->color.y,mr->color.z,1};
    devourEaten_.push_back(std::move(eaten));
}
// 確認用: ステージの物を種類ごとに 1 つずつ記録する
void ChronoSystem::RecordAllKindsForTest(const entt::registry& r){
    std::set<std::string> seen;for(const auto& eaten:devourEaten_)seen.insert(eaten.id);
    for(const auto& prop:life_.props){if(prop.visual==0xffffffffu)continue;auto e=entt::entity(prop.visual);if(!r.valid(e))continue;
        const auto* name=r.try_get<NameComponent>(e);if(!name)continue;const auto id=Collection::KeyOfName(name->name);if(id.empty()||!seen.insert(id).second)continue;
        DevourRecordEaten(r,prop.visual);}
}
// ぶつかって体から飛び出した物は、飲みこんだ記録から外す (拾い直せばもう一度数える)
void ChronoSystem::DevourForgetEaten(uint32_t visual){
    for(size_t i=devourEaten_.size();i-->0;)if(devourEaten_[i].visual==visual){devourEaten_.erase(devourEaten_.begin()+std::ptrdiff_t(i));return;}
}

// 升を作る: 飲みこんだ順に、はじめて出た種類から升を割り当てる。升の大きさは全部が範囲に収まる一番大きなもの
void ChronoSystem::BuildDevourShelf(const std::vector<std::string>& fresh){
    shelf_={};const auto totals=Collection::Load();const auto& entries=Collection::Entries();
    std::map<int,int> cellOf;
    for(const auto& eaten:devourEaten_){const int entry=Collection::IndexOf(eaten.id);if(entry<0)continue;
        auto found=cellOf.find(entry);
        if(found==cellOf.end()){ShelfCell cell;cell.entry=entry;cell.tint=eaten.tint;cell.fresh=std::find(fresh.begin(),fresh.end(),eaten.id)!=fresh.end();
            const auto total=totals.find(entries[size_t(entry)].id);cell.total=total==totals.end()?0:total->second;
            found=cellOf.emplace(entry,int(shelf_.cells.size())).first;shelf_.cells.push_back(cell);}
        shelf_.order.push_back(found->second);}
    if(shelf_.cells.empty())return;
    const float w=kShelfRight-kShelfLeft,h=kShelfBottom-kShelfTop;const int count=int(shelf_.cells.size());float best=0;
    // (同じ大きさなら列を多くして、左上から横に並べる)
    for(int cols=1;cols<=count;++cols){const int rows=(count+cols-1)/cols;const float size=(std::min)({w/float(cols),h/float(rows),kShelfMaxCell});if(size>=best){best=size;shelf_.cols=cols;}}
    shelf_.cell=best;shelf_.left=kShelfLeft;shelf_.top=kShelfTop;shelf_.built=true;
}
UI::Rect ChronoSystem::DevourShelfRect(size_t cell)const{
    const float s=shelf_.cell;const int col=int(cell)%shelf_.cols,row=int(cell)/shelf_.cols;
    return {shelf_.left+float(col)*s+3,shelf_.top+float(row)*s+3,s-6,s-6};
}
// 体の中へ入る量: 測り終えて記録が出たあと、1 秒かけてカメラが体の中へ進む
float ChronoSystem::DevourShelfDive()const{return devour_&&finished_&&shelf_.built?Smooth((finishAge_-kShelfDive)/kShelfDiveTime):0.f;}
// 流体の見え方は、カメラが体の表面を越えるころに体の中の景色へ変わる
float ChronoSystem::DevourShelfInterior()const{return devour_&&finished_&&shelf_.built?Smooth((finishAge_-kShelfDive-kShelfDiveTime*.4f)/(kShelfDiveTime*.45f)):0.f;}

// 体の中のカメラ: 測った体の外形の、手前寄りの真ん中の高さから、奥を水平に見る。
// 物を並べる深さは、床が物の後ろに見えない近さにする (床より手前だけを体の中の物として透かす)
void ChronoSystem::DevourShelfCamera(GameContext& ctx,V& wanted,V& view,float ground){
    const float dive=DevourShelfDive();
    if(dive<=0){ctx.renderer->SetFluidInterior(0,0);return;}
    const V side{std::cos(yaw_),0,-std::sin(yaw_)},flat{std::sin(yaw_),0,std::cos(yaw_)};
    const float height=(std::max)(.5f,resultTopY_-ground),eye=height*.45f,depth=(std::max)(.2f,resultFar_-resultNear_);
    shelfDepth_=std::clamp((std::min)(eye*1.6f,depth*.55f),.25f,60.f);
    V inside=resultOrigin_+side*((resultLo_+resultHi_)*.5f)+flat*(resultNear_+depth*.22f);inside.y=ground+eye;
    wanted=Lerp(wanted,inside,dive);view=Unit(Lerp(view,flat,dive));
    ctx.renderer->SetFluidInterior(DevourShelfInterior(),shelfDepth_*1.2f);
}

// 並べる演出を進め、升のクリックを受け取る
bool ChronoSystem::UpdateDevourShelf(GameContext& ctx,UI::Canvas& ui){
    if(!shelf_.built||finishAge_<kShelfPopStart)return false;
    // 1 つずつ升に入れる。間隔は 0.28 秒から縮み、最後は 1 フレームに何個も入る
    auto popOne=[&]{auto& cell=shelf_.cells[size_t(shelf_.order[shelf_.shown])];++cell.count;if(cell.popAt<0)cell.popAt=finishAge_;else cell.kickAt=finishAge_;++shelf_.shown;};
    if(shelf_.shown<shelf_.order.size()){shelf_.clock+=std::min(ctx.dt,.05f);bool popped=false,fresh=false;
        while(shelf_.shown<shelf_.order.size()&&shelf_.clock>=shelf_.next){fresh|=shelf_.cells[size_t(shelf_.order[shelf_.shown])].popAt<0;popOne();popped=true;
            shelf_.next+=(std::max)(.008f,.28f*std::pow(.9f,float(shelf_.shown)));}
        // 音は鳴らしすぎないよう間を空け、進むほど高くする
        if(popped&&finishAge_-shelf_.soundAt>.045f){shelf_.soundAt=finishAge_;const float pitch=1+.012f*float((std::min)(shelf_.shown,size_t(80)));
            PlayDevourSound(devourSounds_.note,.22f,pitch);if(fresh)PlayDevourSound(devourSounds_.gulpSmall[shelf_.shown%4],.4f,pitch);}}
    const bool done=shelf_.shown>=shelf_.order.size();
    // 大きく見ている間: どこを押しても閉じる
    if(shelf_.detail>=0){
        if(ctx.input->IsMouseTrigger(0)||UI::Pressed(DIK_ESCAPE)||UI::PadPressed(XINPUT_GAMEPAD_B)||UI::PadPressed(XINPUT_GAMEPAD_A)){shelf_.detail=-1;UI::PlayUISound(UI::UISound::Move);}
        return true;}
    for(size_t i=0;i<shelf_.cells.size();++i){if(shelf_.cells[i].popAt<0)continue;
        if(ui.Click(DevourShelfRect(i))){shelf_.detail=int(i);shelf_.detailAt=finishAge_;PlayDevourSound(devourSounds_.gulpMid[i%2],.5f);return true;}}
    // 並べている途中に体の中を押すと、最後まで飛ばす
    if(!done&&ctx.input->IsMouseTrigger(0)&&ui.Hover({kShelfLeft-20,0,1300-kShelfLeft,kShelfBottom})){while(shelf_.shown<shelf_.order.size())popOne();PlayDevourSound(devourSounds_.gulpBig,.6f);return true;}
    return false;
}

// 升の物を 3D で描く: カメラの前の決まった深さの面に、升の位置と大きさを写す (画面に止まって見える)
void ChronoSystem::DrawDevourShelf3D(GameContext& ctx){
    if(!shelf_.built||DevourShelfDive()<1||!ctx.camera)return;
    using namespace DirectX;
    XMFLOAT4X4 world,proj;XMStoreFloat4x4(&world,XMMatrixInverse(nullptr,ctx.camera->View()));XMStoreFloat4x4(&proj,ctx.camera->Proj());
    const V right{world._11,world._12,world._13},up{world._21,world._22,world._23},forward{world._31,world._32,world._33},eye{world._41,world._42,world._43};
    // 並べる深さはカメラの近くの面のすぐ奥にする。画面上の位置と大きさは深さに比例して直すので見た目は変わらず、
    // 壁際で体の中のカメラのすぐ前に壁があっても、物が壁に埋まらない (左手系の射影: near = -_43/_33)
    const float nearZ=proj._33!=0?-proj._43/proj._33:.3f;
    const float tanX=1/proj._11,tanY=1/proj._22,d=(std::min)(shelfDepth_,(std::max)(nearZ,.01f)*3.f);
    const auto& entries=Collection::Entries();
    // 図鑑の絵 (scripts/lowpoly/render_collection.py: 28 度斜めに置き、24 度見下ろす) と同じ見え方にする。
    // カメラは水平に見ているので、物の方をカメラへ向けて傾ける。向きは「カメラから見た座標」で決めてから、カメラの向きへ回す
    const float cameraYaw=std::atan2(forward.x,forward.z);
    auto draw=[&](const ShelfCell& cell,float cx,float cy,float size,float spin){
        const auto& entry=entries[size_t(cell.entry)];const LowModel* model=nullptr;
        try{model=&LowModelOf(entry.model);}catch(...){return;}
        const V mid=(model->lo+model->hi)*.5f,extent=model->hi-model->lo;const float largest=(std::max)({extent.x,extent.y,extent.z,.001f});
        // 設計座標の大きさを、その深さの面の長さに直す (画面の高さ 720 が 2·d·tanY)
        const float worldSize=size/720*2*d*tanY,scale=worldSize/largest;
        // 見せる向き: face はカメラから見た水平の向き (π で物の前 +z がカメラを向き、+.46 で図鑑と同じ斜め)、
        // tilt は上の面をカメラへ倒す角度 (図鑑の見下ろしと同じ 24 度)
        const float wide=(std::max)(extent.x,extent.z),narrow=(std::min)(extent.x,extent.z);
        float face=3.14159f+.46f,tilt=.42f;
        // 寝かせて作った物 (将棋の駒) は、先を上にして起こし、表の文字をこちらへ向ける
        if(entry.stand){face=0;tilt=1.3f;}
        // 平たい物 (カード・ノート・コイン・紙): 横から見ると線になるので、上の面を大きくこちらへ倒す
        else if(extent.y<.45f*wide)tilt=1.f;
        // 立っている薄い物 (横の幅に比べて奥行きが薄い): 広い面をこちらへ向ける (薄いのが x なら 90 度回す)
        else if(narrow<.5f*wide&&extent.x<extent.z)face=1.5708f+.46f;
        const XMMATRIX rotation=XMMatrixRotationY(face+spin)*XMMatrixRotationX(-tilt)*XMMatrixRotationY(cameraYaw);
        // Transform の回転 (XMMatrixRotationRollPitchYaw の順: z → x → y) に直す
        XMFLOAT4X4 m;XMStoreFloat4x4(&m,rotation);
        const float pitch=std::asin(std::clamp(-m._32,-1.f,1.f)),yaw=std::atan2(m._31,m._33),roll=std::atan2(m._12,m._22);
        const V at=eye+forward*d+right*((cx/640-1)*d*tanX)+up*((1-cy/360)*d*tanY);
        XMFLOAT3 turned;XMStoreFloat3(&turned,XMVector3Transform(XMVectorSet(mid.x,mid.y,mid.z,0),rotation));
        const V offset=V{turned.x,turned.y,turned.z}*scale;
        Engine::Transform t;t.scale={scale,scale,scale};t.rotate={pitch,yaw,roll};t.translate=EV(at-offset);
        ctx.renderer->DrawMeshInstanced(model->mesh,lowPalette_,t,cell.tint,"LowpolySurface",lowMaps_);};
    const float clock=finishAge_;
    if(shelf_.detail>=0){const auto& cell=shelf_.cells[size_t(shelf_.detail)];const float grow=UI::Scale::Overshoot((clock-shelf_.detailAt)/.35f);
        draw(cell,kDetailX,kDetailY,kDetailSize*grow,clock*1.1f);return;}
    for(size_t i=0;i<shelf_.cells.size();++i){const auto& cell=shelf_.cells[i];if(cell.popAt<0)continue;const auto b=DevourShelfRect(i);
        const float pop=UI::Scale::Overshoot((clock-cell.popAt)/.28f),kick=cell.kickAt>=0?1+.16f*(1-UI::Scale::Clamp01((clock-cell.kickAt)/.18f)):1.f;
        // 浮かんで見えるよう、升ごとにゆっくり揺らす
        const float sway=std::sin(clock*1.3f+float(i)*1.7f),bob=std::sin(clock*1.1f+float(i)*2.3f)*b.h*.03f;
        draw(cell,b.x+b.w*.5f,b.y+b.h*.46f+bob,b.w*.74f*pop*kick,.35f*sway);}
}

// 升の上の文字: 数・はじめて見つけた印・選んだ物の説明
void ChronoSystem::DrawDevourShelfUI(GameContext&,UI::Canvas& ui){
    const float dive=DevourShelfDive();if(!shelf_.built||dive<=0)return;
    namespace S=UI::Scale;const auto& entries=Collection::Entries();const float clock=finishAge_,in=S::Clamp01((clock-kShelfPopStart+.3f)/.3f);
    if(in<=0)return;
    // 見出し: おなかの中・種類の数・飲みこんだ数
    {int kinds=0;for(const auto& cell:shelf_.cells)kinds+=cell.popAt>=0;
        const std::string head=UI::Str("collection.inside"),detail=std::to_string(kinds)+"しゅるい  "+std::to_string(shelf_.shown)+"こ";
        const float w=ui.Width(head,28,S::Jp)+ui.Width(detail,22,S::Jp)+48,x=kShelfLeft+4,y=24-60*(1-S::EaseOut(in));
        S::Plate(ui,{x,y,w,46},S::Yellow);ui.Text(head,x+16,y+5,28,S::Black,S::Jp);ui.Text(detail,x+32+ui.Width(head,28,S::Jp),y+12,22,S::Black,S::Jp);}
    if(shelf_.detail>=0){
        const auto& cell=shelf_.cells[size_t(shelf_.detail)];const auto& entry=entries[size_t(cell.entry)];const float k=S::EaseOut((clock-shelf_.detailAt)/.3f);
        const UI::Rect card{kShelfLeft+20,452+40*(1-k),kShelfRight-kShelfLeft-40,138};S::Plate(ui,card,S::Cream);
        float x=card.x+22;ui.Text(entry.name,x,card.y+10,36,S::Black,S::Jp);x+=ui.Width(entry.name,36,S::Jp)+14;
        if(cell.fresh){const std::string& label=UI::Str("collection.new");const float w=ui.Width(label,20,S::Num)+18;ui.Fill({x,card.y+20,w,28},S::Red);ui.Text(label,x+9,card.y+19,20,S::White,S::Num);}
        ui.Text(entry.text,card.x+22,card.y+62,22,S::Black,S::Jp);
        // 数: 今回といままで
        const std::string now=UI::Str("collection.thisRun")+" ×"+std::to_string(cell.count),all=UI::Str("collection.total")+" ×"+std::to_string((std::max)(cell.total,cell.count));
        ui.Text(now,card.x+card.w-22-ui.Width(now,24,S::Jp),card.y+12,24,S::Red,S::Jp);ui.Text(all,card.x+card.w-22-ui.Width(all,20,S::Jp),card.y+44,20,S::Black,S::Jp);
        S::Hint(ui,"mouse_left","xbox_button_b",UI::Str("collection.close"),card.x+22,card.y+card.h-40,S::Black,18);
        return;}
    for(size_t i=0;i<shelf_.cells.size();++i){const auto& cell=shelf_.cells[i];if(cell.popAt<0)continue;const auto b=DevourShelfRect(i);
        const float pop=S::Clamp01((clock-cell.popAt)/.2f);
        // 数の札 (右下)。増えた瞬間に少し大きくなる
        const float kick=cell.kickAt>=0?1+.25f*(1-S::Clamp01((clock-cell.kickAt)/.15f)):1.f,size=(std::min)(24.f,b.w*.24f)*kick;
        // (× は数字の書体に無いので日本語の書体で書く)
        const std::string count=std::to_string(cell.count);const float xw=ui.Width("×",size*.8f,S::Jp),cw=xw+ui.Width(count,size,S::Num)+14;
        const float bx=b.x+b.w-cw-2,by=b.y+b.h-size*1.25f-2;ui.Rounded({bx,by,cw,size*1.25f},UI::WithAlpha(S::Black,.82f*pop),6);
        ui.Text("×",bx+6,by+size*.18f,size*.8f,UI::WithAlpha(S::Yellow,pop),S::Jp);ui.Text(count,bx+8+xw,by-1,size,UI::WithAlpha(S::Yellow,pop),S::Num);
        if(cell.fresh){const std::string& label=UI::Str("collection.new");const float ls=(std::min)(16.f,b.w*.17f),lw=ui.Width(label,ls,S::Num)+10;
            ui.Fill({b.x+2,b.y+2,lw,ls*1.3f},UI::WithAlpha(S::Red,pop));ui.Text(label,b.x+7,b.y+2,ls,UI::WithAlpha(S::White,pop),S::Num);}
        // 乗っている升は枠を出し、名前を添える
        if(ui.Hover(b)){ui.RoundedRing({b.x-2,b.y-2,b.w+4,b.h+4},S::Cream,12);const auto& name=entries[size_t(cell.entry)].name;const float nw=ui.Width(name,18,S::Jp)+16;
            ui.Rounded({b.x+b.w*.5f-nw*.5f,b.y-12,nw,26},UI::WithAlpha(S::Black,.85f),8);ui.Text(name,b.x+b.w*.5f-nw*.5f+8,b.y-11,18,S::Cream,S::Jp);}}
    if(shelf_.shown>=shelf_.order.size()){const std::string& hint=UI::Str("collection.hint");const float w=ui.Width(hint,18,S::Jp)+24;
        ui.Rounded({kShelfRight-w,kShelfBottom+2,w,28},UI::WithAlpha(S::Black,.7f),8);ui.Text(hint,kShelfRight-w+12,kShelfBottom+4,18,S::Cream,S::Jp);}
}
}
