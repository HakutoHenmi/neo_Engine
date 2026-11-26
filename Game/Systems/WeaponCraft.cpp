// WeaponCraft.cpp
// 変形武器づくり (ChronoSystem の一部。規則は Chrono/WeaponCraftRules.h)。
//  - 準備フェーズが終わると、チャレンジの始まりの前に「作る画面」を開く。集めた物を全部並べ、3 つを混ぜて武器を 1 本作る
//    (3 つの物の性質 (追尾・貫通・重い・爆発…) が全部まざった弾を撃つ。枠が埋まったあとに入れた物は、選んでいる強化 (弾数・威力・連射) の点になる)
//  - 決めたら、入れなかった残りの物も 3 つの強化へ均等に振り分ける (集めた物は全部どこかで役に立つ)
//  - 武器の形: 材料 3 つの物のモデル (ローポリ) を細かい箱に分けて、本体・前の部品・左右の部品として組み合わせる。
//    スライムの体 (流体の粒子) は撃つ間この形に変形する。材料が 1 つでも違えば、並べる順が違っても違う形になる
//  - 回数の制限はない。押している間、連射の間隔ごとに弾数の分だけ扇に撃つ
// 操作 (作る画面): クリック / Enter / A で入れる・右クリック / Backspace / B で 1 つ戻す・1〜3 / Tab / X で強化・R / Y でおまかせ・Space / START で決定
#include "ChronoSystemInternal.h"
#include "../Data/Collection.h"
#include "../Chrono/WeaponShapeSdf.h"
#include "../Chrono/WeaponShapeGrammar.h"
#include <filesystem>
#include <tuple>
#include <fstream>

namespace Game {
namespace {
bool HasArg(const wchar_t* flag){return wcsstr(GetCommandLineW(),flag)!=nullptr;}
float ArgFloat(const wchar_t* flag,float fallback){const wchar_t* at=wcsstr(GetCommandLineW(),flag);return at?float(_wtof(at+wcslen(flag))):fallback;}
std::string NameOf(const std::string& id){const int index=Collection::IndexOf(id);return index<0?id:Collection::Entries()[size_t(index)].name;}
std::string ImageOf(const std::string& id){const int index=Collection::IndexOf(id);return index<0?std::string():Collection::ImagePath(Collection::Entries()[size_t(index)]);}

// ---- 物のモデルを細かい箱に分ける ----
// 箱の並び: 一番下の角 lo・箱の大きさ cell・数 nx ny nz・埋まっているか。範囲 (lo..hi) はモデルの座標
struct ItemVoxels {Vec lo{},hi{};float cell=1;int nx=0,ny=0,nz=0;std::vector<uint8_t> fill;
    bool At(int x,int y,int z)const{return x>=0&&y>=0&&z>=0&&x<nx&&y<ny&&z<nz&&fill[size_t((z*ny+y)*nx+x)]!=0;}
    bool Inside(Vec p)const{return At(int(std::floor((p.x-lo.x)/cell)),int(std::floor((p.y-lo.y)/cell)),int(std::floor((p.z-lo.z)/cell)));}};
// 三角形の並び (頂点の位置) から、つながった部分ごとに中を埋める。
// 部分ごとに x の向きの線を通して、面を横切る数の偶奇で中かどうかを決める (重なった部分どうしが打ち消し合わないように)。
// 閉じていない部分 (板 1 枚など) は、その部分の範囲の箱を埋める。最後に 1 箱ぶん太らせる (薄い所が切れないように)
ItemVoxels VoxelizeTriangles(const std::vector<Vec>& points,const std::vector<uint32_t>& indices){
    ItemVoxels v;v.lo={1e9f,1e9f,1e9f};v.hi={-1e9f,-1e9f,-1e9f};
    for(const auto& p:points){v.lo={std::min(v.lo.x,p.x),std::min(v.lo.y,p.y),std::min(v.lo.z,p.z)};v.hi={std::max(v.hi.x,p.x),std::max(v.hi.y,p.y),std::max(v.hi.z,p.z)};}
    const Vec size=v.hi-v.lo;const float longest=std::max({size.x,size.y,size.z,1e-3f});v.cell=longest/float(CraftItemCells);
    v.lo=v.lo-Vec{v.cell,v.cell,v.cell};v.nx=int(std::ceil(size.x/v.cell))+3;v.ny=int(std::ceil(size.y/v.cell))+3;v.nz=int(std::ceil(size.z/v.cell))+3;
    v.fill.assign(size_t(v.nx*v.ny*v.nz),0);
    // つながった部分: 同じ位置の頂点と、三角形の頂点どうしをまとめる
    const size_t n=points.size();std::vector<uint32_t> parent(n);for(size_t i=0;i<n;++i)parent[i]=uint32_t(i);
    auto find=[&](uint32_t a){while(parent[a]!=a)a=parent[a]=parent[parent[a]];return a;};
    auto unite=[&](uint32_t a,uint32_t b){parent[find(a)]=find(b);};
    {std::map<std::tuple<int,int,int>,uint32_t> same;const float q=1000/longest;
        for(size_t i=0;i<n;++i){auto key=std::make_tuple(int(std::lround(points[i].x*q)),int(std::lround(points[i].y*q)),int(std::lround(points[i].z*q)));
            auto [match,added]=same.emplace(key,uint32_t(i));if(!added)unite(uint32_t(i),match->second);}}
    for(size_t k=0;k+2<indices.size();k+=3){unite(indices[k],indices[k+1]);unite(indices[k],indices[k+2]);}
    std::map<uint32_t,int> partOf;for(size_t k=0;k+2<indices.size();k+=3)partOf.emplace(find(indices[k]),int(partOf.size()));
    const int parts=int(partOf.size());std::vector<std::array<Vec,2>> partBox(size_t(parts),{Vec{1e9f,1e9f,1e9f},Vec{-1e9f,-1e9f,-1e9f}});std::vector<int> triPart;
    for(size_t k=0;k+2<indices.size();k+=3){const int part=partOf[find(indices[k])];triPart.push_back(part);auto& box=partBox[size_t(part)];
        for(int c=0;c<3;++c){const Vec p=points[indices[k+size_t(c)]];box[0]={std::min(box[0].x,p.x),std::min(box[0].y,p.y),std::min(box[0].z,p.z)};box[1]={std::max(box[1].x,p.x),std::max(box[1].y,p.y),std::max(box[1].z,p.z)};}}
    std::vector<int> partFill(size_t(parts),0);std::vector<uint8_t> owner(v.fill.size(),0);
    std::vector<std::vector<float>> rowHits(static_cast<size_t>(parts));
    for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y){
        // (線は箱の真ん中から少しずらす。面の境目をちょうど通ると数え間違えるため)
        const float py=v.lo.y+(float(y)+.5f)*v.cell+v.cell*.0137f,pz=v.lo.z+(float(z)+.5f)*v.cell+v.cell*.0071f;
        for(auto& h:rowHits)h.clear();
        for(size_t k=0;k+2<indices.size();k+=3){const Vec a=points[indices[k]],b=points[indices[k+1]],c=points[indices[k+2]];
            // yz の面に映した三角形に点が入るか (重心の座標)
            const float d=(b.y-a.y)*(c.z-a.z)-(c.y-a.y)*(b.z-a.z);if(std::abs(d)<1e-9f)continue;
            const float u=((py-a.y)*(c.z-a.z)-(c.y-a.y)*(pz-a.z))/d,w=((b.y-a.y)*(pz-a.z)-(py-a.y)*(b.z-a.z))/d;
            if(u<0||w<0||u+w>1)continue;rowHits[size_t(triPart[k/3])].push_back(a.x+(b.x-a.x)*u+(c.x-a.x)*w);}
        for(int part=0;part<parts;++part){auto& h=rowHits[size_t(part)];if(h.size()<2)continue;std::sort(h.begin(),h.end());
            for(size_t i=0;i+1<h.size();i+=2){const int x0=int(std::floor((h[i]-v.lo.x)/v.cell+.5f)),x1=int(std::floor((h[i+1]-v.lo.x)/v.cell-.5f));
                for(int x=std::max(0,x0);x<=std::min(v.nx-1,x1);++x){const size_t at=size_t((z*v.ny+y)*v.nx+x);if(!v.fill[at]){v.fill[at]=1;++partFill[size_t(part)];}}}}}
    // 閉じていない (ほとんど埋まらなかった) 部分は、範囲の箱を埋める
    for(int part=0;part<parts;++part){const auto& box=partBox[size_t(part)];
        const int x0=int((box[0].x-v.lo.x)/v.cell),x1=int((box[1].x-v.lo.x)/v.cell),y0=int((box[0].y-v.lo.y)/v.cell),y1=int((box[1].y-v.lo.y)/v.cell),z0=int((box[0].z-v.lo.z)/v.cell),z1=int((box[1].z-v.lo.z)/v.cell);
        const int cells=(x1-x0+1)*(y1-y0+1)*(z1-z0+1);if(partFill[size_t(part)]>=std::max(1,cells/30))continue;
        for(int z=z0;z<=z1;++z)for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)if(x>=0&&y>=0&&z>=0&&x<v.nx&&y<v.ny&&z<v.nz)v.fill[size_t((z*v.ny+y)*v.nx+x)]=1;}
    // 1 箱ぶん太らせる
    std::vector<uint8_t> grown=v.fill;
    for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y)for(int x=0;x<v.nx;++x)if(!v.fill[size_t((z*v.ny+y)*v.nx+x)])
        if(v.At(x-1,y,z)||v.At(x+1,y,z)||v.At(x,y-1,z)||v.At(x,y+1,z)||v.At(x,y,z-1)||v.At(x,y,z+1))grown[size_t((z*v.ny+y)*v.nx+x)]=1;
    v.fill=std::move(grown);
    return v;
}
// 範囲 (埋まった所) の下の角と上の角
struct ItemShape {ItemVoxels voxels;Vec lo{},hi{};};
ItemShape SphereShape(){
    // モデルの無い物は丸い玉
    std::vector<Vec> pts;std::vector<uint32_t> idx;const int rings=8,segs=12;
    for(int r=0;r<=rings;++r)for(int s=0;s<segs;++s){const float a=3.1415927f*float(r)/rings,b=6.2831853f*float(s)/segs;pts.push_back(Vec{std::sin(a)*std::cos(b),std::cos(a),std::sin(a)*std::sin(b)});}
    for(int r=0;r<rings;++r)for(int s=0;s<segs;++s){const uint32_t a=uint32_t(r*segs+s),b=uint32_t(r*segs+(s+1)%segs),c=a+uint32_t(segs),d=b+uint32_t(segs);idx.insert(idx.end(),{a,c,b,b,c,d});}
    ItemShape out;out.voxels=VoxelizeTriangles(pts,idx);out.lo={-1,-1,-1};out.hi={1,1,1};return out;
}
}

// 物 (図鑑の id) の形: ローポリのモデルを箱に分けたもの (一度作ったら使い回す)
static const ItemShape& CraftItemShape(const std::string& id){
    static std::map<std::string,ItemShape> cache;auto found=cache.find(id);if(found!=cache.end())return found->second;
    const std::string path="Resources/Models/Lowpoly/"+id+".obj";ItemShape shape;bool made=false;
    if(std::filesystem::exists(path)){auto* render=Engine::Renderer::GetInstance();const uint32_t mesh=render->LoadObjMesh(path);
        if(auto* model=render->GetModel(mesh)){const auto& data=model->GetData();std::vector<Vec> points;points.reserve(data.vertices.size());
            for(const auto& v:data.vertices)points.push_back(Vec{v.position.x,v.position.y,v.position.z});
            std::vector<uint32_t> indices(data.indices.begin(),data.indices.end());if(indices.empty())for(uint32_t i=0;i<uint32_t(points.size());++i)indices.push_back(i);
            if(!points.empty()){shape.voxels=VoxelizeTriangles(points,indices);made=true;}}}
    if(!made)shape=SphereShape();
    // 埋まった範囲
    const auto& v=shape.voxels;Vec lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
    for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y)for(int x=0;x<v.nx;++x)if(v.At(x,y,z)){const Vec a=v.lo+Vec{float(x),float(y),float(z)}*v.cell;
        lo={std::min(lo.x,a.x),std::min(lo.y,a.y),std::min(lo.z,a.z)};hi={std::max(hi.x,a.x+v.cell),std::max(hi.y,a.y+v.cell),std::max(hi.z,a.z+v.cell)};}
    if(lo.x>hi.x){lo=v.lo;hi=v.lo+Vec{1,1,1};}
    shape.lo=lo;shape.hi=hi;
    return cache.emplace(id,std::move(shape)).first->second;
}

// (比べる用: F1 までの作り方。物のモデルの箱をそのまま本体・前・左右に重ねる。--craft-shape-legacy のときだけ使う)
static WeaponShapeGrid BuildVoxelUnionShape(const std::array<std::string,CraftParts>& ids,int filled){
    struct Place {const ItemShape* shape=nullptr;Vec center{};float scale=1;bool side=false;};std::vector<Place> places;
    auto longest=[](const ItemShape& s){const Vec d=s.hi-s.lo;return std::max({d.x,d.y,d.z,1e-3f});};
    const ItemShape& base=CraftItemShape(ids[0]);const float baseScale=CraftBaseSize/longest(base);const Vec bd=(base.hi-base.lo)*baseScale;
    places.push_back({&base,Vec{0,bd.y*.5f,0},baseScale,false});
    if(filled>=2){const ItemShape& s=CraftItemShape(ids[1]);const float k=CraftFrontScale*CraftBaseSize/longest(s);const Vec d=(s.hi-s.lo)*k;
        places.push_back({&s,Vec{0,std::max(d.y*.5f,bd.y*CraftFrontHeight),bd.z*.5f+d.z*.5f*(1-2*CraftOverlap)},k,false});}
    if(filled>=3){const ItemShape& s=CraftItemShape(ids[2]);const float k=CraftSideScale*CraftBaseSize/longest(s);const Vec d=(s.hi-s.lo)*k;
        places.push_back({&s,Vec{bd.x*.5f+d.x*.5f*(1-2*CraftOverlap),std::max(d.y*.5f,bd.y*CraftSideHeight),0},k,true});}
    Vec lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
    for(const auto& p:places){const Vec half=(p.shape->hi-p.shape->lo)*(p.scale*.5f);
        for(float mirror:{1.f,-1.f}){if(mirror<0&&!p.side)continue;const Vec c{p.center.x*mirror,p.center.y,p.center.z};
            lo={std::min(lo.x,c.x-half.x),std::min(lo.y,c.y-half.y),std::min(lo.z,c.z-half.z)};hi={std::max(hi.x,c.x+half.x),std::max(hi.y,c.y+half.y),std::max(hi.z,c.z+half.z)};}}
    lo.y=std::min(lo.y,0.f);
    const Vec size=hi-lo;WeaponShapeGrid g;g.cell=std::max({size.x,size.y,size.z})/float(CraftShapeCells);g.lo=lo;
    g.nx=int(std::ceil(size.x/g.cell))+1;g.ny=int(std::ceil(size.y/g.cell))+1;g.nz=int(std::ceil(size.z/g.cell))+1;g.fill.assign(size_t(g.nx*g.ny*g.nz),0);g.part.assign(g.fill.size(),0);
    int count=0;
    for(int z=0;z<g.nz;++z)for(int y=0;y<g.ny;++y)for(int x=0;x<g.nx;++x){const Vec p=g.lo+Vec{float(x)+.5f,float(y)+.5f,float(z)+.5f}*g.cell;int owner=0;
        for(size_t pi=0;pi<places.size()&&!owner;++pi){const auto& pl=places[pi];const Vec mid=(pl.shape->lo+pl.shape->hi)*.5f;
            for(float mirror:{1.f,-1.f}){if(mirror<0&&!pl.side)continue;const Vec q{p.x*mirror,p.y,p.z};if(pl.shape->voxels.Inside(mid+(q-pl.center)*(1/pl.scale))){owner=int(pi)+1;break;}}}
        if(owner){const size_t i=size_t((z*g.ny+y)*g.nx+x);g.fill[i]=1;g.part[i]=uint8_t(owner);++count;}}
    const float volume=std::max(1e-4f,float(count)*g.cell*g.cell*g.cell);const float k=std::min(std::cbrt(CraftShapeVolume/volume),9.5f/std::max({size.x,size.y,size.z,1e-3f}));
    g.lo=g.lo*k;g.cell*=k;const Vec glo=lo*k,ghi=hi*k;
    g.bounds={std::max(std::abs(glo.x),std::abs(ghi.x)),ghi.y,std::max(std::abs(glo.z),std::abs(ghi.z))};
    g.core={Vec{0,bd.y*.5f*k,0},std::clamp(std::min({bd.x*.5f,bd.y*.5f,bd.z*.5f})*k*.6f,.35f,1.6f)};
    return g;
}

// なめらかな部品の並びを箱に分ける (床より下は作らない)。count に埋まった箱の数
static WeaponShapeGrid RasterizeParts(const std::vector<SdfPart>& parts,const std::string& key,int& count){
    Vec lo{1e9f,0,1e9f},hi{-1e9f,-1e9f,-1e9f};
    for(const auto& s:parts){const Vec e=SdfExtent(s)+Vec{CraftBlend,CraftBlend,CraftBlend};
        for(float m:{1.f,-1.f}){if(m<0&&!s.mirror)continue;lo={std::min(lo.x,s.center.x*m-e.x),std::min(lo.y,s.center.y-e.y),std::min(lo.z,s.center.z-e.z)};hi={std::max(hi.x,s.center.x*m+e.x),std::max(hi.y,s.center.y+e.y),std::max(hi.z,s.center.z+e.z)};}}
    lo.y=std::max(lo.y,0.f);
    const Vec size=hi-lo;WeaponShapeGrid g;g.key=key;g.cell=std::max({size.x,size.y,size.z})/float(CraftShapeCells);g.lo=lo;
    g.nx=int(std::ceil(size.x/g.cell))+1;g.ny=int(std::ceil(size.y/g.cell))+1;g.nz=int(std::ceil(size.z/g.cell))+1;g.fill.assign(size_t(g.nx*g.ny*g.nz),0);g.part.assign(g.fill.size(),0);g.piece.assign(g.fill.size(),0);
    count=0;
    for(int z=0;z<g.nz;++z)for(int y=0;y<g.ny;++y)for(int x=0;x<g.nx;++x){const Vec p=g.lo+Vec{float(x)+.5f,float(y)+.5f,float(z)+.5f}*g.cell;int owner=0,index=0;
        if(SdfShapeDistance(parts,p,CraftBlend,&owner,&index)<0){const size_t i=size_t((z*g.ny+y)*g.nx+x);g.fill[i]=1;g.part[i]=uint8_t(owner);g.piece[i]=uint8_t(std::min(index+1,255));++count;}}
    return g;
}

// 物の形を基本形 1 つに置きかえたもの (一度作ったら使い回す)。一番長い向きの半分が 1
static const SdfPart& CraftItemPart(const std::string& id){
    static std::map<std::string,SdfPart> cache;auto found=cache.find(id);if(found!=cache.end())return found->second;
    const auto& v=CraftItemShape(id).voxels;std::vector<Vec> points;
    for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y)for(int x=0;x<v.nx;++x)if(v.At(x,y,z))points.push_back(v.lo+Vec{float(x)+.5f,float(y)+.5f,float(z)+.5f}*v.cell);
    return cache.emplace(id,SdfFitPoints(points)).first->second;
}

// 武器の形を作る (同じ材料の並びなら前に作った形を使う)。形の番号を返す
// 3 つの物をそれぞれなめらかな基本形 1 つに置きかえ、本体・前・左右 (鏡で 2 つ) に置いて、なめらかにつなぐ。
// 体積をそろえたあとで細すぎる部品は太らせ、置き直してもう一度作る (太さの下限 CraftMinThick)
int ChronoSystem::BuildWeaponShape(const BattleWeapon& w){
    if(w.filled<=0)return 0;
    std::string key="sdf|";for(int i=0;i<w.filled;++i)key+=w.ids[size_t(i)]+"|";
    // (比べる用: --craft-shape-legacy なら F1 までの作り方、--craft-shape-f2 なら F2 の作り方 (物の形をそのまま本体・前・左右に))
    static const bool legacy=wcsstr(GetCommandLineW(),L"--craft-shape-legacy")!=nullptr,f2=wcsstr(GetCommandLineW(),L"--craft-shape-f2")!=nullptr;
    if(legacy)key="voxel|"+key.substr(4);else if(!f2)key="grammar|"+key.substr(4);
    auto& grids=WeaponShapeGrids();for(size_t i=0;i<grids.size();++i)if(grids[i].key==key)return WeaponShapeFirst+int(i);
    if(legacy){WeaponShapeGrid g=BuildVoxelUnionShape(w.ids,w.filled);g.key=key;grids.push_back(std::move(g));return WeaponShapeFirst+int(grids.size())-1;}
    // (F3) 性質から型と部品を組み立てる。体積をそろえた倍率で太さの下限を決め直し、落ち着くまで作り直す
    if(!f2){ShapeDesignInput in;in.lv=FuseLevelsOf(w.actions,w.filled);in.body=CraftItemPart(w.ids[0]);
        // (調整用: --shape-part-scale= と --shape-overlap= で部品の大きさと重なりを試せる)
        {static const float partScale=[]{const wchar_t* at=wcsstr(GetCommandLineW(),L"--shape-part-scale=");return at?float(_wtof(at+19)):ShapePartScale;}();
         static const float overlap=[]{const wchar_t* at=wcsstr(GetCommandLineW(),L"--shape-overlap=");return at?float(_wtof(at+16)):ShapeOverlap;}();in.partScale=partScale;in.overlap=overlap;}
        for(int i=w.filled;i-->0;)for(FuseTrait t:FuseTraitsOf(w.actions[size_t(i)]))if(t!=FuseTrait::Count)in.owner[size_t(t)]=i+1;
        float minHalf=0,k=1;std::vector<SdfPart> parts;WeaponShapeGrid g;
        for(int pass=0;pass<4;++pass){parts=DesignWeaponShape(in,minHalf);int count=0;g=RasterizeParts(parts,key,count);
            const float longest=float(std::max({g.nx,g.ny,g.nz}))*g.cell;const float volume=std::max(1e-4f,float(count)*g.cell*g.cell*g.cell);
            k=std::min(std::cbrt(CraftShapeVolume/volume),9.5f/std::max(longest,1e-3f));const float want=CraftMinThick*.5f/k;
            if(std::abs(want-minHalf)<.01f*want)break;minHalf=want;}
        g.lo=g.lo*k;g.cell*=k;const Vec glo=g.lo,ghi=g.lo+Vec{float(g.nx),float(g.ny),float(g.nz)}*g.cell;
        // (F5) 部品ごとの動きと支点 (形の座標に直す)
        g.pieces.clear();for(const auto& s:parts)g.pieces.push_back({uint8_t(s.motion),s.pivot*k});
        g.bounds={std::max(std::abs(glo.x),std::abs(ghi.x)),ghi.y,std::max(std::abs(glo.z),std::abs(ghi.z))};
        const auto& b=parts[0];const Vec e=SdfExtent(b);g.core={Vec{0,b.center.y*k,0},std::clamp(std::min({e.x,e.y,e.z})*k*.9f,.35f,1.6f)};
        // (F4) 材料の物を浮かべる所: 本体の物は体の真ん中。ほかの物は、その物が持ってきた性質の部品 (左右に 2 つある部品は右) の真ん中。
        // 部品を持たない物 (性質の無い物) は、体の中の上の前・下の後ろに小さく
        const float bodyFit=std::min({e.x,e.y,e.z});g.anchors=w.filled;
        for(int i=0;i<w.filled;++i){const int owner=i+1;const SdfPart* own=nullptr;if(i>0)for(size_t p=1;p<parts.size();++p)if(parts[p].part==owner){own=&parts[p];break;}
            if(i==0){g.anchor[0]=b.center*k;g.anchorFit[0]=bodyFit*k*.7f;}
            else if(own){const Vec oe=SdfExtent(*own);g.anchor[size_t(i)]=own->center*k;g.anchorFit[size_t(i)]=std::min({oe.x,oe.y,oe.z})*k*.85f;}
            else{const Vec off=i==1?Vec{0,e.y*.4f,e.z*.35f}:Vec{0,-e.y*.35f,-e.z*.35f};g.anchor[size_t(i)]=(b.center+off)*k;g.anchorFit[size_t(i)]=bodyFit*k*.4f;}}
        grids.push_back(std::move(g));return WeaponShapeFirst+int(grids.size())-1;}
    // 部品の大きさ (一番長い向きの半分): 本体 0.5・前 0.5 x CraftFrontScale・左右 0.5 x CraftSideScale
    std::vector<SdfPart> parts;
    for(int i=0;i<w.filled;++i){SdfPart s=CraftItemPart(w.ids[size_t(i)]);const float size=.5f*CraftBaseSize*(i==0?1.f:i==1?CraftFrontScale:CraftSideScale);
        s.half=s.half*size;s.round*=size;s.part=i+1;s.mirror=i==2;
        // 向き: 本体と左右の部品は立てる (一番長い向きを上、2 番目を左右、一番短い向きを前後)。ゲームのカメラは後ろの少し上から見るので、
        // 寝かせると平たい丸にしか見えない。前の部品は一番長い向きを前に向ける (鼻先・砲身)
        if(i!=1)s.axes={Vec{1,0,0},Vec{0,0,1},Vec{0,1,0}};
        parts.push_back(s);}
    // 部品の、ワールドの向きごとの半分の大きさ (向きは軸の入れかえだけなので、そのまま足せる)
    auto extent=[](const SdfPart& s){Vec e{};for(int k=0;k<3;++k){const Vec a=s.axes[size_t(k)];const float h=k==0?s.half.x:k==1?s.half.y:s.half.z;e=e+Vec{std::abs(a.x)*h,std::abs(a.y)*h,std::abs(a.z)*h};}return e;};
    // 置き方: 本体は真ん中で床に乗せる。前の部品は本体の前に少し重ね、本体の真ん中より上。左右の部品は本体の横に少し重ね、少し後ろ
    auto layout=[&]{const Vec b=extent(parts[0]);parts[0].center={0,b.y,0};
        if(parts.size()>1){auto& f=parts[1];const Vec e=extent(f);f.center={0,std::max(e.y,b.y*(1+CraftFrontLift)),b.z+e.z*(1-2*CraftOverlap)};}
        if(parts.size()>2){auto& s=parts[2];const Vec e=extent(s);s.center={b.x+e.x*(1-2*CraftOverlap),std::max(e.y,b.y*CraftSideHeight*2),-b.z*CraftSideBack};}};
    layout();
    WeaponShapeGrid g;float k=1;
    for(int pass=0;pass<4;++pass){
        // 範囲 (鏡の部品も入れる)
        Vec lo{1e9f,0,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& s:parts){const float r=std::max({s.half.x,s.half.y,s.half.z})+CraftBlend;
            for(float m:{1.f,-1.f}){if(m<0&&!s.mirror)continue;lo={std::min(lo.x,s.center.x*m-r),std::min(lo.y,s.center.y-r),std::min(lo.z,s.center.z-r)};hi={std::max(hi.x,s.center.x*m+r),std::max(hi.y,s.center.y+r),std::max(hi.z,s.center.z+r)};}}
        lo.y=std::max(lo.y,0.f);
        const Vec size=hi-lo;g=WeaponShapeGrid{};g.key=key;g.cell=std::max({size.x,size.y,size.z})/float(CraftShapeCells);g.lo=lo;
        g.nx=int(std::ceil(size.x/g.cell))+1;g.ny=int(std::ceil(size.y/g.cell))+1;g.nz=int(std::ceil(size.z/g.cell))+1;g.fill.assign(size_t(g.nx*g.ny*g.nz),0);g.part.assign(g.fill.size(),0);
        int count=0;
        for(int z=0;z<g.nz;++z)for(int y=0;y<g.ny;++y)for(int x=0;x<g.nx;++x){const Vec p=g.lo+Vec{float(x)+.5f,float(y)+.5f,float(z)+.5f}*g.cell;int owner=0;
            if(SdfShapeDistance(parts,p,CraftBlend,&owner)<0){const size_t i=size_t((z*g.ny+y)*g.nx+x);g.fill[i]=1;g.part[i]=uint8_t(owner);++count;}}
        // 体積をそろえる倍率 (長い形は長さ 9.5 まで)
        const float volume=std::max(1e-4f,float(count)*g.cell*g.cell*g.cell);k=std::min(std::cbrt(CraftShapeVolume/volume),9.5f/std::max({size.x,size.y,size.z,1e-3f}));
        // 細すぎる部品を太らせて、もう一度 (太らせる所がなければ終わり)
        const float floor=CraftMinThick*.5f/k;bool grew=false;
        for(auto& s:parts){if(s.half.x<floor){s.half.x=floor;grew=true;}if(s.half.y<floor){s.half.y=floor;grew=true;}if(s.half.z<floor){s.half.z=floor;grew=true;}s.round=std::max(s.round,std::min(s.half.y,s.half.x)*.6f);}
        if(!grew)break;layout();}
    g.lo=g.lo*k;g.cell*=k;
    const Vec glo=g.lo,ghi=g.lo+Vec{float(g.nx),float(g.ny),float(g.nz)}*g.cell;
    g.bounds={std::max(std::abs(glo.x),std::abs(ghi.x)),ghi.y,std::max(std::abs(glo.z),std::abs(ghi.z))};
    const auto& b=parts[0];g.core={Vec{0,b.center.y*k,0},std::clamp(std::min({b.half.x,b.half.y,b.half.z})*k*.9f,.35f,1.6f)};
    grids.push_back(std::move(g));
    return WeaponShapeFirst+int(grids.size())-1;
}

// ---- 作る画面 ----
namespace {
// 材料の升の並び (設計座標)
struct CraftGrid {int cols=8;float cell=84,x=56,y=176;};
CraftGrid CraftGridFor(int count){CraftGrid g;g.cols=count<=24?6:count<=40?8:10;const int rows=std::max(1,(count+g.cols-1)/g.cols);
    g.cell=std::min({84.f,668.f/float(g.cols),360.f/float(rows)});return g;}
UI::Rect CraftCellRect(const CraftGrid& g,int i){return {g.x+float(i%g.cols)*g.cell+3,g.y+float(i/g.cols)*g.cell+3,g.cell-6,g.cell-6};}
// 右の武器の札 (1 枚)・枠・形の見本・強化の行・ボタン
constexpr UI::Rect kCard{770,130,470,500};
UI::Rect CraftSocket(int part){return {kCard.x+16+float(part)*92,kCard.y+52,78,78};}
constexpr UI::Rect kPreview{kCard.x+290,kCard.y+46,164,128};
UI::Rect CraftBoostRow(int boost){return {kCard.x+14,kCard.y+352+float(boost)*46,442,40};}
constexpr UI::Rect kAutoButton{770,648,180,52},kGoButton{966,648,274,52};
// 性質の色 (札の性質の印)
Engine::Vector4 TraitColor(int t){static constexpr Engine::Vector4 c[]={{.3f,.6f,.95f,1},{.45f,.75f,.4f,1},{.95f,.4f,.35f,1},{.55f,.4f,.85f,1},{.45f,.42f,.4f,1},{.95f,.6f,.2f,1},{.9f,.25f,.2f,1},{.85f,.65f,.3f,1},
    {.95f,.5f,.75f,1},{.5f,.5f,.62f,1},{.95f,.35f,.15f,1},{.35f,.7f,.75f,1},{.2f,.75f,.95f,1},{.3f,.55f,.95f,1},{.85f,.7f,.35f,1},{.75f,.85f,.95f,1},{.6f,.7f,1,1},{.95f,.8f,.25f,1}};return c[std::clamp(t,0,FuseTraitCount-1)];}
}

// 材料 1 つの点 (袋の物の大きさの平均)
template<class Bag> static int CraftBagValue(const Bag& bag,float unit){float r=0;for(const auto& it:bag.items)r+=it.radius;return CraftValue(bag.items.empty()?0:r/float(bag.items.size()),unit);}

void ChronoSystem::OpenWeaponCraft(){
    auto& b=battle_;auto& c=b.craft;c={};
    for(const auto& bag:b.startBag)if(!bag.items.empty()){c.bags.push_back(bag);auto& copy=c.bags.back();
        std::stable_sort(copy.items.begin(),copy.items.end(),[](const BattleItem& x,const BattleItem& y){return x.radius>y.radius;});}
    // (多く集めた物を前に)
    std::stable_sort(c.bags.begin(),c.bags.end(),[](const BattleBag& x,const BattleBag& y){return x.items.size()>y.items.size();});
    c.used.assign(c.bags.size(),0);
    b.phase=BattleStep::Craft;CraftScreenOpen()=true;
    // 確認用: 自動で遊ぶときは、おまかせで埋めて --craft-hold= 秒だけ見せてから決める (無ければすぐ決める)。
    // --craft-items=id,id,id で材料の 3 つを決め、--craft-boost=弾数,威力,連射 で強化の点を足せる
    if(HasArg(L"--devour-battle-auto")||b.matrix.active||b.impactCheck){AutoWeaponCraft();c.hold=ArgFloat(L"--craft-hold=",0);if(c.hold<=0)FinishWeaponCraft();}
}

// 入れた材料から武器を作る (形はまだ作らない)
ChronoSystem::BattleWeapon ChronoSystem::CraftedWeapon()const{
    const auto& b=battle_;const auto& c=b.craft;BattleWeapon w;
    for(const auto& pick:c.picks){if(pick.bag<0||pick.bag>=int(c.bags.size()))continue;const auto& bag=c.bags[size_t(pick.bag)];
        if(pick.boost<0){if(w.filled>=CraftParts)continue;const size_t i=size_t(w.filled++);w.ids[i]=bag.id;w.actions[i]=bag.action;w.items[i]=bag.items.front();}
        else w.points[size_t(pick.boost)]+=CraftBagValue(bag,b.unit);}
    // 確認用: --craft-boost=弾数,威力,連射 の点を足す
    if(const wchar_t* at=wcsstr(GetCommandLineW(),L"--craft-boost=")){at+=14;for(int k=0;k<CraftBoostCount&&*at;++k){w.points[size_t(k)]+=_wtoi(at);while(*at&&*at!=L','&&*at!=L' ')++at;if(*at==L',')++at;}}
    w.lv=FuseLevelsOf(w.actions,w.filled);w.name=w.filled>0?FuseWeaponName(w.lv,NameOf(w.ids[0])):std::string();
    return w;
}

// おまかせ: 多く集めた物を本体にして、性質の違う物を 2 つ混ぜる。残りは 3 つの強化へ順に配る
void ChronoSystem::AutoWeaponCraft(){
    auto& c=battle_.craft;c.picks.clear();std::fill(c.used.begin(),c.used.end(),0);
    const int n=int(c.bags.size());if(n==0)return;
    auto take=[&](int bag,int boost){if(bag<0||bag>=n||c.used[size_t(bag)]>=int(c.bags[size_t(bag)].items.size()))return false;++c.used[size_t(bag)];c.picks.push_back({bag,boost});return true;};
    // 確認用: --craft-items=id,id,id
    if(const wchar_t* at=wcsstr(GetCommandLineW(),L"--craft-items=")){at+=14;std::string list;while(*at&&*at!=L' '&&*at!=L'"')list+=char(*at++);
        size_t from=0;while(from<=list.size()){const size_t comma=list.find(',',from);const std::string id=list.substr(from,comma==std::string::npos?std::string::npos:comma-from);
            for(int i=0;i<n;++i)if(c.bags[size_t(i)].id==id){take(i,-1);break;}if(comma==std::string::npos)break;from=comma+1;}}
    else{std::vector<BattleAction> seen;
        for(int i=0;i<n&&int(c.picks.size())<CraftParts;++i){const auto traits=FuseTraitsOf(c.bags[size_t(i)].action);const bool plain=traits[0]==FuseTrait::Count;
            if((plain&&!c.picks.empty())||std::find(seen.begin(),seen.end(),c.bags[size_t(i)].action)!=seen.end())continue;seen.push_back(c.bags[size_t(i)].action);take(i,-1);}
        for(int i=0;i<n&&int(c.picks.size())<CraftParts;++i)take(i,-1);}
    int turn=0;for(int bag=0;bag<n;++bag)while(c.used[size_t(bag)]<int(c.bags[size_t(bag)].items.size())){take(bag,turn%CraftBoostCount);++turn;}
}

// 決める: 入れなかった残りを 3 つの強化へ均等に配って、武器と形を作り、始まりの演出へ
void ChronoSystem::FinishWeaponCraft(){
    auto& b=battle_;auto& c=b.craft;
    // 本体が空なら、一番多く集めた物を入れる
    bool hasBase=false;for(const auto& p:c.picks)if(p.boost<0)hasBase=true;
    if(!hasBase)for(int bag=0;bag<int(c.bags.size());++bag)if(c.used[size_t(bag)]<int(c.bags[size_t(bag)].items.size())){++c.used[size_t(bag)];c.picks.insert(c.picks.begin(),{bag,-1});break;}
    int turn=0;for(int bag=0;bag<int(c.bags.size());++bag)while(c.used[size_t(bag)]<int(c.bags[size_t(bag)].items.size())){++c.used[size_t(bag)];c.picks.push_back({bag,turn%CraftBoostCount});++turn;}
    b.weapon=CraftedWeapon();b.weapon.shape=BuildWeaponShape(b.weapon);
    // 確認用 (--craft-shape-dump): 武器の形を横・正面・上から見た影絵にして tests/out/craft-shape.ppm に書く
    if(HasArg(L"--craft-shape-dump"))if(const auto* g=WeaponShapeOf(b.weapon.shape)){std::filesystem::create_directories("tests/out");const int px=5,W=(g->nz+g->nx+g->nx+4)*px,H=std::max(g->ny,g->nz)*px+2*px;
        std::vector<uint8_t> img(size_t(W*H*3),255);auto dot=[&](int x,int y,uint8_t shade){for(int a=0;a<px;++a)for(int k=0;k<px;++k){const int X=x*px+a,Y=y*px+k;if(X<0||Y<0||X>=W||Y>=H)continue;uint8_t* q=&img[size_t((Y*W+X)*3)];q[0]=std::min(q[0],shade);q[1]=std::min(q[1],uint8_t(shade/2+60));q[2]=std::min(q[2],uint8_t(shade/3+40));}};
        for(int z=0;z<g->nz;++z)for(int y=0;y<g->ny;++y)for(int x=0;x<g->nx;++x)if(g->fill[size_t((z*g->ny+y)*g->nx+x)]){
            dot(z,g->ny-1-y,uint8_t(60+150*x/std::max(1,g->nx)));dot(g->nz+1+x,g->ny-1-y,uint8_t(60+150*z/std::max(1,g->nz)));dot(g->nz+g->nx+3+x,g->nz-1-z,uint8_t(60+150*y/std::max(1,g->ny)));}
        std::ofstream out("tests/out/craft-shape.ppm",std::ios::binary);out<<"P6"<<'\n'<<W<<" "<<H<<'\n'<<"255"<<'\n';out.write(reinterpret_cast<const char*>(img.data()),std::streamsize(img.size()));}
    b.crafted=true;b.selected=0;b.phase=BattleStep::Intro;b.intro=0;CraftScreenOpen()=false;
    PlayDevourSound(devourSounds_.rankUp,.6f);
}

void ChronoSystem::UpdateWeaponCraft(GameContext& ctx,float dt){
    auto& b=battle_;auto& c=b.craft;c.age+=dt;
    // 確認用 (--craft-shape-sheet): 流体が落ち着いたら形を測って書き、終わる
    {static bool written=false;if(!written&&c.age>1.5f&&HasArg(L"--craft-shape-sheet")){written=true;WriteWeaponShapeSheet();PostQuitMessage(0);}}
    if(c.hold>=0){if(c.age>=c.hold)FinishWeaponCraft();return;}
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    const int n=int(c.bags.size());const CraftGrid grid=CraftGridFor(n);
    auto parts=[&]{int k=0;for(const auto& p:c.picks)if(p.boost<0)++k;return k;};
    auto add=[&](int bag){if(bag<0||bag>=n)return;if(c.used[size_t(bag)]>=int(c.bags[size_t(bag)].items.size())){PlayDevourSound(devourSounds_.hit,.3f,.6f);return;}
        const int filled=parts();++c.used[size_t(bag)];c.picks.push_back({bag,filled<CraftParts?-1:c.boost});PlayDevourSound(devourSounds_.note,.5f,filled<CraftParts?1.f+.2f*float(filled):1.4f+.1f*float(c.boost));};
    auto remove=[&](size_t at){if(at>=c.picks.size())return;--c.used[size_t(c.picks[at].bag)];c.picks.erase(c.picks.begin()+std::ptrdiff_t(at));PlayDevourSound(devourSounds_.pounce,.35f,.8f);};
    // 升を動かす (矢印・十字キー)。マウスを乗せた升も選ぶ
    if(n>0){const int dx=(UI::Pressed(DIK_RIGHT)||UI::PadPressed(XINPUT_GAMEPAD_DPAD_RIGHT|Engine::Input::kPadLsRight)?1:0)-(UI::Pressed(DIK_LEFT)||UI::PadPressed(XINPUT_GAMEPAD_DPAD_LEFT|Engine::Input::kPadLsLeft)?1:0);
        const int dy=(UI::Pressed(DIK_DOWN)||UI::PadPressed(XINPUT_GAMEPAD_DPAD_DOWN|Engine::Input::kPadLsDown)?1:0)-(UI::Pressed(DIK_UP)||UI::PadPressed(XINPUT_GAMEPAD_DPAD_UP|Engine::Input::kPadLsUp)?1:0);
        if(dx||dy)c.cursor=std::clamp(c.cursor+dx+dy*grid.cols,0,n-1);
        for(int i=0;i<n;++i){const auto r=CraftCellRect(grid,i);if(!UI::UsingPad()&&ui.Hover(r))c.cursor=i;if(ui.MouseClick(r))add(i);}
        if(UI::Pressed(DIK_RETURN)||UI::PadPressed(XINPUT_GAMEPAD_A))add(c.cursor);}
    // 強化を選ぶ (1〜3・X・行のクリック)。枠をクリックするとその材料を外す
    {const uint8_t keys[]={DIK_1,DIK_2,DIK_3};for(int k=0;k<CraftBoostCount;++k)if(UI::Pressed(keys[k])||ui.MouseClick(CraftBoostRow(k)))c.boost=k;}
    if(UI::PadPressed(XINPUT_GAMEPAD_X)||UI::Pressed(DIK_TAB))c.boost=(c.boost+1)%CraftBoostCount;
    for(int part=0;part<CraftParts;++part)if(ui.MouseClick(CraftSocket(part))){int k=-1;for(size_t i=0;i<c.picks.size();++i)if(c.picks[i].boost<0&&++k==part){remove(i);break;}}
    auto* input=Engine::Input::GetInstance();
    if(UI::Pressed(DIK_BACK)||UI::PadPressed(XINPUT_GAMEPAD_B)||(input&&!UI::UsingPad()&&input->IsMouseTrigger(1))){if(!c.picks.empty())remove(c.picks.size()-1);}
    if(UI::Pressed(DIK_R)||UI::PadPressed(XINPUT_GAMEPAD_Y)||ui.MouseClick(kAutoButton)){AutoWeaponCraft();PlayDevourSound(devourSounds_.rankUp,.4f,1.3f);}
    if(UI::Pressed(DIK_SPACE)||UI::PadPressed(XINPUT_GAMEPAD_START)||ui.MouseClick(kGoButton))FinishWeaponCraft();
}

void ChronoSystem::DrawWeaponCraft(GameContext& ctx){
    using namespace UI::Scale;
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);auto& b=battle_;const auto& c=b.craft;
    const int n=int(c.bags.size());const CraftGrid grid=CraftGridFor(n);const float t=c.age;
    ui.Fill({0,0,1280,720},UI::WithAlpha(Yellow,.95f));
    ui.Big("変形武器をつくろう",40,10,64,Black,JpBig,JpBigBase);
    ui.Text("3 つの物を混ぜて、1 つの武器に。性質が全部まざる。そのあと入れた物は強化の点になる (大きい物ほど点が高い)",44,90,19,Black,Jp);
    // ---- 左: 集めた物 ----
    Plate(ui,{40,130,710,436},Cream);ui.Text("集めた物",56,136,22,Black,Jp);
    for(int i=0;i<n;++i){const auto& bag=c.bags[size_t(i)];const auto r=CraftCellRect(grid,i);const int left=int(bag.items.size())-c.used[size_t(i)];
        const float pop=Overshoot((t-float(i)*.015f)/.25f);if(pop<=0)continue;const UI::Rect art{r.x+r.w*.5f*(1-pop)+4,r.y+r.h*.5f*(1-pop)+4,r.w*pop-8,r.h*pop-8};
        ui.Rounded(r,left>0?White:UI::WithAlpha(Black,.12f),10);
        const std::string image=ImageOf(bag.id);if(!image.empty())ui.Photo(image,art,left>0?Engine::Vector4{1,1,1,1}:Engine::Vector4{1,1,1,.35f});
        // 点 (大きさ) と残りの数
        const int value=CraftBagValue(bag,b.unit);for(int k=0;k<value;++k)ui.Dot(r.x+9+float(k)*11,r.y+9,4.5f,Red);
        const std::string count="x"+std::to_string(left);const float cw=ui.Width(count,18,Num)+8;ui.Fill({r.x+r.w-cw-2,r.y+r.h-22,cw,20},left>0?Black:UI::WithAlpha(Black,.4f));ui.Text(count,r.x+r.w-cw+2,r.y+r.h-25,18,Yellow,Num);
        if(i==c.cursor){ui.RoundedRing({r.x-3,r.y-3,r.w+6,r.h+6},Red,12);ui.RoundedRing({r.x-1,r.y-1,r.w+2,r.h+2},Red,11);}}
    const BattleWeapon weapon=CraftedWeapon();int filled=weapon.filled;
    // ---- 左下: 選んでいる物の性質と、入れるとどうなるか・操作 ----
    {Plate(ui,{40,584,710,116},Cream);
        if(n>0){const auto& bag=c.bags[size_t(std::clamp(c.cursor,0,n-1))];const auto traits=FuseTraitsOf(bag.action);
            std::string text;for(FuseTrait tr:traits)if(tr!=FuseTrait::Count)text+=(text.empty()?"":"・")+std::string(FuseTraitName(tr));if(text.empty())text="なし (大きさだけ)";
            ui.Text(NameOf(bag.id),56,590,28,Black,Jp);ui.Text("性質: "+text,56+ui.Width(NameOf(bag.id),28,Jp)+18,600,20,Black,Jp);
            const std::string where=filled<CraftParts?std::string("→ 「")+CraftPartRole(filled)+"」として混ぜる":std::string("→ 「")+CraftBoostName(c.boost)+"」に +"+std::to_string(CraftBagValue(bag,b.unit))+"点";
            ui.Text(where,56,630,20,Red,Jp);}
        const std::string help=UI::UsingPad()?"A 入れる   B 戻す   X 強化を選ぶ   Y おまかせ   START 決定":"クリック・Enter 入れる   右クリック・Backspace 戻す   1〜3・Tab 強化を選ぶ   R おまかせ   Space 決定";
        ui.Text(help,56,664,17,Black,Jp);}
    // ---- 右: 武器の札 ----
    Plate(ui,kCard,White,4,7);ui.Fill({kCard.x,kCard.y,8,kCard.h},Red);
    ui.Text(filled>0?weapon.name:std::string("（材料を 3 つ入れてね）"),kCard.x+18,kCard.y+10,filled>0&&ui.Width(weapon.name,22,Jp)>430?18.f:22.f,Black,Jp);
    for(int part=0;part<CraftParts;++part){const auto s=CraftSocket(part);const bool has=part<filled;
        ui.Rounded(s,has?White:UI::WithAlpha(Black,.1f),10);ui.RoundedRing(s,part==0?Red:Black,10);
        if(has){const std::string image=ImageOf(weapon.ids[size_t(part)]);if(!image.empty())ui.Photo(image,{s.x+5,s.y+5,s.w-10,s.h-10});}
        else ui.Text("?",s.x+s.w*.5f-8,s.y+14,36,UI::WithAlpha(Black,.3f),Num);
        ui.Text(CraftPartRole(part),s.x+4,s.y+s.h+2,14,Black,Jp);
        if(part+1<CraftParts)ui.Text("+",s.x+s.w+3,s.y+22,24,Black,Num);}
    // 形の見本: 体がなる形を横から見た影絵 (3 つの物の組み合わせ)
    {ui.Rounded(kPreview,UI::WithAlpha(Black,.08f),10);ui.Text("変形の形",kPreview.x+8,kPreview.y+4,14,Black,Jp);
        const int shape=filled>0?BuildWeaponShape(weapon):0;
        if(const auto* g=WeaponShapeOf(shape)){static std::map<int,std::vector<uint8_t>> sides;auto& side=sides[shape];
            if(side.empty()){side.assign(size_t(g->nz*g->ny),0);for(int z=0;z<g->nz;++z)for(int y=0;y<g->ny;++y)for(int x=0;x<g->nx;++x)if(g->fill[size_t((z*g->ny+y)*g->nx+x)]){side[size_t(z*g->ny+y)]=1;break;}}
            const float cell=std::min((kPreview.w-16)/float(g->nz),(kPreview.h-28)/float(g->ny)),ox=kPreview.x+(kPreview.w-cell*float(g->nz))*.5f,oy=kPreview.y+kPreview.h-8;
            for(int z=0;z<g->nz;++z)for(int y=0;y<g->ny;++y)if(side[size_t(z*g->ny+y)])ui.Fill({ox+float(z)*cell,oy-float(y+1)*cell,cell+.5f,cell+.5f},Engine::Vector4{.25f,.55f,.85f,1});
            // (F4) 材料の物が体の中のどこに浮かぶか: その所に物の絵を重ねる
            // (本体の物が隠れないよう、本体を最後に重ねる)
            for(int i=std::min(g->anchors,filled);i-->0;){const std::string image=ImageOf(weapon.ids[size_t(i)]);if(image.empty())continue;
                const float size=std::clamp(g->anchorFit[size_t(i)]*2/g->cell*cell,18.f,52.f),px=ox+(g->anchor[size_t(i)].z-g->lo.z)/g->cell*cell,py=oy-(g->anchor[size_t(i)].y-g->lo.y)/g->cell*cell;
                ui.Photo(image,{px-size*.5f,py-size*.5f,size,size});}}}
    // 性質の印 (レベルの数だけ★)
    {float x=kCard.x+16,y=kCard.y+176;ui.Text("性質",x,y,18,Black,Jp);y+=28;
        bool any=false;for(int tr=0;tr<FuseTraitCount;++tr){const int level=weapon.lv[size_t(tr)];if(level<=0)continue;any=true;
            const std::string label=std::string(FuseTraitName(FuseTrait(tr)))+(level>1?" x"+std::to_string(level):"");const float w=ui.Width(label,17,Jp)+18;
            if(x+w>kCard.x+kCard.w-14){x=kCard.x+16;y+=34;}ui.Rounded({x,y,w,28},TraitColor(tr),8);ui.Text(label,x+9,y+2,17,White,Jp);x+=w+8;}
        if(!any)ui.Text(filled>0?"性質なし (ふつうに投げる)":"材料を入れると、性質がここに並ぶ",kCard.x+16,y,17,UI::WithAlpha(Black,.5f),Jp);
        // どう動くか (性質から作る説明)。長ければ 2 行に分ける
        if(filled>0){auto L=[&](FuseTrait tr){return weapon.lv[size_t(tr)];};std::vector<std::string> words;
            if(L(FuseTrait::Roll))words.push_back("床を転がり");else if(L(FuseTrait::Homing)||L(FuseTrait::Float))words.push_back("宙を飛び");else words.push_back("放物線で飛び");
            if(L(FuseTrait::Homing))words.push_back("相手を追いかけ");if(L(FuseTrait::Speed))words.push_back("とても速く");if(L(FuseTrait::Heavy))words.push_back("大きく重く");
            if(L(FuseTrait::Pierce))words.push_back(std::to_string(FusePierceHits*L(FuseTrait::Pierce)+1)+"体まで突き抜け");if(L(FuseTrait::Bounce))words.push_back(std::to_string(FuseBounces*L(FuseTrait::Bounce))+"回跳ね");
            if(L(FuseTrait::Chain))words.push_back("次の相手へ跳び移り");if(L(FuseTrait::Spin))words.push_back("回ってまとめて当て");if(L(FuseTrait::Magnet))words.push_back("まわりを吸い寄せ");
            if(L(FuseTrait::Return))words.push_back("戻ってきて");if(L(FuseTrait::Paint))words.push_back("通った跡を塗り");if(L(FuseTrait::Stun))words.push_back("当てた相手を止め");
            if(L(FuseTrait::Weaken))words.push_back("弱らせ");if(L(FuseTrait::Burn))words.push_back("やけどさせ");if(L(FuseTrait::Split))words.push_back("当たると分かれ");
            if(L(FuseTrait::Explode))words.push_back("爆発する");if(L(FuseTrait::Wall))words.push_back("止まった所に壁を建てる");
            std::string line,second;for(const auto& w:words){std::string& to=ui.Width(line+"、"+w,17,Jp)>430&&!line.empty()?second:line;to+=(to.empty()?"":"、")+w;}
            ui.Text(line,kCard.x+16,y+46,17,Black,Jp);if(!second.empty())ui.Text(second,kCard.x+16,y+72,17,Black,Jp);}}
    // 強化 (弾数・威力・連射): 点と今の値。バーは伸び切りまでの割合
    for(int k=0;k<CraftBoostCount;++k){const auto row=CraftBoostRow(k);const int points=weapon.points[size_t(k)];const bool active=k==c.boost;
        ui.Rounded(row,active?UI::WithAlpha(Red,.16f):UI::WithAlpha(Black,.06f),8);if(active)ui.RoundedRing(row,Red,8);
        ui.Text(CraftBoostName(k),row.x+10,row.y+8,20,Black,Jp);
        float share=0;char value[48];const char* unit="";
        if(k==0){const int shots=CraftShots(points);share=float(shots-1)/float(CraftShotsMax-1);snprintf(value,sizeof(value),"%d",shots);unit="発";}
        else if(k==1){const float p=CraftPower(points);share=(p-1)/(CraftPowerMax-1);snprintf(value,sizeof(value),"x%.1f",p);}
        else{const float iv=CraftInterval(points,weapon.lv[size_t(FuseTrait::Heavy)]);share=std::clamp((CraftIntervalBase-iv)/(CraftIntervalBase-CraftIntervalMin),0.f,1.f);snprintf(value,sizeof(value),"%.1f",1/iv);unit="発/秒";}
        const UI::Rect bar{row.x+80,row.y+13,200,14};ui.Fill(bar,UI::WithAlpha(Black,.2f));ui.Fill({bar.x,bar.y,bar.w*std::clamp(share,0.f,1.f),bar.h},k==0?Engine::Vector4{.24f,.55f,.86f,1}:k==1?Engine::Vector4{.94f,.47f,.16f,1}:Engine::Vector4{.35f,.71f,.31f,1});
        ui.Text(value,bar.x+bar.w+12,row.y+2,26,Black,Num);ui.Text(unit,bar.x+bar.w+14+ui.Width(value,26,Num),row.y+12,15,Black,Jp);
        ui.Text(std::to_string(points)+"点",row.x+row.w-44,row.y+11,15,UI::WithAlpha(Black,.6f),Jp);}
    // ---- 右下: おまかせ・決定 ----
    Plate(ui,kAutoButton,Cream);ui.Text("おまかせ",kAutoButton.x+40,kAutoButton.y+10,24,Black,Jp);
    const float pulse=.5f+.5f*std::sin(t*5);Plate(ui,kGoButton,Red,3,5+2*pulse);ui.Text("これで挑戦！",kGoButton.x+56,kGoButton.y+9,26,White,Jp);
}

// ---- チャレンジで使う ----
// 撃つ: 弾数の分だけ扇に撃つ (回数の制限はない。連射の間隔ごと)。体は武器の形になる
void ChronoSystem::UseBattleWeapon(GameContext&){
    auto& b=battle_;auto& w=b.weapon;if(w.filled<=0||w.cool>0)return;
    auto& body=life_.bodies[0];const float unit=b.unit,R=DevourBodyRadius(body.mass);auto L=[&](FuseTrait t){return w.lv[size_t(t)];};
    const int shots=CraftShots(w.points[0]);const float power=CraftPower(w.points[1]),interval=CraftInterval(w.points[2],L(FuseTrait::Heavy));w.cool=interval;
    // 体液: 1 回分 (全部の弾をあわせて)。弾数・威力・塗りの性質で多く、連射が速いほど 1 回分は少なく
    b.powerScale=std::clamp(power*std::sqrt(float(shots))*(1+.5f*float(L(FuseTrait::Paint)))*std::sqrt(interval/CraftIntervalBase),.25f,6.f);BeginTerritoryUse(BattleAction::Throw,false);b.powerScale=1;
    const bool roll=L(FuseTrait::Roll)>0,flying=!roll&&(L(FuseTrait::Homing)>0||L(FuseTrait::Float)>0||L(FuseTrait::Return)>0);
    const float speed=unit*FuseSpeed*(1+FuseSpeedStep*float(L(FuseTrait::Speed)))/(1+FuseHeavySlow*float(L(FuseTrait::Heavy)))*(L(FuseTrait::Float)>0?FuseFloatSlow:1.f);
    const float radius=unit*FuseRadius*BattleItemScale(unit,w.items[0].radius)*std::sqrt(power)*(1+FuseHeavyGrow*float(L(FuseTrait::Heavy)));
    const int count=std::max(0,std::min(shots,FuseShotLimit-int(b.shots.size())));const float spread=std::min(CraftSpreadMax,CraftSpreadStep*float(shots-1));
    for(int k=0;k<count;++k){const float a=yaw_+(count>1?-spread*.5f+spread*float(k)/float(count-1):0.f);const V dir=Forward(a,0);
        BattleShot s;s.fused=true;s.lv=w.lv;s.mode=BattleAction::Pierce;s.by=BattleAction::Throw;s.visual=w.items[0].visual;s.radius=radius;s.size=radius*2.2f;s.power=power;
        s.at=body.at+dir*(R*.9f)+V{0,R*.45f,0};s.velocity=dir*speed+V{0,roll||flying?0.f:speed*.28f,0};
        if(roll){s.rolling=true;s.at.y=InkGround(s.at)+radius;}
        s.damage=BattleThrowDamage*ChallengePowerNow()*power*(1+FuseDamageHeavy*float(L(FuseTrait::Heavy)));
        s.hits=1+FusePierceHits*L(FuseTrait::Pierce)+L(FuseTrait::Heavy);s.bounces=FuseBounces*L(FuseTrait::Bounce);s.chain=FuseChainHops*L(FuseTrait::Chain);
        s.life=FuseLife+FuseFloatLife*float(L(FuseTrait::Float))+FuseBounceLife*float(L(FuseTrait::Bounce))+FuseReturnLife*float(L(FuseTrait::Return))+(roll?1.f:0.f);
        s.push=3*(1+float(L(FuseTrait::Heavy)));b.shots.push_back(s);}
    // (撃ち続けている間は形を保つ: 次の弾までの間 + CraftFormHold。間に液体へ戻ると、そのたびに平たく広がって見える)
    b.weaponShape=w.shape;b.weaponForm=std::max(CraftFormTime,interval+CraftFormHold);body.forward=Forward(yaw_,0);++b.spent;
    // (F5) 反動: 1 発ごとに前後に縮む。速く連射するほど 1 発の反動は小さい (震えすぎないように)
    b.weaponKick=std::max(b.weaponKick,CraftKick*std::clamp(interval/.25f,.25f,1.f)*(1+.15f*float(L(FuseTrait::Heavy))));
    PlayDevourSound(devourSounds_.pounce,std::min(.7f,.35f+.03f*float(shots)),1.3f/(1+.2f*float(L(FuseTrait::Heavy)))*(interval<.12f?1.25f:1.f));
}

void ChronoSystem::TickBattleWeapons(GameContext&,float dt){
    auto& b=battle_;b.weapon.cool=std::max(0.f,b.weapon.cool-dt);b.weaponForm=std::max(0.f,b.weaponForm-dt);if(b.weaponForm<=0){b.weaponShape=0;b.weaponAge=0;}else b.weaponAge+=dt;
    // (F5) 姿勢: 翼ははばたき、羽根は回り、足と分身の玉は弾み、炎はゆらぐ。撃つたびに前後に縮む反動。
    // 形になるときは小さい所から少し大きくなりすぎて戻り (ぽんと出てくる)、形が終わるときは少し縮む
    {b.weaponPosePrevious=b.weaponPose;auto& pose=b.weaponPose;const float t=b.weaponAge;
        pose.roll=CraftFlap*std::sin(t*8);pose.spin+=dt*20;pose.tip=std::sin(t*7);pose.pitch=std::sin(t*23)*.7f+std::sin(t*13)*.3f;
        b.weaponKick*=std::exp(-14*dt);pose.squash=b.weaponKick;
        const float in=std::clamp(t/CraftPopTime,0.f,1.f),out=std::clamp(b.weaponForm/.12f,0.f,1.f);
        pose.scale=(in<1?.72f+.28f*in+.16f*std::sin(3.1415927f*in):1.f)*(.85f+.15f*out);
        if(b.weaponShape==0)pose={};}
}

// 爆発と分裂 (当たったとき・落ちたとき)。分裂は 1 回だけ (分かれた弾はもう分かれない)
void ChronoSystem::BurstFusedShot(GameContext& ctx,BattleShot& s,bool landed){
    auto& b=battle_;const float unit=b.unit;auto L=[&](FuseTrait t){return s.lv[size_t(t)];};
    if(L(FuseTrait::Explode)>0){const float reach=unit*(FuseExplodeReach+FuseExplodeStep*float(L(FuseTrait::Explode)-1))*std::sqrt(s.power);
        BattleImpact i;i.action=BattleAction::Throw;i.effect=ImpactEffect::Hit;i.shape=ImpactShape::Sphere;i.at=s.at;i.reach=reach;i.damage=s.damage*.8f;i.push=3;i.radial=true;EmitBattleImpact(ctx,i);
        b.fx.push_back({s.at,reach,0,.3f,{1,.6f,.25f,.6f}});LifeImpact(ctx,ImpactStomp,s.at,{0,1,0},std::min(1.f,.3f+reach/(unit*6)));}
    if(L(FuseTrait::Split)>0&&!s.split){s.split=true;const int pieces=FuseSplitPieces+L(FuseTrait::Split)-1;V flat{s.velocity.x,0,s.velocity.z};const float speed=std::max(Length(flat),unit*8);const float face=std::atan2(flat.x,flat.z);
        for(int k=0;k<pieces;++k){if(int(b.shots.size()+b.newShots.size())>=FuseShotLimit)break;BattleShot c=s;c.split=true;c.lv[size_t(FuseTrait::Split)]=0;c.radius*=FuseSplitScale;c.size*=FuseSplitScale;c.damage*=.6f;c.age=0;
            c.life=std::max(.8f,s.life*.5f);c.hits=std::max(1,s.hits);const float a=face+(float(k)/float(std::max(1,pieces-1))-.5f)*2.2f;const V d=Forward(a,0);
            c.velocity=d*speed+V{0,c.rolling?0.f:unit*(landed?5.f:2.f),0};c.at=s.at+d*(s.radius*.6f);b.newShots.push_back(c);}}
}

// 弾が終わる: 爆発・分裂・壁 (止まった所に積み木の塔。多すぎないように 6 つまで)
void ChronoSystem::EndFusedShot(GameContext& ctx,BattleShot& s){
    auto& b=battle_;if(s.age>=99)return;BurstFusedShot(ctx,s,true);
    if(s.lv[size_t(FuseTrait::Wall)]>0){int towers=0;for(const auto& pl:b.placed)if(pl.kind==BattleAction::Tower)++towers;
        if(towers<6){const float R=DevourBodyRadius(life_.bodies[0].mass);BattlePlaced t;t.kind=BattleAction::Tower;t.visual=s.visual;t.itemRadius=s.radius;t.at=s.at;t.at.y=InkGround(t.at);
            V f{s.velocity.x,0,s.velocity.z};t.dir=Length(f)>.01f?Unit(f):V{0,0,1};t.radius=R*BattleTowerHalf*.7f*float(s.lv[size_t(FuseTrait::Wall)]);t.size=std::max(R*.5f,b.unit*.45f);t.life=BattleTowerLife;t.power=s.power;b.placed.push_back(t);
            BattleImpact z;z.action=BattleAction::Tower;z.effect=ImpactEffect::Zone;z.at=t.at;z.reach=t.radius;EmitBattleImpact(ctx,z);}}
    s.age=99;
}

// 変形武器の弾: 性質ごとに動き、当たる
void ChronoSystem::TickFusedShot(GameContext& ctx,BattleShot& s,float dt){
    auto& b=battle_;const auto& body=life_.bodies[0];const float unit=b.unit,R=DevourBodyRadius(body.mass);auto L=[&](FuseTrait t){return s.lv[size_t(t)];};
    if(s.age>=99)return;
    s.age+=dt;s.hitCool=std::max(0.f,s.hitCool-dt);s.spin+=dt*(10+12*float(L(FuseTrait::Spin)));
    V flat{s.velocity.x,0,s.velocity.z};float speed=Length(flat);
    const bool flying=!s.rolling&&L(FuseTrait::Roll)==0&&(L(FuseTrait::Homing)>0||L(FuseTrait::Float)>0||s.returning);
    // 追いかける: 近い相手へ向きを曲げる (当てた相手は追わない)
    auto steer=[&](V to,float rate){to.y=0;if(speed<.01f||Length(to)<.01f)return;const float now=std::atan2(flat.x,flat.z),want=std::atan2(to.x,to.z);
        const float turn=std::clamp(std::remainder(want-now,6.2831853f),-rate*dt,rate*dt);const V d=Forward(now+turn,0);s.velocity={d.x*speed,s.velocity.y,d.z*speed};flat={s.velocity.x,0,s.velocity.z};};
    if(L(FuseTrait::Homing)>0&&!s.returning){BattleTarget target;if(NearestBattleTarget(s.at,unit*30,target,&s.hit))steer(target.at-s.at,FuseHomingTurn*float(L(FuseTrait::Homing)));}
    // 戻ってくる: 残りの半分を過ぎたら体へ戻る (戻ったら消える。行きに当てた相手にもまた当たれる)
    if(L(FuseTrait::Return)>0&&!s.returning&&s.age>s.life*.45f){s.returning=true;s.hit.clear();s.hits=std::max(s.hits,1);}
    if(s.returning){V to=body.at-s.at;to.y=0;if(Length(to)<R*1.2f){s.age=99;return;}steer(to,8);}
    // 上下: 転がる・浮く・放物線
    if(s.rolling){s.at.y=InkGround(s.at)+s.radius;s.velocity.y=0;
        if(L(FuseTrait::Roll)==0){const float k=std::exp(-ChallengeThrowRoll*dt);s.velocity.x*=k;s.velocity.z*=k;if(speed<unit*1.2f){EndFusedShot(ctx,s);return;}}}
    else if(flying){const float want=InkGround(s.at)+R*.6f+(L(FuseTrait::Float)>0?unit*1.2f+.4f*unit*std::sin(s.age*3):0.f);s.at.y+=(want-s.at.y)*std::min(1.f,dt*4);s.velocity.y=0;}
    else s.velocity.y-=unit*FuseGravity*(L(FuseTrait::Float)>0?.35f:1.f)*dt;
    s.at=s.at+s.velocity*dt;
    // 床に落ちた: 跳ねる・転がり始める (当たれる数が残っていれば)・終わる。落ちた所に体液
    if(!s.rolling&&!flying){const float floor=InkGround(s.at)+s.radius*.5f;
        if(s.at.y<floor){s.at.y=floor;
            {BattleImpact z;z.action=BattleAction::Throw;z.effect=ImpactEffect::Zone;z.at=s.at;z.reach=s.radius*1.6f;if(!b.legacy&&b.challenge==ChallengeKind::Territory)PaintTerritoryImpact(z);}
            if(s.bounces>0){--s.bounces;s.velocity.y=std::max(std::abs(s.velocity.y)*.75f,unit*5);if(L(FuseTrait::Explode)>0)BurstFusedShot(ctx,s,true);}
            else if(s.hits>0&&L(FuseTrait::Explode)==0&&L(FuseTrait::Split)==0){s.rolling=true;s.velocity.y=0;}
            else{EndFusedShot(ctx,s);return;}}}
    // 部屋 (陣取り) やステージの縁: 跳ねる・転がる弾は跳ね返る。ほかは終わる
    {const V c=b.arenaAt;bool out=false;V n{};
        if(!b.legacy&&b.challenge==ChallengeKind::Territory){const float half=ChallengeTerritoryHalf*unit-s.radius;
            if(s.at.x-c.x>half){n={-1,0,0};out=true;s.at.x=c.x+half;}else if(c.x-s.at.x>half){n={1,0,0};out=true;s.at.x=c.x-half;}
            if(s.at.z-c.z>half){n={0,0,-1};out=true;s.at.z=c.z+half;}else if(c.z-s.at.z>half){n={0,0,1};out=true;s.at.z=c.z-half;}}
        else if(b.arenaRadius>0){V o=s.at-c;o.y=0;const float len=Length(o);if(len>b.arenaRadius-s.radius&&len>.01f){n=o*(-1/len);out=true;s.at=c+o*((b.arenaRadius-s.radius)/len)+V{0,s.at.y-c.y,0};}}
        if(out){if(L(FuseTrait::Bounce)>0||s.rolling||s.returning){const float into=Dot(s.velocity,n);if(into<0){s.velocity=s.velocity-n*(2*into);s.hit.clear();}}else{EndFusedShot(ctx,s);return;}}}
    // 吸い寄せる: まわりの相手を少しずつ引き寄せる
    if(L(FuseTrait::Magnet)>0){s.pulse-=dt;if(s.pulse<=0){s.pulse=.3f;BattleImpact i;i.action=BattleAction::Throw;i.effect=ImpactEffect::Pull;i.at=s.at;i.reach=unit*FuseMagnetReach*float(L(FuseTrait::Magnet));i.push=1.5f;i.pullCap=unit*1.5f;i.bigScale=.3f;EmitBattleImpact(ctx,i);}}
    // たっぷり塗る: 通った下の床に体液の跡 (陣取り)
    if(L(FuseTrait::Paint)>0&&!b.legacy&&b.challenge==ChallengeKind::Territory){s.trail-=dt;if(s.trail<=0){s.trail=FusePaintEvery;
        BattleImpact z;z.action=BattleAction::Throw;z.effect=ImpactEffect::Zone;z.at={s.at.x,InkGround(s.at),s.at.z};z.reach=unit*.45f*float(L(FuseTrait::Paint))*std::sqrt(s.power);PaintTerritoryImpact(z);}}
    // 飛んだ跡の粒 (細い物でも道すじが分かるように)
    if(std::fmod(s.age,.04f)<dt)b.fx.push_back({s.at,s.radius*.4f,0,.22f,{.55f,.8f,1,.8f}});
    // 当たり: 回る弾は当たりが広く、何体にも当たる
    if(s.hits>0&&s.hitCool<=0){const float reach=s.radius*(1+FuseSpinGrow*float(L(FuseTrait::Spin)));
        BattleImpact i;i.action=BattleAction::Throw;i.effect=ImpactEffect::Hit;i.shape=ImpactShape::Sphere;i.at=s.at;i.reach=reach;i.flat=s.rolling;i.dir=flat;i.damage=s.damage;i.push=s.push;i.limit=L(FuseTrait::Spin)>0?0:1;
        std::vector<BattleTarget> touched;
        if(EmitBattleImpact(ctx,i,&touched,&s.hit)>0){
            for(const auto& t:touched){s.hit.push_back(t.id);
                // 止める・弱らせる・熱い: 当てた相手にだけ、続けて効き目を出す
                auto extra=[&](ImpactEffect e,float seconds,float damage,const char* label){BattleImpact x;x.action=BattleAction::Throw;x.effect=e;x.at=t.at;x.reach=t.radius+unit;x.only=t.id;x.limit=1;x.seconds=seconds;x.damage=damage;x.label=label;EmitBattleImpact(ctx,x);};
                if(L(FuseTrait::Stun)>0)extra(ImpactEffect::Stun,FuseStunTime*float(L(FuseTrait::Stun)),0,"止まった！");
                if(L(FuseTrait::Weaken)>0)extra(ImpactEffect::Weaken,BattleWeakTime,0,nullptr);
                if(L(FuseTrait::Burn)>0)extra(ImpactEffect::Burn,BattleBurnTime,BattleBurnDps*s.power*float(L(FuseTrait::Burn)),"あちっ！");}
            s.hits-=1;b.fx.push_back({s.at,reach*1.4f,0,.18f,{1,.85f,.4f,.8f}});s.hitCool=.08f;
            BurstFusedShot(ctx,s,false);
            // 跳び移る: 次に近い相手へ (いれば当たれる数も戻る)。跳ねる: 当たった相手から離れる向きへ跳ねる
            BattleTarget next;
            if(s.chain>0&&NearestBattleTarget(s.at,unit*12,next,&s.hit)){--s.chain;V to=next.at-s.at;to.y=0;s.velocity=Unit(to)*std::max(speed,unit*14)+V{0,s.velocity.y,0};s.hits=std::max(s.hits,1);PlayDevourSound(devourSounds_.note,.35f,1.9f);}
            else if(L(FuseTrait::Bounce)>0&&s.hits>0&&!touched.empty()){V away=s.at-touched.front().at;away.y=0;away=Length(away)>.01f?Unit(away):V{0,0,1};s.velocity=V{away.x*speed,flying||s.rolling?0.f:unit*6,away.z*speed};}
            if(s.hits<=0){EndFusedShot(ctx,s);return;}}}
    if(s.age>s.life)EndFusedShot(ctx,s);
}

// 下の真ん中: 武器のステッカー (本体の絵と、混ぜた物の小さな絵・連射の待ち) と名前
void ChronoSystem::DrawWeaponStickers(GameContext& ctx){
    using namespace UI::Scale;
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);const auto& b=battle_;const auto& w=b.weapon;if(w.filled<=0)return;
    const float size=84,cx=640,cy=640,rot=-4*.0174533f;
    const UI::Rect outer{cx-size*.5f,cy-size*.5f,size,size},inner{outer.x+5,outer.y+5,size-10,size-10};
    ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",{outer.x+3,outer.y+6,outer.w,outer.h},rot,{0,0,0,.45f});
    ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",outer,rot,Black);ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",inner,rot,Yellow);
    {const std::string image=ImageOf(w.ids[0]);if(!image.empty())ui.ImageRot(image,{inner.x+4,inner.y+4,inner.w-8,inner.h-8},rot);}
    for(int part=1;part<w.filled;++part){const std::string image=ImageOf(w.ids[size_t(part)]);if(image.empty())continue;const float s=28;
        const UI::Rect r{outer.x+outer.w-s*.6f,outer.y-s*.35f+float(part-1)*(s+3),s,s};ui.Rounded({r.x-2,r.y-2,r.w+4,r.h+4},Black,6);ui.Rounded(r,White,5);ui.Photo(image,{r.x+2,r.y+2,r.w-4,r.h-4});}
    const float interval=CraftInterval(w.points[2],w.lv[size_t(FuseTrait::Heavy)]);
    if(w.cool>0&&interval>.15f){const float share=std::clamp(w.cool/interval,0.f,1.f);ui.Fill({inner.x,inner.y+inner.h*(1-share),inner.w,inner.h*share},{0,0,0,.4f});}
    // 名前と、弾数・威力・連射
    char stats[96];snprintf(stats,sizeof(stats),"%d発  x%.1f  %.1f発/秒",CraftShots(w.points[0]),CraftPower(w.points[1]),1/interval);
    const float nw=std::max(ui.Width(w.name,20,Jp),ui.Width(stats,16,Jp))+24,nx=outer.x-16-nw;Plate(ui,{nx,cy-30,nw,60},Cream,3,4);
    ui.Text(w.name,nx+12,cy-28,20,Black,Jp);ui.Text(stats,nx+12,cy+2,16,Black,Jp);
}
}
