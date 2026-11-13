// DevourGimmickBehaviors.h
// 部屋のギミックの種類ごとの動き (Strategy Pattern)。ChronoSystem の内部向け (ChronoSystemInternal.h から読み込む)。
//
// 以前は「乗り始め」「乗っている間の動き」「乗っている間の入力」「仕掛けの毎フレームの動き」「操作の札」
// 「カメラの構図」「照準」「物を置かない範囲」が、それぞれ別の関数の中で GimmickKind の switch / if 列になっており、
// 種類を 1 つ足すたびに 8 か所を探して直す必要があった。
// 種類ごとのクラス (DevourGimmick.cpp) にまとめ、ChronoSystem は GimmickBehaviorFor(kind) の共通の
// インターフェースだけを呼ぶ。動きのオブジェクトはデータを持たない不変の共有インスタンスで、
// 状態 (時間・狙い・倒れたピンなど) は今まで通り DevourGimmick / GimmickRide に置く。
#pragma once
#include "ChronoSystem.h"

namespace Game {

// 乗っている間の 1 フレームの情報。Step は next (体の位置) と end (乗り終わったか) を書き換える
struct ChronoSystem::GimmickRideStep {
    entt::registry& r;
    GameContext& ctx;
    DevourGimmick& g;
    GimmickRide& ride;
    Chrono::LifeBody& body;
    float dt, scale, form;
    V wish, right, next;
    bool end = false;
    // 乗り始めの位置から決まった位置へ、形を作りながら寄せる
    V Settle(V to) const { V at = Lerp(ride.from, to, form); at.y = to.y + 1.25f; return at; }
};

// ギミックの種類ごとの動きのインターフェース
class ChronoSystem::GimmickBehavior {
public:
    virtual ~GimmickBehavior() = default;
    // 目印の看板のモデル
    virtual const char* SignModel() const = 0;
    // 乗り始め: 体の形と、最初の位置・向きを決める
    virtual void Begin(ChronoSystem& sys, DevourGimmick& g, GimmickRide& ride, const Chrono::LifeBody& body, float floorY) const = 0;
    // 乗っている間の体の動き
    virtual void Step(ChronoSystem& sys, GimmickRideStep& step) const = 0;
    // 乗っている間の入力 (撃つ・やめる)。押した入力を使ったら true
    virtual bool RideInput(ChronoSystem&, DevourGimmick&, GimmickRide&, bool pressed, bool) const { return pressed; }
    // 乗っていなくても毎フレーム動く仕掛け (だるまの輪・ドミノの連鎖)
    virtual void TickWorld(ChronoSystem&, entt::registry&, GameContext&, size_t, float) const {}
    // 乗っている間の操作の札
    virtual void RidePrompts(std::vector<UI::Scale::Prompt>&) const {}
    // 乗っている間のカメラの構図。変えるなら true
    virtual bool CameraSubject(const ChronoSystem&, const DevourGimmick&, Chrono::LifeCameraSubject&) const { return false; }
    // 乗っている間の画面の印 (照準など)
    virtual void DrawMarks(const ChronoSystem&, const DevourGimmick&, UI::Canvas&, GameContext&) const {}
    // 輪の外で、散らかした物を置かない範囲 (道・台のまわりなど) に入っているか
    virtual bool KeepsClear(const DevourGimmick&, V, float) const { return false; }
};

} // namespace Game
