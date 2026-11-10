// ChronoComponents.h
// ChronoSystem が使う ECS のコンポーネント (プレイヤー・入力の記録・当たり判定の箱など)。
#pragma once
#include "ChronoRules.h"
#include "../../externals/entt/entt.hpp"
namespace Game::Chrono {
// プレイヤーの行動の状態
enum class Action { Free, Extending, Pulling, Retracting, Recovery, Dodge, Collapsed };
// 固い物 / カメラを遮る物
struct Solid {};
struct CameraOccluder { Vec center{},size{};float opacity=1; };
// チェインが失敗した理由
enum class ChainFailure { None, NoTarget, OutOfRange, Obstructed, Recovering, Unstable, Missed, Fell };
// 決まった再生や確認用のシーンで使う、任意の入力の元
struct ControlFrame {
    // 移動の入力
    Vec move{};
    // 攻撃・狙い・切り替え・ジャンプ・回避・ロックオン
    bool attack=false, aim=false, toggle=false, jump=false, dodge=false,lockOn=false;
    // 強化の選択・引き直し・見送り
    int upgradeChoice=-1;bool upgradeReroll=false,upgradeSkip=false;
    int attackPattern=-1; // 確認用の上書き。通常のプレイは状況で選ぶ
    bool cameraInput=false; // スクリプトによる視点の変更は、マウスの動きと同じ優先度
    // 取り込み・研究所・やり直し・スライムに戻る
    bool absorb=false,lab=false,restart=false,returnToSlime=false;
    // 変形の種類
    int morphForm=0;
    bool morphNow=false; // 数字キーと同じく、morphForm の形へすぐ変形する
    bool demoHit=false; // practice-field input for scripted checks
    bool showcase=false; // スクリプトの確認でも変形の見せ場を出す (通常の確認は自分のタイミングで進める)
    bool intro=false; // スクリプトの確認でもステージ開始のカメラ演出を出す
    // カメラの向き
    float yaw=0, pitch=0;
};
// プレイヤーの状態 (行動・速度・量・タイマー・カメラ・狙い・先行入力・流れ・成績)
struct Player {
    Action action=Action::Free;
    // 速度・手・射撃の向きと始点・終点・回避の向き
    Vec velocity{}, hand{}, shotDirection{}, shotStart{}, endpoint{}, dodgeDirection{};
    // 量・不安定さ・タイマー・先行入力・無敵・待ち時間・コヨーテ・ジャンプの先行入力
    float mass=100, instability=0, timer=0, buffer=0, invincible=0, cooldown=0, coyote=0, jumpBuffer=0;
    // 鎖の距離・上限・負担
    float shotDistance=0, shotLimit=0, pullCost=0;
    float collisionScale=1;
    float airHang=0;
    // 時間の倍率 (狙い・世界)
    float aimScale=1, worldScale=1;
    // ヒットストップ・着地・被弾・失敗の時間
    float hitStop=0, landingAge=1, landingStrength=0, damageAge=1, failureTime=0;
    float cameraOpacity=1;
    // チェインの勢いと、タイミングの良い入力
    float chainDrive=1,lastChainHit=10; bool timedInput=false,shotTimed=false;
    // 被弾の元と復帰位置
    Vec damageSource{}, recoveryPoint{0,3,-27};
    ChainFailure failure=ChainFailure::None;
    bool automatic=false, aiming=false, grounded=false, shotManual=false;
    // 狙い・照準の候補・遮られた候補・先行入力の対象
    entt::entity target=entt::null, preview=entt::null, blockedPreview=entt::null, bufferedTarget=entt::null;
    bool bufferedManual=false;
    float dodgeBuffer=0;
    bool holdCancelled=false;
    Vec bufferedDodge{};
    Vec pullWaypoint{};
    bool pullStep=false;
    const char* damageReason="HIT";
    // コンボの流れと成績
    Flow flow;
    Stats stats;
};
}
