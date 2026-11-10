// ChronoSystem.h
// 現在のゲーム (スライム) の中心になるシステムの宣言。
// 本体は ChronoSystem.cpp と、機能ごとに分けた .inl (SlimeLife・CityStage・HomingDomains など) にある。
#pragma once
#include "ISystem.h"
#include "../Chrono/ChronoComponents.h"
#include "../Chrono/InkRules.h"
#include "../Chrono/LiquidTrail.h"
#include "../Chrono/DomainHighlight.h"
#include "../Chrono/CinematicRules.h"
#include "../Chrono/SlimeWeaponRules.h"
#include "../Chrono/SlimeHydrantRules.h"
#include "../Chrono/SlimeSignalRules.h"
#include "../Chrono/CityStageRules.h"
#include "../Chrono/DevourRules.h"
#include "../Chrono/RingCityRules.h"
#include "../Chrono/DevourStageRules.h"
#include "../Chrono/DevourGimmickRules.h"
#include "../Chrono/DevourSpillRules.h"
#include <filesystem>
#include <functional>
#include <array>
#include <map>
#include <unordered_map>
#include <vector>
namespace Game {
namespace UI {class Canvas;namespace Scale {struct Prompt;}}
namespace CityParts {struct CityBuilder;}
// シーン全体のまとめ役。キャラクターの残る状態はすべて ECS のコンポーネントにある
class ChronoSystem final : public ISystem {
    friend class SlimeDevTools; // エディタ専用の開発ツール (Game/Editor/SlimeDevTools.cpp) から中を触れるようにする
    friend class DevourAnimEditor; // 吸収アニメーションのエディタ (Game/Editor/DevourAnimEditor.cpp)
public:
    // クリア後も更新を続けるシステムとして作る
    explicit ChronoSystem(GameScene* scene):ISystem(true),scene_(scene){}
    // PLAY 開始時の初期化 / 毎フレームの更新 / 3D の描画 / UI の描画
    void Reset(entt::registry&) override;
    void Update(entt::registry&,GameContext&) override;
    void Draw(entt::registry&,GameContext&) override;
    void DrawUI(entt::registry&,GameContext&) override;
    // ゲームが終わったか / ムービー中か / スライムの研究所を開いているか
    bool Finished() const {return finished_;}
    // 吸収モードの結果: カメラが寄る間に顔を向け、測り始めたら体を止める
    float DevourResultFace() const {return devour_&&finished_?std::min(1.f,resultCameraAge_/.8f):0.f;}
    bool DevourResultFrozen() const {return devour_&&finished_&&resultCameraAge_>=kResultFreezeAge;}
    // 鳴り続ける音を止める (シーンを抜けるとき)
    void StopLoopSounds();
    // スライムの状態・敵・街の門・消火栓の狙いを外 (エディタやテスト) から読むための関数
    const Chrono::SlimeLife& LifeState()const {return life_;}
    // 移動を街の通れる範囲に収めた位置 (テストで通れるかを確かめる)
    Chrono::Vec MoveWithinCity(Chrono::Vec from,Chrono::Vec to,float radius)const {return ConstrainLifeMovement(from,to,radius);}
    const std::vector<Chrono::SwarmEnemy>& LifeEnemies()const {return swarm_;}
    // 吸収モードか・制限時間と大きさの段階 (テストから読み書きする)
    bool DevourMode()const {return devour_;}
    // 自動テスト用: 輪の壁を全部開ける
    void DevourOpenAllRings(){devourOpenAll_=true;}
    void DevourUseAllForms(){devourFreeForms_=true;}
    const std::filesystem::path& DevourLogPath()const{return devourLog_.path;}
    float DevourBiteAge()const{return devourBiteAge_;}
    int DevourRank()const{return devourRank_;}
    int DevourStageIndex()const{return devourStage_;}
    const Chrono::DevourStage& DevourStageInfo()const{return CurrentDevourStage();}
    bool DevourItemReady()const{return devourItemReady_;}
    void DevourSetMorphLeft(float seconds){devourMorphLeft_=seconds;}
    Chrono::DevourClock& DevourClockState() {return devourClock_;}
    // 変形アイテムを初めて取ったときの見せ場 (カメラが正面へ寄り、周りの時間が止まる) の最中か
    bool MorphShowcaseActive()const {return showcase_.active;}
    // 手応えの設定 (ヒットストップ秒・揺れの秒・揺れの大きさ・揺れの速さ・振動の重い側・軽い側・振動の秒)
    struct LifeImpactSpec {float stop=0,shakeTime=0,shake=0,frequency=14,low=0,high=0,rumble=0;};
    // ステージ開始のカメラ演出の最中か
    const Chrono::CityGates& CityGateState()const {return cityGates_;}
    const Chrono::HydrantAim& HydrantAimState()const {return hydrantAim_;}
    // 確認用: 区画の出口を手前から exits 個だけ壊したように開ける (1 路地の壁・2 バリケード・3 鉄の門)
    void OpenCityGates(entt::registry& r,int exits=3){
        if(exits>=1)for(size_t i=0;i<Chrono::CityWalls().size();++i)if(!cityGates_.WallOpen(i))BreakCityWall(i);
        if(exits>=2&&!cityGates_.barricadeOpen)BreakCityGate(r,true,false);
        if(exits>=3&&!cityGates_.gateOpen){cityGateUnlocked_=true;BreakCityGate(r,false,false);}}
    // スライムの研究所の画面を描く
    // 次の Update で作り直す (シーンを空にしたときも同じ)
    void Invalidate() {initialized_=false;}
    void Clear(entt::registry&) override { Invalidate(); }
private:
    // スライムのカメラの対象 (体の中心や大きさ)
    Chrono::LifeCameraSubject GetLifeCameraSubject()const;
    // 保存を読み込み済みか・デモのシーンか
    bool lifeLoaded_=false,slimePlayerDemo_=false;
    // 吸収モード (DevourRules.h): 敵なしで、制限時間内に全身で飛びかかって物を飲み込み、街を飲み込む。
    // デモ (スライム試験場) 以外は常にこのモード
    bool devour_=false;
    Chrono::DevourClock devourClock_{};
    // 飛びかかり: 跳んだ位置・目標の小物 (無ければ -1)・覆えるか・覆う処理を済ませたか
    struct DevourPounce {Chrono::Vec from{},to{};int target=-1;bool fits=true,covered=false,bounced=false;float seed=0;} devourPounce_;
    // 体の中に見えたまま溶けていく物 (小物の見た目・体の中心からのずれ・経過時間・体に収める縮小)
    // dir は体の中心から見た向き (飲んだ方向)、now は今の深さ (1 で表面)、lift は前の物の外へ積む量 (体の半径が単位)。古い順に並ぶ
    struct DevourMeal {uint32_t visual=0xffffffffu;Chrono::Vec dir{},tint{};float age=0,fit=1,radius=1,now=1,lift=0;bool orb=false;};
    // ギミックの形になっている割合 (0 普段の体〜1 形)。体の中の物の皮を消し、物を小さく形の芯へ集める
    float devourMealMorph_=0;
    bool DevourMealsMorphed()const{return DevourToolShape()>0;}
    // 形の中の物の位置と縮める倍率 (extent は倍率 1 のときの物の半分の幅)
    Chrono::Vec GimmickMealAt(const DevourMeal&)const;float GimmickMealFit(const DevourMeal&,float extent)const;
    std::vector<DevourMeal> devourMeals_;
    // 飲み込んで、まだ体へ移していない量
    float devourDigest_=0;
    // 最後の飲み込みからの時間とその重さ (体のつぶれとカメラの引きに使う)
    struct DevourGulp {float age=1,weight=0;} devourGulp_;
    // 自己ベストの量 (保存する)・今回で更新したか・最後の段階に届いたか
    bool devourNewBest_=false;
    // 開いた輪・輪の壁のエンティティ・門の小物の番号と開ける輪・このフレームに開いた輪 (演出を出す)
    std::array<bool,Chrono::RingStyleCount> ringOpen_{true};std::array<std::vector<entt::entity>,Chrono::RingStyleCount> ringWalls_;std::map<size_t,int> devourGates_;int devourRingOpened_=0;bool devourOpenAll_=false;
    void DevourOpenRing(entt::registry&,int ring);
    // 掃除機: 飛んでくる途中の物 (小物の番号・吸い始めた位置・経過時間)・次に吸い込むまでの時間・吸い込んでいるか
    // (触れて飲み込む物は体の中心へ、掃除機で吸う物は吸い口へ飛ぶ)
    struct DevourSuck {size_t index=0;Chrono::Vec from{};float age=0,flight=.3f;bool toBody=false;};
    std::vector<DevourSuck> devourSucks_;float devourSuckClock_=0,devourPounceWait_=0;bool devourSucking_=false;
    void DevourPull(entt::registry&,size_t index,float flight,bool toBody);
    void DevourOpenMouth(const Chrono::LifeCollectible&,bool turn);
    // 触れて飲むときの大口: 飛びかかりの形の時間 (負なら開けていない) と大きさの倍率
    float devourBiteAge_=-1,devourBiteScale_=1;
    Chrono::Vec DevourMealAt(const DevourMeal&)const;
    Chrono::Vec DevourSuckAt(const DevourSuck&)const;
    // 大きい物を飲み込んでいる間の、足が重くなる残り時間と、大きさの文字が弾む残り
    float devourChew_=0,devourSizePulse_=0;
    // 弾かれて動けない残り時間・次に弾かれるまでの時間・大物を飲んで遅い残り時間
    float devourStun_=0,devourBumpCool_=0,devourHeavy_=0,devourBumpMemory_=0;
    // 動く物: 道をたどる物 (points が 2 点以上) と、その場で踊る物。index は life_.props の番号 (最初の更新で探す)
    struct DevourMover {uint32_t visual=0;int index=-1;std::vector<Chrono::Vec> points;float speed=0,phase=0,age=0,sway=0,hop=0,baseY=0,baseYaw=0;};
    std::vector<DevourMover> devourMovers_;
    void AddRoute(entt::entity e,std::vector<Chrono::Vec> points,float speed,float phase);
    void AddDance(entt::registry& r,entt::entity e,float speed,float sway,float hop);
    void TickDevourMovers(entt::registry&,float dt);
    // 漫画の描き文字: 飲み込んだ所や、ぶつかった所に出して飛び散らせる。kind は 0 飲む音・1 ぶつかる音・2 小さくなった印 (青い下矢印と暗い影)
    struct DevourPop {Chrono::Vec at{};std::string text;float age=0,weight=0;int kind=0;float seed=0;};
    // 体から飛び出して、弧を描いて落ちる物 (着地すると拾い直せる)
    struct DevourFly {size_t index=0;Chrono::Vec from{},to{},last{},rotate{};float age=0,time=.7f,height=0,spin=0;};
    std::vector<DevourFly> devourFlies_;Chrono::Vec devourEatAt_{};
    std::vector<DevourPop> devourPops_;
    void DevourMealSkin(std::vector<Engine::Renderer::FluidTendril>&)const;
    // ---- 部屋のギミック (DevourGimmick.cpp): 床の輪の中で変形キーを押すと、体が道具の形になって決まった動きをする ----
    // parts は動かす見た目 (だるまの輪・ドミノ・ピン)、partProps はその物の番号、partRadius は止めている間の本当の大きさ。
    // rewards は成功したときにばらまく物 (最初は隠しておく)。link は液体の道の反対側の端
    struct DevourGimmick {Chrono::GimmickKind kind=Chrono::GimmickKind::Paint;Chrono::Vec at{};float radius=16,minCm=6;Chrono::Vec face{0,0,1};std::string name;
        std::vector<Chrono::Vec> path;std::vector<uint32_t> parts;std::vector<int> partProps;std::vector<float> partRadius,partAge;std::vector<Chrono::Vec> partBase,partRotate;
        std::vector<uint32_t> rewards;std::vector<int> rewardProps;Chrono::Vec burstAt{};uint32_t extra=0xffffffffu;Chrono::Vec extraBase{},extraRotate{};
        std::vector<Engine::Vector4> colors;std::vector<Chrono::Vec> spots;int link=-1,state=0,hits=0,pending=-1;float clock=0,wobble=0,scale=1;bool done=false,reversed=false;
        uint32_t ring=0xffffffffu,sign=0xffffffffu;};
    std::vector<DevourGimmick> gimmicks_;
    // 乗っている間の状態: どのギミックか・体の形・時間・道の上の距離・横のずれ・形の姿勢 (今と前のフレーム)
    struct GimmickRide {int index=-1,shape=0;float age=0,s=0,length=0,lateral=0,release=-1;Chrono::Vec anchor{},heading{0,0,1},from{};Chrono::GimmickPose pose{},previous{};bool fired=false;};
    GimmickRide gimmickRide_;int gimmickNear_=-1;
    // 作動範囲の縁から立つ光の膜 (半径 1・高さ 1 の円筒の側面)
    uint32_t gimmickCurtain_=0;
    void DrawGimmickCurtains(GameContext&);
    bool GimmickCameraSubject(Chrono::LifeCameraSubject&)const;
    // ---- 最初のチュートリアル (DevourTutorial.cpp): ベッドの下で課題を 1 つずつ出す。step が 0 以上の間は制限時間を止める ----
    // step 0 動かし方と視点・1 巻尺の見方 (ゲームを止める)・2 段差の大きさまで育つ・3 段差の上り方・4 スタートの知らせ
    struct DevourTutorial {int step=-1,page=0;float age=0,moved=0,looked=0,clear=-1,yaw=0,pitch=0;Chrono::Vec last{};bool started=false,previousAdvance=true;};
    DevourTutorial devourTutorial_;
    bool DevourTutorialHolding()const{return devourTutorial_.step>=0&&devourTutorial_.step<4;}
    void StartDevourTutorial();void TickDevourTutorial(float dt);bool TickDevourTutorialPause(float dt);void DrawDevourTutorial(GameContext&);float DevourTutorialGoal()const;
    void ClearGimmickAreas(entt::registry&);
    int GimmickAreaAt(Chrono::Vec at,float margin)const;
    // 水鉄砲の水: 銃口から当たる所まで伸びる液体の管
    struct GimmickJet {Chrono::Vec from{},to{};float age=0;};
    std::vector<GimmickJet> gimmickJets_;
    // 絵の具で塗った体の色 (w が 0 なら塗っていない) と、塗った瞬間の光
    Engine::Vector4 devourPaint_{0,0,0,0};float devourPaintFlash_=0,gimmickMatchCool_=0;
    int AddGimmick(entt::registry&,Chrono::GimmickKind kind,const std::string& name,Chrono::Vec at,float radius,float minCm,float signYaw,Chrono::Vec signAt);
    void ResetDevourGimmicks(entt::registry&);
    void HoldGimmickProp(DevourGimmick&,size_t part,bool hold);
    void BurstGimmick(entt::registry&,GameContext&,DevourGimmick&,Chrono::Vec from,float spread);
    bool TickGimmickInput(entt::registry&,Chrono::Player&,GameContext&,bool pressed,bool cancel);
    Chrono::Vec GimmickStep(entt::registry&,Chrono::Player&,GameContext&,float dt,Chrono::Vec wish);
    void TickDevourGimmicks(entt::registry&,GameContext&,float dt);
    void EndGimmickRide(bool burst);
    bool GimmickRiding()const{return gimmickRide_.index>=0;}
    // 高い所から落ちたときのヘリコプター (DevourHeli.cpp): 回転翼を回してゆっくり降りる
    struct DevourHeli {bool active=false;float age=0;Chrono::GimmickPose pose{},previous{};};
    DevourHeli devourHeli_;
    // 部屋のこぼれた液体の粒子 (DevourSpill.cpp)。visible は近くて動かし・描いているか
    struct DevourSpillFlow {struct Drop {Chrono::Vec at{},velocity{};float radius=1,age=0,speed=0,lane=0;int phase=0;uint32_t serial=0;};
        Chrono::SpillKind kind=Chrono::SpillKind::Honey;Engine::Vector4 color{1,1,1,1};Chrono::Vec source{},edge{},foot{},pool{},out{},side{},low{},high{};
        float emit=0;uint32_t serial=0,seed=1;bool visible=false,warmed=false;std::vector<Drop> drops;};
    std::vector<DevourSpillFlow> devourSpills_;
    void AddDevourSpill(Chrono::SpillKind,Engine::Vector4 color,Chrono::Vec source,Chrono::Vec edge,Chrono::Vec foot,Chrono::Vec pool);
    void TickDevourSpills(GameContext&,float dt);void DrawDevourSpills(GameContext&);
    void TickDevourHeli(Chrono::Player&,float dt,Chrono::Vec wish,bool blocked);
    // 体が道具の形になっている (ギミックに乗っている・ヘリコプター)。形の番号 (無ければ 0)・形になってからの時間・姿勢
    int DevourToolShape()const{return devour_&&GimmickRiding()&&gimmickRide_.shape>0?gimmickRide_.shape:devour_&&devourHeli_.active?Chrono::GimmickHeliShape:0;}
    float DevourToolAge()const{return GimmickRiding()?gimmickRide_.age:devourHeli_.age;}
    const Chrono::GimmickPose& DevourToolPose(bool previous=false)const{return GimmickRiding()?(previous?gimmickRide_.previous:gimmickRide_.pose):(previous?devourHeli_.previous:devourHeli_.pose);}
    void GimmickJets(std::vector<Engine::Renderer::FluidTendril>&)const;
    void DrawGimmickMarks(UI::Canvas&,GameContext&);
    // 乗っている間の操作 (下の操作の札) と、作動範囲の中で押せる操作 (体の上の吹き出し。無ければ false)
    void GimmickRidePrompts(std::vector<UI::Scale::Prompt>&)const;bool GimmickNearPrompt(UI::Scale::Prompt&)const;
    float GimmickFoodBonus(const Chrono::LifeCollectible&)const;
    Chrono::Vec DevourVacuumMouth()const;
    void TickDevourVacuum(entt::registry&,GameContext&,float dt,bool held);
    // バネ: 跳んでいる間の出発点・着地点・経過時間・全体の時間・高さと、次に跳べるまでの時間
    struct DevourHop {bool active=false,landed=false;Chrono::Vec from{},to{};float age=0,time=1,height=0;bool step=false;} devourHop_;float devourHopCooldown_=0,devourStepPush_=0;
    void TickDevourSpring(Chrono::Player&,float dt,bool held,Chrono::Vec wish);
    void StartDevourHop(float power,Chrono::Vec wish);
    // ステージの番号・読み込んだか・届いた目標の数・ステージごとの自己ベストの量
    int devourStage_=0,devourRank_=0;bool devourStageLoaded_=false;std::array<float,8> devourBests_{};
    // 変形アイテムを拾って使える状態か・変形の残り時間・テスト用に全部の形を使うか
    bool devourItemReady_=false,devourFreeForms_=false;float devourMorphLeft_=0;
    // 台座の上の変形アイテムの高さ
    float devourItemY_=0;
    const Chrono::DevourStage& CurrentDevourStage()const{return Chrono::DevourStages()[size_t(devourStage_)];}
    void LoadDevourStage();
    // プレイ記録 (DevourPlayLog.cpp)。飲み方は 0 触れる・1 掃除機・2 飛びかかり・3 バネ・4 ハンマー・5 タイヤ
    struct DevourPlayLog {
        struct Sample {float t,cm,mass;int eaten,area;float x,z,speed;int combo;float fps;};
        struct Event {float t;std::string name;float cm;};
        bool active=false;std::string stamp;std::filesystem::path path;
        float elapsed=0,nextSample=0,nextSave=15,moved=0,sinceEat=0,longestIdle=0,frameSum=0,worstFrame=0,morphTime=0,itemTaken=-1,biggest=0,biggestAt=0,bestBefore=0;
        int frames=0,eaten=0,total=0,pounces=0,bounces=0,hops=0,morphs=0,maxCombo=0;Chrono::Vec last{};
        std::array<int,6> eatBy{};std::array<float,6> gainBy{};
        std::array<int,8> areaEaten{},areaTotal{};std::array<float,8> areaGain{},areaMassTotal{},areaFirst{};
        std::vector<float> goalTimes,gateSizeTimes,gateOpenTimes;std::vector<Sample> samples;std::vector<Event> events;
    } devourLog_;
    int devourEatBy_=0;
    std::filesystem::path DevourLogFolder()const;
    void StartDevourLog();
    void TickDevourLog(float dt);
    void DevourLogEvent(const std::string&);
    void DevourLogEat(const Chrono::LifeCollectible&,float gain);
    void SaveDevourLog(const char* result);
    int DevourAreaAt(Chrono::Vec)const;
    Chrono::Vec DevourHopStep(float dt);
    void DevourSpringLand(entt::registry&,GameContext&);
    // 変形の並び (1 からのキーの順): 吸収モードはステージのアイテムの形だけ・試験場 (全部の形)・戦闘モード
    const std::vector<Chrono::LifeAction>& LifeForms()const{using A=Chrono::LifeAction;
        static const std::vector<A> all{A::Hammer,A::Tire,A::Vacuum,A::Spring},tire{A::Tire},vacuum{A::Vacuum},spring{A::Spring},battle{A::Hammer,A::Tire,A::Hydrant,A::Signal},
            demo{A::Hammer,A::Tire,A::Hydrant,A::Signal,A::Vacuum,A::Spring,A::Sword,A::Plane};
        if(devour_&&!devourFreeForms_){A form=Chrono::DevourItemForm(CurrentDevourStage().item);return form==A::Tire?tire:form==A::Spring?spring:vacuum;}
        return devour_?all:slimePlayerDemo_?demo:battle;}
    int LifeFormIndex()const{const auto& forms=LifeForms();for(size_t i=0;i<forms.size();++i)if(forms[i]==life_.form)return int(i);return 0;}
    // 取り込むときに周りへ体の流体をばらまく (本体と同じ流体シミュレーションの粒子)。
    // 飛んでいる範囲 (中心・半径・経過時間) は、流体を描く範囲に入れるために覚えておく
    struct DevourSplashArea {Chrono::Vec at{};float radius=0,age=0;};
    std::vector<DevourSplashArea> devourSplashes_;
    // エディタでの確認: 0 以上なら、ゲームのスライムをその時間の飛びかかりの形にして見せる (動かない)。
    // 距離は目標までの距離 (体の半径の何倍か)。エディタが毎フレーム hold を戻し、閉じれば止まる
    float devourPreviewTime_=-1,devourPreviewDistance_=3;int devourPreviewHold_=0;bool devourWasPreview_=false;
    // 前のフレームの飛びかかりの形 (体の半径を単位、進んだ分を含む) とその時間。点の速度をここからの差で求める
    std::vector<Chrono::Vec> devourPosePrevious_;float devourPoseAge_=-1;
    void DevourSplash(Chrono::Vec at,float radius,int count,float strength);
    // 路地の塀・バリケードを吸収の対象にした小物の番号 (塀は CityWalls の順)
    std::vector<int> devourWallProps_;int devourBarricadeProp_=-1;
    bool StartDevourPounce(Chrono::Player&,GameContext&);
    void TickDevourPounce(entt::registry&,Chrono::Player&,GameContext&,float);
    int DevourCapture(entt::registry&,Chrono::Vec landing,float& gained);
    void DevourTouch(entt::registry&,Chrono::Vec from);
    void TickDevourFlies(entt::registry&,float dt);
    // 飲めない物へぶつかる: 弾かれて体が縮み、飲んだ物が飛び出す
    void DevourBump(entt::registry&,Chrono::Player&,GameContext&,Chrono::Vec source,float impact);
    Chrono::Vec DevourPropBlock(entt::registry&,Chrono::Player&,GameContext&,Chrono::Vec old,Chrono::Vec next,float topSpeed);
    float DevourEat(entt::registry&,size_t index);
    void DevourRemoveProp(entt::registry&,size_t index);
    void DevourShatter(entt::registry&,GameContext&,size_t index);
    void DevourHammerHits(entt::registry&,GameContext&,const Chrono::LifeBody&,int pulse);
    void DevourSmash(entt::registry&,GameContext&,float reach);
    void FinishDevour(Chrono::Player&);
    void DevourShedBody(Chrono::Player&,GameContext&,float damage,Chrono::Vec source);
    void DrawDevourMeals(GameContext&);
    void DrawDevourUI(GameContext&);
    void BuildSlimePlayerDemo(entt::registry&);
    Chrono::Vec ConstrainLifeMovement(Chrono::Vec from,Chrono::Vec to,float radius)const;
    void UpdateSlimePlayerDemo(entt::registry&,Chrono::Player&,GameContext&);
    void DrawSlimePlayerDemoUI(GameContext&);
    // スライムの状態と、研究所のレシピ・素材・クリア数
    Chrono::SlimeLife life_;
    std::array<Chrono::SlimeRecipe,3> lifeRecipes_{};
    int lifeRecipeSlot_=0,lifeMaterials_=3,lifeClears_=0;
    // 各体の前回の行動と粒子数 (音や演出の切り替えに使う)
    std::array<int,Chrono::LifeBodyCount> lifeLastAction_{};
    std::array<uint32_t,Chrono::LifeBodyCount> lifeLastCounts_{};
    bool lifeWasLab_=false,lifeReseed_=true,lifePreviewEmitted_=false;
    // 章の見出しを出した段階と、出し始めた時刻 (life_.age)
    int lifeTitleStep_=-1;float lifeTitleStart_=0;
    // 信号機: 横断歩道 (中心・渡る向き・前方向・長さ・経過時間・ダメージの倍率・当てた人影と敵の組)・広がる輪・変形アイテムの足元と大きさ
    struct SignalWalk {Chrono::Vec center{},side{},forward{};float length=14,age=0,damage=1,size=1,dir=1;std::vector<uint64_t> hits;};
    struct SignalPulse {Chrono::Vec at{};float age=0,radius=10;Chrono::SignalColor color=Chrono::SignalColor::Red;};
    std::vector<SignalWalk> signalWalks_;std::vector<SignalPulse> signalPulses_;int signalWalkCount_=0; // 横断歩道を敷く向きを交互にする
    Chrono::Vec signalItemAt_{};static constexpr float SignalItemScale=1.4f;
    // 変形の見せ場: 形・経過時間・形が完成した時刻・寄る構図の中心と半径・戻す太陽の強さと被写界深度
    struct MorphShowcase {bool active=false,morphed=false;Chrono::LifeAction form=Chrono::LifeAction::Tire;float age=0,readyAt=-1,radius=1,sun=1;
        Chrono::Vec center{},facing{0,0,1};float dofFocus=30,dofRange=30,dofStrength=0;} showcase_;
    // ステージ開始のカメラ演出: 経過時間・スキップの長押しの時間・終わった後の暗転の残り
    // ヒットストップの残り時間 (この間はスライムの世界を止め、カメラの揺れだけを進める)
    float lifeHitStop_=0;
    // 武器の形から吸収ボタンで戻ったとき、戻りきったら吸収を始めるための受付の残り時間
    float lifeAbsorbBuffer_=0;
    // 形が変わったときのカメラの構図のつなぎ (形の種類・進み・始めと今の体からの中心のずれと半径)、消火栓の肩越しの構図の割合
    struct LifeCameraBlend {int kind=-1;float age=1;Chrono::Vec fromOffset{};float fromRadius=1;Chrono::Vec offset{};float radius=1;} cameraBlend_;
    float cameraHydrant_=0;
    // スライムの生活 (SlimeLife.inl) の処理: 触手の描画・研究所・試練・作成・更新・描画・UI・吸収・被弾
    void DrawLifeTentacles(GameContext&,const Chrono::LifeBody&);
    // 分裂と飲み込みの音
    uint32_t lifeSplitSounds_[3]{0xffffffffu,0xffffffffu,0xffffffffu},lifeEngulfSound_=0xffffffffu;
    // 吸収モードの音 (Resources/Sound/Devour/)
    struct DevourSounds {
        static constexpr uint32_t None=0xffffffffu;
        uint32_t gulpSmall[4]{None,None,None,None},gulpMid[2]{None,None},gulpBig=None,note=None,splat=None,pounce=None,bounce=None,hit=None,shatter=None,
            vacuumLoop=None,rankUp=None,rankTop=None,gateOpen=None,tick=None,hurry=None,timeUp=None,morph=None;
    } devourSounds_;
    uint32_t devourGulpTurn_=0;size_t devourVacuumVoice_=0;float devourNoteAt_=-1;
    void LoadDevourSounds();
    // 効果音の音量を掛けて鳴らす
    void PlayDevourSound(uint32_t sound,float volume,float pitch=1);
    // 飲んだ音: 重さで音を選び、コンボの音階を重ねる
    void PlayDevourGulp(float weight,int combo);
    // 残り時間の知らせ (before は前のフレームの残り時間)
    void TickDevourTimeSounds(float before);
    void BuildSlimeLife(entt::registry&);
    void UpdateSlimeLife(entt::registry&,Chrono::Player&,GameContext&);
    void DrawSlimeLife(entt::registry&,GameContext&);
    void DrawSlimeLifeUI(entt::registry&,GameContext&);
    void SyncSlimeBodies(GameContext&);
    void MeasureDevourBody(GameContext&);
    void DamageSlimeLife(entt::registry&,Chrono::Player&,float,GameContext&,Chrono::Vec);
    void LifeHit(entt::registry&,Chrono::Player&,GameContext&,uint32_t,float,Chrono::LifeAction source=Chrono::LifeAction::Idle,Chrono::Vec from={});
    bool StartLifeAbsorb(int);
    void CityTireDashHits(entt::registry&,GameContext&,const Chrono::LifeBody&,uint64_t);
    void TickLifeChild(entt::registry&,Chrono::Player&,GameContext&,size_t,float);
    void DrawLifeChildMarkers(GameContext&);
    void DrawLifeEnemyMarkers(GameContext&);
    void StartMorphShowcase(entt::registry&,GameContext&,Chrono::LifeAction);
    void UpdateMorphShowcase(entt::registry&,Chrono::Player&,GameContext&,float);
    void DrawMorphShowcase(GameContext&);
    // ステージ開始のカメラ演出 (SlimeIntro.cpp) の更新・描画
    // 手応え (ヒットストップ・カメラの揺れ・コントローラーの振動) を at で起こす。scale で揺れと振動の強さを変える
    void LifeImpact(GameContext&,const LifeImpactSpec&,Chrono::Vec at,Chrono::Vec direction={0,1,0},float scale=1);
    void FireSignal(entt::registry&,Chrono::Player&,GameContext&,Chrono::SignalColor);
    void UpdateSignalEffects(entt::registry&,Chrono::Player&,GameContext&,float);
    void DrawSignal(GameContext&);
    void DrawSignalUI(GameContext&);
    bool SaveSlimeLab();
    // 消火栓の水 (SlimeHydrant.inl): 水の発射と狙いの更新
    void UpdateHydrantWater(entt::registry&,Chrono::Player&,GameContext&,float,bool,bool);
    void UpdateHydrantAim(GameContext&);
    Chrono::HydrantAim hydrantAim_{};
    uint64_t hydrantWaterSerial_=0;
    // 街のステージ (CityStage.inl): 作った建物・CC0 の小物・門・役割ごとの敵
    // 街のモデル (メッシュ・テクスチャ・大きさ)
    struct CityModel {uint32_t mesh=0,texture=0;std::vector<uint32_t> extra;Chrono::Vec lo{},hi{};};
    // 吸収モードの輪の町 (RingCity.cpp): 街のステージの道具 (小物を置く・材質・色の補正) を借りて作る
    struct RingCityTools {
        std::function<const CityModel&(int)> surface;std::function<DirectX::XMFLOAT4(int)> tint;};
    void BuildRingGround(CityParts::CityBuilder&);
    void BuildRingCity(entt::registry&,const RingCityTools&);
    void BuildRoomGround(CityParts::CityBuilder&);
    void BuildRoomStage(entt::registry&,const RingCityTools&);
    void BuildRoomSets(entt::registry&);
    void BuildRoomGimmicks(entt::registry&);
    bool GimmickClear(Chrono::Vec at,float margin)const;
    // ローポリのモデル (scripts/lowpoly/build_lowpoly.py): メッシュと大きさの範囲。部屋と町で同じ置き方を使う
    struct LowModel {uint32_t mesh=0;Chrono::Vec lo{},hi{};};
    std::map<std::string,LowModel> lowModels_;uint32_t lowPalette_=0;std::vector<uint32_t> lowMaps_;float lowOcclude_=12;
    const LowModel& LowModelOf(const std::string& id);
    float LowScaleFor(const std::string& id,float width);
    void BeginLowProps(float occludeHeight);
    entt::entity LowProp(entt::registry& r,const std::string& id,Chrono::Vec at,float scale,float yaw,float fill,Engine::Vector4 tint={1,1,1,1},float tilt=0);
    void BuildStageItem(entt::registry&,const RingCityTools&);
    // 建物の敷地と、敷地ごとの吸収の対象の番号 (life_.props の番号。建物は飲み込むまで通れない)
    std::vector<Chrono::RingLot> ringLots_;std::vector<int> ringLotProps_;
    // 建物の残りの部位と当たり判定 (飲み込んだら隠し、当たり判定は消す)。キーは life_.props の番号
    std::unordered_map<size_t,std::vector<entt::entity>> devourBuildingParts_;
    // まだ閉じている輪の壁に触れるか
    bool RingWallClosed(Chrono::Vec p,float margin)const{float d=Chrono::RingDistance(p);for(int z=1;z<Chrono::RingStyleCount;++z)if(!ringOpen_[size_t(z)]&&std::abs(d-Chrono::RingWallAt(z))<margin+3)return true;return false;}
    // 吸収モードの地図の中で、半径 margin の体が歩けるか
    bool DevourWalkable(Chrono::Vec p,float margin)const{float half=CurrentDevourStage().half;if(half>0&&(std::abs(p.x)+margin>half||std::abs(p.z)+margin>half))return false;
        // (部屋の本棚やおもちゃ箱のように上に乗れる家具は、その上の面より上にいれば止めない)
        return !RingWallClosed(p,margin)&&Chrono::RingWalkable(p,margin,ringLots_,[&](size_t i){return RingBlocked(i)&&!(ringLots_[i].top>0&&p.y>=ringLots_[i].top);});}
    bool RingBlocked(size_t lot)const{int k=lot<ringLotProps_.size()?ringLotProps_[lot]:-1;return k<0||size_t(k)>=life_.props.size()||life_.props[size_t(k)].active;}
    // 街の小物の見た目
    struct CityPropVisual {uint32_t mesh=0,texture=0;std::vector<uint32_t> extra;Chrono::Vec scale{1,1,1},rotate{},center{};float height=1;std::string shader="EnvironmentSurface";Engine::Vector4 color{1,1,1,1};
        // 皮で包む形: 半分の大きさと角の丸み (0 なら飲み込む大きさの箱にする)
        Chrono::Vec half{};float round=0;};
    // 街の光る看板など (kind: 0 は光り続ける、1 は炎のように揺らぐ、2 は非常灯のように点滅する)
    struct CityGlow {Chrono::Vec center{},right{},up{},size{};Engine::Vector4 color{1,1,1,1};int kind=0;float phase=0;};
    std::vector<CityGlow> cityGlows_;
    // 街の小物 (触手で取り込める物)
    struct CityPropSeed {entt::entity entity=entt::null;Chrono::Vec at{};float required=0,reward=0;Chrono::LifeMorphItem morphItem=Chrono::LifeMorphItem::None;float radius=1,fill=1,reach=0;int ring=0,gate=0;bool beacon=false;float foot=0,height=0;};
    // バリケードや塀の部品 (材質: 0 木・1 鉄・2 ブロック・3 金属)・敵の弾・壊れた破片・踏みつけの衝撃
    struct CityPlank {Chrono::Vec at{},size{},rotate{};int material=0;};
    struct CityShot {Chrono::Vec at{},velocity{};float age=0;bool shell=false;Chrono::Vec target{};};
    struct CityDebris {Chrono::Vec at{},velocity{},spin{},size{},rotate{};float age=0;int material=0;};
    struct CityStomp {Chrono::Vec at{};float age=0,radius=30;};
    // 小物の見た目・小物・バリケード・弾・破片・衝撃の一覧
    std::map<uint32_t,CityPropVisual> cityVisuals_;
    std::vector<CityPropSeed> cityProps_;
    std::vector<CityPlank> cityBarricade_;
    // 路地裏の壁 (部品・体力・揺れ・最後に当たった攻撃)
    struct CityWallState {std::vector<CityPlank> pieces;float health=0,shake=0;uint64_t hit=0;};
    std::vector<CityWallState> cityWalls_;
    // 火事場の炎 (残りの火の量。0 で消える)・炎に触れた時間
    struct CityFire {Chrono::Vec at{};float radius=12,health=0,hiss=0;};
    std::vector<CityFire> cityFires_;
    std::vector<CityShot> cityShots_;
    std::vector<CityDebris> cityDebris_;
    std::vector<CityStomp> cityStomps_;
    // 街の敵とバリケード・門のモデル
    CityModel cityRat_,cityCannon_,cityWood_,cityStoneSkin_,cityIron_,cityArmor_,cityBlock_;
    uint32_t cityCube_=0;
    // 門のエンティティと門の状態・体力・揺れ・ヒントの時間
    entt::entity cityGateEntity_=entt::null;
    Chrono::CityGates cityGates_{};
    float cityBarricadeHealth_=0,cityGateHealth_=0,cityBarricadeShake_=0,cityGateShake_=0,cityHintClock_=0;
    uint64_t cityBarricadeHit_=0,cityGateHit_=0;
    int cityGateProp_=-1,cityAnnounced_=-1;bool cityGateUnlocked_=false,cityHammerAnnounced_=false;
    // 街のステージの作成・敵の出現・門の破壊・武器の当たり・更新・描画
    void BuildCityStage(entt::registry&);
    void BreakCityGate(entt::registry&,bool barricade,bool absorbed);
    void BreakCityWall(size_t,bool absorbed=false);
    void ResetCityHazards();
    void CityWeaponHits(entt::registry&,GameContext&,const Chrono::LifeBody&,int pulse,uint64_t hitKey);
    void UpdateCityStage(entt::registry&,Chrono::Player&,GameContext&,float);
    void DrawCityStage(GameContext&);
    bool DrawCityPropInTentacle(GameContext&,uint32_t visual,Chrono::Vec at,float shrink);
    // 群れの敵 (HordeCombat.inl / SwarmStates.inl) とその演出・メッシュ・出現の時間
    std::vector<Chrono::SwarmEnemy> swarm_;
    struct SwarmEffect {Chrono::Vec at{};float age=0,size=1;int kind=1;};
    std::vector<SwarmEffect> swarmEffects_;
    struct SwarmMesh {uint32_t handle=0;Chrono::Vec inverseExtent{1,1,1},center{};};
    std::array<SwarmMesh,3> swarmMeshes_{};
    uint32_t swarmQuad_=0,swarmNextId_=1;float swarmSpawnClock_=0,swarmSoundClock_=0,swarmAttackClock_=0;int swarmAttackBurst_=0;
    size_t swarmAttackCursor_=0;bool swarmRestPending_=false;
    Chrono::Vec swarmEmergenceAt_{};
    // 群れの作成・描画
    void BuildSwarm(entt::registry&);
    void DrawSwarm(entt::registry&,GameContext&);
    // reach が 1e8 未満なら、origin (発射した輪) から水平距離 reach 以内の敵だけを選ぶ
    uint32_t ChooseSwarmTarget(entt::registry&,Chrono::Vec,bool,Chrono::Vec origin={},float reach=1e9f);
    // インク (ChronoInk.cpp): カメラが潜らないための地面の面・作成・カメラ
    // こぼれた液体の管のメッシュ
    uint32_t slimeBeamMesh_=0;
    std::vector<Chrono::InkSurface> inkSurfaces_;
    void BuildInk(entt::registry&);
    void InkCamera(entt::registry&,Chrono::Player&,GameContext&);
    // インクの地面の高さ
    float InkGround(Chrono::Vec,int* =nullptr)const;
    using V=Chrono::Vec;
    // シーン・プレイヤー
    GameScene* scene_;
    entt::entity player_=entt::null;
    // 当たり判定に使う箱 (地形)
    std::vector<Chrono::Bounds> solids_;
    // カメラの追従・構図・ポスト効果の強さ・音・テクスチャ
    V cameraFollow_{};
    bool cameraReady_=false;
    // 街のカメラ: 後ろが塞がれたときに持ち上げる量と、届く距離の割合 (なめらかに戻す)
    float cameraLift_=0,cameraReach_=1;
    float postStrength_=0;
    uint32_t hitSound_=0xffffffff,coreSound_=0xffffffff;
    uint32_t white_=0,sphere_=0;
    // 状態 (初期化済み・終了・勝利・前フレームの入力)
    bool initialized_=false,finished_=false,won_=false,prevSpace_=false;
    bool diagnostic_=false;
    // カメラ (向き・距離・肩越し・視野角・補助・揺れの設定)
    float yaw_=0,pitch_=0.16f,finishAge_=0;float devourHudCm_=0,resultCameraAge_=0,resultLo_=0,resultHi_=0,resultNear_=0,resultFar_=0,resultTopY_=0;static constexpr float kResultFreezeAge=.95f,kResultSettledAge=1.3f;float resultLeft_=0,resultRight_=0,resultTop_=0;bool resultScreenSet_=false;bool resultMeasured_=false;Chrono::Vec resultOrigin_{};std::vector<entt::entity> resultHidden_;
    float fov_=1.0472f;int shakeSetting_=2;bool prevShake_=false;
    // 対象へ届くか / メッシュや箱の作成 / 当たり判定の箱の記録
    entt::entity Mesh(entt::registry&,const std::string&,const std::string&,V,V);
    void CacheSolids(entt::registry&);
    // 当たり判定 (移動の掃引・空いているか・壁に沿った移動・中心)
    bool Sweep(V,V,V,float&,V&) const;
    bool Clear(V,V,float=0) const;
    V Move(V,V,V,bool&,V* =nullptr) const;
    // 攻撃の対象選び・射撃・命中・被弾・反応
    void Damage(entt::registry&,Chrono::Player&,float,GameContext&,V,float perfectWindow=.12f);
    // チェインの失敗・演出・プレイヤー・ワールド・ボス・カメラの更新と終了
    void Presentation(entt::registry&,Chrono::Player&,GameContext&);
    void Finish(Chrono::Player&,bool);
};
}
