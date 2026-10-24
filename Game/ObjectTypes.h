// ObjectTypes.h
// ゲーム (アプリケーション層) で使う ECS コンポーネントと列挙型をまとめて読み込むヘッダー。
// シーン上のエンティティ (entt::entity) は、これらの構造体を組み合わせて振る舞いを持つ。
// 定義は役割ごとに Components/ に分けている。エディタの保存・読み込み (Editor/EditorSceneIO.cpp) もこの定義に合わせて JSON を読み書きする。
#pragma once
#include "Components/ComponentBase.h"           // 基底の Component・種類の番号・エンティティの別名
#include "Components/CoreComponents.h"          // 名前・親子・位置・タグ・変数・モーション・スクリプト・体力
#include "Components/RenderComponents.h"        // メッシュ・アニメーター・パーティクル・ライト・川・流体エミッター
#include "Components/PhysicsComponents.h"       // 当たり判定・剛体
#include "Components/AudioComponents.h"         // 音の発信源と、音を聞く位置
#include "Components/UIComponents.h"            // UI の配置・画像・文字・ボタン・頭上の HP バー
#include "Components/LegacyActionComponents.h"  // 旧アクションゲーム (缶・ボス戦)
