#pragma once
// ==================================================
// LoadingScreen (ロード画面)
//   シーンの初期化はメインスレッドで同期的に行う。ロード中は専用のスレッドが
//   レンダラーの UI 専用コマンドリストで約 60 fps のロード画面を描き続けるので、
//   長いロードでも画面は止まらない。メインスレッドはロードの合間に Pump() を呼んで
//   ウィンドウのメッセージを処理し続ける。タップやクリックはそのメッセージからためておき、
//   ゲームパッドはロード用スレッドで直接読む。見た目はゲーム側が
//   SetDrawer() で渡す。
// ==================================================
#include <functional>
#include <string>

namespace Engine {

class LoadingScreen {
public:
	struct State {
		std::string scene;
		float progress = 0; // 0..1、減ることのない見積もり
		float elapsed = 0;  // Begin からの秒数
		float dt = 0;       // 前のロード画面のフレームからの秒数
		bool startup = false;
		bool finished = false;
		// ロード用スレッドを始める前にメインスレッドで描く最初のフレーム:
		// 描画で使いうるテクスチャと文字にすべて触れておく。
		bool warmup = false;
		// 前のロード画面のフレームからの入力。
		int step = 0;         // -1 前へ / +1 次へ (矢印キー, A / D, 十字キー, LB / RB, スティック)
		bool clicked = false; // (clickX, clickY) で左クリック (WindowDX::kW x kH のピクセル単位)
		float clickX = 0, clickY = 0;
		float mouseX = -1, mouseY = -1;
		bool padMode = false; // ゲームパッドを使っている
	};
	using Drawer = std::function<void(const State&)>;

	static void SetDrawer(Drawer drawer);
	// メインスレッドで呼ぶ。入れ子の Begin / End は外側のロードに合流する。
	static void Begin(const std::string& scene, bool startup = false);
	// ローダーがメインスレッドで呼ぶ。軽く、描画はしない。
	static void Pump();
	static void End();
	static bool Active();
	// 直近のロードの最終状態 (ロード画面をフェードアウトさせるのに使う)。
	static const State& Last();
};

} // namespace Engine
