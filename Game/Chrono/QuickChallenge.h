// QuickChallenge.h
// タイトルの隠しコマンドで入る「チャレンジ直行」(フル装備)。エンジンに依存しない。
//  - タイトルで ↑↑↓↓←→←→ B A (キーボードの矢印と B・A キー、またはパッドの十字キーと B・A ボタン) を入れると、チャレンジを選ぶ札が開く
//  - 決めるとセレクト画面へ進み、選んだステージは成長を飛ばして、そのチャレンジをフル装備 (ステージの物を種類ごとに最大 6 個・スロット 4 つ) で始める
//  - タイトルに戻ると解ける
#pragma once
#include <array>
#include <cstdint>
namespace Game::Chrono {
struct QuickChallengeState {
	bool active = false;
	int kind = 0;
};
inline QuickChallengeState& QuickChallenge() {
	static QuickChallengeState state;
	return state;
}
// コマンドの並び (0 上・1 下・2 左・3 右・4 B・5 A)
inline constexpr std::array<uint8_t, 10> QuickChallengeCode{0, 0, 1, 1, 2, 3, 2, 3, 4, 5};
} // namespace Game::Chrono
