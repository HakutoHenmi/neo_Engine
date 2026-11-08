// CityBuilder.h
// 街の形を材質ごとのメッシュとして組み立てる道具 (CityStage.cpp と RingCity.cpp で使う)。
// 材質の種類・箱や多角形の追加・建物の情報・階の高さ・疑似乱数
#pragma once
#include "ChronoSystemInternal.h"

namespace Game::CityParts {
// 街の材質の種類 (道路・歩道・塗装・コンクリート・ガラス・金属など)
enum CityMaterialId {CityRoad,CitySidewalk,CityAlleyFloor,CityPaint,CityPaintYellow,CityConcrete,CityCladding,CityFacadeTiles,CityDarkTiles,
    CitySlab,CityGreyTiles,CityShutter,CityRustShutter,CityGlass,CityMetal,CityWhiteBrick,CityWood,CityStoneSkin,CityScorch,
    // 吸収モードの輪の町: 家の壁 (レンガ・しっくい・板張り)・屋根 (瓦 2 種・スレート)・歩道 (敷石 2 種)・
    // オフィスのガラスの外壁 4 種・高層ビルの外壁 5 種 (ambientCG)
    CityBrick,CityPlaster,CitySiding,CityRoofClay,CityRoofGrey,CityRoofSlate,CityPaving,CityPavingBrick,
    CityOffice1,CityOffice2,CityOffice3,CityOffice4,CityTower1,CityTower2,CityTower3,CityTower4,CityTower5,CityMaterialCount};
// 吸収モードの材質の色 (写真の材質の代わりに単色で塗り、ローポリの小物と見た目をそろえる)
inline Engine::Vector4 FlatCityColor(int material){
    switch(material){case CityRoad:return {.3f,.31f,.35f,1};case CitySidewalk:case CityPaving:return {.74f,.73f,.68f,1};case CityPavingBrick:return {.82f,.6f,.44f,1};case CityAlleyFloor:return {.46f,.45f,.46f,1};
        case CityPaint:return {.95f,.95f,.9f,1};case CityPaintYellow:return {1,.8f,.24f,1};case CityConcrete:return {.68f,.68f,.66f,1};case CityDarkTiles:return {.34f,.34f,.38f,1};case CityGlass:return {.3f,.52f,.72f,1};case CityMetal:return {.6f,.62f,.66f,1};
        case CityWhiteBrick:return {.9f,.88f,.82f,1};case CityWood:return {.6f,.38f,.22f,1};case CityBrick:return {.82f,.46f,.34f,1};case CityPlaster:return {.96f,.9f,.74f,1};case CitySiding:return {.58f,.78f,.82f,1};
        case CityRoofClay:return {.82f,.28f,.22f,1};case CityRoofGrey:return {.32f,.4f,.58f,1};case CityRoofSlate:return {.28f,.56f,.42f,1};case CityShutter:return {.7f,.72f,.76f,1};case CityRustShutter:return {.78f,.42f,.24f,1};default:return {.7f,.7f,.7f,1};}
}
// 材質ごとの頂点と添字
struct CityGeometry {std::vector<Engine::VertexData> vertices;std::vector<uint32_t> indices;};
// 街の形を材質ごとのメッシュとして組み立てる
struct CityBuilder {
    // 材質ごとの形と、テクスチャを繰り返す大きさ
    std::array<CityGeometry,CityMaterialCount> geometry;
    // (外壁の写真は 1 枚で数階分。オフィスは 3 階、高層ビルは 4 階で 1 枚になる大きさ)
    std::array<float,CityMaterialCount> tile{30,10,16,6,6,20,8,12,14,18,8,8,8,8,6,10,8,22,30,
        14,16,10,14,14,14,12,10,57,57,57,57,76,76,76,76,76};
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
    // 屋根や床の無い 4 枚の壁だけの箱 (上は別の材質で塞ぐ)
    void Walls(int material,Chrono::Vec lo,Chrono::Vec hi){
        using Chrono::Vec;
        Polygon(material,{{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{hi.x,hi.y,lo.z}},{1,0,0});
        Polygon(material,{{lo.x,lo.y,hi.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,hi.y,hi.z}},{-1,0,0});
        Polygon(material,{{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{hi.x,hi.y,hi.z}},{0,0,1});
        Polygon(material,{{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z}},{0,0,-1});
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
inline float CityNoise(int seed){float n=std::sin(float(seed)*12.9898f+78.233f)*43758.5453f;return n-std::floor(n);}
}
