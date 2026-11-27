#pragma once
// WaterDioramaDemoScene.h
// 水のデモ: 箱庭の中に岩に見立てた cube を積み重ね、いちばん上の岩の割れ目から水を流し続ける。
// 水はプレイヤーのスライムと同じ GPU 流体 (PBF の計算と体積の描画) を、水専用の種類 (Renderer::kPoolWaterType) で使う。
// 時間で消えない水なので、流れ落ちた水は石の枠の中にたまっていく (いっぱいになると古い水から出口へ戻り、流れは止まらない)。
// シーンの中身は Resources/Scenes/water_diorama.json (scripts/make_water_diorama.py で作る)。起動は --water-demo。
// 操作: 右ドラッグ / A・D / ←→ で回る、ホイール / W・S で近づく・離れる、R で水を空にする、Space で水を止める・流す
// 確認用: --water-demo-capture を付けると、決まった時間に画面を tests/out/water-demo-*.png に撮り、
// 1 コマの時間と GPU の流体の時間を tests/out/water-demo.txt に書いて終わる
#include "GameScene.h"
#include <chrono>

namespace Game {
class WaterDioramaDemoScene : public GameScene {
public:
	static constexpr const char* Stage = "Resources/Scenes/water_diorama.json";
	~WaterDioramaDemoScene() override;
	void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
	void Update() override;
	void Draw() override;

private:
	// ---- 確認用の撮影 ----
	Engine::WindowDX* window_ = nullptr;
	bool capture_ = false;
	float clock_ = 0;
	int shot_ = 0;
	std::string notes_;
	std::chrono::steady_clock::time_point last_;
	double wall_ = 0, sim_ = 0, fluid_ = 0;
	int frames_ = 0, gpuFrames_ = 0;
	void Capture(const std::wstring& path);
	void UpdateCapture();
	// 箱庭の中心を見る、回り込むカメラ
	float yaw_ = -.72f, pitch_ = .62f, distance_ = 27.f, idle_ = 0;
	void ApplyCamera();
};
} // namespace Game
