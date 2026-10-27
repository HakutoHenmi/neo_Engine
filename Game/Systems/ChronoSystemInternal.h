// ChronoSystemInternal.h
// ChronoSystem の実装ファイルどうしで共有する、内部向けの include と補助の関数。
// ChronoSystem の外のコードからは読み込まない (公開する機能は ChronoSystem.h にある)。
//
// ChronoSystem の実装は役割ごとにファイルを分けている:
//   ChronoSystem.cpp      共通の処理 (初期化・当たり判定・ダメージ・演出・終了・更新と描画の振り分け)
//   ChronoChainMode.cpp   旧来の確認用のモード: 鎖で敵を引き寄せる戦闘とアリーナ
//   ChronoCreature.cpp    旧来の確認用のモード: 生き物のボス (蛇と鳥)
//   ChronoInk.cpp         新しいゲームのシミュレーションの流れ全体
//   ChronoCinematic.cpp   開幕とボス出現のムービー
//   SlimeAssault.cpp      輪を描いて誘導弾を撃つ戦闘
//   HordeCombat.cpp       群れの敵との戦闘
//   HomingDomains.cpp     描いた輪から出る誘導弾
//   RogueliteCombat.cpp   1 回の挑戦の中での成長 (強化カード)
//   BossDefeat.cpp        ボスの被弾の反応と倒れる演出
//   SlimeLife.cpp         スライムの生活モード (現在のゲームの本編)
//   SlimeHydrant.cpp      消火栓に変形したスライムの水
//   CityStage.cpp         スライムの街のステージ
#pragma once
#include "../../Engine/FluidMorphShape.h"
#include "../../Engine/LoadProfile.h"
#include "../Chrono/SlimeTentacleRules.h"
#include <map>
#include "../UI/SceneMusic.h"
#include "ChronoSystem.h"
#include "GameControls.h"
#include <set>
#include <unordered_map>
#include "../UI/GameUI.h"
#include "../UI/RogueliteUI.h"
#include "../Chrono/CreatureMotion.h"
#include "../Chrono/MeadowTerrain.h"
#include "../Scenes/GameScene.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Model.h"
#include "../../Engine/Audio.h"
#include "../../externals/nlohmann/json.hpp"
#include <Windows.h>
#include <fstream>
#include <filesystem>
#include <limits>
#include <cstdio>

namespace Game {
// Chrono のゲームの規則の型 (Vec など) をそのまま使う
using namespace Chrono;

// ChronoSystem の実装ファイルで共有する補助の関数
namespace ChronoDetail {
// XMFLOAT3 と Vec の変換・前方向・キーが押されているか
inline Vec Read(const DirectX::XMFLOAT3& p){return {p.x,p.y,p.z};}
inline DirectX::XMFLOAT3 Write(Vec p){return {p.x,p.y,p.z};}
inline Engine::Vector3 EV(Vec p){return {p.x,p.y,p.z};}
inline Vec Forward(float yaw,float pitch){return {std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};}
inline bool Down(int key){return (GetAsyncKeyState(key)&0x8000)!=0;}
// 旧モードで使うモデルのパス
inline constexpr const char* grass="Resources/Models/Quaternius/Modular Platforms/Single Cube/FBX/Cube_Grass_Single.fbx";
inline constexpr const char* horn="Resources/Models/Quaternius/Enemies/FBX/Enemy.fbx";
inline constexpr const char* ball="Resources/Models/player_ball/ball.obj";
// 最高記録の保存先 (%LOCALAPPDATA%/LiquidTime/records-v1.json)
inline std::filesystem::path RecordPath(){
    wchar_t buffer[32768]{};
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
    return n>0 && n<32768 ? std::filesystem::path(buffer)/L"LiquidTime"/L"records-v1.json" : std::filesystem::path{};
}
// 秒を「分:秒.百分の一秒」の文字列にする
inline std::string TimeText(float seconds){char b[32];int s=static_cast<int>(seconds);std::snprintf(b,sizeof(b),"%02d:%02d.%02d",s/60,s%60,static_cast<int>(seconds*100)%100);return b;}
} // namespace ChronoDetail
using namespace ChronoDetail;
} // namespace Game
