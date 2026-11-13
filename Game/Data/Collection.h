// Collection.h
// 図鑑: 飲みこんだ物の種類と数。項目は Resources/Data/collection.json、集めた数は保存の記録
// (%LOCALAPPDATA%/LiquidTime/slime-lab-v1.json の "collection") に書く。
// 結果画面の「おなかの中」とタイトルの「ずかん」が同じ記録を読む。
#pragma once
#include "GameData.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <Windows.h>

namespace Game::Collection {

// 図鑑の 1 項目。image は画像の名前 (Resources/Textures/UI/Collection/<image>.png)、model は 3D で見せるモデル
// stand は寝かせて作ったモデル (上の面が表、先が +z) を、3D で見せるとき起こして表をこちらへ向ける印 (将棋の駒)
struct Entry {std::string id,name,text,image,model;std::vector<std::string> ids;bool stand=false;};

// 項目の一覧 (図鑑の順)
inline const std::vector<Entry>& Entries(){
    static const std::vector<Entry> entries=[]{std::vector<Entry> out;
        try{for(const auto& e:Data::Require(Data::Load("collection.json"),"entries","collection.json")){Entry entry;entry.id=e.at("id").get<std::string>();
                entry.name=e.at("name").get<std::string>();entry.text=e.value("text",std::string());entry.image=e.value("image",entry.id);
                // 3D は画像と同じモデルで見せる (トランプは赤い札、町のたてものは家)
                entry.model=entry.image;entry.stand=e.value("stand",false);
                if(e.contains("ids"))for(const auto& id:e.at("ids"))entry.ids.push_back(id.get<std::string>());else entry.ids.push_back(entry.id);
                out.push_back(std::move(entry));}}
        catch(const std::exception& error){OutputDebugStringA((std::string("Collection: ")+error.what()+"\n").c_str());}
        return out;}();
    return entries;
}
// 項目の番号 (無ければ -1)
inline int IndexOf(const std::string& id){const auto& list=Entries();for(size_t i=0;i<list.size();++i)if(list[i].id==id)return int(i);return -1;}
// 置いた物の名前 (NameComponent) から項目の id を求める。図鑑に無い物は空
inline std::string KeyOfName(const std::string& name){
    static const std::map<std::string,std::string> byModel=[]{std::map<std::string,std::string> out;for(const auto& e:Entries())for(const auto& id:e.ids)out[id]=e.id;return out;}();
    std::string model;
    const std::string lowpoly="Chrono Lowpoly ";
    if(name.compare(0,lowpoly.size(),lowpoly)==0)model=name.substr(lowpoly.size());
    else if(name.find("ring building")!=std::string::npos)model="building";
    else return {};
    auto found=byModel.find(model);return found==byModel.end()?std::string():found->second;
}
inline std::string ImagePath(const Entry& entry,bool silhouette=false){return "Resources/Textures/UI/Collection/"+entry.image+(silhouette?"_sil.png":".png");}

// 保存の記録の場所 (ScaleUI の LabPath と同じ)
inline std::filesystem::path SavePath(){
    wchar_t buffer[32768]{};const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
    return n>0&&n<32768?std::filesystem::path(buffer)/L"LiquidTime"/L"slime-lab-v1.json":std::filesystem::path{};
}
// 集めた数 (項目の id → これまでに飲みこんだ数)
using Counts=std::map<std::string,int>;
inline Counts Load(){
    Counts out;
    try{std::ifstream file(SavePath());if(!file)return out;nlohmann::json data;file>>data;
        if(data.contains("collection")&&data["collection"].is_object())for(const auto& [id,count]:data["collection"].items())if(count.is_number_integer())out[id]=count.get<int>();}
    catch(...){}
    return out;
}
// 今回飲みこんだ物を足して保存する。今回はじめて見つけた項目の id を返す
inline std::vector<std::string> Add(const std::vector<std::string>& eaten){
    std::vector<std::string> fresh;if(eaten.empty())return fresh;
    // 自動テストでは本物の記録を書き換えない
    if(wcsstr(GetCommandLineW(),L"--slime-life-smoke")||wcsstr(GetCommandLineW(),L"--ui-smoke"))return fresh;
    try{const auto path=SavePath();if(path.empty())return fresh;nlohmann::json data=nlohmann::json::object();
        {std::ifstream file(path);if(file)file>>data;}
        if(!data.contains("collection")||!data["collection"].is_object())data["collection"]=nlohmann::json::object();
        auto& counts=data["collection"];
        for(const auto& id:eaten){const int before=counts.value(id,0);if(before==0&&std::find(fresh.begin(),fresh.end(),id)==fresh.end())fresh.push_back(id);counts[id]=before+1;}
        std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<data.dump();}
    catch(...){OutputDebugStringA("Collection: could not be saved.\n");}
    return fresh;
}
}
