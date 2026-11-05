// DevourPlayLog.cpp
// 吸収モードのプレイ記録 (ChronoSystem の一部)。バランスの調整に使う。
// 1 回のプレイごとに JSON を 1 つ書き (15 秒ごとに上書きするので、途中でやめても残る)、終わったら summary.csv に 1 行足す。
// 保存先は %LOCALAPPDATA%/LiquidTime/playlogs (自動テストでは tests/out/playlogs)
#include "ChronoSystemInternal.h"
#include "../Chrono/DevourMorphRules.h"
#include <ctime>

namespace Game {
namespace {
// 区画の名前 (部屋と輪の町)
const char* const kRoomAreas[]={"bed","floor","back","high"};
const char* const kRingAreas[]={"alley","house","shop","office","tower"};
// 飲み方の名前 (DevourEatBy の順)
const char* const kEatNames[]={"touch","vacuum","pounce","spring","hammer","tire"};
std::string DevourLogNumber(float value){char text[32];snprintf(text,sizeof(text),"%.2f",value);return text;}
}
std::filesystem::path ChronoSystem::DevourLogFolder()const{
    if(wcsstr(GetCommandLineW(),L"--slime-life-smoke"))return std::filesystem::path(L"tests/out/playlogs");
    return RecordPath().parent_path()/L"playlogs";
}
// プレイの記録を始める (小物を作った後に呼ぶ)
void ChronoSystem::StartDevourLog(){
    devourLog_={};if(!devour_)return;auto& log=devourLog_;const auto& stage=CurrentDevourStage();log.active=true;log.last=life_.bodies[0].at;
    std::time_t now=std::time(nullptr);std::tm local{};localtime_s(&local,&now);char stamp[32];std::strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",&local);log.stamp=stamp;
    log.goalTimes.assign(stage.goals.size(),-1);log.gateSizeTimes.assign(stage.gates.size(),-1);log.gateOpenTimes.assign(stage.gates.size(),-1);log.areaFirst.fill(-1);
    for(const auto& prop:life_.props)if(!prop.orb){int area=DevourAreaAt(prop.at);++log.areaTotal[size_t(area)];log.areaMassTotal[size_t(area)]+=prop.reward;++log.total;}
    log.areaFirst[size_t(DevourAreaAt(log.last))]=0;
}
void ChronoSystem::DevourLogEvent(const std::string& name){auto& log=devourLog_;if(log.active)log.events.push_back({log.elapsed,name,DevourSizeCm(CurrentDevourStage(),life_.bodies[0].mass)});}
// 毎フレームの記録: 時間・移動・目標と壁の到達・区画・5 秒ごとの推移
void ChronoSystem::TickDevourLog(float dt){
    auto& log=devourLog_;if(!log.active||finished_)return;const auto& stage=CurrentDevourStage();const auto& body=life_.bodies[0];float cm=DevourSizeCm(stage,body.mass);
    log.elapsed+=dt;log.sinceEat+=dt;log.longestIdle=std::max(log.longestIdle,log.sinceEat);log.frameSum+=dt;log.worstFrame=std::max(log.worstFrame,dt);++log.frames;
    V step=body.at-log.last;step.y=0;log.moved+=Length(step);log.last=body.at;log.maxCombo=std::max(log.maxCombo,devourClock_.combo);
    if(LifeWeaponForm(body.action))log.morphTime+=dt;
    for(size_t i=0;i<stage.goals.size();++i)if(log.goalTimes[i]<0&&cm+1e-3f>=stage.goals[i]){log.goalTimes[i]=log.elapsed;DevourLogEvent("goal_"+DevourRankName(stage,int(i)+1));}
    for(size_t i=0;i<stage.gates.size();++i)if(log.gateSizeTimes[i]<0&&cm+1e-3f>=stage.gates[i]){log.gateSizeTimes[i]=log.elapsed;DevourLogEvent("gate_size_"+std::to_string(i+1));}
    int area=DevourAreaAt(body.at);if(log.areaFirst[size_t(area)]<0){log.areaFirst[size_t(area)]=log.elapsed;DevourLogEvent("enter_area_"+std::to_string(area));}
    if(log.elapsed>=log.nextSample){log.nextSample+=5;log.samples.push_back({log.elapsed,cm,body.mass,log.eaten,area,body.at.x,body.at.z,Length(step)/std::max(dt,1e-4f),devourClock_.combo,log.frames/std::max(log.frameSum,1e-4f)});}
    if(log.elapsed>=log.nextSave){log.nextSave+=15;SaveDevourLog("playing");}
}
// 物を飲んだときの記録
void ChronoSystem::DevourLogEat(const LifeCollectible& prop,float gain){
    auto& log=devourLog_;if(!log.active)return;const auto& stage=CurrentDevourStage();auto& body=life_.bodies[0];
    int by=devourEatBy_==0&&body.action==LifeAction::Tire?5:devourEatBy_,area=DevourAreaAt(prop.at);log.sinceEat=0;
    if(!prop.orb){++log.eaten;++log.areaEaten[size_t(area)];}
    ++log.eatBy[size_t(by)];log.gainBy[size_t(by)]+=gain;log.areaGain[size_t(area)]+=gain;
    if(prop.radius>log.biggest){log.biggest=prop.radius;log.biggestAt=log.elapsed;}
    // 積み木の壁を初めて飲んだ時刻
    for(size_t i=0;i<stage.gates.size();++i)if(log.gateOpenTimes[i]<0&&std::abs(prop.radius-DevourCoverRatio*DevourBodyRadius(DevourGoalMass(stage,stage.gates[i]))*.98f)<.01f){log.gateOpenTimes[i]=log.elapsed;DevourLogEvent("gate_open_"+std::to_string(i+1));}
}
// 記録を書く。result は playing (途中)・time_up・all_eaten
void ChronoSystem::SaveDevourLog(const char* result){
    auto& log=devourLog_;if(!log.active)return;const auto& stage=CurrentDevourStage();const auto& body=life_.bodies[0];bool room=stage.map=="room";int areas=room?4:RingStyleCount;
    float cm=DevourSizeCm(stage,body.mass),fps=log.frames/std::max(log.frameSum,1e-4f);int rank=DevourRankAt(stage,body.mass);bool done=std::string(result)!="playing";
    using nlohmann::json;
    try{auto folder=DevourLogFolder();std::filesystem::create_directories(folder);
        json data{{"version",1},{"build",__DATE__},{"stamp",log.stamp},{"stage",stage.id},{"stageIndex",devourStage_},{"result",result},{"rank",DevourRankName(stage,rank)},{"clear",rank>0},
            {"finalCm",cm},{"finalMass",body.mass},{"bestMassBefore",log.bestBefore},{"timeLimit",stage.time},{"played",log.elapsed},{"left",devourClock_.left},
            {"eaten",log.eaten},{"total",log.total},{"moved",log.moved},{"longestIdle",log.longestIdle},{"maxCombo",log.maxCombo},{"pounces",log.pounces},{"bounces",log.bounces},{"hops",log.hops},
            {"biggestRadius",log.biggest},{"biggestAt",log.biggestAt},{"itemTaken",log.itemTaken},{"morphs",log.morphs},{"morphTime",log.morphTime},{"avgFps",fps},{"worstFrameMs",log.worstFrame*1000},
            {"goals",stage.goals},{"goalTimes",log.goalTimes},{"gates",stage.gates},{"gateSizeTimes",log.gateSizeTimes},{"gateOpenTimes",log.gateOpenTimes}};
        data["tuning"]={{"growth",DevourGrowth},{"coverRatio",DevourCoverRatio},{"leapRange",DevourLeapRange(LifeStartMass)},{"coverRadius",DevourCoverRadius(LifeStartMass)},{"pounceCooldown",DevourPounceCooldown},{"itemTime",stage.itemTime},{"half",stage.half},{"vacuumRange",DevourVacuumRange(LifeStartMass)}};
        for(int i=0;i<6;++i)data["eatBy"][kEatNames[i]]={{"count",log.eatBy[size_t(i)]},{"mass",log.gainBy[size_t(i)]}};
        for(int i=0;i<areas;++i)data["areas"][room?kRoomAreas[i]:kRingAreas[i]]={{"firstEnter",log.areaFirst[size_t(i)]},{"eaten",log.areaEaten[size_t(i)]},{"total",log.areaTotal[size_t(i)]},{"massEaten",log.areaGain[size_t(i)]},{"massTotal",log.areaMassTotal[size_t(i)]}};
        data["events"]=json::array();for(const auto& event:log.events)data["events"].push_back({{"t",event.t},{"name",event.name},{"cm",event.cm}});
        data["samples"]=json::array();for(const auto& s:log.samples)data["samples"].push_back({{"t",s.t},{"cm",s.cm},{"mass",s.mass},{"eaten",s.eaten},{"area",s.area},{"x",s.x},{"z",s.z},{"speed",s.speed},{"combo",s.combo},{"fps",s.fps}});
        log.path=folder/(log.stamp+"_"+stage.id+".json");{std::ofstream file(log.path);file<<data.dump(1);}
        if(!done)return;
        // 一覧に 1 行足す (表計算で開ける)
        auto summary=folder/L"summary.csv";bool fresh=!std::filesystem::exists(summary);std::ofstream file(summary,std::ios::app);
        if(fresh){file<<"stamp,stage,result,rank,final_cm,time_limit,played_s,left_s,eaten,total,goal1_s,goal2_s,goal3_s,goal4_s,gate1_size_s,gate1_open_s,gate2_size_s,gate2_open_s,item_taken_s,morph_s,pounces,bounces,max_combo,longest_idle_s,moved";
            for(const char* name:kEatNames)file<<",mass_"<<name;file<<",avg_fps,worst_frame_ms\n";}
        auto at=[](const std::vector<float>& values,size_t i){return i<values.size()?DevourLogNumber(values[i]):std::string("-1");};
        file<<log.stamp<<','<<stage.id<<','<<result<<','<<DevourRankName(stage,rank)<<','<<DevourLogNumber(cm)<<','<<DevourLogNumber(stage.time)<<','<<DevourLogNumber(log.elapsed)<<','<<DevourLogNumber(devourClock_.left)<<','<<log.eaten<<','<<log.total;
        for(size_t i=0;i<4;++i)file<<','<<at(log.goalTimes,i);
        for(size_t i=0;i<2;++i)file<<','<<at(log.gateSizeTimes,i)<<','<<at(log.gateOpenTimes,i);
        file<<','<<DevourLogNumber(log.itemTaken)<<','<<DevourLogNumber(log.morphTime)<<','<<log.pounces<<','<<log.bounces<<','<<log.maxCombo<<','<<DevourLogNumber(log.longestIdle)<<','<<DevourLogNumber(log.moved);
        for(int i=0;i<6;++i)file<<','<<DevourLogNumber(log.gainBy[size_t(i)]);
        file<<','<<DevourLogNumber(fps)<<','<<DevourLogNumber(log.worstFrame*1000)<<'\n';
        log.active=false;
    }catch(...){log.active=false;}
}
}
