// DevourAnimation.h
// 吸収モードの飛びかかりの体の形のアニメーション。エンジンに依存しない。
// 体をいくつかの「塊」で表し、時間ごとのキーで各塊の位置・大きさ・形の値を決める。キーの間はなめらかにつなぐ。
// エディタ (Game/Editor/DevourAnimEditor.cpp) で動かして作り、Resources/Data/devour_animation.json に保存する。
// 位置と大きさの単位は体の半径 (大きくなっても同じ形)。x は横・y は上 (地面が 0)・z は前 (飛びかかる向き)。
// 塊の種類:
//   楕円体: 中身の詰まった塊 (胴体など)
//   口: 前が開いた中空のじょうごで、縁は触手のような唇に分かれて前へ伸び、内側へ巻き込む。体の表面には波が前へ流れる
// 体の粒子は、普段の体の中の後ろから前の順に、塊の並び順で割り当てる (塊は後ろの物から並べる)。
// 口の塊では、後ろの粒子ほどのど (奥)、前の粒子ほど縁と触手の先に行くので、体の前の粒子が先に物へ触れる
#pragma once
#include "DevourRules.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <vector>
namespace Game::Chrono {
enum class DevourBlobKind {Lump=0,Mouth=1,Tail=2,Amoeba=3,Gulp=4};
// 塊: 種類・中心・半径 (口は縁の横と縦の半径と奥行き)。口だけが使う値:
//   thickness 壁の厚さ (0〜1。1 で中まで詰まる)・reach 触手が前へ伸びる長さ・curl 縁が内側へ巻き込む強さ (0〜1)・
//   wave 表面を前へ流れる波の大きさ・tentacles 触手の本数・twist 粒子の流れのねじれ (渦を巻いて流れ込む)
// しずく形 (胴体) が使う値: curl 後ろを細くする強さ (0 で楕円体、1 で後ろへ細く伸びるしずく形)・wave 尾の左右の揺れ
// アメーバ (広がる体) が使う値: thickness 平たさ (0 で楕円体、1 で縁の丸いパンケーキ)・reach 縁から出る仮足の長さ・
//   tentacles 仮足の本数・curl 上ほど前へ傾く強さ・wave 縁を流れる波の大きさ。
//   体の各点をなめらかに動かすだけの形なので、隣の粒子と離れず、広がってもちぎれない
// 大口 (口を開けて飛びかかる体) が使う値: reach 口の開き (0 で楕円体、1 で後ろが尾のように細く、前が大きく丸く開いた口)・
//   thickness 唇と壁の厚さ・curl 唇が内側へ巻き込んで閉じる強さ・wave 口から尾へ流れる波・twist 尾の左右の揺れ。
//   size は口の横と縦の半径と体の前後の長さ。体 1 つを変形させ、粒子は形の量に合わせて前後に配るので、口の壁が薄くまばらにならない
struct DevourBlob {
    Vec at{0,.4f,0},size{1.12f,.4f,1.12f};
    DevourBlobKind kind=DevourBlobKind::Lump;
    float thickness=.4f,reach=0,curl=0,wave=0,tentacles=6,twist=0;
};
struct DevourKey {float time=0;std::vector<DevourBlob> blobs;};
struct DevourAnimation {std::vector<std::string> names;std::vector<DevourKey> keys;};
// 普段の体と同じ量 (半楕円体の体積を体の半径の 3 乗で割った値の 3/4π 倍)。塊の量の合計がこれに等しいと同じ量
inline constexpr float DevourBodyVolume=.5f;
// 塊の量 (楕円体は半径の積。口は壁の分だけ)
// しずく形の前後の位置 t (0 尾の先〜1 前の端) での太さ (0〜1)。taper 0 で楕円体、1 で前が丸く後ろへ細く伸びる
inline float DevourTailProfile(float t,float taper){
    t=std::clamp(t,0.f,1.f);float ellipse=std::sqrt(std::max(0.f,1-(2*t-1)*(2*t-1)));
    constexpr float c=.62f;float drop=t<c?std::pow(std::sin(1.5707963f*t/c),1.4f):std::sqrt(std::max(0.f,1-(t-c)*(t-c)/((1-c)*(1-c))));
    return ellipse+(drop-ellipse)*std::clamp(taper,0.f,1.f);
}
// しずく形の表: 量の割合 (楕円体を 1) と、粒子を量の割合で前後に並べるための累積 (taper ごとに作り直す)
struct DevourTailTable {float taper=-1,volume=1;std::array<float,49> cumulative{};};
inline const DevourTailTable& DevourTail(float taper){
    // 1 フレームで今と少し前・少し後の形を交互に使うので、数個を覚えておく
    // (1 個だけだと粒子ごとに表を作り直し、飛びかかりの間だけ処理が何倍も重くなる)
    thread_local std::array<DevourTailTable,4> tables;thread_local size_t next=0;
    for(const auto& t:tables)if(std::abs(t.taper-taper)<1e-5f)return t;
    auto& table=tables[next];next=(next+1)%tables.size();
    table.taper=taper;float sum=0;table.cumulative[0]=0;
    for(size_t i=1;i<table.cumulative.size();++i){float t=(float(i)-.5f)/48;float r=DevourTailProfile(t,taper);sum+=r*r/48;table.cumulative[i]=sum;}
    for(auto& c:table.cumulative)c/=std::max(1e-6f,sum);
    table.volume=1.5f*sum;return table;
}
// 大口の唇の反りの大きさ (口を開け切ったとき、縁の太さを何割増やすか)
inline constexpr float DevourGulpFlare=.7f;
// 大口の前後の位置 t (0 尾の先〜1 口の縁) での外側と内側 (口の中の空洞) の太さ (0〜1+反り)
inline void DevourGulpProfile(float t,float open,float wall,float curl,float& outer,float& inner){
    t=std::clamp(t,0.f,1.f);open=std::clamp(open,0.f,1.f);
    auto smooth=[](float a,float b,float x){float u=std::clamp((x-a)/(b-a),0.f,1.f);return u*u*(3-2*u);};
    float ellipse=std::sqrt(std::max(0.f,1-(2*t-1)*(2*t-1)));
    // 開くほど、後ろは尾のように細く、前は縁まで太いまま口になる。空洞は体の後ろ 3 割の所から口へ広がる
    outer=ellipse+(std::sqrt(t)-ellipse)*open;
    // 唇はラッパのように大きく外へ反り、口が体でいちばん太くなる (体の幅の約 1.7 倍)
    outer*=1+DevourGulpFlare*open*smooth(.55f,1,t);
    inner=open*outer*(1-std::clamp(wall,.05f,1.f))*std::pow(smooth(.2f,1,t),.6f);
    // 巻き込み: 縁ほど内側へすぼまる
    float k=1-std::clamp(curl,0.f,1.f)*.6f*smooth(.7f,1,t);outer*=k;inner*=k;
}
// 大口の表: 量の割合 (楕円体を 1) と、粒子を量の割合で前後に並べるための累積 (形の値ごとに作り直す)
struct DevourGulpTable {float open=-1,wall=-1,curl=-1,volume=1;std::array<float,49> cumulative{};};
inline const DevourGulpTable& DevourGulp(float open,float wall,float curl){
    // しずく形の表と同じく、交互に使う形の分を覚えておく
    thread_local std::array<DevourGulpTable,4> tables;thread_local size_t next=0;
    for(const auto& t:tables)if(std::abs(t.open-open)<1e-5f&&std::abs(t.wall-wall)<1e-5f&&std::abs(t.curl-curl)<1e-5f)return t;
    auto& table=tables[next];next=(next+1)%tables.size();
    table.open=open;table.wall=wall;table.curl=curl;float sum=0;table.cumulative[0]=0;
    for(size_t i=1;i<table.cumulative.size();++i){float outer,inner;DevourGulpProfile((float(i)-.5f)/48,open,wall,curl,outer,inner);sum+=(outer*outer-inner*inner)/48;table.cumulative[i]=sum;}
    for(auto& c:table.cumulative)c/=std::max(1e-6f,sum);
    table.volume=1.5f*sum;return table;
}
inline float DevourBlobVolume(const DevourBlob& b){
    float box=std::max(0.f,b.size.x)*std::max(0.f,b.size.y)*std::max(0.f,b.size.z);
    if(b.kind==DevourBlobKind::Lump)return box;
    if(b.kind==DevourBlobKind::Tail)return box*DevourTail(b.curl).volume;
    // アメーバ: 平たくすると縁が厚くなる分 (最大 18%) と、仮足で広がる分
    if(b.kind==DevourBlobKind::Amoeba)return box*(1+.18f*std::clamp(b.thickness,0.f,1.f))*(1+.35f*b.reach);
    if(b.kind==DevourBlobKind::Gulp)return box*DevourGulp(b.reach,b.thickness,b.curl).volume;
    float wall=std::clamp(b.thickness,.05f,1.f);return box*wall*(2-wall)*.75f;
}
inline float DevourVolume(const std::vector<DevourBlob>& blobs){float sum=0;for(const auto& b:blobs)sum+=DevourBlobVolume(b);return sum;}
// 初めの形: 体 1 つの大口。溜め (ぐっと縮む) → 体ごと前へ突き出しながら口を開け →
// 体と同じくらい大きく丸い口で物を丸ごと包み (後ろは尾のように細く流れる) → 厚い唇を内側へ巻き込んで閉じ →
// 背の高いドームへ行き過ぎてから普段の体に戻る (ぷるん)。体の量はどのキーでも普段の体の 95〜110%
inline DevourAnimation DevourDefaultAnimation(){
    auto gulp=[](Vec at,Vec size,float open=0,float wall=.5f,float curl=0,float wave=0,float swing=0){
        DevourBlob b;b.kind=DevourBlobKind::Gulp;b.at=at;b.size=size;b.reach=open;b.thickness=wall;b.curl=curl;b.wave=wave;b.twist=swing;b.tentacles=0;return b;};
    DevourAnimation a;a.names={"体"};
    a.keys={
        {0,{gulp({0,.4f,0},{1.12f,.4f,1.12f})}},
        {DevourWindup,{gulp({0,.33f,-.1f},{1.18f,.33f,1.15f},0,.5f,0,.02f)}},
        {DevourWindup+DevourLeap*.45f,{gulp({0,.7f,.3f},{.9f,.62f,1.1f},.75f,.34f,0,.07f,.07f)}},
        {DevourLandAt,{gulp({0,.95f,.3f},{1.f,.8f,.98f},1,.24f,0,.08f,.08f)}},
        {DevourLandAt+DevourCover*.5f,{gulp({0,.88f,.28f},{1.f,.76f,.98f},.92f,.3f,.55f,.07f,.05f)}},
        {DevourCoverEnd,{gulp({0,.62f,.1f},{1.f,.6f,1.f},.45f,.55f,1,.05f,.02f)}},
        {DevourCoverEnd+DevourSettle*.4f,{gulp({0,.5f,0},{1.f,.5f,1.f})}},
        {DevourPounceTime,{gulp({0,.4f,0},{1.12f,.4f,1.12f})}}};
    return a;
}
// ある時間の塊 (キーの間は 3 次の曲線でなめらかにつなぎ、キーで止まらないようにする。種類はその前のキーのもの)
inline std::vector<DevourBlob> DevourSampleBlobs(const DevourAnimation& a,float time){
    const size_t count=a.names.size();std::vector<DevourBlob> out(count);
    if(a.keys.empty())return out;
    auto blobAt=[&](size_t key,size_t i){return i<a.keys[key].blobs.size()?a.keys[key].blobs[i]:DevourBlob{};};
    if(time<=a.keys.front().time){for(size_t i=0;i<count;++i)out[i]=blobAt(0,i);return out;}
    if(time>=a.keys.back().time){for(size_t i=0;i<count;++i)out[i]=blobAt(a.keys.size()-1,i);return out;}
    size_t k=0;while(k+1<a.keys.size()&&a.keys[k+1].time<=time)++k;
    const float t0=a.keys[k].time,t1=a.keys[k+1].time,span=std::max(1e-4f,t1-t0),s=(time-t0)/span;
    const float h00=2*s*s*s-3*s*s+1,h10=s*s*s-2*s*s+s,h01=-2*s*s*s+3*s*s,h11=s*s*s-s*s;
    // 値をまとめて 3 つずつ補間する (位置・大きさ・口の値 2 組)
    auto pack=[](const DevourBlob& b,int which){
        return which==0?b.at:which==1?b.size:which==2?Vec{b.thickness,b.reach,b.curl}:Vec{b.wave,b.tentacles,b.twist};};
    // キーでの傾き: 前後のキーの差 (端のキーでは 0。そこで止まる)
    auto slope=[&](size_t key,size_t i,int which){
        if(key==0||key+1>=a.keys.size())return Vec{};
        return (pack(blobAt(key+1,i),which)-pack(blobAt(key-1,i),which))*(1/std::max(1e-4f,a.keys[key+1].time-a.keys[key-1].time));};
    for(size_t i=0;i<count;++i){const DevourBlob p0=blobAt(k,i),p1=blobAt(k+1,i);Vec v[4];
        for(int w=0;w<4;++w)v[w]=pack(p0,w)*h00+slope(k,i,w)*(h10*span)+pack(p1,w)*h01+slope(k+1,i,w)*(h11*span);
        auto& b=out[i];b.kind=p0.kind;b.at=v[0];b.size={std::max(.005f,v[1].x),std::max(.005f,v[1].y),std::max(.005f,v[1].z)};
        b.thickness=std::clamp(v[2].x,.05f,1.f);b.reach=std::max(0.f,v[2].y);b.curl=std::clamp(v[2].z,0.f,1.f);
        b.wave=std::max(0.f,v[3].x);b.tentacles=std::max(0.f,v[3].y);b.twist=v[3].z;}
    return out;
}
// 体の粒子の割り当ての準備: 普段の体の中の点 (半楕円体 横 2.55・高さ 1.8) ごとの、後ろから前への順位 (0〜1)
inline std::vector<float> DevourRanks(const std::vector<Vec>& rest){
    std::vector<size_t> order(rest.size());std::iota(order.begin(),order.end(),size_t(0));
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return rest[a].z<rest[b].z;});
    std::vector<float> rank(rest.size());for(size_t i=0;i<order.size();++i)rank[order[i]]=(float(i)+.5f)/float(order.size());
    return rank;
}
// 各塊が受け持つ順位の始まり (量の割合で分ける)
inline std::vector<float> DevourBlobStarts(const std::vector<DevourBlob>& blobs){
    std::vector<float> start(blobs.size(),0);float total=std::max(1e-6f,DevourVolume(blobs)),sum=0;
    for(size_t i=0;i<blobs.size();++i){start[i]=sum;sum+=DevourBlobVolume(blobs[i])/total;}
    return start;
}
// 順位から受け持つ塊の番号
inline size_t DevourBlobOf(float rank,const std::vector<float>& start){size_t b=0;while(b+1<start.size()&&rank>=start[b+1])++b;return b;}
// 口の塊の中の 1 つの粒子の位置。along は奥 (0) から縁 (1)、angle は口の周りの角度、layer は壁の内側 (0) から外側 (1)
// 0〜1 の疑似乱数 (整数と種から)
inline float DevourHash(int n,float seed){float v=std::sin(float(n)*127.1f+seed*311.7f)*43758.5453f;return v-std::floor(v);}
// 仮足の伸び (0〜約 1.2): 縁を本数分に分け、1 本ずつ長さ・太さ・向きのずれを乱数で決める。
// 各仮足は幅の広いなめらかな山の形 (ガウス) で、隣と重なって縁が切れ目なくつながる
// (細く長い指にすると、流体の粒子が足りずに先がちぎれて粒になる)。先はゆっくり揺れる
inline float DevourLobe(float angle,float tentacles,float time,float seed){
    int n=std::max(1,int(std::round(tentacles)));if(tentacles<.5f)return 0;
    constexpr float tau=6.2831853f;
    float u=angle/tau*float(n)+seed*3.1f+.08f*std::sin(time*5+seed*7),sum=0;
    int center=int(std::floor(u+.5f));
    for(int k=center-1;k<=center+1;++k){int i=((k%n)+n)%n;
        float length=.6f+.5f*DevourHash(i,seed),width=.34f+.14f*DevourHash(i+97,seed),shift=(DevourHash(i+31,seed)-.5f)*.25f;
        float f=(u-float(k)-shift)/width;sum+=length*std::exp(-.5f*f*f);}
    return std::min(1.25f,sum);
}
// 口ののどから縁への広がり (のど .32〜縁 1)。のどを胴体の前の太さに近くして、胴体と口がくびれずにつながる
inline float DevourMouthFlare(float along){return .32f+.68f*along*std::sqrt(along);}
// 口の粒子の奥から縁への位置: 順位をそのまま使うと周りの長い縁ほど粒子がまばらになり、縁が破れてちぎれる。
// 広がりの周の長さに比べて並べ、縁にも同じ濃さで粒子を置く (u は 0〜1。順番は変わらない)
inline float DevourMouthAlong(float u){
    // 周の長さの累積 C(a) = .32a + .68a^2.5/2.5 を、ニュートン法で逆にする
    const float total=.32f+.68f/2.5f,target=std::clamp(u,0.f,1.f)*total;float a=std::clamp(u,0.f,1.f);
    for(int i=0;i<5;++i){float c=.32f*a+.68f/2.5f*a*a*std::sqrt(a);a=std::clamp(a-(c-target)/std::max(.05f,DevourMouthFlare(a)),0.f,1.f);}
    return a;
}
inline Vec DevourMouthPoint(const DevourBlob& b,float along,float angle,float layer,float time,float seed=0){
    constexpr float tau=6.2831853f;
    // 流れ込むねじれ: 奥へ行くほど角度がずれ、粒子の列が渦を巻く
    angle+=b.twist*(1-along);
    // 触手 (アメーバの仮足): 不ぞろいな仮足の房。房の中ほどの粒子ほど外へ広がりながら前へ伸びる
    float lobe=DevourLobe(angle,b.tentacles,time,seed);
    float tip=along*along;
    // のどから縁へラッパのように広がる。内側の粒子は壁の厚さの分だけ内へ。
    // 縁は唇のように厚くふくらませ、薄い膜が破れて穴が開かないようにする
    float wall=b.thickness+(1-b.thickness)*.25f*tip;
    float radius=DevourMouthFlare(along)*(1-wall*(1-layer))*(1+.5f*b.reach*lobe*tip);
    float z=(along*2-1)*b.size.z+.6f*b.reach*lobe*tip;
    // 巻き込み: 縁と触手の先ほど内側へ曲がり、少し前へかぶさる
    float curl=b.curl*tip*(.55f+.45f*lobe);radius*=1-.85f*curl;z+=.25f*curl*b.size.z;
    // 表面を前へ流れる波 (時間で進む)
    // (仮足の上では波を弱め、仮足の形をきれいに見せる)
    float ripple=std::sin(tau*(1.6f*along-2.2f*time)+2*angle);
    radius+=b.wave*ripple*(.3f+.7f*along)*(1-.6f*std::min(1.f,lobe));z+=b.wave*.6f*std::sin(tau*(along-2.2f*time)+angle);
    return b.at+Vec{std::cos(angle)*radius*b.size.x,std::sin(angle)*radius*b.size.y,z};
}
// 点を塊の中へ移す。順位で塊を選び、塊の中では普段の体の中の位置関係を保つ。単位は体の半径
// wall には、その点が大口の壁 (形を保つ所) である割合 (0〜1) を返す
inline Vec DevourBlobPoint(Vec rest,float rank,const std::vector<DevourBlob>& blobs,const std::vector<float>& start,float time=0,float seed=0,float* wall=nullptr){
    if(wall)*wall=0;
    const size_t b=DevourBlobOf(rank,start);
    const float begin=start[b],end=b+1<blobs.size()?start[b+1]:1.f;
    // 塊の中の位置 (-1〜1): 前後は順位、横と高さは普段の体のその前後の位置での断面の中の割合
    Vec n{rest.x/2.55f,rest.y/1.8f,rest.z/2.55f};
    float z=std::clamp((rank-begin)/std::max(1e-5f,end-begin)*2-1,-1.f,1.f);
    float width=std::sqrt(std::max(.0025f,1-n.z*n.z)),x=std::clamp(n.x/width,-1.f,1.f);
    float height=std::sqrt(std::max(.0025f,1-n.z*n.z-n.x*n.x)),y=std::clamp(n.y/height,0.f,1.f)*2-1;
    const auto& blob=blobs[b];
    if(blob.kind==DevourBlobKind::Gulp){
        // 前後は形の量の割合で並べる (口の周りの広い壁にも同じ濃さで粒子が来る)
        const auto& table=DevourGulp(blob.reach,blob.thickness,blob.curl);float u=(z+1)*.5f;size_t i=1;while(i+1<table.cumulative.size()&&table.cumulative[i]<u)++i;
        float c0=table.cumulative[i-1],c1=table.cumulative[i],t=(float(i-1)+std::clamp((u-c0)/std::max(1e-6f,c1-c0),0.f,1.f))/48;
        // 断面は円にし、空洞と外側の間の輪に面積が同じになるように置く (中心の点は空洞の壁へ)。
        // 横の位置は普段の体 (半楕円の断面) では中ほどが密なので、先に一様な値へ直す。
        // 正方形から円へは、面積と角度の割合を保つ写し方 (同心写像) にする。
        // (角度を atan2 で取ると対角へ粒子が寄り、口の左右と上下の壁がまばらになって穴が開く)
        float ux=(x*std::sqrt(std::max(0.f,1-x*x))+std::asin(x))/1.5707963f,rho=0,angle=0;
        if(std::abs(ux)>std::abs(y)){rho=std::abs(ux);angle=.7853982f*(y/ux)+(ux<0?3.1415927f:0.f);}
        else if(std::abs(y)>1e-6f){rho=std::abs(y);angle=1.5707963f-.7853982f*(ux/y)+(y<0?3.1415927f:0.f);}
        float outer,inner;DevourGulpProfile(t,blob.reach,blob.thickness,blob.curl,outer,inner);
        float r=std::sqrt(inner*inner+rho*rho*(outer*outer-inner*inner));
        auto smooth=[](float a,float b,float v){float w=std::clamp((v-a)/(b-a),0.f,1.f);return w*w*(3-2*w);};
        // 口から尾へ流れる波
        r*=1+blob.wave*std::sin(6.2831853f*(3*t+2.4f*time)+2*angle)*(.3f+.7f*t);
        // 不ぞろいなぷるぷる: 種で位相をずらした低い揺れを 2 つ掛け合わせ、左右対称の作り物に見せない
        r*=1+.8f*blob.wave*std::sin(2*angle+4.1f*t+9*time+seed*5.3f)*std::sin(3*angle-2.7f*t-6.3f*time+seed*2.1f);
        float front=(2*t-1)*blob.size.z;
        // 厚い唇: 縁の壁の中ほどが前へ丸くふくらむ。巻き込むと唇が前へかぶさる
        float rim=smooth(.85f,1,t),open=std::clamp(blob.reach,0.f,1.f);
        front+=blob.size.z*(.1f*open*rim*(1-(2*rho-1)*(2*rho-1))+.25f*blob.curl*smooth(.7f,1,t));
        // 尾は左右に揺れる (後ろほど大きく)
        float swing=blob.twist*(1-t)*(1-t)*std::sin(6.2831853f*(1.3f*t-2.2f*time))*blob.size.x;
        // 液体は重さで地面に広がる: 口より後ろは底を地面に着けたまま平たく横へ広がり、口だけが持ち上がる
        // (断面の中心を口の高さのままにすると、細い後ろ側が地面から浮いた管になる)
        float rear=(1-smooth(.45f,.95f,t))*open,squash=1-.3f*rear,spread=1+.2f*rear;
        float lift=1-(1-smooth(.6f,1,t))*open,grounded=outer*blob.size.y*squash;
        float center=grounded+(blob.at.y-grounded)*lift;
        // 口の壁 (空洞を囲む所) の点か。ここは形を保ち、空洞の無い尾は液体のまま柔らかくする
        if(wall)*wall=open*smooth(.2f,.55f,t);
        return Vec{blob.at.x+std::cos(angle)*r*blob.size.x*spread+swing,center+std::sin(angle)*r*blob.size.y*squash,blob.at.z+front};}
    if(blob.kind==DevourBlobKind::Amoeba){
        // 立方体の中の位置を球の中へ移し、水平の半径・向きと、その位置の上下の割合 (-1〜1) にする
        Vec q{x,y,z};float length=Length(q),most=std::max({std::abs(x),std::abs(y),std::abs(z)});
        if(length>1e-5f)q=q*(most/length);
        float rho=std::min(1.f,std::sqrt(q.x*q.x+q.z*q.z)),angle=std::atan2(q.x,q.z);
        float dome=std::sqrt(std::max(0.f,1-rho*rho)),v=dome>1e-4f?std::clamp(q.y/dome,-1.f,1.f):0.f;
        // 断面の高さ: 楕円体から、縁まで厚みの残るパンケーキへ (縁が薄くなって破れないように)
        float flat=std::clamp(blob.thickness,0.f,1.f),profile=dome+(std::sqrt(std::max(0.f,1-rho*rho*rho*rho))-dome)*flat;
        // 縁の仮足と、縁を流れる波 (中心ほど動かない。どの点も隣とつながったまま動く)
        float lobe=DevourLobe(angle,blob.tentacles,time,seed);
        float ripple=blob.wave*rho*std::sin(6.2831853f*(1.4f*rho-2.4f*time)+3*angle);
        float spread=rho*(1+blob.reach*lobe*rho*rho)*(1+ripple);
        // 上ほど前へ傾く (乗り出す・飛び込む勢い)
        float lean=blob.curl*(v*.5f+.5f)*profile*.5f;
        return blob.at+Vec{std::sin(angle)*spread*blob.size.x,v*profile*blob.size.y,(std::cos(angle)*spread+lean)*blob.size.z};}
    if(blob.kind==DevourBlobKind::Tail){
        // 前後は量の割合で並べ (細い尾に粒子が詰まらない)、断面は円にする
        const auto& table=DevourTail(blob.curl);float u=(z+1)*.5f;size_t i=1;while(i+1<table.cumulative.size()&&table.cumulative[i]<u)++i;
        float c0=table.cumulative[i-1],c1=table.cumulative[i],t=(float(i-1)+std::clamp((u-c0)/std::max(1e-6f,c1-c0),0.f,1.f))/48;
        float length=std::sqrt(x*x+y*y),most=std::max(std::abs(x),std::abs(y));if(length>1e-5f){x*=most/length;y*=most/length;}
        float r=DevourTailProfile(t,blob.curl),tail=(1-t)*(1-t);
        // ゴクン: 覆い終えてから、飲み込んだ物のふくらみが前から後ろへ流れていく
        if(time>DevourCoverEnd&&time<DevourPounceTime){float g=(time-DevourCoverEnd)/(DevourSettle*.8f);
            if(g<1){float f=(t-(1.05f-.9f*g))/.16f;r*=1+.28f*std::sin(3.1415927f*g)*std::exp(-.5f*f*f);}}
        // 尾は時間で左右に揺れ、後ろほど大きく揺れる
        float swing=blob.wave*2*tail*std::sin(6.2831853f*(1.3f*t-2.2f*time));
        return blob.at+Vec{x*r*blob.size.x+swing,y*r*blob.size.y,(2*t-1)*blob.size.z};}
    if(blob.kind==DevourBlobKind::Mouth){
        // 断面の中の位置を角度と壁の層にする (断面の外側の粒子ほど壁の外側)
        float layer=std::min(1.f,std::max(std::abs(x),std::abs(y)));
        return DevourMouthPoint(blob,DevourMouthAlong((z+1)*.5f),std::atan2(y,x),layer,time,seed);}
    // 楕円体: 立方体の中の位置を球の中へ (中心からの向きを保って縮める)
    Vec q{x,y,z};float length=Length(q),most=std::max({std::abs(x),std::abs(y),std::abs(z)});
    if(length>1e-5f)q=q*(most/length);
    return blob.at+Vec{q.x*blob.size.x,q.y*blob.size.y,q.z*blob.size.z};
}
// ある時間の全部の点の位置 (単位は体の半径。地面より下へは出さない)
// seed は飛びかかりごとの乱数の種 (仮足の形が毎回変わる)
// いちばん後ろの粒子が遅れる時間 (秒)
inline constexpr float DevourFollowLag=.07f;
// wall を渡すと、点ごとの口の壁の割合 (DevourBlobPoint) も返す
inline void DevourPose(const DevourAnimation& a,float time,const std::vector<Vec>& rest,const std::vector<float>& rank,std::vector<Vec>& out,float seed=0,std::vector<float>* wall=nullptr){
    out.resize(rest.size());auto blobs=DevourSampleBlobs(a,time);
    if(wall)wall->assign(rest.size(),0.f);
    if(blobs.empty()){for(size_t i=0;i<rest.size();++i)out[i]={rest[i].x/2.27f,rest[i].y/2.27f,rest[i].z/2.27f};return;}
    auto start=DevourBlobStarts(blobs);
    // 液体の遅れ: 後ろの粒子ほど少し前の時間の形をとる。前が先に飛び出し、後ろが引きずられて追いつく
    // (全体が同じ時間の形だと、1 つの固い作り物が変形しているように見える)
    constexpr float lag=DevourFollowLag;auto late=DevourSampleBlobs(a,std::max(0.f,time-lag));auto lateStart=DevourBlobStarts(late);
    for(size_t i=0;i<rest.size();++i){Vec p=DevourBlobPoint(rest[i],rank[i],blobs,start,time,seed,wall?&(*wall)[i]:nullptr);
        float w=(1-rank[i])*(1-rank[i]);
        if(w>1e-3f&&late.size()==blobs.size())p=p+(DevourBlobPoint(rest[i],rank[i],late,lateStart,time-lag,seed)-p)*w;
        p.y=std::max(.03f,p.y);out[i]=p;}
}
// 時間 time のキーを探す (無ければ -1)
inline int DevourKeyAt(const DevourAnimation& a,float time,float tolerance=.004f){
    for(size_t i=0;i<a.keys.size();++i)if(std::abs(a.keys[i].time-time)<=tolerance)return int(i);return -1;}
// 時間 time にキーを作る (既にあればそれ)。作るときは今の形をそのままキーにする。キーの番号を返す
inline int DevourEnsureKey(DevourAnimation& a,float time){
    int found=DevourKeyAt(a,time);if(found>=0)return found;
    DevourKey key{time,DevourSampleBlobs(a,time)};
    auto at=std::upper_bound(a.keys.begin(),a.keys.end(),time,[](float t,const DevourKey& k){return t<k.time;});
    return int(a.keys.insert(at,key)-a.keys.begin());
}
// キーを時間の順に並べ直す (並べ直した後の、元 index のキーの番号を返す)
inline int DevourSortKeys(DevourAnimation& a,int index){
    float time=index>=0&&size_t(index)<a.keys.size()?a.keys[size_t(index)].time:-1;
    std::stable_sort(a.keys.begin(),a.keys.end(),[](const DevourKey& l,const DevourKey& r){return l.time<r.time;});
    return index>=0?DevourKeyAt(a,time,0):-1;
}
// 塊を足す (全部のキーに同じ値で足す) / 消す / 種類を変える (全部のキーで)
inline void DevourAddBlob(DevourAnimation& a,const std::string& name,DevourBlob blob){a.names.push_back(name);for(auto& key:a.keys){key.blobs.resize(a.names.size()-1);key.blobs.push_back(blob);}}
inline void DevourRemoveBlob(DevourAnimation& a,size_t index){if(index>=a.names.size())return;a.names.erase(a.names.begin()+long(index));for(auto& key:a.keys)if(index<key.blobs.size())key.blobs.erase(key.blobs.begin()+long(index));}
inline void DevourSetBlobKind(DevourAnimation& a,size_t index,DevourBlobKind kind){for(auto& key:a.keys)if(index<key.blobs.size())key.blobs[index].kind=kind;}
}
namespace Game {
// 使っているアニメーション (初めて使うときに Resources/Data/devour_animation.json から読む。無ければ初めの形)。
// 保存と読み直しは失敗の理由を message に入れる。実装は Game/Systems/SlimeDevour.cpp
Chrono::DevourAnimation& DevourAnim();
bool SaveDevourAnim(std::string& message);
bool ReloadDevourAnim(std::string& message);
}
