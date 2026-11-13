// DevourStageRules.h
// 吸収モードのステージの規則 (数値は Resources/Data/devour_stages.json)。エンジンに依存しない。
// ステージごとに開始の大きさ・制限時間・目標の大きさの列・変形アイテム 1 つを持つ。
// 最初の目標に届けばクリアで、時間切れまで次の目標を目指す。最後の目標が S ランク
#pragma once
#include "DevourRules.h"
#include "RingCityRules.h"
#include <string>
#include <vector>
namespace Game::Chrono {
// map は "room" (部屋) か "ring" (輪の町)。half は部屋の半分の大きさ (輪の町は町の大きさで決まるので 0)
struct DevourStage {std::string id,name,map,music="Battle";float time=240,start=5,itemTime=15,half=0,smallFood=0;std::vector<float> goals,gates;LifeMorphItem item=LifeMorphItem::None;Vec itemAt{},spawn{};};
inline const std::vector<DevourStage>& DevourStages(){
    static const std::vector<DevourStage> stages=[]{
        const char* file="devour_stages.json";const auto& json=Data::Load(file);std::vector<DevourStage> out;
        auto vec=[&](const nlohmann::json& v){auto a=Data::ReadArray<float,3>(v,file);return Vec{a[0],a[1],a[2]};};
        for(const auto& s:Data::Require(json,"stages",file)){DevourStage stage;stage.id=s.at("id").get<std::string>();stage.name=s.at("name").get<std::string>();stage.map=s.at("map").get<std::string>();stage.music=s.value("music",stage.music);
            stage.time=s.at("time").get<float>();stage.start=s.at("start").get<float>();stage.itemTime=s.at("itemTime").get<float>();stage.half=s.value("half",0.f);stage.smallFood=s.value("smallFood",0.f);
            stage.goals=s.at("goals").get<std::vector<float>>();stage.gates=s.value("gates",std::vector<float>{});stage.itemAt=vec(s.at("itemAt"));stage.spawn=vec(s.at("spawn"));
            // 吸収モードで使える変形アイテムの名前の表。知らない名前は None のままで、下の確認で止める
            struct ItemName {const char* name;LifeMorphItem item;};
            static constexpr ItemName items[]={{"tire",LifeMorphItem::Tire},{"vacuum",LifeMorphItem::Vacuum},{"spring",LifeMorphItem::Spring}};
            const std::string item=s.at("item").get<std::string>();stage.item=LifeMorphItem::None;
            for(const auto& entry:items)if(item==entry.name)stage.item=entry.item;
            if(stage.goals.empty()||stage.item==LifeMorphItem::None)throw std::runtime_error(std::string(file)+": stage needs goals and an item");
            for(size_t i=1;i<stage.goals.size();++i)if(stage.goals[i]<=stage.goals[i-1])throw std::runtime_error(std::string(file)+": goals must grow");
            out.push_back(std::move(stage));}
        if(out.empty())throw std::runtime_error(std::string(file)+": no stages");
        return out;}();
    return stages;
}
// 体の大きさ (cm) と、その大きさになる量。開始の量のとき開始の大きさになる
inline float DevourSizeCm(const DevourStage& stage,float mass){return stage.start*LifeScale(mass);}
inline float DevourGoalMass(const DevourStage& stage,float cm){float scale=cm/stage.start;return LifeStartMass*scale*scale;}
// 届いた目標の数 (0 は未クリア、目標の数と同じなら S ランク)
inline int DevourRankAt(const DevourStage& stage,float mass){float cm=DevourSizeCm(stage,mass);int rank=0;for(float goal:stage.goals)if(cm+1e-3f>=goal)++rank;return rank;}
// ランクの名前 (一番上が S。その下は A・B・C の順で、未クリアは D)
inline std::string DevourRankName(const DevourStage& stage,int rank){
    if(rank<=0)return "D";int fromTop=int(stage.goals.size())-rank;return fromTop==0?"S":fromTop==1?"A":fromTop==2?"B":"C";
}
// 大きさの文字 (1 m からは m で出す)
inline std::string DevourSizeText(float cm){char text[32];if(cm<100)snprintf(text,sizeof(text),"%.1f cm",cm);else snprintf(text,sizeof(text),"%.2f m",cm/100);return text;}
// 変形アイテムの形
// 変形アイテム → 変形 (LifeMorphItem の順のテーブル)。吸収モードの変形はタイヤ・バネ・掃除機の 3 つで、それ以外は掃除機として扱う
inline LifeAction DevourItemForm(LifeMorphItem item){
    static constexpr LifeAction forms[]={LifeAction::Vacuum,LifeAction::Tire,LifeAction::Vacuum,LifeAction::Vacuum,LifeAction::Vacuum,LifeAction::Spring};
    const size_t index=size_t(item);return index<std::size(forms)?forms[index]:LifeAction::Vacuum;
}
}
