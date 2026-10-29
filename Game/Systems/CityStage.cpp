// CityStage.cpp
// スライムの街のステージ (Docs/SlimeLife.md の「街ステージ」)。
// 動かない建物は PBR の材質ごとに 1 つの動的メッシュとして作る。小物・
// バリケード・シャッターの門・街の敵は、Resources/Credits/CityStage.md にある
// CC0 の Poly Haven の素材を使う
#include "ChronoSystemInternal.h"

namespace Game {
// このファイルの中だけで使う型と関数
namespace {
// 街の材質の種類 (道路・歩道・塗装・コンクリート・ガラス・金属など)
enum CityMaterialId {CityRoad,CitySidewalk,CityAlleyFloor,CityPaint,CityPaintYellow,CityConcrete,CityCladding,CityFacadeTiles,CityDarkTiles,
    CitySlab,CityGreyTiles,CityShutter,CityRustShutter,CityGlass,CityMetal,CityWhiteBrick,CityWood,CityStoneSkin,CityScorch,CityMaterialCount};
// 材質ごとの頂点と添字
struct CityGeometry {std::vector<Engine::VertexData> vertices;std::vector<uint32_t> indices;};
// 街の形を材質ごとのメッシュとして組み立てる
struct CityBuilder {
    // 材質ごとの形と、テクスチャを繰り返す大きさ
    std::array<CityGeometry,CityMaterialCount> geometry;
    std::array<float,CityMaterialCount> tile{30,10,16,6,6,20,8,12,14,18,8,8,8,8,6,10,8,22,30};
    // UV は面の平面に沿ってワールドの単位で付けるので、繰り返しの大きさが物理的にそろう
    // 多角形を三角形に分けて追加する (法線から UV の向きを決める)
    void Polygon(int material,std::initializer_list<Chrono::Vec> points,Chrono::Vec normal){
        // 材質の形と、追加する頂点の最初の番号
        using Chrono::Vec;auto& g=geometry[size_t(material)];uint32_t base=uint32_t(g.vertices.size());
        Vec n=Chrono::Unit(normal);Vec u=std::abs(n.y)>.99f?Vec{1,0,0}:Chrono::Unit(Chrono::SwarmCross({0,1,0},n));Vec v=Chrono::SwarmCross(n,u);
        float size=tile[size_t(material)];
        // 各頂点の位置・UV (面の上のワールド座標を繰り返しの大きさで割る)・法線
        for(Vec p:points){Engine::VertexData vertex{};vertex.position={p.x,p.y,p.z,1};vertex.texcoord={Chrono::Dot(p,u)/size,-Chrono::Dot(p,v)/size};
            vertex.normal={n.x,n.y,n.z};g.vertices.push_back(vertex);}
        const auto* p=points.begin();
        // 地形で確かめた通り、表の面は cross(p1-p0,p2-p0) が法線の向きになる
        bool forward=Chrono::Dot(Chrono::SwarmCross(p[1]-p[0],p[2]-p[0]),n)>0;
        // 扇形に三角形を作る (表の向きに合わせて頂点の順を決める)
        for(uint32_t i=1;i+1<uint32_t(points.size());++i){
            if(forward)g.indices.insert(g.indices.end(),{base,base+i,base+i+1});else g.indices.insert(g.indices.end(),{base,base+i+1,base+i});}
    }
    // 箱を作る (bottom なら底面も作る)
    void Box(int material,Chrono::Vec lo,Chrono::Vec hi,bool bottom=false){
        using Chrono::Vec;
        // 6 面を順に追加する (底は指定したときだけ)
        Polygon(material,{{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{hi.x,hi.y,lo.z}},{1,0,0});
        Polygon(material,{{lo.x,lo.y,hi.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,hi.y,hi.z}},{-1,0,0});
        Polygon(material,{{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{hi.x,hi.y,hi.z}},{0,0,1});
        Polygon(material,{{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z}},{0,0,-1});
        Polygon(material,{{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}},{0,1,0});
        if(bottom)Polygon(material,{{lo.x,lo.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,lo.y,lo.z}},{0,-1,0});
    }
    // 壁に付いた箱: along は壁に沿う向き、out は壁から出る向き。ずれは壁の平面からの距離
    void FacadeBox(int material,Chrono::Vec center,Chrono::Vec along,Chrono::Vec out,float halfAlong,float halfY,float out0,float out1){
        // パイプラインは両面を描くので、壁から始まる箱は裏の面が壁の平面に重なって
        // Z ファイティングを起こす。そこで壁に付く箱は少し壁に埋める
        // 外へ出る箱が壁から始まるなら、壁へ少し埋める
        if(out0<=.01f&&out1>out0)out0=-.4f;
        // 壁に沿う向きと外へ出る向きで箱の 2 つの角を求める
        using Chrono::Vec;Vec a=center+along*halfAlong+out*out0,b=center-along*halfAlong+out*out1;
        Box(material,{std::min(a.x,b.x),center.y-halfY,std::min(a.z,b.z)},{std::max(a.x,b.x),center.y+halfY,std::max(a.z,b.z)});
    }
    // 道路に塗った平らな帯 (a から b まで、指定の幅)
    void Strip(int material,Chrono::Vec a,Chrono::Vec b,float width,float y=-.08f){
        // 道の向きに垂直な幅の分だけ広げた四角形
        using Chrono::Vec;Vec d=Chrono::Unit(b-a),n{-d.z,0,d.x};n=n*(width*.5f);a.y=b.y=y;
        Polygon(material,{a-n,b-n,b+n,a+n},{0,1,0});
    }
};
// 建物の種類 (路地・通り・高層ビル) と建物の情報
enum CityStyle {CityStyleAlley,CityStyleStreet,CityStyleTower};
struct CityBuilding {Chrono::CityRect foot;Chrono::Vec out;float height;int style,skin,trim;bool allSides;};
// スキャンしたネズミと大砲: 敵の大きさあたりの長さと、向きに合わせるヨー
constexpr float CityRatWidth=5.6f,CityRatYaw=0,CityCannonWidth=3.4f,CityCannonYaw=0;
// 1 階と 2 階以上の高さ
constexpr float CityGroundFloor=24,CityFloor=19;
// 路面の塗装の高さ (路面は -0.2)。重なる塗装どうしを同じ高さに置くと奥行きの精度が足りずにちらつくので、
// 重なり得る層ごとに高さを分ける: 車線と中央線 < タイヤ痕 < 横断歩道と停止線 < 斜めの横断歩道の片方 < 事故の焦げ跡
constexpr float PaintLaneY=-.16f,PaintSkidY=-.13f,PaintCrossY=-.1f,PaintCrossTopY=-.07f,PaintStainY=-.04f;
// 整数から 0〜1 の疑似乱数を作る
float CityNoise(int seed){float n=std::sin(float(seed)*12.9898f+78.233f)*43758.5453f;return n-std::floor(n);}
}
// 街のステージを作る: 影の範囲・シェーダー・材質・道路と横断歩道・建物・小物・バリケード・門・敵のモデル
void ChronoSystem::BuildCityStage(entt::registry& r){
    Engine::LoadProfile::Scope profile("phase","ChronoSystem::BuildCityStage");
    // 材質の画像と小物の LOD は予約だけして組み立てを進め、関数を抜けるときにまとめて並列で作る
    struct AssetBatch{AssetBatch(){Engine::Renderer::GetInstance()->BeginAssetBatch();}~AssetBatch(){Engine::Renderer::GetInstance()->EndAssetBatch();}} assetBatch;
    using namespace Chrono;
    // 影の範囲を広げる
    auto* render=Engine::Renderer::GetInstance();
    // 塔は約 600 の高さがある。カメラの前に広く深い影を取って、影が欠けないようにする
    render->SetDirectionalShadowRange(460,1800,.3f);
    // 景観・街の面・ガラス・鎧・光・強調のシェーダー
    render->CreateShaderPipeline("EnvironmentSurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/EnvironmentSurfacePS.hlsl");
    // 通りの谷間のために空の光を足した環境の陰影。動的メッシュは範囲を持たないので、
    // 「EnvironmentSurface」で行う景観の個別カリングに入れてはいけない
    render->CreateShaderPipeline("CitySurface",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CitySurfacePS.hlsl");
    render->CreateShaderPipeline("CityGlass",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CityGlassPS.hlsl");
    render->CreateShaderPipeline("CityArmor",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/CityArmorPS.hlsl");
    render->CreateShaderPipelineTransparent("DomainGlow",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/DomainGlowPS.hlsl",true,true);
    render->CreateShaderPipelineTransparent("MorphItemHighlight",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/MorphItemHighlightPS.hlsl",true,true);
    // Poly Haven の材質 (色・法線・粗さ・AO) を読み込む
    auto material=[&](const std::string& id){CityModel m;const std::string base="Resources/Textures/PolyHaven/"+id;
        // BCn の DDS があればそれを、無ければ JPEG を使う
        auto map=[&](const char* suffix,bool sRGB){for(const char* ext:{".dds",".jpg"}){std::string path=base+"_"+suffix+"_2k"+ext;
            if(std::filesystem::exists(path))return render->LoadTexture2D(path,sRGB);}
            // AO の無いスキャンもある。白にすると光を遮らない
            return white_;};
        // NTC の材質: 色・粗さ・AO を GPU で BCn に復元する。法線は NTC に通さず BC5 の DDS のまま使う
        if(std::filesystem::exists(base+"_material.ntc")){auto set=render->LoadNtcTextureSet(base+"_material.ntc",{"albedo","roughness","ao"});
            if(set.size()==3){m.texture=set[0];m.extra={map("nor_dx",false),set[1],set[2]};return m;}}
        // 色と、法線・粗さ・AO のマップ
        m.texture=map("diff",true);
        for(const char* suffix:{"nor_dx","Rough","AO"})m.extra.push_back(map(suffix,false));
        return m;};
    // 材質ごとの Poly Haven の素材の名前
    const char* surfaceIds[CityMaterialCount]={"asphalt_02","large_grey_tiles","concrete_floor_worn_001","concrete_floor_worn_001","concrete_floor_worn_001",
        "concrete_wall_008","exterior_wall_cladding_02","rectangular_facade_tiles_02","concrete_tile_facade","concrete_slab_wall","grey_tiles",
        "painted_metal_shutter","rusty_metal_shutter","concrete_floor_worn_001","concrete_floor_worn_001","white_bricks","medieval_wood","castle_wall_slates","asphalt_02"};
    // 材質を読み込む
    std::array<CityModel,CityMaterialCount> surfaces;for(int i=0;i<CityMaterialCount;++i)surfaces[size_t(i)]=material(surfaceIds[i]);
    Engine::LoadProfile::Record("mark","city: surfaces requested",0);
    // 材質ごとの色の補正
    auto tint=[](int i)->DirectX::XMFLOAT4{
        // 塗装は明るく、黄色の塗装・ガラス・金属・道路は色を変える
        switch(i){case CityPaint:return {2.5f,2.5f,2.45f,1};case CityPaintYellow:return {2.5f,1.85f,.45f,1};case CityAlleyFloor:return {1.3f,1.28f,1.25f,1};case CityGlass:return {.2f,.26f,.33f,1};
            case CityMetal:return {.24f,.25f,.27f,1};case CityRoad:return {.85f,.85f,.85f,1};case CityScorch:return {.16f,.14f,.13f,1};default:return {1,1,1,1};}};
    // 敵や破片に使う材質を記録する
    cityWood_=surfaces[CityWood];cityStoneSkin_=surfaces[CityStoneSkin];cityIron_=surfaces[CityShutter];cityArmor_=surfaces[CityMetal];cityBlock_=surfaces[CityConcrete];
    CityBuilder city;
    // 道路: 全体をアスファルトにし、歩道・路地の床・塗装を重ねる
    city.Polygon(CityRoad,{{-1500,-.2f,-1600},{1700,-.2f,-1600},{1700,-.2f,2500},{-1500,-.2f,2500}},{0,1,0});
    // 床の四角形を作る関数
    auto floorQuad=[&](int m,CityRect w,float y){city.Polygon(m,{{w.x0,y,w.z0},{w.x1,y,w.z0},{w.x1,y,w.z1},{w.x0,y,w.z1}},{0,1,0});};
    // 区画 1: 脱出路地の床
    for(int i=0;i<3;++i)floorQuad(CityAlleyFloor,CityWalk()[size_t(i)],0);
    // 歩道 (縁石を付ける辺を選べる)
    auto pavement=[&](CityRect w,bool curbX0,bool curbX1,bool curbZ0,bool curbZ1){floorQuad(CitySidewalk,w,.15f);
        // 縁石: 道路側の端に沿った低いコンクリートの帯 (見た目だけで、床は平らのまま)
        if(curbX0)city.Box(CityConcrete,{w.x0-.6f,-.05f,w.z0},{w.x0+.6f,.45f,w.z1});if(curbX1)city.Box(CityConcrete,{w.x1-.6f,-.05f,w.z0},{w.x1+.6f,.45f,w.z1});
        if(curbZ0)city.Box(CityConcrete,{w.x0,-.05f,w.z0-.6f},{w.x1,.45f,w.z0+.6f});if(curbZ1)city.Box(CityConcrete,{w.x0,-.05f,w.z1-.6f},{w.x1,.45f,w.z1+.6f});};
    // 横断歩道の縞
    auto zebra=[&](CityRect band,bool alongZ){// 縞は道路を横切る。alongZ は道路が z 方向に通っていること
        if(alongZ){for(float x=band.x0+3;x<band.x1-2;x+=9)city.Strip(CityPaint,{x,0,band.z0},{x,0,band.z1},4.5f,PaintCrossY);}
        else for(float z=band.z0+3;z<band.z1-2;z+=9)city.Strip(CityPaint,{band.x0,0,z},{band.x1,0,z},4.5f,PaintCrossY);};
    // 焦げ跡やタイヤ痕: 道路の塗装より少し上に置く、黒い不規則な多角形
    auto scorch=[&](V at,float radius,int seedBase){std::vector<V> rim;for(int k=0;k<10;++k){float a=float(k)*.6283f,r=radius*(.65f+.5f*CityNoise(seedBase*11+k));rim.push_back(at+V{std::cos(a)*r,0,std::sin(a)*r});}
        for(int k=0;k<10;++k){V a=rim[size_t(k)],b=rim[size_t((k+1)%10)];a.y=b.y=at.y;city.Polygon(CityScorch,{at,b,a},{0,1,0});}};
    // 区画 2: 都心大通り。両側に幅 30 の歩道、間は 6 車線 (脇道の入口で歩道が切れる)
    const CityRect avenue=CityWalk()[3];const float avenueX=(avenue.x0+avenue.x1)*.5f,road0=avenue.x0+30,road1=avenue.x1-30;
    const float sideGaps[2][2]={{-430,-370},{-40,20}};
    {float z=avenue.z0;for(int g=0;g<=2;++g){float z1=g<2?sideGaps[g][0]:avenue.z1;
        pavement({avenue.x0,z,road0,z1},false,true,false,false);pavement({road1,z,avenue.x1,z1},true,false,false,false);if(g<2)z=sideGaps[g][1];}}
    // 車線の白線と中央の黄色い線 (交差する脇道の所は空ける)
    for(float z=avenue.z0+18;z<avenue.z1-20;z+=40){bool gap=false;for(const auto& s:sideGaps)gap|=z>s[0]-34&&z<s[1]+14;if(gap)continue;
        for(float lane:{-72.f,-36.f,36.f,72.f})city.Strip(CityPaint,{avenueX+lane,0,z},{avenueX+lane,0,z+20},1,PaintLaneY);}
    city.Strip(CityPaintYellow,{avenueX-1,0,avenue.z0+10},{avenueX-1,0,avenue.z1-10},.8f,PaintLaneY);city.Strip(CityPaintYellow,{avenueX+1,0,avenue.z0+10},{avenueX+1,0,avenue.z1-10},.8f,PaintLaneY);
    // 脇道の前後と大通りの両端の横断歩道
    for(const auto& s:sideGaps){zebra({road0,s[0]-30,road1,s[0]-8},true);zebra({road0,s[1]+8,road1,s[1]+30},true);}
    zebra({road0,avenue.z0+40,road1,avenue.z0+62},true);zebra({road0,avenue.z1-36,road1,avenue.z1-14},true);
    // 区画 3: 大火事現場。外周は歩道、中は焼けたアスファルトと焦げ跡
    const CityRect fire=CityWalk()[9];
    pavement({fire.x0,fire.z0,fire.x0+24,fire.z1},false,true,false,false);pavement({fire.x1-24,fire.z0,fire.x1,fire.z1},true,false,false,false);
    pavement({fire.x0+24,fire.z1-24,fire.x1-24,fire.z1},false,false,true,false);
    {int k=0;for(const auto& spot:CityFires()){scorch(spot.at+V{0,-.08f,0},spot.radius*2.4f,++k);}
        for(V at:{V{-40,0,640},V{120,0,560},V{260,0,760},V{100,0,820},V{-110,0,520}})scorch(at+V{0,-.14f,0},26,++k);}
    // 区画 4: 事故多発交差点。幅 160 の道路 2 本が中心で交わり、四隅は歩道
    // 交差点の中心と道路の幅の半分
    const CityRect plaza=CityWalk().back();const float cx=CityCrossing().x,cz=CityCrossing().z,h=CityRoadHalf();
    // 交差点の四隅の歩道
    pavement({plaza.x0,plaza.z0,cx-h,cz-h},false,true,false,true);pavement({cx+h,plaza.z0,plaza.x1,cz-h},true,false,false,true);
    pavement({plaza.x0,cz+h,cx-h,plaza.z1},false,true,true,false);pavement({cx+h,cz+h,plaza.x1,plaza.z1},true,false,true,false);
    // 交差点の 4 方向の横断歩道
    zebra({cx-h,cz-h-46,cx+h,cz-h-8},true);zebra({cx-h,cz+h+8,cx+h,cz+h+46},true);
    zebra({cx-h-46,cz-h,cx-h-8,cz+h},false);zebra({cx+h+8,cz-h,cx+h+46,cz+h},false);
    // スクランブル交差点の特徴の斜めの横断歩道 (縞はそれぞれの対角線に垂直)
    // 交差点の 2 本の対角線に沿った縞
    for(int d=0;d<2;++d){V a{cx-h+12,0,d?cz+h-12:cz-h+12},b{cx+h-12,0,d?cz-h+12:cz+h-12};V dir=Unit(b-a),side{-dir.z,0,dir.x};
        for(float t=0;t<Length(b-a);t+=9){V m=a+dir*t;city.Strip(CityPaint,m-side*16,m+side*16,4.5f,d?PaintCrossTopY:PaintCrossY);}}
    // 停止線
    for(float s:{-1.f,1.f}){city.Strip(CityPaint,{cx-h,0,cz+s*(h+52)},{cx+h,0,cz+s*(h+52)},2,PaintCrossY);city.Strip(CityPaint,{cx+s*(h+52),0,cz-h},{cx+s*(h+52),0,cz+h},2,PaintCrossY);}
    // 交差点から延びる道路: 車線の線を遠くまで続ける
    for(float t=plaza.z1+20;t<2350;t+=40){city.Strip(CityPaint,{cx-40,0,t},{cx-40,0,t+20},1,PaintLaneY);city.Strip(CityPaint,{cx+40,0,t},{cx+40,0,t+20},1,PaintLaneY);}
    for(float t=plaza.x1+20;t<1650;t+=40){city.Strip(CityPaint,{t,0,cz-40},{t+20,0,cz-40},1,PaintLaneY);city.Strip(CityPaint,{t,0,cz+40},{t+20,0,cz+40},1,PaintLaneY);}
    for(float t=plaza.x0-20;t>-1450;t-=40){city.Strip(CityPaint,{t,0,cz-40},{t-20,0,cz-40},1,PaintLaneY);city.Strip(CityPaint,{t,0,cz+40},{t-20,0,cz+40},1,PaintLaneY);}
    // 事故の跡: 衝突した車の組 (中心と向き)。急ブレーキのタイヤ痕と、こぼれた油の染み
    const std::array<std::pair<V,float>,6> crashes={{{{cx+40,0,cz+60},.4f},{{cx-60,0,cz-50},2.2f},{{cx+120,0,cz-20},-1.2f},
        {{cx-20,0,cz+170},3.6f},{{cx-140,0,cz+30},.9f},{{cx+30,0,cz-170},-.4f}}};
    {int k=40;for(const auto& [at,yaw]:crashes){V d{std::sin(yaw),0,std::cos(yaw)},side{d.z,0,-d.x};
        for(float lane:{-4.f,4.f})for(int i=0;i<12;++i){float t0=float(i)/12,t1=float(i+1)/12;
            auto point=[&](float t){return at-d*(90*(1-t))+side*(lane+std::sin(t*2.2f)*12);};city.Strip(CityScorch,point(t0),point(t1),1.3f,PaintSkidY);}
        scorch(at+side*9+V{0,PaintStainY,0},7,++k);}}
    // 床の範囲の外側に沿って建物を並べる
    // 建物の一覧と乱数の種
    std::vector<CityBuilding> buildings;int seed=0;
    // 外壁の材質の候補
    const int skins[]={CityConcrete,CityCladding,CityFacadeTiles,CityGreyTiles,CityWhiteBrick,CitySlab,CityDarkTiles};
    // 建物を 1 列並べる (幅と階数は乱数で決める)
    auto row=[&](float fixed,float from,float to,V out,float depth,int minFloors,int maxFloors,int style){
        float at=from;
        // 建物の幅 (高層は広い)
        while(at<to-4){float width=std::min(to-at,(style==CityStyleTower?70.f:34.f)+CityNoise(++seed)*(style==CityStyleTower?60.f:36.f));if(to-(at+width)<20)width=to-at;
            CityRect foot{};
            // 外向きの向きで建物の敷地を決める
            if(std::abs(out.x)>.5f){float x0=out.x>0?fixed-depth:fixed,x1=out.x>0?fixed:fixed+depth;foot={x0,at,x1,at+width};}
            else{float z0=out.z>0?fixed-depth:fixed,z1=out.z>0?fixed:fixed+depth;foot={at,z0,at+width,z1};}
            // 階数と高さ
            int floors=minFloors+int(CityNoise(++seed)*float(maxFloors-minFloors+1));
            float height=CityGroundFloor+float(floors)*CityFloor;
            // 外壁と縁の材質 (種類で選ぶ)
            int skin=style==CityStyleAlley?(CityNoise(++seed)<.4f?CityWhiteBrick:CityNoise(++seed)<.5f?CityConcrete:CitySlab):skins[int(CityNoise(++seed)*6.99f)];
            int trim=style==CityStyleTower?(CityNoise(++seed)<.5f?CityDarkTiles:CitySlab):CityConcrete;
            buildings.push_back({foot,out,height,style,skin,trim,style==CityStyleTower});at+=width;}
    };
    // 区画 1: 脱出路地。高く近い壁が折れ曲がった細い道を作る
    row(-18,-1220,-1028,{1,0,0},50,6,10,CityStyleAlley);row(18,-1220,-1064,{-1,0,0},50,6,10,CityStyleAlley);row(-1220,-68,68,{0,0,1},50,7,10,CityStyleAlley);
    row(-1028,-68,84,{0,0,-1},50,6,9,CityStyleAlley);row(-1064,68,120,{0,0,1},50,6,9,CityStyleAlley);
    row(120,-1114,-888,{-1,0,0},50,6,10,CityStyleAlley);row(84,-978,-888,{1,0,0},50,6,8,CityStyleAlley);
    // 区画 2: 都心大通り。両側に高いビルが並び、脇道は奥の建物で行き止まり
    row(avenue.z0,-88,34,{0,0,1},50,8,12,CityStyleStreet);row(avenue.z0,170,292,{0,0,1},50,8,12,CityStyleStreet);
    {float z=avenue.z0;for(int g=0;g<=2;++g){float z1=g<2?sideGaps[g][0]:avenue.z1;
        row(avenue.x0,z,z1,{1,0,0},50,10,18,CityStyleStreet);row(avenue.x1,z,z1,{-1,0,0},50,10,18,CityStyleStreet);if(g<2)z=sideGaps[g][1];}}
    for(const auto& s:sideGaps){row(avenue.x0-50,s[0],s[1],{1,0,0},50,6,10,CityStyleStreet);row(avenue.x1+50,s[0],s[1],{-1,0,0},50,6,10,CityStyleStreet);}
    // 大通りの北の端: バリケードの通路の両側
    const CityRect passage=CityWalk()[8];
    row(avenue.z1,-88,passage.x0,{0,0,-1},56,9,14,CityStyleStreet);row(avenue.z1,passage.x1,292,{0,0,-1},56,9,14,CityStyleStreet);
    // 区画 3: 大火事現場を囲む建物 (焼けた窓の光は後で付ける)
    row(fire.z0,-210,passage.x0,{0,0,1},56,6,10,CityStyleStreet);row(fire.z0,passage.x1,414,{0,0,1},56,6,10,CityStyleStreet);
    row(fire.x0,fire.z0,fire.z1,{1,0,0},50,6,11,CityStyleStreet);row(fire.x1,fire.z0,fire.z1,{-1,0,0},50,6,11,CityStyleStreet);
    // 双子の塔が火事場と交差点の間のシャッターの門を挟む
    const CityRect gateWay=CityWalk()[10];
    buildings.push_back({{plaza.x0+240,fire.z1,gateWay.x0,plaza.z0},{0,0,-1},CityGroundFloor+17*CityFloor,CityStyleTower,CityGlass,CityDarkTiles,true});
    buildings.push_back({{gateWay.x1,fire.z1,plaza.x0+520,plaza.z0},{0,0,-1},CityGroundFloor+19*CityFloor,CityStyleTower,CityGlass,CitySlab,true});
    // 区画 4: 交差点の四隅に高層ビル。延びる道路の間は中層の建物
    row(plaza.z0,plaza.x0-50,plaza.x0+240,{0,0,1},60,12,26,CityStyleTower);row(plaza.z0,plaza.x0+520,plaza.x1+50,{0,0,1},60,12,26,CityStyleTower);
    row(plaza.x0,plaza.z0,cz-h,{1,0,0},60,14,30,CityStyleTower);row(plaza.x0,cz+h,plaza.z1,{1,0,0},60,14,30,CityStyleTower);
    row(plaza.x1,plaza.z0,cz-h,{-1,0,0},60,14,30,CityStyleTower);row(plaza.x1,cz+h,plaza.z1,{-1,0,0},60,14,30,CityStyleTower);
    row(plaza.z1,plaza.x0-50,cx-h,{0,0,-1},60,16,32,CityStyleTower);row(plaza.z1,cx+h,plaza.x1+50,{0,0,-1},60,16,32,CityStyleTower);
    row(cx-h,plaza.z1+60,2300,{1,0,0},50,8,16,CityStyleStreet);row(cx+h,plaza.z1+60,2300,{-1,0,0},50,8,16,CityStyleStreet);
    row(cz-h,-1400,plaza.x0-60,{0,0,1},50,8,16,CityStyleStreet);row(cz+h,-1400,plaza.x0-60,{0,0,-1},50,8,16,CityStyleStreet);
    row(cz-h,plaza.x1+60,1600,{0,0,1},50,8,16,CityStyleStreet);row(cz+h,plaza.x1+60,1600,{0,0,-1},50,8,16,CityStyleStreet);
    // 光る看板の一覧と、ネオンの色
    cityGlows_.clear();
    const Engine::Vector4 neon[]={{1,.25f,.55f,1},{.2f,.85f,1,1},{1,.75f,.2f,1},{.55f,1,.35f,1},{.75f,.4f,1,1}};
    // 各建物: 本体の箱・屋上・正面 (高層ビルはガラスの外壁、それ以外は店と窓)・カメラ用の当たり判定
    for(const auto& b:buildings){
        const auto& f=b.foot;
        // 建物の本体の箱
        city.Box(b.skin==CityGlass?b.trim:b.skin,{f.x0,0,f.z0},{f.x1,b.height,f.z1});
        // 屋上: 手すりの輪と植木の箱
        float p=1.2f;
        // 屋上の 4 辺の手すり
        city.Box(b.trim,{f.x0,b.height,f.z0},{f.x1,b.height+2.5f,f.z0+p});city.Box(b.trim,{f.x0,b.height,f.z1-p},{f.x1,b.height+2.5f,f.z1});
        city.Box(b.trim,{f.x0,b.height,f.z0},{f.x0+p,b.height+2.5f,f.z1});city.Box(b.trim,{f.x1-p,b.height,f.z0},{f.x1,b.height+2.5f,f.z1});
        // 高層ビルの屋上の塔とアンテナ
        if(b.style==CityStyleTower){V c{(f.x0+f.x1)*.5f,b.height,(f.z0+f.z1)*.5f};float hx=(f.x1-f.x0)*.5f,hz=(f.z1-f.z0)*.5f;
            float crown=12+CityNoise(++seed)*22;
            city.Box(b.trim,c+V{-hx+6,0,-hz+6},c+V{hx-6,crown,hz-6});
            city.Box(CityMetal,c+V{-hx*.45f,crown,-hz*.45f},c+V{hx*.45f,crown+9,hz*.45f});
            if(CityNoise(++seed)<.6f)city.Box(CityMetal,c+V{-.8f,crown+9,-.8f},c+V{.8f,crown+9+30+CityNoise(++seed)*40,.8f});}
        // 屋上の設備
        {V c{(f.x0+f.x1)*.5f,b.height,(f.z0+f.z1)*.5f};float sx=(f.x1-f.x0)*.18f,sz=(f.z1-f.z0)*.18f;
            city.Box(CityConcrete,c+V{-sx,0,-sz},c+V{sx*.3f,9,sz*.4f});city.Box(CityMetal,c+V{sx*.5f,0,-sz},c+V{sx*1.4f,6,-sz*.1f});}
        // 4 つの面のうち、正面 (全面を飾る高層は全部) だけを飾る
        V faces[4]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        for(V out:faces){
            bool main=std::abs(out.x-b.out.x)<.1f&&std::abs(out.z-b.out.z)<.1f;
            if(!main&&!b.allSides)continue;
            // 面に沿う向き・面の幅・面の始点・中央
            V along=std::abs(out.x)>.5f?V{0,0,1}:V{1,0,0};
            float width=std::abs(out.x)>.5f?f.z1-f.z0:f.x1-f.x0;
            V face=std::abs(out.x)>.5f?V{out.x>0?f.x1:f.x0,0,f.z0}:V{f.x0,0,out.z>0?f.z1:f.z0};
            V mid=face+along*(width*.5f);
            if(b.style==CityStyleTower){
                // 低層部: 柱の間の全面ガラスの店、その上はガラスの帯と腰壁のカーテンウォール
                float podium=CityGroundFloor+CityFloor;
                city.FacadeBox(CityGlass,mid+V{0,podium*.5f,0},along,out,width*.5f,podium*.5f,0,.45f);
                for(float s=0;s<=width+.1f;s+=16)city.FacadeBox(b.trim,face+along*std::clamp(s,1.f,width-1.f)+V{0,podium*.5f,0},along,out,1,podium*.5f,0,1.2f);
                // 帯と柱は建物の幅に収める。はみ出すと隣の建物の帯と同じ面で重なってちらつく
                city.FacadeBox(b.trim,mid+V{0,podium+1.5f,0},along,out,width*.5f,1.5f,0,2.5f);
                for(float y=podium+3;y<b.height-2;y+=CityFloor){city.FacadeBox(CityGlass,mid+V{0,y+8.5f,0},along,out,width*.5f,7.5f,0,.45f);
                    city.FacadeBox(b.trim,mid+V{0,y+17.5f,0},along,out,width*.5f,1.5f,0,1.1f);}
                for(float s=8;s<width-2;s+=12)city.FacadeBox(CityMetal,face+along*s+V{0,(podium+b.height)*.5f,0},along,out,.35f,(b.height-podium)*.5f,0,1.6f);
                // 低層部の上の大きな画面は交差点の方を向く
                if(width>60&&CityNoise(++seed)<.55f){float w=std::min(width*.5f,46.f);V c=mid+V{0,podium+30,0}+out*1.2f;
                    city.FacadeBox(CityMetal,c,along,out,w*.5f+1,13,0,1);
                    cityGlows_.push_back({c+out*1.5f,along,{0,1,0},{w,24,.4f},neon[seed%5]});}
                continue;
            }
            // 柱の間の数
            int bays=std::max(1,int(width/14));
            // 1 階: 通りはガラス・シャッター・看板の帯の店、路地は錆びたシャッターと扉
            // 1 階の各区画
            for(int bay=0;bay<bays;++bay){V c=face+along*(width*(float(bay)+.5f)/float(bays));float half=width/float(bays)*.5f-1.2f;
                // 路地: 扉か錆びたシャッター
                if(b.style==CityStyleAlley){bool door=(bay+seed)%3==0;
                    if(door){city.FacadeBox(CityMetal,c+V{0,9,0},along,out,3.4f,9,0,.3f);city.FacadeBox(CityConcrete,c+V{0,18.6f,0},along,out,4.4f,.6f,0,.8f);}
                    else{city.FacadeBox(CityRustShutter,c+V{0,9.5f,0},along,out,half,9.5f,0,.35f);city.FacadeBox(CityMetal,c+V{0,20.2f,0},along,out,half+.4f,1.2f,0,1.4f);}}
                // 通り: シャッターかガラスの店と、柱
                else{bool shut=CityNoise(seed*7+bay)<.25f;
                    city.FacadeBox(shut?CityShutter:CityGlass,c+V{0,9.5f,0},along,out,half,9.5f,0,.35f);
                    for(float s:{-1.f,1.f})city.FacadeBox(CityMetal,c+along*(s*(half+.6f))+V{0,10,0},along,out,.6f,10,0,.8f);}}
            // 通りの建物の看板の帯
            if(b.style==CityStyleStreet){city.FacadeBox(CityDarkTiles,mid+V{0,22,0},along,out,width*.5f,2,0,1.6f);
                // 通りの一部の正面には縦のネオン看板を付ける
                if(CityNoise(++seed)<.45f){V c=face+along*(width*(.15f+.7f*CityNoise(++seed)))+V{0,48,0}+out*4;
                    city.FacadeBox(CityMetal,c,along,out,.6f,1,0,4);
                    cityGlows_.push_back({c+out*2.2f,out,{0,1,0},{3,32,2.2f},neon[seed%5]});}}
            // 上の階: 枠と窓台のある窓。通りの一部の建物にはバルコニー
            // 上の各階の窓
            for(float y=CityGroundFloor;y+CityFloor<=b.height+.1f;y+=CityFloor){float wy=y+10;
                for(int bay=0;bay<bays;++bay){V c=face+along*(width*(float(bay)+.5f)/float(bays))+V{0,wy,0};
                    float half=b.style==CityStyleAlley?2.6f:std::min(4.6f,width/float(bays)*.5f-2.f);
                    city.FacadeBox(CityGlass,c,along,out,half,5,0,.3f);
                    city.FacadeBox(CityMetal,c-V{0,5.4f,0},along,out,half+.6f,.4f,0,1.1f);
                    city.FacadeBox(CityMetal,c+V{0,5.3f,0},along,out,half+.4f,.3f,0,.8f);
                    for(float s:{-1.f,1.f})city.FacadeBox(CityMetal,c+along*(s*(half+.2f)),along,out,.25f,5,0,.7f);
                    if(b.style==CityStyleAlley&&CityNoise(int(wy)*13+bay*7+seed)<.18f)city.FacadeBox(CityMetal,c+along*(half+5)-V{0,3,0},along,out,2.4f,1.8f,0,2.4f);
                    if(b.style==CityStyleStreet&&CityNoise(int(wy)*17+bay*5+seed)<.22f){
                        city.FacadeBox(CityConcrete,c-V{0,6.2f,0},along,out,half+2.5f,.5f,0,4.5f);city.FacadeBox(CityMetal,c-V{0,4.4f,0}+out*4.3f,along,out,half+2.5f,1.3f,0,.2f);}}}
            // 路地の建物の縦の配管
            if(b.style==CityStyleAlley)for(int k=0;k<2;++k){V c=face+along*(width*(.2f+.6f*float(k)))+V{0,b.height*.5f,0};
                city.FacadeBox(CityMetal,c,along,out,.5f,b.height*.5f,.3f,1.3f);}
        }
        // 追従カメラが通りの中に留まるよう、カメラ用の当たり判定を置く
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono City building collision");
        r.emplace<TransformComponent>(e).translate=Write(V{(f.x0+f.x1)*.5f,b.height*.5f,(f.z0+f.z1)*.5f});
        r.emplace<BoxColliderComponent>(e).size=Write(V{f.x1-f.x0,b.height,f.z1-f.z0});
        r.emplace<TagComponent>(e).tag=TagType::Wall;r.emplace<Solid>(e);
    }
    // 火事場を囲む建物: 窓の奥で炎が揺らぎ、屋上から火の手が上がる
    for(const auto& b:buildings){const auto& f=b.foot;V c{(f.x0+f.x1)*.5f,0,(f.z0+f.z1)*.5f};
        if(b.style!=CityStyleStreet||CityAreaAt(c)!=CityArea::Fire||c.x<fire.x0-60||c.x>fire.x1+60)continue;
        V out=b.out,along=std::abs(out.x)>.5f?V{0,0,1}:V{1,0,0};float width=std::abs(out.x)>.5f?f.z1-f.z0:f.x1-f.x0;
        V face=std::abs(out.x)>.5f?V{out.x>0?f.x1:f.x0,0,(f.z0+f.z1)*.5f}:V{(f.x0+f.x1)*.5f,0,out.z>0?f.z1:f.z0};
        for(int k=0;k<4;++k){float s=(CityNoise(++seed)-.5f)*(width-12),y=CityGroundFloor+10+float(int(CityNoise(++seed)*4))*CityFloor;if(y>b.height-8)continue;
            cityGlows_.push_back({face+along*s+V{0,y,0}+out*.6f,along,{0,1,0},{6,7,.3f},{1.6f,.5f,.1f,1},1,CityNoise(++seed)*6.283f});}
        if(CityNoise(++seed)<.5f)for(int k=0;k<3;++k)cityGlows_.push_back({c+V{(CityNoise(++seed)-.5f)*12,b.height+4+float(k)*3,(CityNoise(++seed)-.5f)*12},{1,0,0},{0,1,0},{9-float(k)*2.5f,8,9-float(k)*2.5f},{1.6f,.45f,.08f,1},1,CityNoise(++seed)*6.283f});}
    // シャッターの門の上のまぐさで双子の塔をつなぐ
    {const V g=CityGateCenter();city.Box(CityDarkTiles,{g.x-32,58,g.z-8},{g.x+32,96,g.z+8});}
    Engine::LoadProfile::Record("mark","city: geometry built",0);
    // 材質ごとのメッシュをエンティティとして置く
    for(int i=0;i<CityMaterialCount;++i){auto& g=city.geometry[size_t(i)];if(g.indices.empty())continue;
        auto e=r.create();r.emplace<NameComponent>(e,std::string("Chrono City surface ")+surfaceIds[i]);
        auto& t=r.emplace<TransformComponent>(e);t.translate={0,0,0};t.scale={1,1,1};t.rotate={0,0,0};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=render->CreateDynamicMesh(g.vertices,g.indices);mr.useCubemap=false;
        mr.textureHandle=surfaces[size_t(i)].texture;mr.extraTextureHandles=surfaces[size_t(i)].extra;mr.shaderName=i==CityGlass?"CityGlass":"CitySurface";mr.color=tint(i);}
    // 小物はスキャンの比率を保つ。目標の幅で等倍の拡大率を決め、一番低い頂点を路面に置く
    // 読み込んだ小物のモデルのキャッシュ
    std::map<std::string,CityModel> models;
    // モデルを読み込み、大きさの範囲を求める (同じモデルは使い回す)
    auto model=[&](const std::string& id)->CityModel& {auto found=models.find(id);if(found!=models.end())return found->second;
        CityModel m=material(id);m.mesh=render->LoadObjMesh("Resources/Models/PolyHaven/"+id+"_2k.fbx");
        if(auto* source=render->GetModel(m.mesh)){m.lo={1e9f,1e9f,1e9f};m.hi={-1e9f,-1e9f,-1e9f};
            for(const auto& v:source->GetData().vertices){m.lo={std::min(m.lo.x,v.position.x),std::min(m.lo.y,v.position.y),std::min(m.lo.z,v.position.z)};
                m.hi={std::max(m.hi.x,v.position.x),std::max(m.hi.y,v.position.y),std::max(m.hi.z,v.position.z)};}
            render->PrepareDistanceLods(m.mesh);}
        return models.emplace(id,m).first->second;};
    // 小物と見た目の記録を消す
    Engine::LoadProfile::Record("mark","city: surface entities",0);
    cityProps_.clear();cityVisuals_.clear();
    // スキャンした小物を置く (required が 0 より大きければ取り込める物になる)
    auto place=[&](const std::string& id,V at,float width,float yaw,float required=0,float reward=0,LifeMorphItem item=LifeMorphItem::None){
        // 幅から拡大率を決め、一番低い頂点を地面に合わせて置く
        const auto& m=model(id);V lo=m.lo,hi=m.hi;
        float scale=width/std::max({hi.x-lo.x,hi.z-lo.z,.001f});V mid=(lo+hi)*.5f;float cs=std::cos(yaw),sn=std::sin(yaw);
        // 小物のエンティティを作る
        auto e=r.create();r.emplace<NameComponent>(e,"Chrono City prop "+id);
        auto& t=r.emplace<TransformComponent>(e);t.scale=Write(V{scale,scale,scale});t.rotate=Write(V{0,yaw,0});
        t.translate=Write(V{at.x-(mid.x*cs+mid.z*sn)*scale,at.y-lo.y*scale,at.z-(-mid.x*sn+mid.z*cs)*scale});
        // メッシュ・材質・シェーダー
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=m.mesh;mr.textureHandle=m.texture;mr.extraTextureHandles=m.extra;mr.shaderName="CitySurface";mr.useCubemap=false;
        // 見た目の中心を記録する (触手で運ぶときに使う)
        V center=V{at.x,at.y+(mid.y-lo.y)*scale,at.z};
        cityVisuals_[uint32_t(e)]={m.mesh,m.texture,m.extra,{scale,scale,scale},{0,yaw,0},center-Read(t.translate),(hi.y-lo.y)*scale,"CitySurface"};
        // 触手は背の高い小物を足元近くでつかむので、取り込みの高さの判定が路面と合う
        if(required>0)cityProps_.push_back({e,V{at.x,at.y+std::min(1.2f,(mid.y-lo.y)*scale),at.z},required,reward,item});
        return e;};
    // 作った小物 (街灯・シャッターの門) はそれぞれ 1 つのメッシュを共有し、スキャンと同じように取り込める
    // 作ったメッシュの小物を置く (取り込める物にもできる)
    auto placeMesh=[&](const char* name,uint32_t mesh,int skin,V at,float yaw,float height,float required,float reward,LifeMorphItem item=LifeMorphItem::None){
        auto e=r.create();r.emplace<NameComponent>(e,std::string("Chrono City prop ")+name);
        auto& t=r.emplace<TransformComponent>(e);t.scale={1,1,1};t.rotate=Write(V{0,yaw,0});t.translate=Write(at);
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=mesh;mr.textureHandle=surfaces[size_t(skin)].texture;mr.extraTextureHandles=surfaces[size_t(skin)].extra;
        mr.shaderName="CitySurface";mr.useCubemap=false;mr.color=tint(skin);
        cityVisuals_[uint32_t(e)]={mesh,surfaces[size_t(skin)].texture,surfaces[size_t(skin)].extra,{1,1,1},{0,yaw,0},{0,height*.5f,0},height,"CitySurface"};
        if(required>0)cityProps_.push_back({e,at+V{0,1.2f,0},required,reward,item});
        return e;};
    // ローカルの形から金属のメッシュを作る
    auto localMesh=[&](auto&& build){CityBuilder local;build(local);auto& g=local.geometry[CityMetal];return render->CreateDynamicMesh(g.vertices,g.indices);};
    // 現代的な街灯: 台座・柱・張り出した腕・照明 (ローカルの +x が腕の向き)
    uint32_t lampMesh=localMesh([](CityBuilder& b){b.Box(CityMetal,{-1,0,-1},{1,2.4f,1});b.Box(CityMetal,{-.45f,0,-.45f},{.45f,42,.45f});
        b.Box(CityMetal,{0,40.4f,-.3f},{12,41.2f,.3f});b.Box(CityMetal,{9,39.2f,-1.4f},{14.5f,40.6f,1.4f});});
    // 街灯を置く
    auto lamp=[&](V at,float yaw){placeMesh("street light",lampMesh,CityMetal,at,yaw,42,420,30);};
    // 区画 1: 脱出路地。敵はおらず、散らかった物を吸い込んで大きくなり、ハンマーで塀を壊して進む
    // 小さな散らかった物の種類・幅・報酬
    const char* smallIds[]={"trashbag","cardboard_box_01","plastic_crate_01","plastic_crate_02","metal_jerrycan","trashbag","WetFloorSign_01","wooden_crate_01"};
    const float smallWidth[]={3.4f,3.2f,3,3.4f,2.6f,3.8f,2.4f,4.6f},smallReward[]={12,12,10,10,10,12,8,14};
    // 散らかった物を順に置く (路地にタイヤは置かない。タイヤは大通りで手に入れる)
    int clutter=0;
    // 小さな散らかった物を置く
    auto scatter=[&](V at){int k=clutter%8;place(smallIds[k],at,smallWidth[k]*(.9f+.25f*CityNoise(clutter*3+1)),CityNoise(clutter*3+2)*6.283f,15,smallReward[k]);++clutter;};
    // 最初の塀の手前だけでハンマーの大きさ (本体 180) に届く量を置く
    for(V at:{V{-14,0,-1212},V{14,0,-1206},V{-15,0,-1196},V{15,0,-1188},V{-14,0,-1178},V{13,0,-1170},V{-15,0,-1160},V{15,0,-1150},
              V{-13,0,-1142},V{14,0,-1134},V{4,0,-1200},V{-4,0,-1160},V{6,0,-1140},
              // 板塀とブロック塀の間
              V{-14,0,-1100},V{15,0,-1090},V{-15,0,-1076},V{10,0,-1034},V{30,0,-1060},V{-12,0,-1040},
              // ブロック塀と金網の間、金網の先
              V{66,0,-1060},V{76,0,-1032},V{116,0,-1060},V{116,0,-1040},V{88,0,-1010},V{116,0,-1000},V{88,0,-985},V{116,0,-975},
              V{88,0,-940},V{116,0,-920},V{88,0,-900}})scatter(at);
    // 中くらいの物の種類・幅・取り込みに必要な量
    const char* mediumIds[]={"industrial_pastic_container","plastic_container","propane_tank","wooden_crate_02","hand_truck"};
    const float mediumWidth[]={6,5.6f,3.6f,6.8f,4};const float mediumRequired[]={120,100,110,140,130};
    int medium=0;
    // 中くらいの物を置く
    for(V at:{V{-14,0,-1186},V{14,0,-1104},V{36,0,-1034},V{100,0,-1046},V{90,0,-996},V{114,0,-930}}){
        int k=medium%5;place(mediumIds[k],at,mediumWidth[k],CityNoise(medium*5+3)*6.283f,mediumRequired[k],30);++medium;}
    // 配電盤とマンホール
    for(V at:{V{-15,0,-1090},V{117,0,-1020}})place("power_box_01",at,3.4f,at.x<0?1.5708f:-1.5708f,160,26);
    place("water_manhole_cover",{0,.02f,-1180},4.4f,0);place("water_manhole_cover",{102,.02f,-1000},4.4f,.7f);
    // 路地の塀とフェンス: 部品を立方体で組む。壁の延びる向きをローカルの x にする
    cityWalls_.clear();
    for(const auto& wall:CityWalls()){CityWallState state;V c=wall.Center(),along=wall.Axis(),out{along.z,0,-along.x};float half=wall.Span()*.5f+1,yaw=wall.AlongX()?0.f:1.5708f;
        auto piece=[&](float x,float y,float z,V size,int material,float roll=0){state.pieces.push_back({c+along*x+out*z+V{0,y,0},size,{0,yaw,roll},material});};
        if(wall.kind==CityWallKind::Wood){
            // 板塀: 柱の間に横板を重ね、上に笠木
            for(float x=-half;x<=half+.1f;x+=8)piece(x,6.5f,0,{1.3f,13,1.3f},0);
            for(int i=0;i<6;++i)piece((CityNoise(i+70)-.5f)*.8f,1.2f+float(i)*2.05f,.75f,{half*2,1.8f,.45f},0,(CityNoise(i+90)-.5f)*.02f);
            piece(0,13.3f,0,{half*2+1,.6f,1.8f},0);}
        else if(wall.kind==CityWallKind::Block){
            // ブロック塀: 段ごとに半分ずらして積む
            for(int course=0;course<7;++course){float shift=course%2?2.f:0.f;
                for(float x=-half+shift;x<half;x+=4){float x1=std::min(half,x+4);piece((x+x1)*.5f,1+float(course)*2,0,{x1-x-.15f,1.9f,2},2);}}
            piece(0,14.4f,0,{half*2,.8f,2.4f},2);}
        else{
            // 金網フェンス: 柱と上下の横棒、細い針金の格子
            for(float x=-half;x<=half+.1f;x+=10)piece(x,7,0,{.7f,14,.7f},3);
            for(float y:{.6f,13.6f})piece(0,y,0,{half*2,.4f,.4f},3);
            for(float x=-half+1.5f;x<half;x+=1.5f)piece(x,7,0,{.12f,13,.12f},3);
            for(float y=2;y<13.5f;y+=1.5f)piece(0,y,0,{half*2,.12f,.12f},3);}
        cityWalls_.push_back(std::move(state));}
    // 区画 2: 都心大通り。入口に金色のタイヤ (変形アイテム)、両側に街灯・止めた車・公衆電話・配電盤
    place("old_tyre",CityTireItem(),4.6f,.35f,15,12,LifeMorphItem::Tire);
    // 街灯を置く
    for(float z=avenue.z0+40;z<avenue.z1-10;z+=120){bool gap=false;for(const auto& s:sideGaps)gap|=z>s[0]-10&&z<s[1]+10;if(gap)continue;
        lamp({avenue.x0+24,0,z},0);lamp({avenue.x1-24,0,z+60},3.14159f);}
    // 路肩に止めた車
    for(V at:{V{road0+14,0,-820},V{road0+14,0,-700},V{road0+14,0,-560},V{road0+14,0,-300},V{road0+14,0,-160},V{road0+14,0,120},V{road0+14,0,280},
              V{road1-14,0,-760},V{road1-14,0,-640},V{road1-14,0,-500},V{road1-14,0,-260},V{road1-14,0,-120},V{road1-14,0,100},V{road1-14,0,300}})
        place("covered_car",at,26,0,600,45);
    // 公衆電話・配電盤
    for(V at:{V{avenue.x0+8,0,-620},V{avenue.x1-8,0,-340},V{avenue.x0+8,0,140},V{avenue.x1-8,0,220}})place("korean_public_payphone_01",at,4.4f,at.x<avenueX?1.5708f:-1.5708f,300,24);
    for(V at:{V{avenue.x0+6,0,-760},V{avenue.x1-6,0,-560},V{avenue.x0+6,0,-200},V{avenue.x1-6,0,-80},V{avenue.x0+6,0,300}})
        place(CityNoise(int(at.z))<.5f?"utility_box_01":"utility_box_02",at,5.4f,at.x<avenueX?1.5708f:-1.5708f,260,20);
    // 脇道の奥の封鎖と、置かれたコンテナ
    for(const auto& s:sideGaps){float zc=(s[0]+s[1])*.5f;
        for(float t:{-18.f,0.f,18.f}){place("concrete_road_barrier",{avenue.x0-40,0,zc+t},10,1.5708f,350,25);place("concrete_road_barrier",{avenue.x1+40,0,zc+t},10,1.5708f,350,25);}
        place("industrial_pastic_container",{avenue.x0-20,0,zc-20},6,CityNoise(int(zc))*6.283f,120,22);place("industrial_pastic_container",{avenue.x1+20,0,zc+20},6,CityNoise(int(zc)+5)*6.283f,120,22);}
    // マンホール
    for(V at:{V{avenueX-50,0,-600},V{avenueX+50,0,-200},V{avenueX-50,0,200}})place("water_manhole_cover",at+V{0,.02f,0},4.4f,0);
    // 道路封鎖のバリケード: 通路をふさぐ柱と合板の囲い、後ろに積んだ木箱。タイヤの溜め突進で壊す
    // バリケードの柱・板・斜めの補強・木箱
    cityBarricade_.clear();const V bc=CityBarricadeCenter();const float bw=(CityBarricade().x1-CityBarricade().x0)*.5f;
    for(float x=-bw;x<=bw+.1f;x+=bw*.5f)cityBarricade_.push_back({bc+V{x,7,0},{1.6f,14,1.6f},{0,0,0}});
    for(int i=0;i<6;++i){float y=1.6f+float(i)*2.25f;cityBarricade_.push_back({bc+V{(CityNoise(i)-.5f)*2.f,y,.8f+float(i%2)*.4f},{bw*2+2,1.7f,.55f},{0,0,(CityNoise(i+9)-.5f)*.04f}});}
    for(float s:{-1.f,1.f})for(float k:{.25f,.75f})cityBarricade_.push_back({bc+V{s*bw*k,7,1.6f},{bw*.55f,1.4f,.5f},{0,0,s*.48f}});
    for(int i=0;i<9;++i)cityBarricade_.push_back({bc+V{-bw+5+float(i)*(bw*2-10)/8,2.4f,-3.5f},{4.8f,4.8f,4.8f},{0,CityNoise(i+31)*.6f,0}});
    // 封鎖の手前の標識
    for(float s:{-1.f,1.f})place("WetFloorSign_01",bc+V{s*(bw-6),0,-14},2.6f,s*.4f,15,8);
    // 区画 3: 大火事現場。入口の消火栓 (変形アイテム)・焼けた車・崩れた瓦礫
    // 消火栓のメッシュ (円柱を組み合わせる)
    uint32_t hydrantMesh=localMesh([](CityBuilder& b){
        // 円柱 (先細りも可) を作る
        auto cylinder=[&](V origin,V axis,V right,V up,float radius,float endRadius,float length){
            constexpr int sides=16;for(int i=0;i<sides;++i){float a=float(i)*6.2831853f/sides,c=float(i+1)*6.2831853f/sides;
                V ra=right*std::cos(a)+up*std::sin(a),rc=right*std::cos(c)+up*std::sin(c),end=origin+axis*length;
                b.Polygon(CityMetal,{origin+ra*radius,origin+rc*radius,end+rc*endRadius,end+ra*endRadius},Unit(ra+rc));
                b.Polygon(CityMetal,{origin,origin+rc*radius,origin+ra*radius},axis*-1.f);
                b.Polygon(CityMetal,{end,end+ra*endRadius,end+rc*endRadius},axis);}};
        cylinder({0,0,0},{0,1,0},{1,0,0},{0,0,1},1.75f,1.75f,.5f);
        cylinder({0,.5f,0},{0,1,0},{1,0,0},{0,0,1},1.15f,1.15f,3.5f);
        cylinder({0,4,0},{0,1,0},{1,0,0},{0,0,1},1.2f,.2f,1.1f);
        cylinder({-2.2f,2.9f,0},{1,0,0},{0,1,0},{0,0,1},.45f,.45f,4.4f);
        cylinder({0,3.1f,1},{0,0,1},{1,0,0},{0,1,0},.5f,.5f,1.2f);
    });
    // 消火栓を置く (赤くする)
    for(V at:{V{40,0,fire.z0+18},V{164,0,fire.z0+18},V{fire.x0+12,0,700},V{fire.x1-12,0,760}}){auto e=placeMesh("fire hydrant",hydrantMesh,CityMetal,at,0,5.1f,15,24,LifeMorphItem::Hydrant);
        r.get<MeshRendererComponent>(e).color={1.8f,.22f,.08f,1};}
    // 焼けた車 (黒く煤けた色) と崩れた瓦礫
    for(V at:{V{-60,0,600},V{150,0,690},V{270,0,600},V{-120,0,790},V{60,0,880},V{320,0,820}}){
        auto e=place("covered_car",at,26,CityNoise(int(at.x*3+at.z))*6.283f,600,45);r.get<MeshRendererComponent>(e).color={.22f,.19f,.17f,1};}
    for(V at:{V{-140,0,560},V{-30,0,690},V{110,0,760},V{240,0,680},V{340,0,560},V{-130,0,880},V{220,0,880},V{30,0,560}}){
        place(CityNoise(int(at.x+at.z))<.5f?"boulder_01":"wooden_crate_02",at,CityNoise(int(at.z))<.5f?7.f:6.f,CityNoise(int(at.x))*6.283f,140,30);}
    for(V at:{V{-150,0,620},V{350,0,720},V{-40,0,880},V{250,0,500}})place("propane_tank",at,3.6f,0,110,30);
    for(float x:{fire.x0+40,fire.x1-40})for(float z:{fire.z0+120,fire.z1-120})lamp({x,0,z},x<avenueX?0.f:3.14159f);
    // 鋼鉄のシャッターの門: 火事場を制圧した後に取り込むか壊す
    // シャッターの門のメッシュ (板と枠)
    uint32_t gateMesh=[&]{CityBuilder local;local.Box(CityShutter,{-31,0,-1.2f},{31,58,1.2f});local.Box(CityShutter,{-32,52,-3.5f},{32,58,3.5f});
        auto& g=local.geometry[CityShutter];return render->CreateDynamicMesh(g.vertices,g.indices);}();
    // 門を置き、当たり判定を付ける
    cityGateEntity_=placeMesh("shutter gate",gateMesh,CityShutter,CityGateCenter(),0,58,0,0);
    {auto& collider=r.emplace<BoxColliderComponent>(cityGateEntity_);collider.size={64,58,2.4f};collider.center={0,29,0};r.emplace<TagComponent>(cityGateEntity_).tag=TagType::Wall;}
    // 区画 4: 事故多発交差点。四隅の街灯・衝突した車・倒れたバリア・像
    // 交差点の四隅と辺の街灯
    for(float sx:{-1.f,1.f})for(float sz:{-1.f,1.f}){V corner{cx+sx*(h+10),0,cz+sz*(h+10)};
        lamp(corner,sx>0?(sz>0?-2.356f:2.356f):(sz>0?-.785f:.785f));
        lamp(corner+V{sx*90,0,0},sz>0?-1.5708f:1.5708f);lamp(corner+V{0,0,sz*90},sx>0?3.14159f:0);}
    // 放置された車 (交差点の中だけ)
    for(int i=0;i<14;++i){float a=float(i)*.4488f,radius=40+CityNoise(i*7)*120;V at{cx+std::cos(a)*radius,0,cz+std::sin(a)*radius};
        if(std::abs(at.x-cx)>h&&std::abs(at.z-cz)>h)continue;place("covered_car",at,26,a+CityNoise(i*11)*1.2f,600,45);}
    // 衝突した車の組: 2 台がぶつかった向きで重なり、上で非常灯が点滅する
    {int k=0;for(const auto& [at,yaw]:crashes){V d{std::sin(yaw),0,std::cos(yaw)},side{d.z,0,-d.x};
        place("covered_car",at,26,yaw,600,45);place("covered_car",at+d*14+side*10,26,yaw+1.3f+CityNoise(k*5)*.5f,600,45);
        for(V beacon:{at+V{0,7.5f,0},at+d*14+side*10+V{0,7.5f,0}})cityGlows_.push_back({beacon,{1,0,0},{0,1,0},{1.4f,.7f,1.4f},{1,.55f,.05f,1},2,float(k)*1.3f});
        place(k%2?"concrete_road_barrier_02":"concrete_road_barrier",at-side*18+d*6,10,yaw+.9f,350,25);++k;}}
    // 大砲の周りのバリア
    for(const auto& spawn:CityStepSpawns(5))if(spawn.kind==CityEnemy::Cannon){V cannon=spawn.at;
        for(int k=0;k<3;++k){float a=float(k)*1.0472f+(cannon.z>cz?3.6652f:.5236f)+(cannon.x>cx?.5236f:0);
            place(k==1?"concrete_road_barrier_02":"concrete_road_barrier",cannon+V{std::cos(a)*20,0,std::sin(a)*20},10,a+1.5708f,350,25);}}
    // 遊べる交差点の端で、延びる道路を封鎖する
    // 延びる道路の封鎖
    for(float t=-60;t<=60;t+=20){place("concrete_road_barrier",{cx+t,0,plaza.z1-6},10,0);place("concrete_road_barrier",{plaza.x0+6,0,cz+t},10,1.5708f);place("concrete_road_barrier",{plaza.x1-6,0,cz+t},10,1.5708f);}
    // 公衆電話・配電盤
    for(V at:{V{plaza.x0+22,0,plaza.z0+20},V{plaza.x1-18,0,plaza.z0+20},V{plaza.x0+22,0,plaza.z1-16},V{plaza.x1-18,0,plaza.z1-16}})place("korean_public_payphone_01",at,4.4f,0,300,24);
    for(V at:{V{cx-118,0,cz-140},V{cx+118,0,cz-140},V{cx-118,0,cz+140},V{cx+118,0,cz+140},V{cx-338,0,cz-100},V{cx+318,0,cz+100}})place(CityNoise(int(at.x+at.z))<.5f?"utility_box_01":"utility_box_02",at,5.4f,0,260,20);
    // 変形アイテムの信号機: 台座・柱・灯器の箱 (信号機の形を 1.4 倍にした寸法)。交差点の入口に置く
    signalItemAt_=CitySignalItem();
    {CityBuilder local;const float k=SignalItemScale;local.Box(CityMetal,{-1.2f*k,0,-1.2f*k},{1.2f*k,.4f*k,1.2f*k});local.Box(CityMetal,{-.42f*k,.4f*k,-.42f*k},{.42f*k,5.2f*k,.42f*k});
        local.Box(CityMetal,{-2.3f*k,(SignalHousingY-.8f)*k,-.55f*k},{2.3f*k,(SignalHousingY+.8f)*k,.55f*k});auto& g=local.geometry[CityMetal];
        auto e=placeMesh("traffic signal",render->CreateDynamicMesh(g.vertices,g.indices),CityMetal,signalItemAt_,0,SignalHeight*k,15,24,LifeMorphItem::Signal);
        r.get<MeshRendererComponent>(e).color={.32f,.34f,.36f,1};}
    // 台座と像
    {V pedestal{plaza.x0+102,0,plaza.z0+30};CityBuilder local;local.Box(CityMetal,{-8,0,-8},{8,6,8});auto& g=local.geometry[CityMetal];
        placeMesh("statue pedestal",render->CreateDynamicMesh(g.vertices,g.indices),CityConcrete,pedestal,0,6,0,0);
        place("gothic_statue",pedestal+V{0,6,0},12,.6f,2200,260);}
    // 敵のモデル (ネズミと大砲)
    Engine::LoadProfile::Record("mark","city: props placed",0);
    cityRat_=model("street_rat");cityCannon_=model("cannon_01");
    // 板・ゴーレムの部品・矢・破片・光る看板に共通で使う単位立方体
    std::vector<Engine::VertexData> cube;std::vector<uint32_t> cubeIndex;
    // 単位立方体のメッシュ (面ごとに 0〜1 の UV)
    {CityBuilder unit;unit.tile.fill(1);unit.Box(0,{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true);cube=unit.geometry[0].vertices;cubeIndex=unit.geometry[0].indices;
        for(auto& v:cube){// 面ごとに 0〜1 の UV
            float ax=std::abs(v.normal.x),ay=std::abs(v.normal.y);
            v.texcoord=ay>.5f?DirectX::XMFLOAT2{v.position.x+.5f,v.position.z+.5f}:ax>.5f?DirectX::XMFLOAT2{v.position.z+.5f,.5f-v.position.y}:DirectX::XMFLOAT2{v.position.x+.5f,.5f-v.position.y};}}
    cityCube_=render->CreateDynamicMesh(cube,cubeIndex);
    // 門・弾・破片などの状態を初期化する
    cityGates_={};cityBarricadeHealth_=CityBarricadeHealth();cityGateHealth_=CityGateHealth();cityBarricadeShake_=cityGateShake_=0;
    cityShots_.clear();cityDebris_.clear();cityStomps_.clear();cityGateProp_=-1;cityAnnounced_=-1;ResetCityHazards();
}
// 段階の敵を出す
void ChronoSystem::SpawnCityStep(int step){
    using namespace Chrono;
    // 段階の敵を作り、地面の高さに置く
    for(const auto& spawn:CityStepSpawns(step)){
        auto enemy=CitySpawnEnemy(spawn,uint32_t(swarm_.size()+1),step);enemy.at.y=InkGround(enemy.at)+enemy.scale;swarm_.push_back(enemy);}
}
// 現在の段階で生きている敵の数
int ChronoSystem::CityStepAlive()const{
    // 今の段階で生きている敵を数える
    int alive=0;for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&enemy.step==life_.step)++alive;return alive;
}
// 路地の壁の体力と火事場の炎を最初の状態に戻す
void ChronoSystem::ResetCityHazards(){
    using namespace Chrono;
    for(size_t i=0;i<cityWalls_.size()&&i<CityWalls().size();++i){cityWalls_[i].health=CityWalls()[i].health;cityWalls_[i].shake=0;cityWalls_[i].hit=0;}
    cityFires_.clear();for(const auto& spot:CityFires())cityFires_.push_back({spot.at,spot.radius,CityFireHealth(),0});
    cityHammerAnnounced_=false;
}
// 路地の壁を壊す (部品を破片にして飛ばし、通れるようにする。すべて壊れたら大通りへ)
void ChronoSystem::BreakCityWall(size_t index){
    using namespace Chrono;
    if(index>=CityWalls().size()||cityGates_.WallOpen(index))return;
    cityGates_.wallsOpen|=1u<<index;
    // 部品の一部をそのまま破片にして飛ばす (長い部品は短く切る)
    if(index<cityWalls_.size()){int i=0;for(const auto& piece:cityWalls_[index].pieces){if(i++%2)continue;
        float a=CityNoise(i*7+int(index)*13)*6.283f,u=CityNoise(i*13+int(index));V size=piece.size;size.x=std::min(size.x,6.f);
        cityDebris_.push_back({piece.at,V{std::cos(a)*6*(.4f+u),10+u*14,std::sin(a)*6*(.4f+u)},V{u*6,a,u*4},size,piece.rotate,0,piece.material});}}
    const auto& wall=CityWalls()[index];int left=0;for(size_t i=0;i<CityWalls().size();++i)if(!cityGates_.WallOpen(i))++left;
    life_.message=left?wall.name+"を破壊！ 残り "+std::to_string(left)+" か所":wall.name+"を破壊！ 路地を抜けて大通りへ";life_.messageAge=3;
    auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[2],false,.7f*audio->GetMasterSEVolume(),.55f);
}
// バリケードか門を壊す (破片を飛ばし、通れるようにしてメッセージを出す)
void ChronoSystem::BreakCityGate(entt::registry& r,bool barricade,bool absorbed){
    using namespace Chrono;
    // 破片を飛ばす
    auto burst=[&](V at,V size,int material,int count,float spread){
        for(int i=0;i<count;++i){float a=CityNoise(i*7+int(at.x))*6.283f,u=CityNoise(i*13+int(at.z));
            V velocity{std::cos(a)*spread*(.4f+u),14+u*18,std::sin(a)*spread*(.4f+u)+16};
            cityDebris_.push_back({at+V{(CityNoise(i*3)-.5f)*size.x,CityNoise(i*5)*size.y,0},velocity,V{u*6,a,u*4},
                V{1.2f+u*3,.5f+u*.6f,.5f+u*.4f},{},0,material});}
    };
    // バリケード: 開けて破片を飛ばす
    if(barricade){cityGates_.barricadeOpen=true;burst(CityBarricadeCenter()+V{0,4,0},{44,14,1},0,26,10);
        life_.message="バリケード突破！ 大火事現場へ";life_.messageAge=3;}
    // 門: 開けて当たり判定と表示を消す (取り込んだときは破片なし)
    else{cityGates_.gateOpen=true;
        if(auto* collider=r.try_get<BoxColliderComponent>(cityGateEntity_))collider->enabled=false;
        if(r.valid(cityGateEntity_))r.get<MeshRendererComponent>(cityGateEntity_).enabled=false;
        if(cityGateProp_>=0&&size_t(cityGateProp_)<life_.props.size())life_.props[size_t(cityGateProp_)].active=false;
        if(!absorbed)burst(CityGateCenter()+V{0,10,0},{60,50,1},1,30,14);
        life_.message=absorbed?"シャッターごと吸い込んだ！ 事故多発交差点へ":"シャッターを破壊！ 事故多発交差点へ";life_.messageAge=3;}
    // 壊れた音
    auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[2],false,.7f*audio->GetMasterSEVolume(),.55f);
}
// スライムの武器が路地の壁・バリケード・門に当たったか (路地の壁はハンマーだけで壊せる。門は火事場を制圧した後だけ)
void ChronoSystem::CityWeaponHits(entt::registry& r,GameContext& ctx,const Chrono::LifeBody& b,int pulse,uint64_t hitKey){
    using namespace Chrono;
    // 路地の壁: 壁に沿って並べた点のどこかに武器が触れたら、ハンマーなら体力を減らす
    for(size_t i=0;i<CityWalls().size()&&i<cityWalls_.size();++i){if(cityGates_.WallOpen(i)||cityWalls_[i].hit==hitKey)continue;
        const auto& wall=CityWalls()[i];auto& state=cityWalls_[i];bool touched=false;
        for(float s:{-.35f,0.f,.35f})for(float y:{2.f,6.f})touched|=LifeWeaponTouches(b,wall.Center()+wall.Axis()*(s*wall.Span())+V{0,y,0},6,pulse);
        if(!touched)continue;state.hit=hitKey;
        if(b.action!=LifeAction::Hammer){life_.message=wall.name+"はハンマーで叩き壊せる";life_.messageAge=2;continue;}
        state.health-=LifeWeaponDamage(b);state.shake=.35f;
        LifeImpact(ctx,ImpactStructureHit,wall.Center()+V{0,4,0});swarmEffects_.push_back({wall.Center()+V{0,4,0},0,8,2});
        auto* audio=Engine::Audio::GetInstance();audio->Play(hitSound_,false,.55f*audio->GetMasterSEVolume(),.8f);
        if(state.health<=0){BreakCityWall(i);LifeImpact(ctx,ImpactStructureBreak,wall.Center()+V{0,4,0});}
        else{life_.message=wall.name+" 残り "+std::to_string(int(std::ceil(state.health/wall.health*100)))+"%";life_.messageAge=1.2f;}}
    // 武器が中心から左右に並んだ点のどこかに触れたか
    auto touches=[&](V center,float y,float radius){for(float x:{-16.f,0.f,16.f})if(LifeWeaponTouches(b,center+V{x,y,0},radius,pulse))return true;return false;};
    // バリケード: ハンマーなら体力を減らし、尽きたら壊す。それ以外の武器は案内を出す
    if(!cityGates_.barricadeOpen&&cityBarricadeHit_!=hitKey&&touches(CityBarricadeCenter(),6,8)){cityBarricadeHit_=hitKey;
        if(b.action==LifeAction::Hammer){cityBarricadeHealth_-=LifeWeaponDamage(b);cityBarricadeShake_=.35f;
            LifeImpact(ctx,ImpactStructureHit,CityBarricadeCenter()+V{0,4,0});swarmEffects_.push_back({CityBarricadeCenter()-V{0,0,4},0,10,2});
            if(cityBarricadeHealth_<=0){BreakCityGate(r,true,false);LifeImpact(ctx,ImpactStructureBreak,CityBarricadeCenter()+V{0,4,0});}
            else{life_.message="バリケード 残り "+std::to_string(int(std::ceil(cityBarricadeHealth_/CityBarricadeHealth()*100)))+"%";life_.messageAge=1.2f;}}
        else{life_.message="道路封鎖のバリケードはタイヤの溜め突進で壊せる";life_.messageAge=2;}
    }
    // 門: 騎士団を倒すまでは案内だけ。それ以外は体力を減らし、尽きたら壊す
    if(!cityGates_.gateOpen&&cityGateHit_!=hitKey&&(touches(CityGateCenter(),10,10)||touches(CityGateCenter()+V{0,0,-3},30,10))){cityGateHit_=hitKey;
        if(!cityGateUnlocked_){life_.message="兵士がシャッターを守っている / 先に火事場を制圧しよう";life_.messageAge=2;return;}
        cityGateHealth_-=LifeWeaponDamage(b)*(b.action==LifeAction::Hammer?1.4f:1.f);cityGateShake_=.35f;
        LifeImpact(ctx,ImpactStructureHit,CityGateCenter()+V{0,10,0});swarmEffects_.push_back({CityGateCenter()-V{0,0,6},0,14,2});
        if(cityGateHealth_<=0){BreakCityGate(r,false,false);LifeImpact(ctx,ImpactStructureBreak,CityGateCenter()+V{0,10,0});}
        else{life_.message="封鎖シャッター 残り "+std::to_string(int(std::ceil(cityGateHealth_/CityGateHealth()*100)))+"% / 本体"+std::to_string(int(CityGateAbsorbMass()))+"ならシャッターごと吸い込める";life_.messageAge=1.4f;}
    }
}
// タイヤの突進がバリケードや門に当たったか (溜めが強いほど大きく削る。門は火事場を制圧した後だけ)
void ChronoSystem::CityTireDashHits(entt::registry& r,GameContext& ctx,const Chrono::LifeBody& b,uint64_t hitKey){
    using namespace Chrono;
    // 体の中心から範囲の矩形までの水平の距離
    float reach=LifeTireRadius*LifeScale(b.mass)+1.5f;
    auto touching=[&](const CityRect& box){float dx=std::max({box.x0-b.at.x,0.f,b.at.x-box.x1}),dz=std::max({box.z0-b.at.z,0.f,b.at.z-box.z1});return std::sqrt(dx*dx+dz*dz)<reach;};
    float damage=LifeTireDashGateDamage(b.weapon.dashPower);
    if(!cityGates_.barricadeOpen&&cityBarricadeHit_!=hitKey&&touching(CityBarricade())){cityBarricadeHit_=hitKey;
        cityBarricadeHealth_-=damage;cityBarricadeShake_=.35f;
        LifeImpact(ctx,ImpactStructureHit,CityBarricadeCenter()+V{0,4,0},{0,1,0},1.2f);swarmEffects_.push_back({CityBarricadeCenter()-V{0,0,4},0,10,2});
        if(cityBarricadeHealth_<=0){BreakCityGate(r,true,false);LifeImpact(ctx,ImpactStructureBreak,CityBarricadeCenter()+V{0,4,0});}
        else{life_.message="バリケード 残り "+std::to_string(int(std::ceil(cityBarricadeHealth_/CityBarricadeHealth()*100)))+"% / もっと溜めて突進しよう";life_.messageAge=1.4f;}}
    if(!cityGates_.gateOpen&&cityGateHit_!=hitKey&&touching(CityIronGate())){cityGateHit_=hitKey;
        if(!cityGateUnlocked_){life_.message="兵士がシャッターを守っている / 先に火事場を制圧しよう";life_.messageAge=2;return;}
        cityGateHealth_-=damage*1.4f;cityGateShake_=.35f;
        LifeImpact(ctx,ImpactStructureHit,CityGateCenter()+V{0,10,0});swarmEffects_.push_back({CityGateCenter()-V{0,0,6},0,14,2});
        if(cityGateHealth_<=0){BreakCityGate(r,false,false);LifeImpact(ctx,ImpactStructureBreak,CityGateCenter()+V{0,10,0});}
        else{life_.message="封鎖シャッター 残り "+std::to_string(int(std::ceil(cityGateHealth_/CityGateHealth()*100)))+"%";life_.messageAge=1.4f;}}
}
// 街のステージの更新: エリアの表示・門の開放・ヒント・段階の進行・破片・敵の弾・踏みつけ
void ChronoSystem::UpdateCityStage(entt::registry& r,Chrono::Player& p,GameContext& ctx,float dt){
    using namespace Chrono;auto& body=life_.bodies[0];
    // 揺れとヒントの時間を減らす
    cityBarricadeShake_=std::max(0.f,cityBarricadeShake_-dt);cityGateShake_=std::max(0.f,cityGateShake_-dt);cityHintClock_=std::max(0.f,cityHintClock_-dt);
    for(auto& wall:cityWalls_)wall.shake=std::max(0.f,wall.shake-dt);
    // 本体がハンマーに変形できる大きさになったら、一度だけ知らせて形を選んでおく
    if(!cityHammerAnnounced_&&LifeCanMorph(body,LifeAction::Hammer)){cityHammerAnnounced_=true;life_.form=LifeAction::Hammer;
        life_.message=UI::UsingPad()?"ハンマー変形 解放！ 十字キー↑で変形し、RTで叩きつけて塀を壊そう":"ハンマー変形 解放！ 1で変形し、左クリックで叩きつけて塀を壊そう";life_.messageAge=4;}
    // 新しいエリアに入ったら名前を表示する
    auto area=CityAreaAt(body.at);
    if(int(area)!=cityAnnounced_){cityAnnounced_=int(area);
        life_.message="AREA "+std::to_string(int(area)+1)+"  "+CityAreaName(area);life_.messageAge=3;}
    // 今の段階で生きている敵の数
    int alive=CityStepAlive();
    // 火事場の兵士が倒れると、鉄の門が取り込める (または壊せる) 対象になる
    if(!cityGateUnlocked_&&(life_.step>4||(life_.step==4&&alive==0))){cityGateUnlocked_=true;
        if(cityGateProp_>=0&&size_t(cityGateProp_)<life_.props.size())life_.props[size_t(cityGateProp_)].active=true;
        life_.message="シャッターの守りが解けた！ 本体"+std::to_string(int(CityGateAbsorbMass()))+"でシャッターごと吸い込む / 武器で破壊";life_.messageAge=4;}
    // 門が取り込まれたら壊す
    if(cityGateUnlocked_&&!cityGates_.gateOpen&&cityGateProp_>=0&&size_t(cityGateProp_)<life_.props.size()&&!life_.props[size_t(cityGateProp_)].active)BreakCityGate(r,false,true);
    // 閉じた門に近づいたら、大きさに応じたヒントを出す
    if(cityHintClock_<=0&&life_.messageAge<=0){
        // 路地の壁: 大きさが足りなければ吸収を、足りればハンマーへの変形を促す
        size_t nearWall=CityWalls().size();for(size_t i=0;i<CityWalls().size();++i)if(!cityGates_.WallOpen(i)&&LifeDistance(body.at,CityWalls()[i].Center())<36)nearWall=i;
        if(nearWall<CityWalls().size()){cityHintClock_=4;const auto& name=CityWalls()[nearWall].name;
            life_.message=!LifeCanMorph(body,LifeAction::Hammer)?"ゴミを吸い込んで本体"+std::to_string(int(LifeHammerMass))+"まで育つと、ハンマーで"+name+"を壊せる"
                :body.action!=LifeAction::Hammer?"1でハンマーに変形し、"+name+"を叩き壊そう":std::string("左クリックで叩きつけ！ 押し続けると連続で叩く");life_.messageAge=3;}
        else if(!cityGates_.barricadeOpen&&LifeDistance(body.at,CityBarricadeCenter())<40){cityHintClock_=4;
            life_.message=!body.tireUnlocked?std::string("大通りの入口の金色のタイヤを吸収してタイヤ変形を解放しよう"):body.action!=LifeAction::Tire?std::string("2でタイヤに変形し、溜め突進でバリケードを壊そう"):std::string("左長押しで回転を溜め、離して突進！ 溜めきればひと突きで壊せる");life_.messageAge=3;}
        else if(!cityGates_.gateOpen&&LifeDistance(body.at,CityGateCenter())<50){cityHintClock_=4;
            life_.message=!cityGateUnlocked_?std::string("火事場の兵士を倒すとシャッターの守りが解ける"):body.mass<CityGateAbsorbMass()?"本体"+std::to_string(int(CityGateAbsorbMass()))+"でシャッターごと吸い込める / 今は武器で破壊":std::string("右クリック／Xでシャッターごと吸い込もう");life_.messageAge=3;}
    }
    // 門が開いていて敵がいなければ、少し待って次の段階へ進む
    // 区画の出口: 路地の壁 (段階 1)・バリケード (段階 3)・シャッター (段階 5) が開くまで次へ進まない
    bool gate=life_.step==0?cityGates_.AlleyOpen():life_.step==2?cityGates_.barricadeOpen:life_.step==4?cityGates_.gateOpen:true;
    if(alive==0&&gate&&life_.step+1<CityStepCount){
        life_.stepDelay=std::max(0.f,life_.stepDelay-dt);
        if(life_.stepDelay<=0){++life_.step;life_.stepDelay=2;SpawnCityStep(life_.step);
            life_.message="段階 "+std::to_string(life_.step+1)+" / "+std::to_string(CityStepCount)+"  "+CityStepInfo(life_.step).title;life_.messageAge=3.5f;}
    }
    // 火事場の炎: 触れると燃える (被弾の無敵時間ごとにダメージ)。消火栓の放水で小さくなり、消える
    for(auto& fire:cityFires_){fire.hiss=std::max(0.f,fire.hiss-dt);if(fire.health<=0)continue;
        float size=fire.radius*(.45f+.55f*fire.health/CityFireHealth());
        if(body.mass>0&&!body.down&&LifeDistance(body.at,fire.at)<size*.8f+LifeRadii(body,life_.recipe).x*.5f){Damage(r,p,CityFireDamage(),ctx,fire.at);
            if(cityHintClock_<=0){cityHintClock_=3;life_.message=body.hydrantUnlocked?"熱い！ 消火栓に変形して放水で火を消そう":"熱い！ 火事場の入口の金色の消火栓を吸収しよう";life_.messageAge=2;}}}
    // 破片は重力で落ち、地面で弾む
    for(auto& d:cityDebris_){d.age+=dt;d.velocity.y-=34*dt;d.at=d.at+d.velocity*dt;d.rotate=d.rotate+d.spin*dt;
        if(d.at.y<d.size.y*.5f){d.at.y=d.size.y*.5f;d.velocity=d.velocity*.35f;d.velocity.y=std::abs(d.velocity.y)*.3f;d.spin=d.spin*.5f;}}
    cityDebris_.erase(std::remove_if(cityDebris_.begin(),cityDebris_.end(),[](const CityDebris& d){return d.age>4;}),cityDebris_.end());
    // スライムの本体か分身に当たったらダメージ
    auto hurt=[&](V at,float radius,float damage){
        // 弾は本体 (体力と分裂) に当たるか、分身をすり減らす
        float reach=LifeRadii(body,life_.recipe).x*.85f+radius;
        if(body.mass>0&&!body.down&&LifeDistance(body.at,at)<reach&&std::abs(at.y-body.at.y)<reach+2){Damage(r,p,damage,ctx,at);return true;}
        for(size_t i=1;i<life_.bodies.size();++i){auto& c=life_.bodies[i];if(c.mass<=0||c.down)continue;
            float cr=2.4f*LifeScale(c.mass)+radius;if(LifeDistance(c.at,at)<cr&&std::abs(at.y-c.at.y)<cr+2){
                c.health=std::max(0.f,c.health-damage*.6f);if(c.health<=0){c.down=true;LifeReturnToSlime(c);++life_.revision;}return true;}}
        return false;};
    // 敵の弾: 防御の水の幕に当たれば止まり、本体に当たればダメージ
    for(auto& shot:cityShots_){V previous=shot.at;shot.age+=dt;shot.velocity.y-=(shot.shell?30.f:9.f)*dt;shot.at=shot.at+shot.velocity*dt;
        bool blocked=false;for(const auto& sample:ctx.renderer->GetHydrantWaterSnapshot().particles){
            V at{sample.at.x,sample.at.y,sample.at.z},velocity{sample.velocity.x,sample.velocity.y,sample.velocity.z};
            if(sample.type>1.15f&&LifeHydrantLiveWater(sample.type,sample.contactAge,velocity)&&
                LifeWaterSegmentDistance(previous,shot.at,at-velocity*std::min(.05f,dt*2),at)<(shot.shell?1.7f:1.1f)){
                blocked=true;shot.age=99;++life_.waterBlocks;if(swarmEffects_.size()<128)swarmEffects_.push_back({shot.at,0,2,2});break;}}
        if(blocked)continue;
        float ground=InkGround(shot.at);
        // 砲弾は地面で爆発して範囲ダメージ
        if(shot.shell){if(shot.at.y<=ground){shot.age=99;swarmEffects_.push_back({shot.at,0,11,1});LifeImpact(ctx,ImpactShellBlast,shot.at);
                hurt({shot.at.x,body.at.y,shot.at.z},9,CityShotDamage(true));}}
        // 矢は当たるか地面・壁で消える
        else if(hurt(shot.at,.6f,CityShotDamage(false))||shot.at.y<=ground||!CityWalkable({shot.at.x,0,shot.at.z},.1f,{true,true}))shot.age=99;
    }
    // 古い弾を消す
    cityShots_.erase(std::remove_if(cityShots_.begin(),cityShots_.end(),[](const CityShot& s){return s.age>4;}),cityShots_.end());
    // 守護者の踏みつけ: 0.85 秒後に範囲にダメージ
    for(auto& stomp:cityStomps_){float previous=stomp.age;stomp.age+=dt;
        if(previous<.85f&&stomp.age>=.85f){swarmEffects_.push_back({stomp.at,0,stomp.radius,1});LifeImpact(ctx,ImpactGuardianStomp,stomp.at);
            hurt({stomp.at.x,body.at.y,stomp.at.z},stomp.radius,22);}}
    cityStomps_.erase(std::remove_if(cityStomps_.begin(),cityStomps_.end(),[](const CityStomp& s){return s.age>1.2f;}),cityStomps_.end());
}
// 街の敵の更新: 種類ごとの行動 (大砲・巡回・弓兵・突進と守護者) と、本体や分身への攻撃
void ChronoSystem::UpdateCityEnemies(entt::registry& r,Chrono::Player& p,GameContext& ctx,float dt){
    // 攻撃中の数と、同時に攻撃できる数
    using namespace Chrono;auto& ink=r.get<InkPlayer>(player_);auto& body=life_.bodies[0];
    int active=0;for(const auto& enemy:swarm_)if(SwarmAlive(enemy)&&(enemy.phase==SwarmPhase::Windup||enemy.phase==SwarmPhase::Dash))++active;
    const int maxActive=life_.step>=5?4:2;
    // 生きている敵の数を数え直す
    ink.swarmAlive=0;
    // 一番近いスライムの体を狙う
    for(auto& enemy:swarm_)if(SwarmAlive(enemy)){
        // 敵の種類
        ++ink.swarmAlive;auto kind=CityKind(enemy);
        int victim=0;float nearest=LifeDistance(body.at,enemy.at);
        for(size_t i=1;i<life_.bodies.size();++i)if(life_.bodies[i].mass>0&&!life_.bodies[i].down){float d=LifeDistance(life_.bodies[i].at,enemy.at);if(d<nearest){nearest=d;victim=int(i);}}
        // 狙う体と、前の位置と状態
        auto& targetBody=life_.bodies[size_t(victim)];V before=enemy.at;auto phase=enemy.phase;
        // よろけの時間を減らす
        enemy.stagger=std::max(0.f,enemy.stagger-dt);
        // 近づいたら気付く
        if(!enemy.alerted&&nearest<CityAlertRange(kind)){enemy.alerted=true;
            // 侵入者を見つけたら、同じ巡回の近くの仲間にも知らせる
            for(auto& other:swarm_)if(SwarmAlive(other)&&!other.alerted&&LifeDistance(other.at,enemy.at)<45)other.alerted=true;}
        // 触手につかまれているか
        bool held=LifeTargetReserved(life_,enemy.id);
        // 狙う体への向きと距離
        V toward=targetBody.at-enemy.at;toward.y=0;float distance=Length(toward);V direction=distance>.01f?toward*(1/distance):enemy.heading;
        // 時間を進める関数 (出現が終わったら追跡へ)
        auto idleTick=[&](){enemy.age+=dt;enemy.clock+=dt;enemy.flash=std::max(0.f,enemy.flash-dt);
            if(enemy.phase==SwarmPhase::Spawn&&enemy.clock>=.6f)ChangeSwarmPhase(enemy,SwarmPhase::Chase);};
        // 触手につかまれている間は動かない
        if(held){}
        // 信号機の赤で止められている間は、移動も攻撃もしない。青で並べられたら列の位置へ寄る
        else if(enemy.signalStop>0){enemy.flash=std::max(0.f,enemy.flash-dt);enemy.signalStop=std::max(0.f,enemy.signalStop-dt);
            if(enemy.signalLined){V goal=enemy.signalLine-enemy.at;goal.y=0;enemy.at=enemy.at+goal*(1-std::exp(-SignalLineRate*dt));}
            if(enemy.signalStop<=0)enemy.signalLined=false;}
        // 大砲: 向きを合わせて放物線の弾を撃つ
        else if(kind==CityEnemy::Cannon){idleTick();
            enemy.heading=Unit(Lerp(enemy.heading,direction,1-std::exp(-2.5f*dt)));
            if(enemy.alerted&&enemy.phase!=SwarmPhase::Spawn){enemy.shotClock-=dt;
                if(enemy.shotClock<=0&&distance<170){enemy.shotClock=4+SwarmVariation(enemy.id,enemy.attacks++)*1.5f;
                    V muzzle=enemy.at+enemy.heading*(enemy.scale*1.6f)+V{0,enemy.scale*.6f,0};V target=targetBody.at;target.y=InkGround(target);
                    cityShots_.push_back({muzzle,CityShellVelocity(muzzle,target,1.7f),0,true,target});enemy.flash=.25f;
                    LifeImpact(ctx,ImpactCannonFire,muzzle);}}}
        else if(!enemy.alerted){idleTick();
            // 家と巡回の地点の間を歩く速さで巡回する
            V goal=enemy.patrol-enemy.at;goal.y=0;float speed=2.6f*std::sqrt(enemy.scale);
            if(Length(goal)<1.5f)std::swap(enemy.home,enemy.patrol);
            else{V step=Unit(goal);enemy.heading=Unit(Lerp(enemy.heading,step,1-std::exp(-5*dt)));enemy.at=enemy.at+step*std::min(Length(goal),speed*dt);}}
        // 弓兵: 距離を保って横に動き、先を読んで矢を撃つ
        else if(kind==CityEnemy::Archer){idleTick();
            if(enemy.phase!=SwarmPhase::Spawn){float standoff=CityStandoff(kind);
                V side{direction.z,0,-direction.x};if(enemy.id%2)side=side*-1.f;
                V move=direction*((distance-standoff)*.6f)+side*(4.f*std::sin(enemy.age*.7f+float(enemy.id)));
                float speed=6.f*std::sqrt(enemy.scale)*(enemy.stagger>0?.3f:1.f);
                if(Length(move)>.5f)enemy.at=enemy.at+Unit(move)*std::min(Length(move),speed*dt);
                enemy.heading=Unit(Lerp(enemy.heading,direction,1-std::exp(-8*dt)));
                enemy.shotClock-=dt;
                if(enemy.shotClock<=0&&distance<115&&enemy.stagger<=0){enemy.shotClock=2.2f+SwarmVariation(enemy.id,enemy.attacks++)*1.2f;
                    V from=enemy.at+V{0,enemy.scale*.9f,0}+enemy.heading*enemy.scale;float flight=distance/62;
                    V aim=targetBody.at+targetBody.velocity*std::min(flight,1.2f)+V{0,.6f,0};
                    V velocity=Unit(aim-from)*62;velocity.y+=4.5f*flight;
                    cityShots_.push_back({from,velocity,0,false,aim});}}}
        // それ以外: 群れと同じ追跡と突進。守護者は踏みつけも行う
        else{
            float pace=kind==CityEnemy::Guardian?.45f:1.f;
            TickSwarm(enemy,targetBody.at,dt*pace,InkGround(enemy.at),active<maxActive&&enemy.stagger<=0);
            if(kind==CityEnemy::Guardian&&enemy.phase==SwarmPhase::Chase){enemy.shotClock-=dt;
                if(enemy.shotClock<=0&&distance<75){enemy.shotClock=6.5f;cityStomps_.push_back({V{enemy.at.x,0,enemy.at.z},0,36});}}
        }
        // 攻撃の予告に入った数を数える
        if(phase!=SwarmPhase::Windup&&enemy.phase==SwarmPhase::Windup)++active;
        // 通れる範囲に収め、突進が当たったらダメージ
        float margin=kind==CityEnemy::Guardian?18.f:std::max(1.f,enemy.scale*1.1f);
        enemy.at=CityConstrain(before,enemy.at,margin,cityGates_);
        enemy.at.y=InkGround(enemy.at)+enemy.scale;enemy.corrosion=enemy.health<=enemy.maxHealth*.3f?1.f:0.f;
        // 突進が当たったらダメージ (本体なら体力、子なら子の体力)
        if(phase==SwarmPhase::Dash&&!enemy.struck){float hit,hitRadius=2.2f*LifeScale(targetBody.mass)+1.2f*enemy.scale;V delta=enemy.at-before;
            if(LifeDistance(enemy.at,targetBody.at)<hitRadius||(Length(delta)>.001f&&RaySphere(before,Unit(delta),targetBody.at,hitRadius,Length(delta),hit))){
                enemy.struck=true;float damage=CityDamage(kind);if(victim==0)Damage(r,p,damage,ctx,enemy.at);
                else {targetBody.health=std::max(0.f,targetBody.health-damage*.7f);if(targetBody.health<=0){targetBody.down=true;LifeReturnToSlime(targetBody);++life_.revision;}}
            }}
        // 信号機の黄のマーカー: 時間切れで爆発、付いている間に動いたら大爆発
        if(int burst=SignalTickMark(enemy,dt)){bool moved=burst==2;V at=enemy.at;LifeHit(r,p,ctx,enemy.id,SignalMarkDamage(body.mass,moved),LifeAction::Signal,at);
            if(swarmEffects_.size()<128)swarmEffects_.push_back({at,0,(moved?5.f:3.f)+enemy.scale,2});
            LifeImpact(ctx,moved?ImpactMarkerBig:ImpactMarker,at);auto* audio=Engine::Audio::GetInstance();audio->Play(hitSound_,false,(moved?.6f:.35f)*audio->GetMasterSEVolume(),moved?.7f:1.f);
            if(moved){life_.message="動いた！ マーカーが大爆発";life_.messageAge=1.2f;}}
        // カメラとの間の透明度
        enemy.cameraOpacity=SwarmCameraOpacity(Read(ctx.camera->Position()),body.at,enemy.at,false);
    }
}
// 街のステージの描画: バリケード・破片・弾・光る看板・踏みつけの予告・敵 (ネズミ・大砲・石の守護者)
void ChronoSystem::DrawCityStage(GameContext& ctx){
    using namespace Chrono;auto* render=ctx.renderer;
    // 立方体を軸と大きさで描く
    auto part=[&](const CityModel& skin,V center,V right,V up,V forward,V size,Engine::Vector4 color){
        Engine::Matrix4x4 m{};V axes[3]={right*size.x,up*size.y,forward*size.z};
        for(int i=0;i<3;++i){m.m[i][0]=axes[i].x;m.m[i][1]=axes[i].y;m.m[i][2]=axes[i].z;m.m[i][3]=0;}
        m.m[3][0]=center.x;m.m[3][1]=center.y;m.m[3][2]=center.z;m.m[3][3]=1;
        render->DrawMeshInstanced(cityCube_,skin.texture,m,color,"CitySurface",skin.extra);};
    // 回転の角度から 3 つの軸を求める関数
    auto euler=[&](V rotate,V& right,V& up,V& forward){
        // ヨー (y) の後、ヨーした座標系でピッチ (x) とロール (z) を回す
        float cy=std::cos(rotate.y),sy=std::sin(rotate.y),cp=std::cos(rotate.x),sp=std::sin(rotate.x),cr=std::cos(rotate.z),sr=std::sin(rotate.z);
        V r0{cy,0,-sy},u0{0,1,0},f0{sy,0,cy};
        V u1=u0*cp+f0*sp,f1=f0*cp-u0*sp;right=r0*cr+u1*sr;up=u1*cr-r0*sr;forward=f1;};
    // モデルを指定の幅で地面に置いて描く
    auto model=[&](const CityModel& m,V ground,float width,float yaw,Engine::Vector4 color){
        float scale=width/std::max({m.hi.x-m.lo.x,m.hi.z-m.lo.z,.001f});V mid=(m.lo+m.hi)*.5f;float cs=std::cos(yaw),sn=std::sin(yaw);
        Engine::Transform t;t.scale={scale,scale,scale};t.rotate={0,yaw,0};
        t.translate=EV(V{ground.x-(mid.x*cs+mid.z*sn)*scale,ground.y-m.lo.y*scale,ground.z-(-mid.x*sn+mid.z*cs)*scale});
        render->DrawMeshInstanced(m.mesh,m.texture,t,color,"CitySurface",m.extra);};
    // 床の輪のエフェクトを描く
    auto floorRing=[&](V at,float size,int kind,float progress,Engine::Vector4 color){Engine::Transform t;
        t.translate=EV(V{at.x,InkGround(at)+.2f,at.z});t.scale={size,1,size};color.w=float(kind)*2+std::clamp(progress,0.f,.999f);
        render->DrawMeshInstanced(swarmQuad_,white_,t,color,"HordeEffect");};
    // バリケード: 揺れと、壊れ具合で暗くなる色
    if(!cityGates_.barricadeOpen){float shake=cityBarricadeShake_>0?std::sin(cityBarricadeShake_*90)*cityBarricadeShake_:0.f;
        float wear=1-.45f*(1-cityBarricadeHealth_/CityBarricadeHealth());
        for(const auto& plank:cityBarricade_){V right,up,forward;euler(plank.rotate,right,up,forward);
            part(cityWood_,plank.at+V{shake,0,0},right,up,forward,plank.size,{wear,wear*.95f,wear*.9f,1});}}
    // 材質の番号 (0 木・1 鉄・2 ブロック・3 金属) から立方体の材質を選ぶ
    auto skinOf=[&](int material)->const CityModel& {return material==0?cityWood_:material==1?cityIron_:material==2?cityBlock_:cityArmor_;};
    // 路地の壁: 揺れと、壊れ具合で暗くなる色
    for(size_t i=0;i<cityWalls_.size()&&i<CityWalls().size();++i){if(cityGates_.WallOpen(i))continue;const auto& wall=cityWalls_[i];
        V shake=CityWalls()[i].Axis()*(wall.shake>0?std::sin(wall.shake*90)*wall.shake:0.f);float wear=1-.45f*(1-wall.health/CityWalls()[i].health);
        for(const auto& piece:wall.pieces){V right,up,forward;euler(piece.rotate,right,up,forward);
            Engine::Vector4 color=piece.material==3?Engine::Vector4{.6f*wear,.62f*wear,.65f*wear,1}:piece.material==2?Engine::Vector4{.95f*wear,.93f*wear,.9f*wear,1}:Engine::Vector4{wear,wear*.95f,wear*.9f,1};
            part(skinOf(piece.material),piece.at+shake,right,up,forward,piece.size,color);}}
    // 破片: 時間が経つと小さくなる
    for(const auto& d:cityDebris_){V right,up,forward;euler(d.rotate,right,up,forward);float fade=std::clamp(4-d.age,0.f,1.f);
        part(skinOf(d.material),d.at,right,up,forward,d.size*fade,{1,1,1,1});}
    // 火事場の炎: 揺らぐ炎の舌を輪に並べ、中心は明るい芯。火が弱まるほど小さくなる
    {float t=life_.age;
    // 光る立方体を軸と大きさで描く
    auto glowBox=[&](V center,V right,V up,V forward,V size,Engine::Vector4 color){Engine::Matrix4x4 m{};V axes[3]={right*size.x,up*size.y,forward*size.z};
        for(int k=0;k<3;++k){m.m[k][0]=axes[k].x;m.m[k][1]=axes[k].y;m.m[k][2]=axes[k].z;m.m[k][3]=0;}
        m.m[3][0]=center.x;m.m[3][1]=center.y;m.m[3][2]=center.z;m.m[3][3]=1;render->DrawMeshInstanced(cityCube_,white_,m,color,"DomainGlow");};
    for(size_t i=0;i<cityFires_.size();++i){const auto& fire=cityFires_[i];if(fire.health<=0)continue;
        float power=.35f+.65f*fire.health/CityFireHealth(),size=fire.radius*power;V ground{fire.at.x,InkGround(fire.at),fire.at.z};
        // 外側は低く赤い炎、内側ほど高く黄色い炎。舌は細く、高さと傾きが揺らぐ
        for(int k=0;k<14;++k){float ring=k<8?.6f:.25f,a=float(k)*(k<8?.785f:1.047f)+float(i),wave=std::sin(t*(6+float(k%4))+float(k)*1.7f+float(i));
            float height=size*(k<8?.7f:1.25f)*(.75f+.3f*wave)+3,lean=.12f*std::sin(t*3.1f+float(k));
            V out{std::cos(a),0,std::sin(a)},side{-out.z,0,out.x},up=Unit(V{0,1,0}+side*lean);
            V at=ground+out*(size*ring)+up*(height*.5f);float width=size*(k<8?.2f:.26f);
            glowBox(at,side,up,out,{width,height,width},k<8?Engine::Vector4{1.7f,.32f+.08f*wave,.05f,1}:Engine::Vector4{1.8f,.75f+.15f*wave,.15f,1});}
        float flicker=1+.15f*std::sin(t*17+float(i)*3);
        glowBox(ground+V{0,size*.35f,0},{1,0,0},{0,1,0},{0,0,1},{size*.5f,size*.7f*flicker,size*.5f},{1.9f,1.05f,.3f,1});
        floorRing(fire.at,size*1.2f,0,.5f+.4f*std::sin(t*3+float(i)),{1,.35f,.05f,1});}}
    // 弾: 砲弾は球と着弾点の輪、矢は細長い箱
    for(const auto& shot:cityShots_){
        if(shot.shell){Engine::Transform t;t.translate=EV(shot.at);t.scale={1.3f,1.3f,1.3f};render->DrawMeshInstanced(sphere_,white_,t,{.06f,.06f,.07f,1});
            floorRing(shot.target,9,0,std::clamp(shot.age/1.7f,0.f,1.f),{1,.3f,.05f,1});}
        else{V forward=Unit(shot.velocity),right=Unit(SwarmCross({0,1,0},forward)),up=SwarmCross(forward,right);
            part(cityWood_,shot.at,right,up,forward,{.25f,.25f,3.2f},{.75f,.6f,.45f,1});}
    }
    // 光る看板・燃える窓 (揺らぐ)・非常灯 (点滅する)
    for(const auto& glow:cityGlows_){V forward=SwarmCross(glow.right,glow.up);Engine::Matrix4x4 m{};V axes[3]={glow.right*glow.size.x,glow.up*glow.size.y,forward*glow.size.z};
        Engine::Vector4 color=glow.color;float t=life_.age+glow.phase;
        if(glow.kind==1){float f=.7f+.25f*std::sin(t*9)+.12f*std::sin(t*23+glow.phase*2);color={color.x*f,color.y*f,color.z*f,color.w};axes[1]=axes[1]*(.85f+.2f*std::sin(t*5));}
        else if(glow.kind==2&&std::fmod(t,1.f)>.5f)continue;
        for(int i=0;i<3;++i){m.m[i][0]=axes[i].x;m.m[i][1]=axes[i].y;m.m[i][2]=axes[i].z;m.m[i][3]=0;}
        m.m[3][0]=glow.center.x;m.m[3][1]=glow.center.y;m.m[3][2]=glow.center.z;m.m[3][3]=1;
        render->DrawMeshInstanced(cityCube_,white_,m,color,"DomainGlow");}
    // 踏みつけの予告の輪
    for(const auto& stomp:cityStomps_)if(stomp.age<.85f)floorRing(stomp.at,stomp.radius,0,stomp.age/.85f,{1,.25f,.04f,1});
    // 各敵: 光・状態異常の色・向き・地面の位置・出現の高さ
    for(const auto& enemy:swarm_)if(SwarmAlive(enemy)){auto kind=CityKind(enemy);
        // 今の段階の敵は足元に赤い輪を出して目立たせる
        if(enemy.step==life_.step&&enemy.phase!=SwarmPhase::Spawn)floorRing(enemy.at,std::max(4.f,enemy.scale*3.f),0,.55f+.3f*std::sin(life_.age*5+float(enemy.id)),{1,.12f,.08f,1});
        float glow=1+enemy.flash*5;Engine::Vector4 tint{glow,glow,glow,1};if(enemy.corrosion>0)tint={.55f*glow,1.1f*glow,.55f*glow,1};
        float yaw=std::atan2(enemy.heading.x,enemy.heading.z);V ground{enemy.at.x,InkGround(enemy.at),enemy.at.z};
        float rise=enemy.phase==SwarmPhase::Spawn?std::clamp(enemy.clock/.6f,.05f,1.f):1.f;
        // ネズミ: ちょこちょこ揺れる
        if(kind==CityEnemy::Rat){float scurry=std::sin(enemy.age*18+float(enemy.id))*.08f;
            model(cityRat_,ground+V{0,std::abs(scurry),0},CityRatWidth*enemy.scale/.75f*rise,yaw+CityRatYaw,tint);}
        // 大砲
        else if(kind==CityEnemy::Cannon)model(cityCannon_,ground,enemy.scale*CityCannonWidth,yaw+CityCannonYaw,tint);
        // 石の守護者: 脚・腕・胴・頭・目を立方体で組み、歩きと攻撃の構えで動かす
        else if(kind==CityEnemy::Guardian){
            float s=enemy.scale*.42f*rise,walk=enemy.phase==SwarmPhase::Chase?std::sin(enemy.age*2.4f):0.f;
            bool windup=enemy.phase==SwarmPhase::Windup;V f{std::sin(yaw),0,std::cos(yaw)},rt{std::cos(yaw),0,-std::sin(yaw)},up{0,1,0};
            for(float sideSign:{-1.f,1.f}){float swing=walk*.42f*sideSign;V legUp=up*std::cos(swing)+f*std::sin(swing),legF=f*std::cos(swing)-up*std::sin(swing);
                V hip=ground+V{0,s*2.2f,0}+rt*(sideSign*s*.62f);V leg=hip-legUp*(s*1.1f);
                part(cityStoneSkin_,leg,rt,legUp,legF,{s*.82f,s*2.2f,s*.9f},tint);
                if(enemy.armor>0)part(cityIron_,leg+legF*(s*.5f)-legUp*(s*.3f),rt,legUp,legF,{s*.95f,s*1.3f,s*.18f},{.55f*glow,.55f*glow,.6f*glow,1});
                float armSwing=windup?-2.3f:-walk*.5f*sideSign;V armUp=up*std::cos(armSwing)+f*std::sin(armSwing),armF=f*std::cos(armSwing)-up*std::sin(armSwing);
                V shoulder=ground+V{0,s*4.4f,0}+rt*(sideSign*s*1.75f);
                part(cityStoneSkin_,shoulder-armUp*(s*1.2f),rt,armUp,armF,{s*.75f,s*2.5f,s*.75f},tint);
                part(cityStoneSkin_,shoulder-armUp*(s*2.6f),rt,armUp,armF,{s*1.05f,s*.9f,s*1.05f},tint);}
            part(cityStoneSkin_,ground+V{0,s*2.45f,0},rt,up,f,{s*2.1f,s*.7f,s*1.2f},tint);
            part(cityStoneSkin_,ground+V{0,s*3.65f,0}+f*(s*.1f),rt,up,f,{s*2.9f,s*1.9f,s*1.55f},tint);
            part(cityStoneSkin_,ground+V{0,s*5.05f,0}+f*(s*.25f),rt,up,f,{s,s*.95f,s},tint);
            for(float sideSign:{-1.f,1.f}){Engine::Transform eye;eye.translate=EV(ground+V{0,s*5.15f,0}+f*(s*.78f)+rt*(sideSign*s*.25f));eye.scale={s*.13f,s*.13f,s*.13f};
                render->DrawMeshInstanced(sphere_,white_,eye,windup?Engine::Vector4{1,.2f,.05f,1}:Engine::Vector4{1,.6f,.15f,1},"DomainGlow");}
        }
    }
}
// 取り込みが決まった小物は、スキャンのモデルのまま触手に沿って戻ってくる
bool ChronoSystem::DrawCityPropInTentacle(GameContext& ctx,uint32_t visual,Chrono::Vec at,float shrink){
    // モデルの見た目の記録を探す (無ければ false)
    auto found=cityVisuals_.find(visual);if(found==cityVisuals_.end())return false;const auto& v=found->second;
    // 縮めながら触手の先の位置に描く
    Engine::Transform t;t.scale=EV(v.scale*shrink);t.rotate=EV(v.rotate);t.translate=EV(at-v.center*shrink);
    ctx.renderer->DrawMeshInstanced(v.mesh,v.texture,t,{1,1,1,1},v.shader,v.extra);return true;
}
}
