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
enum class DevourBlobKind {Lump=0,Mouth=1,Tail=2};
// 塊: 種類・中心・半径 (口は縁の横と縦の半径と奥行き)。口だけが使う値:
//   thickness 壁の厚さ (0〜1。1 で中まで詰まる)・reach 触手が前へ伸びる長さ・curl 縁が内側へ巻き込む強さ (0〜1)・
//   wave 表面を前へ流れる波の大きさ・tentacles 触手の本数・twist 粒子の流れのねじれ (渦を巻いて流れ込む)
// しずく形 (胴体) が使う値: curl 後ろを細くする強さ (0 で楕円体、1 で後ろへ細く伸びるしずく形)・wave 尾の左右の揺れ
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
    thread_local DevourTailTable table;
    if(std::abs(table.taper-taper)<1e-5f)return table;
    table.taper=taper;float sum=0;table.cumulative[0]=0;
    for(size_t i=1;i<table.cumulative.size();++i){float t=(float(i)-.5f)/48;float r=DevourTailProfile(t,taper);sum+=r*r/48;table.cumulative[i]=sum;}
    for(auto& c:table.cumulative)c/=std::max(1e-6f,sum);
    table.volume=1.5f*sum;return table;
}
inline float DevourBlobVolume(const DevourBlob& b){
    float box=std::max(0.f,b.size.x)*std::max(0.f,b.size.y)*std::max(0.f,b.size.z);
    if(b.kind==DevourBlobKind::Lump)return box;
    if(b.kind==DevourBlobKind::Tail)return box*DevourTail(b.curl).volume;
    float wall=std::clamp(b.thickness,.05f,1.f);return box*wall*(2-wall)*.75f;
}
inline float DevourVolume(const std::vector<DevourBlob>& blobs){float sum=0;for(const auto& b:blobs)sum+=DevourBlobVolume(b);return sum;}
// 初めの形: 胴体と口。溜め → 口を胴体より大きくラッパのように広げて飛び出し、仮足のような唇が外へ広がって物に触れる →
// 唇が上下左右から巻き込んで包む → 胴体ごと前へ流れ込んで飲み込み、丸いドームに戻る。
// (口が胴体と同じ太さだと、胴体と口が前後に並んで円柱に見える。口は胴体よりずっと広く、壁は薄くする)
inline DevourAnimation DevourDefaultAnimation(){
    // 胴体はしずく形: 飛びかかる間は前が丸く後ろへ細く伸びて揺れ、四角い塊が後ろに残らない
    auto lump=[](Vec at,Vec size,float taper=0,float swing=0){DevourBlob b;b.kind=DevourBlobKind::Tail;b.at=at;b.size=size;b.curl=taper;b.wave=swing;return b;};
    auto mouth=[](Vec at,Vec size,float thickness,float reach,float curl,float wave,float twist){
        DevourBlob b;b.kind=DevourBlobKind::Mouth;b.at=at;b.size=size;b.thickness=thickness;b.reach=reach;b.curl=curl;b.wave=wave;b.tentacles=9;b.twist=twist;return b;};
    const Vec hidden{.01f,.01f,.01f};
    DevourAnimation a;a.names={"胴体","口"};
    a.keys={
        {0,{lump({0,.4f,0},{1.12f,.4f,1.12f}),mouth({0,.4f,.6f},hidden,.5f,0,0,0,0)}},
        {DevourWindup,{lump({0,.32f,-.12f},{1.2f,.33f,1.2f},.1f),mouth({0,.36f,.5f},hidden,.5f,0,0,0,0)}},
        {DevourWindup+DevourLeap*.4f,{lump({0,.45f,-.85f},{.72f,.45f,1.2f},.85f,.12f),mouth({0,.55f,.5f},{1.1f,.8f,.5f},.3f,.4f,0,.1f,.5f)}},
        {DevourLandAt,{lump({0,.48f,-1.2f},{.62f,.44f,1.35f},1,.15f),mouth({0,.7f,.6f},{1.7f,1.25f,.65f},.2f,.6f,.15f,.12f,.8f)}},
        {DevourCoverEnd,{lump({0,.55f,-.75f},{.7f,.5f,1.05f},.8f,.1f),mouth({0,.65f,.55f},{1.5f,1.1f,.8f},.25f,.35f,.85f,.08f,1.f)}},
        {DevourCoverEnd+DevourSettle*.5f,{lump({0,.5f,-.15f},{.95f,.47f,1.f},.3f,.04f),mouth({0,.5f,.35f},{.8f,.6f,.55f},.7f,0,1,.03f,.6f)}},
        {DevourPounceTime,{lump({0,.4f,0},{1.12f,.4f,1.12f}),mouth({0,.4f,.3f},hidden,1,0,1,0,0)}}};
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
// 各仮足はなめらかな山の形 (ガウス) なので、不ぞろいでも縁はきれいにつながる。先はゆっくり揺れる
inline float DevourLobe(float angle,float tentacles,float time,float seed){
    int n=std::max(1,int(std::round(tentacles)));if(tentacles<.5f)return 0;
    constexpr float tau=6.2831853f;
    float u=angle/tau*float(n)+seed*3.1f+.08f*std::sin(time*5+seed*7),sum=0;
    int center=int(std::floor(u+.5f));
    for(int k=center-1;k<=center+1;++k){int i=((k%n)+n)%n;
        float length=.45f+.75f*DevourHash(i,seed),width=.2f+.16f*DevourHash(i+97,seed),shift=(DevourHash(i+31,seed)-.5f)*.3f;
        float f=(u-float(k)-shift)/width;sum+=length*std::exp(-.5f*f*f);}
    return std::min(1.25f,sum);
}
inline Vec DevourMouthPoint(const DevourBlob& b,float along,float angle,float layer,float time,float seed=0){
    constexpr float tau=6.2831853f;
    // 流れ込むねじれ: 奥へ行くほど角度がずれ、粒子の列が渦を巻く
    angle+=b.twist*(1-along);
    // 触手 (アメーバの仮足): 不ぞろいな仮足の房。房の中ほどの粒子ほど外へ広がりながら前へ伸びる
    float lobe=DevourLobe(angle,b.tentacles,time,seed);
    float tip=along*along;
    // のどから縁へラッパのように広がる。内側の粒子は壁の厚さの分だけ内へ
    float radius=(.22f+.78f*along*std::sqrt(along))*(1-b.thickness*(1-layer))*(1+.5f*b.reach*lobe*tip);
    float z=(along*2-1)*b.size.z+.7f*b.reach*lobe*tip;
    // 巻き込み: 縁と触手の先ほど内側へ曲がり、少し前へかぶさる
    float curl=b.curl*tip*(.55f+.45f*lobe);radius*=1-.85f*curl;z+=.25f*curl*b.size.z;
    // 表面を前へ流れる波 (時間で進む)
    // (仮足の上では波を弱め、仮足の形をきれいに見せる)
    float ripple=std::sin(tau*(1.6f*along-2.2f*time)+2*angle);
    radius+=b.wave*ripple*(.3f+.7f*along)*(1-.6f*std::min(1.f,lobe));z+=b.wave*.6f*std::sin(tau*(along-2.2f*time)+angle);
    return b.at+Vec{std::cos(angle)*radius*b.size.x,std::sin(angle)*radius*b.size.y,z};
}
// 点を塊の中へ移す。順位で塊を選び、塊の中では普段の体の中の位置関係を保つ。単位は体の半径
inline Vec DevourBlobPoint(Vec rest,float rank,const std::vector<DevourBlob>& blobs,const std::vector<float>& start,float time=0,float seed=0){
    const size_t b=DevourBlobOf(rank,start);
    const float begin=start[b],end=b+1<blobs.size()?start[b+1]:1.f;
    // 塊の中の位置 (-1〜1): 前後は順位、横と高さは普段の体のその前後の位置での断面の中の割合
    Vec n{rest.x/2.55f,rest.y/1.8f,rest.z/2.55f};
    float z=std::clamp((rank-begin)/std::max(1e-5f,end-begin)*2-1,-1.f,1.f);
    float width=std::sqrt(std::max(.0025f,1-n.z*n.z)),x=std::clamp(n.x/width,-1.f,1.f);
    float height=std::sqrt(std::max(.0025f,1-n.z*n.z-n.x*n.x)),y=std::clamp(n.y/height,0.f,1.f)*2-1;
    const auto& blob=blobs[b];
    if(blob.kind==DevourBlobKind::Tail){
        // 前後は量の割合で並べ (細い尾に粒子が詰まらない)、断面は円にする
        const auto& table=DevourTail(blob.curl);float u=(z+1)*.5f;size_t i=1;while(i+1<table.cumulative.size()&&table.cumulative[i]<u)++i;
        float c0=table.cumulative[i-1],c1=table.cumulative[i],t=(float(i-1)+std::clamp((u-c0)/std::max(1e-6f,c1-c0),0.f,1.f))/48;
        float length=std::sqrt(x*x+y*y),most=std::max(std::abs(x),std::abs(y));if(length>1e-5f){x*=most/length;y*=most/length;}
        float r=DevourTailProfile(t,blob.curl),tail=(1-t)*(1-t);
        // 尾は時間で左右に揺れ、後ろほど大きく揺れる
        float swing=blob.wave*2*tail*std::sin(6.2831853f*(1.3f*t-2.2f*time));
        return blob.at+Vec{x*r*blob.size.x+swing,y*r*blob.size.y,(2*t-1)*blob.size.z};}
    if(blob.kind==DevourBlobKind::Mouth){
        // 断面の中の位置を角度と壁の層にする (断面の外側の粒子ほど壁の外側)
        float layer=std::min(1.f,std::max(std::abs(x),std::abs(y)));
        return DevourMouthPoint(blob,(z+1)*.5f,std::atan2(y,x),layer,time,seed);}
    // 楕円体: 立方体の中の位置を球の中へ (中心からの向きを保って縮める)
    Vec q{x,y,z};float length=Length(q),most=std::max({std::abs(x),std::abs(y),std::abs(z)});
    if(length>1e-5f)q=q*(most/length);
    return blob.at+Vec{q.x*blob.size.x,q.y*blob.size.y,q.z*blob.size.z};
}
// ある時間の全部の点の位置 (単位は体の半径。地面より下へは出さない)
// seed は飛びかかりごとの乱数の種 (仮足の形が毎回変わる)
inline void DevourPose(const DevourAnimation& a,float time,const std::vector<Vec>& rest,const std::vector<float>& rank,std::vector<Vec>& out,float seed=0){
    out.resize(rest.size());auto blobs=DevourSampleBlobs(a,time);
    if(blobs.empty()){for(size_t i=0;i<rest.size();++i)out[i]={rest[i].x/2.27f,rest[i].y/2.27f,rest[i].z/2.27f};return;}
    auto start=DevourBlobStarts(blobs);
    for(size_t i=0;i<rest.size();++i){Vec p=DevourBlobPoint(rest[i],rank[i],blobs,start,time,seed);p.y=std::max(.03f,p.y);out[i]=p;}
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
