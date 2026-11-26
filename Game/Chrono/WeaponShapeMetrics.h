// WeaponShapeMetrics.h
// 変形武器の形の見やすさを測る (F1。エンジンに依存しない)。形は細かい箱 (ボクセル) の並びで受け取る。
//  - 細すぎ: 太さが minThick より細い所の体積の割合 (液体の表面でならされて消える部分)。
//    半径 minThick/2 の球で削ってから戻す (モルフォロジーのオープニング) と消える部分を数える
//  - 塊の数: 半径 massRadius の球で削って戻したあとに残る、つながった塊の数 (遠くから見て読める大きなかたまり)。小さすぎる塊は数えない
//  - 離れた部品: 元の形の中で、つながっていない塊の数 (1 が正しい。2 以上は宙に浮いた部品)
//  - 部品の割合: 箱ごとにどの部品 (本体・材料・材料) が作ったかを持っていれば、部品ごとの体積の割合。小さすぎる部品は見えない
//  - 影絵: 横・正面・上から見た影絵の、囲む四角に対する面積の割合と、輪郭の込み具合 (周の長さの 2 乗 / (4π 面積)。円で 1) と、
//    凸包 (影絵を輪ゴムで囲んだ形) に対する面積の割合 (へこみの少なさ。1 に近いほど、どこから見ても 1 つの塊)。
//    「塊が 1 つだけ」は、塊が 1 つで、しかもどの向きの影絵にもへこみが少ないとき (太くつながった部品でも、影絵のへこみで読めれば良い)
//  - 判定: 上の数を基準と比べて、問題ごとに印を付ける (WeaponShapeIssue)。点数は 100 点から問題ごとに引く
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace Game::Chrono {
// 基準 (形の座標の長さ。体の形は高さ 6 くらい)
struct WeaponShapeLimits {
    float minThick=.8f;       // これより細い所は消える (呼ぶ側が流体の升と粒の間隔から決める)
    float massRadius=.9f;     // 塊を数えるときに削る半径
    float massShare=.04f;     // 全体のこれより小さい塊は数えない
    float thinMax=.15f;       // 細すぎの割合の上限
    int massMin=2,massMax=5;  // 読める塊の数の範囲
    float partMin=.1f;        // 部品の体積の割合の下限 (材料の部品がこれより小さいと見えない)
    float baseMax=.85f;       // 本体の割合の上限 (これより大きいと、ほかの材料がほとんど見えない)
    float pillarWidth=.45f;   // 柱: 端の部分の幅が、一番広い所の幅のこの割合より狭く
    float pillarHeight=.25f;  //      その細い部分が、その向きの長さのこの割合より長く、
    float pillarAspect=1.6f;  //      しかも細い部分の長さが、その幅のこの倍より長いと「柱」
    float solidMax=.88f;      // 影絵の凸包に対する割合の上限 (どの向きもこれより大きいと、ただの塊)
};
// (Pillar は、影絵の上に細い柱が長く突き出している形。形として良くないので、必ず直す (大きく減点))
enum class WeaponShapeIssue : uint8_t {Thin,Blob,Busy,Floating,TinyPart,BaseOnly,Pillar,Count};
inline const char* WeaponShapeIssueName(WeaponShapeIssue i){static constexpr const char* names[]={"細すぎ","塊が1つだけ","塊が多すぎ","離れた部品","小さすぎる部品","本体ばかり","柱の突き出し"};return names[std::min(int(i),int(WeaponShapeIssue::Count)-1)];}
struct WeaponShapeReport {
    int voxels=0,masses=0,pieces=0;float thinShare=0;std::array<float,3> partShare{};std::array<float,3> fill{},outline{},solid{};float pillar=0;int pillarFrom=-1; // (pillarFrom: どの影絵のどの端か。影絵 x 4 + 端 (0 上・1 下・2 後ろ・3 前))
    std::array<bool,size_t(WeaponShapeIssue::Count)> issues{};int score=100;
    bool Has(WeaponShapeIssue i)const{return issues[size_t(i)];}
};
// 箱の並び: nx ny nz・箱の大きさ cell・埋まっているか (並びは (z*ny+y)*nx+x)・どの部品か (0 は無し、1〜3。空でもよい)
struct WeaponShapeVoxels {int nx=0,ny=0,nz=0;float cell=1;const std::vector<uint8_t>* fill=nullptr;const std::vector<uint8_t>* part=nullptr;};

namespace ShapeMetricsDetail {
inline size_t Index(const WeaponShapeVoxels& v,int x,int y,int z){return size_t((z*v.ny+y)*v.nx+x);}
// 距離 (箱の数): seed の箱からの近似のユークリッド距離 (26 近傍の 2 パスのチャンファー)。seed でない箱へ広げる
inline std::vector<float> Distance(const WeaponShapeVoxels& v,const std::vector<uint8_t>& seed){
    const float big=1e9f;std::vector<float> d(seed.size(),big);for(size_t i=0;i<seed.size();++i)if(seed[i])d[i]=0;
    const float a=1,b=std::sqrt(2.f),c=std::sqrt(3.f);
    auto pass=[&](int dir){
        const int z0=dir>0?0:v.nz-1,z1=dir>0?v.nz:-1,y0=dir>0?0:v.ny-1,y1=dir>0?v.ny:-1,x0=dir>0?0:v.nx-1,x1=dir>0?v.nx:-1;
        for(int z=z0;z!=z1;z+=dir)for(int y=y0;y!=y1;y+=dir)for(int x=x0;x!=x1;x+=dir){float& here=d[Index(v,x,y,z)];if(here==0)continue;
            for(int dz=-1;dz<=0;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                // 前にたどった側 (dir の向きで先に来る箱) だけを見る
                const int key=dz*9+dy*3+dx;if(key>=0)continue;
                const int X=x+dx*dir,Y=y+dy*dir,Z=z+dz*dir;if(X<0||Y<0||Z<0||X>=v.nx||Y>=v.ny||Z>=v.nz)continue;
                const int n=std::abs(dx)+std::abs(dy)+std::abs(dz);const float step=n==1?a:n==2?b:c;
                here=std::min(here,d[Index(v,X,Y,Z)]+step);}}};
    pass(1);pass(-1);return d;
}
// 半径 r (箱の数) のオープニング: 中から r より深い箱を残し、そこから r 以内に戻す (元の形の中だけ)
inline std::vector<uint8_t> Open(const WeaponShapeVoxels& v,float r){
    const auto& fill=*v.fill;std::vector<uint8_t> empty(fill.size());for(size_t i=0;i<fill.size();++i)empty[i]=fill[i]?0:1;
    // (並びの外は空とみなすので、外枠の箱も「空からの距離 1」で数える)
    auto depth=Distance(v,empty);
    for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y)for(int x=0;x<v.nx;++x)if(x==0||y==0||z==0||x==v.nx-1||y==v.ny-1||z==v.nz-1){float& d=depth[Index(v,x,y,z)];if(fill[Index(v,x,y,z)])d=std::min(d,1.f);}
    std::vector<uint8_t> core(fill.size());for(size_t i=0;i<fill.size();++i)core[i]=fill[i]&&depth[i]>r?1:0;
    auto back=Distance(v,core);std::vector<uint8_t> out(fill.size());for(size_t i=0;i<fill.size();++i)out[i]=fill[i]&&back[i]<=r+.5f?1:0;
    return out;
}
// つながった塊 (6 近傍か 26 近傍) の大きさの一覧
inline std::vector<int> Components(const WeaponShapeVoxels& v,const std::vector<uint8_t>& set,bool corners){
    std::vector<int> sizes;std::vector<uint8_t> seen(set.size(),0);std::vector<int> stack;
    for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y)for(int x=0;x<v.nx;++x){const size_t start=Index(v,x,y,z);if(!set[start]||seen[start])continue;
        int size=0;seen[start]=1;stack.assign(1,int(start));
        while(!stack.empty()){const int at=stack.back();stack.pop_back();++size;const int cx=at%v.nx,cy=(at/v.nx)%v.ny,cz=at/(v.nx*v.ny);
            for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int n=std::abs(dx)+std::abs(dy)+std::abs(dz);if(n==0||(!corners&&n>1))continue;
                const int X=cx+dx,Y=cy+dy,Z=cz+dz;if(X<0||Y<0||Z<0||X>=v.nx||Y>=v.ny||Z>=v.nz)continue;const size_t i=Index(v,X,Y,Z);if(set[i]&&!seen[i]){seen[i]=1;stack.push_back(int(i));}}}
        sizes.push_back(size);}
    return sizes;
}
// 影絵 (axis 0: x を潰す横から、1: z を潰す正面から、2: y を潰す上から) の面積・囲む四角・周の長さ
inline void Silhouette(const WeaponShapeVoxels& v,int axis,float& fill,float& outline,float& solid){
    const auto& f=*v.fill;const int w=axis==0?v.nz:v.nx,h=axis==2?v.nz:v.ny;std::vector<uint8_t> img(size_t(w*h),0);
    for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y)for(int x=0;x<v.nx;++x)if(f[Index(v,x,y,z)]){const int u=axis==0?z:x,t=axis==2?z:y;img[size_t(t*w+u)]=1;}
    int area=0,perimeter=0,u0=w,u1=-1,t0=h,t1=-1;
    for(int t=0;t<h;++t)for(int u=0;u<w;++u){if(!img[size_t(t*w+u)])continue;++area;u0=std::min(u0,u);u1=std::max(u1,u);t0=std::min(t0,t);t1=std::max(t1,t);
        auto open=[&](int uu,int tt){return uu<0||tt<0||uu>=w||tt>=h||!img[size_t(tt*w+uu)];};perimeter+=open(u-1,t)+open(u+1,t)+open(u,t-1)+open(u,t+1);}
    if(area==0){fill=0;outline=0;solid=0;return;}
    fill=float(area)/float((u1-u0+1)*(t1-t0+1));outline=float(perimeter)*float(perimeter)/(4*3.1415927f*float(area));
    // 凸包 (埋まった升の 4 つの角の点を囲む。単調連鎖法) の面積
    std::vector<std::array<int,2>> pts;for(int t=0;t<h;++t)for(int u=0;u<w;++u)if(img[size_t(t*w+u)])for(int a=0;a<2;++a)for(int c=0;c<2;++c)pts.push_back({u+a,t+c});
    std::sort(pts.begin(),pts.end());pts.erase(std::unique(pts.begin(),pts.end()),pts.end());
    auto cross=[](std::array<int,2> o,std::array<int,2> a,std::array<int,2> b){return (long long)(a[0]-o[0])*(b[1]-o[1])-(long long)(a[1]-o[1])*(b[0]-o[0]);};
    std::vector<std::array<int,2>> hull(pts.size()*2);size_t k=0;
    for(size_t i=0;i<pts.size();++i){while(k>=2&&cross(hull[k-2],hull[k-1],pts[i])<=0)--k;hull[k++]=pts[i];}
    for(size_t i=pts.size()-1,low=k+1;i-->0;){while(k>=low&&cross(hull[k-2],hull[k-1],pts[i])<=0)--k;hull[k++]=pts[i];}
    hull.resize(k>0?k-1:0);long long twice=0;for(size_t i=0;i<hull.size();++i){const auto& a=hull[i];const auto& b=hull[(i+1)%hull.size()];twice+=(long long)a[0]*b[1]-(long long)b[0]*a[1];}
    solid=twice!=0?std::min(1.f,float(area)/(float(std::llabs(twice))*.5f)):1.f;
}
}

inline WeaponShapeReport MeasureWeaponShape(const WeaponShapeVoxels& v,const WeaponShapeLimits& limits={}){
    using namespace ShapeMetricsDetail;WeaponShapeReport r;if(!v.fill||v.nx<=0)return r;
    const auto& fill=*v.fill;for(uint8_t f:fill)r.voxels+=f?1:0;if(r.voxels==0)return r;
    // 細すぎ: 半径 minThick/2 のオープニングで消える割合
    {const auto opened=Open(v,limits.minThick*.5f/v.cell);int kept=0;for(uint8_t o:opened)kept+=o?1:0;r.thinShare=1-float(kept)/float(r.voxels);}
    // 塊の数 (小さい塊は数えない)
    {const auto opened=Open(v,limits.massRadius/v.cell);for(int s:Components(v,opened,false))if(float(s)>=limits.massShare*float(r.voxels))++r.masses;}
    // 離れた部品 (元の形の中で、全体の 1% 以上の塊)
    for(int s:Components(v,fill,true))if(float(s)>=.01f*float(r.voxels))++r.pieces;
    // 部品の割合
    if(v.part&&v.part->size()==fill.size()){std::array<int,3> count{};for(size_t i=0;i<fill.size();++i)if(fill[i]){const int p=(*v.part)[i];if(p>=1&&p<=3)++count[size_t(p-1)];}
        for(int p=0;p<3;++p)r.partShare[size_t(p)]=float(count[size_t(p)])/float(r.voxels);}
    for(int axis=0;axis<3;++axis)Silhouette(v,axis,r.fill[size_t(axis)],r.outline[size_t(axis)],r.solid[size_t(axis)]);
    // 柱: 横・正面・上の 3 つの影絵で、端から段ごとの幅を見る。一番広い幅のある割合より細い段が端から続く長さ (その向きの長さに対する割合) の一番長いもの。
    // 形はいつも左右対称で、左右に出る部品は 2 つ 1 組 (翼・こぶし) なので、左右の端からは数えない。1 本だけ突き出すのは、真ん中の線に沿った上と前後だけ
    for(int axis=0;axis<3;++axis){const auto& f=*v.fill;const int w=axis==0?v.nz:v.nx,h=axis==2?v.nz:v.ny;std::vector<uint8_t> img(size_t(w*h),0);
        for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y)for(int x=0;x<v.nx;++x)if(f[Index(v,x,y,z)]){const int u=axis==0?z:x,t=axis==2?z:y;img[size_t(t*w+u)]=1;}
        for(int dir=0;dir<2;++dir){if(dir==1&&axis!=0)continue;
            // dir 0: 段は t (幅は u の向き)、dir 1: 段は u (幅は t の向き)
            const int rows=dir==0?h:w,cols=dir==0?w:h;std::vector<int> width(size_t(rows),0);
            for(int a=0;a<rows;++a){int c0=cols,c1=-1;for(int c=0;c<cols;++c){const bool on=dir==0?img[size_t(a*w+c)]!=0:img[size_t(c*w+a)]!=0;if(on){c0=std::min(c0,c);c1=std::max(c1,c);}}width[size_t(a)]=c1>=c0?c1-c0+1:0;}
            int widest=0,last=-1,first=rows;for(int a=0;a<rows;++a){widest=std::max(widest,width[size_t(a)]);if(width[size_t(a)]>0){last=std::max(last,a);first=std::min(first,a);}}
            if(last<0)continue;const float limit=limits.pillarWidth*float(widest),span=float(last-first+1);
            // (横の影絵: 段 y は上の端だけ・段 z は前後の両端。正面の影絵: 上の端だけ。上の影絵: 段 z は前後の両端。下の端は床に乗る所なので数えない)
            // 細い段の続き (長さと幅の合計)。柱は「長さが幅の pillarAspect 倍以上」のときだけ数える (胴体の端や肩の上の頭のように、幅と長さが同じくらいの物は柱ではない)
            auto take=[&](int run,int sum,int end){if(run<=0)return;const float meanWidth=float(sum)/float(run);if(float(run)<limits.pillarAspect*meanWidth)return;
                if(float(run)/span>r.pillar){r.pillar=float(run)/span;r.pillarFrom=axis*4+end;}};
            int run=0,sum=0;for(int a=last;a>=first&&float(width[size_t(a)])<limit;--a){++run;sum+=width[size_t(a)];}take(run,sum,dir==0&&axis!=2?0:3);
            const bool both=(axis==0&&dir==1)||axis==2;
            run=0;sum=0;for(int a=first;a<=last&&float(width[size_t(a)])<limit;++a){++run;sum+=width[size_t(a)];}if(both)take(run,sum,2);}}
    // 判定と点数
    auto flag=[&](WeaponShapeIssue i,bool on,int cost){r.issues[size_t(i)]=on;if(on)r.score-=cost;};
    flag(WeaponShapeIssue::Thin,r.thinShare>limits.thinMax,25);
    flag(WeaponShapeIssue::Blob,r.masses<limits.massMin&&std::min({r.solid[0],r.solid[1],r.solid[2]})>limits.solidMax,25);
    flag(WeaponShapeIssue::Busy,r.masses>limits.massMax,10);
    flag(WeaponShapeIssue::Floating,r.pieces>1,20);
    bool tiny=false;if(v.part)for(int p=1;p<3;++p)if(r.partShare[size_t(p)]>0&&r.partShare[size_t(p)]<limits.partMin)tiny=true;
    flag(WeaponShapeIssue::TinyPart,tiny,10);
    flag(WeaponShapeIssue::BaseOnly,v.part&&r.partShare[0]>limits.baseMax,10);
    flag(WeaponShapeIssue::Pillar,r.pillar>limits.pillarHeight,60);
    r.score=std::max(0,r.score);
    return r;
}
}
