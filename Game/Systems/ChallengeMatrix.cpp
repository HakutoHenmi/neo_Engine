// ChallengeMatrix.cpp
// 全部の能力 x チャレンジの効き方の実測 (--devour-battle --challenge-matrix。ChronoSystem の一部)。
// 決まった場面 (チャレンジ・配置・体の位置) を作り、何もしない 1 回と、44 種の能力を 1 つずつ使う 44 回を試して、起きたことを数える。
//  場面: 積み木 (普通の壁の前・硬い壁の前。前へ歩き続ける)・陣取り (手前の広い所。動かない。a はぬったマスの数)
// 結果は tests/out/challenge-matrix.txt (1 行 1 回の試し) と、能力ごとのまとめ (どの場面でも効かない能力)
#include "ChronoSystemInternal.h"

namespace Game {
namespace {
struct MatrixScenario {ChallengeKind kind;int pattern;float x,z;bool walk;const char* name;};
constexpr MatrixScenario kScenarios[]={
    {ChallengeKind::Blocks,0,0,5,true,"積み木_壁"},
    {ChallengeKind::Blocks,0,0,13,true,"積み木_硬い壁"},
    {ChallengeKind::Territory,0,0,-6,false,"陣取り"}};
constexpr int kTrialsPerScenario=int(BattleAction::Count)+1;
constexpr const char* kKeys[]={"throw","ride","smash","guard","heal","heli","water","roll","spin","sword","fly","spring","vacuum","rc","fu","kyo","kei","gin","kin","kaku","hi","ou","marbles","tower","domino","paint","decoy","coin","disc","bounce","homing","fan","pierce","pause","read","cage","sticky","erase","tea","roly","beckon","brew","fluff","smell"};
static_assert(std::size(kKeys)==size_t(BattleAction::Count));
constexpr float kBaseTime=1e6f;
// 試しの長さ: 積み木の塔とぬいぐるみは崩れる・弾けるまで待つ
// (陣取りは、どの能力も体液を使い終わる (いちばん長い積み木の塔で 6.3 秒) まで待つ)
float TrialLength(ChallengeKind kind,int action){if(kind==ChallengeKind::Territory)return 7.5f;return action==int(BattleAction::Tower)?6.3f:action==int(BattleAction::Decoy)?5.3f:3.5f;}
}

// 試しを始める: チャレンジを作り直し、体を場面の位置に置き、能力の状態を全部消す
void ChronoSystem::StartMatrixTrial(){
    auto& b=battle_;auto& m=b.matrix;auto& body=life_.bodies[0];
    const auto& sc=kScenarios[size_t(m.trial/kTrialsPerScenario)];
    b.challenge=sc.kind;b.forcedPattern=sc.pattern;BuildChallenge();
    // (陣取りは能力 1 回の量を測るので、取り返すもの (お掃除ロボ・扇風機) は止めておく)
    b.territory.reclaim=false;
    b.placed.clear();b.shots.clear();b.fx.clear();b.pops.clear();b.drops.clear();b.impactMemo.clear();
    b.ability=BattleAction::Throw;b.abilityLeft=0;b.abilityHit=false;b.toolShape=0;b.toolLeft=0;b.cooldown=0;
    b.guard=b.regen=b.fluff=b.dropBoost=b.heavy=b.timeStop=0;b.score=0;b.combo=0;b.comboClock=0;b.time=kBaseTime;b.velocity={};
    m.clock=0;m.used=false;m.walk=sc.walk;
    body.at=ArenaPoint(sc.x,sc.z)+V{0,ChallengeBodyGround,0};body.forward=body.liquidForward={0,0,1};yaw_=0;cameraReady_=false;lifeReseed_=true;
}

// 試しを進める: 少し待って能力を使い、決まった時間で数えて次へ
void ChronoSystem::TickChallengeMatrix(GameContext& ctx,float dt,V& input){
    auto& b=battle_;auto& m=b.matrix;
    const int total=int(std::size(kScenarios))*kTrialsPerScenario;
    // (--challenge-matrix-only=能力 で、その能力と「何もしない」だけを試す)
    static const std::string only=[]{const wchar_t* at=wcsstr(GetCommandLineW(),L"--challenge-matrix-only=");std::string out;if(at)for(at+=24;*at&&*at!=L' ';++at)out+=char(*at);return out;}();
    // (--challenge-matrix-territory で、陣取りの場面だけを試す)
    static const bool territoryOnly=wcsstr(GetCommandLineW(),L"--challenge-matrix-territory")!=nullptr;
    auto skip=[&](int trial){const int a=trial%kTrialsPerScenario-1;if(territoryOnly&&kScenarios[size_t(trial/kTrialsPerScenario)].kind!=ChallengeKind::Territory)return true;return !only.empty()&&a>=0&&only!=kKeys[a];};
    if(m.trial<0){m.trial=0;while(m.trial<total&&skip(m.trial))++m.trial;StartMatrixTrial();}
    const auto& sc=kScenarios[size_t(m.trial/kTrialsPerScenario)];const int action=m.trial%kTrialsPerScenario-1;
    m.clock+=dt;
    // 歩く: 積み木の場面は前へ歩き続ける
    input={};yaw_=0;
    if(m.walk)input={0,0,1};
    // 能力を使う (始めて 0.3 秒)
    MatrixResult* row=nullptr;
    if(!m.used&&m.clock>=.3f){m.used=true;
        if(action>=0){BattleItem item;item.radius=b.unit*.5f;++b.spent;StartBattleAbility(ctx,BattleAction(action),item);}
        MatrixResult r;r.scenario=m.trial/kTrialsPerScenario;r.action=action;r.self=b.heavy>0||b.fluff>0||b.timeStop>0||b.time>kBaseTime||b.guard>0||b.regen>0;m.results.push_back(r);}
    if(m.used&&!m.results.empty())row=&m.results.back();
    // 終わり: 決まった時間
    const bool done=m.used&&m.clock>=TrialLength(sc.kind,action);
    if(!done||!row)return;
    row->score=b.score;row->time=b.time-kBaseTime;
    if(sc.kind==ChallengeKind::Territory){row->a=TerritoryCoveredCells();for(const auto& use:b.territory.uses)if(!use.candy){row->b+=use.used;row->c=std::max(row->c,use.budget);}}
    else{row->a=b.broken[0]+b.broken[1]+b.broken[2];row->c=b.broken[1]+b.broken[2];
        for(const auto& blk:b.blocks)if(blk.alive&&(blk.hp<blk.maxHp||blk.fragile>0||blk.painted||blk.burn>0||blk.loose))++row->b;}
    ++m.trial;while(m.trial<total&&skip(m.trial))++m.trial;
    if(m.trial<total){StartMatrixTrial();return;}
    // 全部終わった: 書き出して、チャレンジを終わらせる
    WriteChallengeMatrix();m.active=false;b.time=b.clock;b.forcedPattern=-1;
    // (確認用の起動なので、書き出したらゲームを閉じる)
    PostQuitMessage(0);
}

// 書き出す: 1 行 1 回の試しと、判定 (効く・少し・自分・なし)。最後に能力ごとのまとめ
void ChronoSystem::WriteChallengeMatrix()const{
    const auto& m=battle_.matrix;std::filesystem::create_directories("tests/out");std::ofstream out("tests/out/challenge-matrix.txt");
    out<<"scenario\taction\tname\tverdict\tscore\ta\tb\tc\ttime\tself\n";
    // 何もしない 1 回 (場面ごと)
    std::vector<const MatrixResult*> base(std::size(kScenarios),nullptr);for(const auto& r:m.results)if(r.action<0)base[size_t(r.scenario)]=&r;
    // 能力ごとの一番よい判定 (0 なし・1 自分・2 少し・3 効く)
    constexpr size_t actions=size_t(BattleAction::Count);std::vector<int> best(actions,0);std::vector<std::string> where(actions);
    static constexpr const char* names[]={"なし","自分","少し","効く"};
    for(const auto& r:m.results){
        const auto& sc=kScenarios[size_t(r.scenario)];const MatrixResult* b0=base[size_t(r.scenario)];int level=0;std::string verdict;
        if(r.action<0)verdict="基準";else if(r.a>(b0?b0->a:0)){level=3;verdict="効く";}else if(r.b>0){level=2;verdict="少し";}else if(r.self||r.time>0){level=1;verdict="自分";}else verdict="なし";
        out<<sc.name<<'\t'<<(r.action<0?"(none)":kKeys[r.action])<<'\t'<<(r.action<0?"何もしない":BattleActionName(BattleAction(r.action)))<<'\t'<<verdict<<'\t'<<r.score<<'\t'<<r.a<<'\t'<<r.b<<'\t'<<r.c<<'\t'<<r.time<<'\t'<<r.self<<'\n';
        if(r.action>=0){auto& bst=best[size_t(r.action)];if(level>bst)bst=level;if(level==3){auto& w=where[size_t(r.action)];if(!w.empty())w+=",";w+=sc.name;}}}
    out<<"\n# summary (action\tname\tbest\twhere_effective)\n";
    for(size_t a=0;a<size_t(BattleAction::Count);++a)out<<kKeys[a]<<'\t'<<BattleActionName(BattleAction(a))<<'\t'<<names[best[a]]<<'\t'<<(where[a].empty()?"-":where[a])<<'\n';
}

}
