// DomainRules.h
// 描いた輪 (領域) の規則。線の記録・閉じた輪の検出・形の認識 (三角・四角・8 の字)・弾の数・届く範囲・内側の判定・誘導弾の速度。
#pragma once
#include "ChronoRules.h"
#include <vector>
#include <array>
#include <map>
#include <set>

namespace Game::Chrono {
// 水平の距離 / XZ 平面の外積
inline float DomainDistance(Vec a,Vec b){a.y=b.y=0;return Length(a-b);}
inline float DomainCross(Vec a,Vec b){return a.x*b.z-a.z*b.x;}
// 輪の形と、その技の名前
enum class DomainShape {Loop,Triangle,Square,Infinity};
inline const char* DomainSkillName(DomainShape shape){
    switch(shape){case DomainShape::Triangle:return "TRIDENT";case DomainShape::Square:return "FORTRESS";
        case DomainShape::Infinity:return "TWIN STORM";default:return "HOMING DOMAIN";}
}
// 点と線分の水平の距離
inline float DomainPointEdgeDistance(Vec p,Vec a,Vec b){
    Vec d=b-a;d.y=0;Vec offset=p-a;offset.y=0;
    float t=std::clamp(Dot(offset,d)/std::max(.00001f,Dot(d,d)),0.f,1.f);
    return DomainDistance(p,Lerp(a,b,t));
}
// 線を単純化する (Douglas-Peucker 法)
inline void DomainSimplify(const std::vector<Vec>& points,size_t first,size_t last,float tolerance,std::vector<Vec>& out){
    // 始点と終点を結ぶ線から一番遠い点を探す
    float best=tolerance;size_t split=first;
    for(size_t i=first+1;i<last;++i){float distance=DomainPointEdgeDistance(points[i],points[first],points[last]);
        if(distance>best){best=distance;split=i;}}
    // 許容より遠ければそこで分けて再帰、そうでなければ始点だけ残す
    if(split!=first){DomainSimplify(points,first,split,tolerance,out);DomainSimplify(points,split,last,tolerance,out);}
    else out.push_back(points[first]);
}
// 閉じた輪 (点・囲んだ領域・角・形・長さ・面積)
struct DomainLoop {
    // 点の列
    std::vector<Vec> points;
    // 今の一続きの線で囲まれた面。共有する辺は 1 回だけ数える
    std::vector<std::vector<Vec>> regions;
    // 四角・三角の角
    std::array<Vec,4> corners{};
    // 形と、長さ・面積
    DomainShape shape=DomainShape::Loop;
    float length=0,area=0;
    // 囲んだ領域の数 / 発動できる大きさか
    int Enclosures()const{return regions.empty()?1:int(regions.size());}
    bool Ready()const{return points.size()>=4&&length>=12&&area>=8;}
    // 長さと面積を求める
    void Measure(){
        length=area=0;
        if(points.empty())return;
        // アリーナの中心から遠い所での桁落ちを避けるため、局所の原点を使う
        // 長さを足し、外積で面積を求める
        for(size_t i=1;i<points.size();++i){length+=DomainDistance(points[i-1],points[i]);
            area+=DomainCross(points[i-1]-points.front(),points[i]-points.front());}
        area=std::abs(area)*.5f;
    }
    // 中心
    Vec Center()const{Vec center{};for(auto p:points)center=center+p;return points.empty()?center:center*(1.f/float(points.size()));}
    // 形を認識する: 単純化して 3 つか 4 つの角になり、同じ向きに曲がり、辺の長さがそろっていれば三角か四角
    void Recognize(){
        shape=DomainShape::Loop;if(!Ready())return;
        // 始点から一番遠い点で 2 つに分けて単純化する
        std::vector<Vec> simplified;size_t opposite=1;float farthest=0;
        for(size_t i=1;i+1<points.size();++i){float distance=DomainDistance(points.front(),points[i]);if(distance>farthest){farthest=distance;opposite=i;}}
        // 単純化の許容 (長さに比例)
        float tolerance=std::max(1.f,length*.018f);
        DomainSimplify(points,0,opposite,tolerance,simplified);DomainSimplify(points,opposite,points.size()-1,tolerance,simplified);
        // ほぼ一直線になっている点を取り除く
        bool removed=true;
        while(removed&&simplified.size()>3){removed=false;
            for(size_t i=0;i<simplified.size();++i){Vec a=simplified[(i+simplified.size()-1)%simplified.size()],b=simplified[i],c=simplified[(i+1)%simplified.size()];
                if(DomainPointEdgeDistance(b,a,c)<tolerance*.65f){simplified.erase(simplified.begin()+i);removed=true;break;}}}
        // 3 つか 4 つの角でなければ普通の輪
        if(simplified.size()!=3&&simplified.size()!=4)return;
        float turnSign=0,minEdge=length,maxEdge=0;
        // 各角で同じ向きに曲がり、曲がる角度が適度で、辺の長さを記録する
        for(size_t i=0;i<simplified.size();++i){Vec a=simplified[i],b=simplified[(i+1)%simplified.size()],c=simplified[(i+2)%simplified.size()];
            Vec d=b-a,e=c-b;d.y=e.y=0;float cross=DomainCross(d,e);
            if(std::abs(cross)<.01f||(turnSign!=0&&turnSign*cross<0))return;turnSign=cross;
            float turn=std::acos(std::clamp(Dot(Unit(d),Unit(e)),-1.f,1.f));
            if(turn<.6f||turn>2.65f)return;
            float edge=DomainDistance(a,b);minEdge=std::min(minEdge,edge);maxEdge=std::max(maxEdge,edge);
            // 元の各点は、認識した直線の辺のどれかの近くになければならない
        }
        // 辺の長さの差が大きすぎれば普通の輪
        if(maxEdge>minEdge*3.5f)return;
        // 元の点がどれかの辺の近くにあるか
        for(auto p:points){float distance=length;for(size_t i=0;i<simplified.size();++i)distance=std::min(distance,DomainPointEdgeDistance(p,simplified[i],simplified[(i+1)%simplified.size()]));
            if(distance>tolerance*1.2f)return;}
        // 三角か四角に決め、角を記録する
        shape=simplified.size()==3?DomainShape::Triangle:DomainShape::Square;
        for(size_t i=0;i<simplified.size();++i)corners[i]=simplified[i];
    }
};
// 弾の数は囲んだ面積に従う: 最小の輪 (8 m2) は 6 発、群れを
// 囲む大きな輪 (約 540 m2, 直径 26 m の円) は最大の 180 発
inline int DomainAreaAmmo(float area){return std::clamp(int(std::round(area/3.f)),6,180);}
// 囲んだ数による弾の威力 / 輪の弾の数 (形で倍率をかける)
inline float DomainBulletPower(int enclosures){return 1+.45f*std::sqrt(float(std::clamp(enclosures-1,0,13)));}
// 8 の字は 1.3 倍、四角は 1.15 倍
inline int DomainAmmo(const DomainLoop& loop){
    float multiplier=loop.shape==DomainShape::Infinity?1.3f:loop.shape==DomainShape::Square?1.15f:loop.shape==DomainShape::Triangle?1.35f:1.f;
    return int(float(DomainAreaAmmo(loop.area)+8*std::clamp(loop.Enclosures()-1,0,13))*multiplier);
}
// 弾は撃った輪の近くの敵だけを追う: 届く範囲は輪の大きさで広がるので、
// どこで (何の周りに) 描くかが大切になる
struct DomainReachArea { Vec center{}; float radius=0; };
inline DomainReachArea DomainReach(const DomainLoop& loop){
    // 点の中心と、中心から一番遠い点までの距離
    DomainReachArea reach;if(loop.points.empty())return reach;
    for(auto p:loop.points)reach.center=reach.center+p;
    reach.center=reach.center*(1.f/float(loop.points.size()));
    float extent=0;for(auto p:loop.points)extent=std::max(extent,DomainDistance(reach.center,p));
    // 届く範囲の半径
    reach.radius=10+1.3f*extent;
    return reach;
}
// 点が輪の内側にあるか (偶奇の判定)
inline bool DomainContains(const DomainLoop& loop,Vec p){
    // 囲んだ領域が複数あれば、どれかの中にあるか
    if(!loop.regions.empty()){
        for(const auto& points:loop.regions){DomainLoop region;region.points=points;if(DomainContains(region,p))return true;}
        return false;
    }
    // 偶奇の判定: 点から右へ伸ばした線が辺を横切る回数
    bool inside=false;
    for(size_t i=1;i<loop.points.size();++i){Vec a=loop.points[i-1],b=loop.points[i];
        if((a.z>p.z)!=(b.z>p.z)&&p.x<(b.x-a.x)*(p.z-a.z)/(b.z-a.z)+a.x)inside=!inside;}
    return inside;
}
// 平面のグラフを作り、交差と重なる線を分けてから、囲まれた面をたどる。
// 同じ辺の繰り返しや、既にある領域を囲んでも報酬は重複しない
inline std::vector<DomainLoop> DomainStrokeRegions(const std::vector<Vec>& stroke){
    constexpr float weld=.10f; // 単純化の誤差より大きく、液体の帯よりずっと小さい
    // 4 点未満なら領域は無い
    std::vector<DomainLoop> regions;if(stroke.size()<4)return regions;
    // 線を単純化する
    std::vector<Vec> path;DomainSimplify(stroke,0,stroke.size()-1,.04f,path);path.push_back(stroke.back());
    // 辺 (始点・終点・分ける位置の一覧)
    struct Edge {Vec a,b;std::vector<float> cuts{0,1};};
    std::vector<Edge> edges;
    for(size_t i=1;i<path.size();++i)if(DomainDistance(path[i-1],path[i])>.02f)edges.push_back({path[i-1],path[i],{0,1}});
    // すべての辺の組の交点を分ける位置として加える
    for(size_t i=0;i<edges.size();++i)for(size_t j=i+1;j<edges.size();++j){
        auto& a=edges[i];auto& b=edges[j];Vec d=a.b-a.a,e=b.b-b.a;float den=DomainCross(d,e);
        if(std::abs(den)>.00001f){
            float t=DomainCross(b.a-a.a,e)/den,u=DomainCross(b.a-a.a,d)/den;
            if(t>=0&&t<=1&&u>=0&&u<=1&&std::abs(Lerp(a.a,a.b,t).y-Lerp(b.a,b.b,u).y)<1.5f){a.cuts.push_back(t);b.cuts.push_back(u);}
        }
        // 近い端点も、平行でない辺の途中につなぐ。
        // そうしないと単純化で、受け入れた閉じ方が元の辺から少しずれることがある
        {
            auto split=[&](Edge& edge,Vec p){Vec delta=edge.b-edge.a;delta.y=0;
                float t=Dot(Vec{p.x-edge.a.x,0,p.z-edge.a.z},delta)/Dot(delta,delta);
                if(t>=0&&t<=1&&DomainDistance(Lerp(edge.a,edge.b,t),p)<weld&&std::abs(Lerp(edge.a,edge.b,t).y-p.y)<1.5f)edge.cuts.push_back(t);};
            split(a,b.a);split(a,b.b);split(b,a.a);split(b,a.b);
        }
    }
    // 近い点をまとめて頂点にする (格子で近くの頂点を探す)
    std::vector<Vec> vertices;std::map<std::pair<int,int>,std::vector<size_t>> cells;
    auto vertex=[&](Vec p){int x=int(std::floor(p.x/weld)),z=int(std::floor(p.z/weld));
        for(int dx=-1;dx<=1;++dx)for(int dz=-1;dz<=1;++dz){auto found=cells.find({x+dx,z+dz});if(found==cells.end())continue;
            for(auto n:found->second)if(DomainDistance(vertices[n],p)<weld&&std::abs(vertices[n].y-p.y)<1.5f)return n;}
        size_t n=vertices.size();vertices.push_back(p);cells[{x,z}].push_back(n);return n;};
    // 辺を分けた区間をつなぎ (重複しない)
    std::set<std::pair<size_t,size_t>> links;
    for(auto& edge:edges){std::sort(edge.cuts.begin(),edge.cuts.end());
        for(size_t i=1;i<edge.cuts.size();++i){size_t a=vertex(Lerp(edge.a,edge.b,edge.cuts[i-1])),b=vertex(Lerp(edge.a,edge.b,edge.cuts[i]));
            if(a!=b)links.insert(std::minmax(a,b));}}
    // 各頂点の隣の頂点を角度の順に並べる
    std::vector<std::vector<size_t>> neighbors(vertices.size());
    for(auto edge:links){neighbors[edge.first].push_back(edge.second);neighbors[edge.second].push_back(edge.first);}
    for(size_t i=0;i<neighbors.size();++i)std::sort(neighbors[i].begin(),neighbors[i].end(),[&](size_t a,size_t b){
        Vec da=vertices[a]-vertices[i],db=vertices[b]-vertices[i];return std::atan2(da.z,da.x)<std::atan2(db.z,db.x);});
    // 各辺を両方向にたどり、左回りの面を取り出す
    std::set<std::pair<size_t,size_t>> visited;
    for(auto edge:links)for(int direction=0;direction<2;++direction){
        size_t start=direction?edge.second:edge.first,next=direction?edge.first:edge.second,a=start,b=next;
        if(visited.count({a,b}))continue;DomainLoop face;bool closed=false;
        // 次の辺は、来た辺の 1 つ手前 (時計回り) の隣
        for(size_t step=0;step<=links.size()*2;++step){
            if(!visited.insert({a,b}).second)break;face.points.push_back(vertices[a]);
            const auto& adjacent=neighbors[b];auto back=std::find(adjacent.begin(),adjacent.end(),a);
            size_t index=size_t(back-adjacent.begin());size_t c=adjacent[(index+adjacent.size()-1)%adjacent.size()];a=b;b=c;
            if(a==start&&b==next){closed=true;break;}
        }
        // 閉じていて 3 点以上の面だけ
        if(!closed||face.points.size()<3)continue;face.points.push_back(face.points.front());
        // 向きが正 (内側の面) で大きさが足りる面を領域にする
        float signedArea=0;for(size_t i=1;i<face.points.size();++i)signedArea+=DomainCross(face.points[i-1]-face.points.front(),face.points[i]-face.points.front());
        face.Measure();if(signedArea>0&&face.Ready())regions.push_back(std::move(face));
    }
    return regions;
}
// 描いている線: 点を記録し、自分の線と交わったら閉じた輪を候補にする
struct DomainPath {
    // 記録できる点の上限・閉じたとみなす距離
    static constexpr size_t MaxPoints=1536;
    size_t pointLimit=MaxPoints;
    static constexpr float ClosureRadius=1.45f; // 見える液体の幅の半分の 2 つ分 (.72m)
    // 点・候補の輪・最後の輪・最後に閉じた長さ・全体の長さ
    std::vector<Vec> points;
    DomainLoop candidate;
    DomainLoop lastLoop;
    float lastClosure=-100;
    float length=0;
    // 消す / 空中で線を切る / 長さを数え直す
    void Clear(){points.clear();candidate={};lastLoop={};length=0;lastClosure=-100;}
    void BreakTrail(){points.clear();lastLoop={};length=0;lastClosure=-100;}
    void Recount(){length=0;for(size_t i=1;i<points.size();++i)length+=DomainDistance(points[i-1],points[i]);}
    void Damage(){
        candidate={};lastLoop={};lastClosure=-100;
        // 直近の 4 分の 1 だけを失い、残りは次の閉じ方に使う
        if(points.size()>1)points.resize(std::max(size_t(1),points.size()*3/4));
        Recount();
    }
    // 点を追加する (1 m ごとに区切り、交差や近い端点で閉じたかを調べる)
    void Append(Vec at){
        // 最初の点
        if(points.empty()){points.push_back(at);return;}
        // 大きく離れたら線を切って始め直す
        if(DomainDistance(points.back(),at)>8||std::abs(points.back().y-at.y)>5){Clear();points.push_back(at);return;}
        // 1 m ごとに点を足しながら、交差や近い点で閉じたかを調べる
        for(int samples=0;samples<16&&DomainDistance(points.back(),at)>.001f;++samples){
            float distance=DomainDistance(points.back(),at);bool sampled=distance>=1,accepted=false;
            Vec from=points.back(),to=sampled?Lerp(from,at,1/distance):at;
            // 隣の辺はとばし、小さすぎる・つぶれた輪は除く
            // 古い辺と交わるか調べる
            if(points.size()>=5)for(size_t i=points.size()-3;i>0;--i){
                Vec a=points[i-1],b=points[i],d=to-from,e=b-a;
                float den=DomainCross(d,e);Vec hit{};bool closed=false;
                // 交差の位置
                if(std::abs(den)>.00001f){
                    float t=DomainCross(a-from,e)/den,u=DomainCross(a-from,d)/den;
                    closed=t>=0&&t<=1&&u>=0&&u<=1;
                    if(closed)hit=Lerp(a,b,u);
                }
                // 小さな閉じの許容で、1 m 離れて記録した端点も扱える
                if(!closed){float q=std::clamp(Dot(Vec{to.x-a.x,0,to.z-a.z},Vec{e.x,0,e.z})/std::max(.00001f,e.x*e.x+e.z*e.z),0.f,1.f);
                    hit=Lerp(a,b,q);closed=DomainDistance(hit,to)<ClosureRadius;}
                if(!closed||std::abs(hit.y-to.y)>1.5f||DomainDistance(hit,from)<.02f)continue;
                // 許容でのスナップは今の先端の方へ進む向きにだけ行い、
                // 古い交点へ戻って何度も記録し直さないようにする
                // 進む向きに対して前にある交点だけを使う
                float advance=(hit.x-from.x)*(to.x-from.x)+(hit.z-from.z)*(to.z-from.z);
                if(advance<=.0001f)continue;
                // 交点から今の点までで輪を作る
                DomainLoop loop;loop.points.push_back(hit);
                loop.points.insert(loop.points.end(),points.begin()+i,points.end());
                loop.points.push_back(to);loop.points.push_back(hit);loop.Measure();
                // 面の取り出しで面積は別に確かめる: 逆回りが
                // 実際に囲んだ 2 つの領域を打ち消してはいけない。距離の待ち時間は無い
                if(loop.length>=12){
                    // 形を認識し、前の輪と合わせて 8 の字かを調べる
                    loop.Recognize();
                    DomainShape hidden=DomainShape::Loop;
                    if(lastLoop.Ready()&&DomainDistance(lastLoop.points.front(),loop.points.front())<2.5f){
                        Vec junction=loop.points.front(),lobeA=lastLoop.Center()-junction,lobeB=loop.Center()-junction;lobeA.y=lobeB.y=0;
                        float ratio=std::min(loop.area,lastLoop.area)/std::max(loop.area,lastLoop.area);
                        if(ratio>.2f&&Dot(Unit(lobeA),Unit(lobeB))<-.2f&&!DomainContains(lastLoop,loop.Center())&&!DomainContains(loop,lastLoop.Center())){
                            hidden=DomainShape::Infinity;
                        }
                    }
                    // 見た目の点を受け入れた閉じ方にそろえる。プレイヤーの位置は
                    // 変えない。これで許容した閉じ方も形として閉じたままになる
                    // 線全体で囲まれた領域を取り出す
                    auto stroke=points;stroke.push_back(hit);
                    auto regions=DomainStrokeRegions(stroke);
                    if(!regions.empty()){
                        // 領域の面積の合計
                        float area=0;for(const auto& region:regions)area+=region.area;
                        // 既に完成した境界をなぞり直しても、今の線を再びスナップしたり
                        // 最後の意味のある閉じ方を置き換えたりしてはいけない
                        if(lastClosure>=0&&candidate.Ready()&&int(regions.size())==candidate.Enclosures()&&area<=candidate.area+.25f)break;
                        // 候補の輪を作る (1 つの領域なら認識した形、2 つで 8 の字なら 8 の字)
                        DomainLoop joined;joined.points=std::move(stroke);joined.length=length+DomainDistance(from,hit);
                        for(auto& region:regions){joined.area+=region.area;joined.regions.push_back(std::move(region.points));}
                        if(joined.Enclosures()==1){DomainLoop single;single.points=joined.regions.front();single.Measure();single.Recognize();joined.shape=single.shape;joined.corners=single.corners;}
                        else if(joined.Enclosures()==2&&(hidden==DomainShape::Infinity||candidate.shape==DomainShape::Infinity))joined.shape=DomainShape::Infinity;
                        candidate=std::move(joined);
                        to=hit;
                        accepted=true;
                    }
                    if(accepted){lastLoop=std::move(loop);lastClosure=length;break;}
                }
            }
            if(!sampled&&!accepted)break; // 線全体を細かくせずに、今の先端だけを調べる
            // 点を加えて長さを足す (上限を超えたら古い点を捨てる)
            points.push_back(to);length+=DomainDistance(from,to);
            if(points.size()>pointLimit){points.erase(points.begin());Recount();}
            if(!sampled)break;
        }
    }
};
// まっすぐ上へ撃ち、外へ広がってから、動くボスへ向かう
inline Vec DomainMissileVelocity(Vec position,Vec velocity,Vec target,float age,float seed,float dt){
    // 最初の 0.5 秒は上、0.9 秒までは外へ広がりながら目標の上、その後は目標へ
    Vec desired;
    if(age<.5f)desired={std::cos(seed)*12,65,std::sin(seed)*12};
    else if(age<.9f)desired=Unit(target-position+Vec{std::cos(seed)*22,45,std::sin(seed)*22})*90;
    else desired=Unit(target-position)*125;
    // 目標の速度へなめらかに近づける
    return Lerp(velocity,desired,1-std::exp(-(age<.9f?5.f:9.f)*dt));
}
}
