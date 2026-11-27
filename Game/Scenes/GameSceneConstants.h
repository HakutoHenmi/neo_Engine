// GameSceneConstants.h
// GameScene (GameScene*.cpp) で使う調整値・決まった値をまとめたもの。
// 数値をコードに直接書かず、ここで名前と意味を付けてから使う。
#pragma once

#include "../../Engine/Math/Matrix4x4.h"
#include <DirectXMath.h>
#include <cstdint>

namespace Game::GameSceneConstants {

// ------------------------------------------------------------
// カメラ
// ------------------------------------------------------------
namespace CameraDefaults {
// 縦の視野角 (45 度)
inline constexpr float kFovY = DirectX::XM_PIDIV4;
// 描画する奥行きの範囲
inline constexpr float kNearZ = 0.1f;
inline constexpr float kFarZ = 10000.0f;
// ゲームとエディタのカメラの初期位置と、見下ろす角度 (ラジアン)
inline constexpr float kStartX = 0.0f;
inline constexpr float kStartY = 2.0f;
inline constexpr float kStartZ = -5.0f;
inline constexpr float kStartPitch = 0.2f;
} // namespace CameraDefaults

// 停止中にプレイヤーを映すときのカメラ (PLAY 中の追従カメラと同じ見え方にする)
namespace StoppedPreviewCamera {
// 最低限離れる距離と、プレイヤーの大きさ 1 あたりに離れる距離
inline constexpr float kMinDistance = 17.0f;
inline constexpr float kDistancePerScale = 8.0f;
// 注視点をプレイヤーの足元から上げる高さ
inline constexpr float kLookHeight = 1.5f;
// 見下ろす角度 (ラジアン)
inline constexpr float kPitch = 0.2f;
} // namespace StoppedPreviewCamera

// 環境光の色
inline constexpr Engine::Vector3 kAmbientColor = {0.4f, 0.4f, 0.45f};

// 箱の角の数
inline constexpr int kBoxCornerCount = 8;

// ------------------------------------------------------------
// フレームの時間
// ------------------------------------------------------------
// これより長く止まったフレーム (極端なラグ) は kFallbackDeltaTime として扱う
inline constexpr float kMaxDeltaTime = 1.0f / 10.0f;
inline constexpr float kFallbackDeltaTime = 1.0f / 60.0f;
// アニメーションの 1 秒あたりのフレーム数
inline constexpr float kAnimationFramesPerSecond = 60.0f;
// アニメーションを別スレッドで処理するときの 1 まとまりの件数
inline constexpr uint32_t kAnimationJobBatchSize = 64;

// ------------------------------------------------------------
// エンティティの探索
// ------------------------------------------------------------
// 高さを調べるレイが何にも当たらなかったときの高さ
inline constexpr float kNoGroundHeight = -10000.0f;
// 当たった面の最高点を探すときの初期値 (どの面よりも低い値)
inline constexpr float kLowestSurfaceHeight = -1000.0f;
// 親子をたどる深さの上限 (これを超えたら循環とみなす)
inline constexpr int kMaxHierarchyDepth = 32;

// ------------------------------------------------------------
// ポーズ・グラフィック設定画面
// ------------------------------------------------------------
// 保存失敗の注意文の位置と文字の大きさ
inline constexpr float kSaveFailedTextX = 380.0f;
inline constexpr float kSaveFailedTextY = 510.0f;
inline constexpr float kSaveFailedTextSize = 18.0f;

// ------------------------------------------------------------
// プレイヤーのスライム (GPU 流体での見た目)
// ------------------------------------------------------------
namespace PlayerFluid {
// プレイヤーが見つからないときの中心と体の半径
inline constexpr DirectX::XMFLOAT3 kDefaultCore = {0.0f, 1.0f, 0.0f};
inline constexpr DirectX::XMFLOAT3 kDefaultRadii = {1.2f, 0.85f, 1.2f};
// プレビュー時に、エミッターからどれだけ下を中心にするか
inline constexpr float kPreviewCoreDrop = 3.0f;
// 最初に粒子を放出するときの下向きの速さ
inline constexpr float kSpawnFallSpeed = 2.0f;
// 粒子を体の中心へ引き寄せる力
inline constexpr float kAttraction = 32.0f;
// 流体の中心 (圧力の源) を体の中心から上げる高さ
inline constexpr float kCoreLift = 0.18f;
// 流体の描き方 (Renderer::SetGPUFluidCore の mode)
inline constexpr float kModeDefault = 2.0f;
inline constexpr float kModeInkPlayer = 4.0f;
inline constexpr float kModeDodgeLiquid = 5.0f;
// 流体の大きさの基準 (スライムの最大質量をこの値に換算する)
inline constexpr float kMassScale = 100.0f;
// ちょうど避けた直後に一瞬だけ見た目を縮める時間と倍率
inline constexpr float kPerfectShrinkSeconds = 0.16f;
inline constexpr float kPerfectShrinkScale = 0.18f;
// 被弾時の点滅 (時間・速さ・明るさ)
inline constexpr float kDamageFlashSeconds = 0.6f;
inline constexpr float kDamageFlashFrequency = 70.0f;
inline constexpr float kDamageFlashIntensity = 0.75f;
// 不安定なときのゆっくりした明滅 (しきい値・速さ・明るさ)
inline constexpr float kUnstableThreshold = 75.0f;
inline constexpr float kUnstablePulseFrequency = 10.0f;
inline constexpr float kUnstablePulseIntensity = 0.35f;
} // namespace PlayerFluid

// 分身の流体を使わないときに置いておく、画面外の位置
inline constexpr float kHiddenDecoyY = -1000.0f;

// 水のエミッターの真下にいるときの回復
namespace WaterHeal {
// 水とみなす流体の種類のしきい値
inline constexpr float kWaterTypeThreshold = 0.5f;
// 真下とみなす水平の半径
inline constexpr float kRadius = 1.5f;
// 1 回復する間隔 (秒)。0.05 秒ごとなので 1 秒で 20 回復する
inline constexpr float kInterval = 0.05f;
} // namespace WaterHeal

// ------------------------------------------------------------
// エディタの線 (ギズモ)
// ------------------------------------------------------------
namespace Gizmo {
// 選択中の箱 (黄)・箱の当たり判定 (緑)・メッシュの当たり判定 (青 / 重なり中は赤)
inline constexpr Engine::Vector4 kSelectionColor = {1.0f, 0.85f, 0.0f, 1.0f};
inline constexpr Engine::Vector4 kBoxColliderColor = {0.2f, 1.0f, 0.2f, 0.8f};
inline constexpr Engine::Vector4 kMeshColliderColor = {0.2f, 0.2f, 1.0f, 0.8f};
inline constexpr Engine::Vector4 kMeshColliderHitColor = {1.0f, 0.2f, 0.2f, 0.8f};
// 変形ツール: 軸の長さ・矢印の先の長さ・矢印の開き具合
inline constexpr float kAxisLength = 2.0f;
inline constexpr float kArrowLength = 0.3f;
inline constexpr float kArrowSpread = 0.4f;
// 変形ツール: 回転の円の分割数と半径、拡大の × 印の大きさ
inline constexpr int kRotateRingSegments = 32;
inline constexpr float kRotateRingRadius = 1.5f;
inline constexpr float kScaleMarkSize = 0.15f;
// 軸の色の濃さ (通常 / ドラッグ中)
inline constexpr float kAxisDim = 0.2f;
inline constexpr float kAxisHighlight = 0.6f;

// 地面の格子: 範囲 (±)・間隔・濃く描く間隔・線の透明度と色
inline constexpr float kGridHalfSize = 100.0f;
inline constexpr float kGridStep = 1.0f;
inline constexpr float kGridMajorStep = 10.0f;
inline constexpr float kGridEpsilon = 0.01f;
inline constexpr float kGridMajorAlpha = 0.35f;
inline constexpr float kGridMinorAlpha = 0.15f;
inline constexpr float kGridGray = 0.6f;
inline constexpr Engine::Vector4 kGridAxisXColor = {0.8f, 0.2f, 0.2f, 0.7f};
inline constexpr Engine::Vector4 kGridAxisZColor = {0.2f, 0.2f, 0.8f, 0.7f};
// 原点の XYZ 軸の長さ
inline constexpr float kOriginAxisLength = 1.5f;

// ライト: 向きの線の長さ・十字の大きさ・透明度 (選択中 / 通常)
inline constexpr float kLightDirectionLength = 5.0f;
inline constexpr float kLightCrossSize = 0.5f;
inline constexpr float kLightSelectedAlpha = 1.0f;
inline constexpr float kLightIdleAlpha = 0.4f;
inline constexpr Engine::Vector4 kDirectionalLightColor = {1.0f, 0.9f, 0.2f, 1.0f};
inline constexpr Engine::Vector4 kPointLightColor = {0.2f, 0.9f, 0.2f, 1.0f};
inline constexpr Engine::Vector4 kSpotLightColor = {0.2f, 0.8f, 1.0f, 1.0f};
} // namespace Gizmo

} // namespace Game::GameSceneConstants
