#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/Systems/ChronoSystem.h"
#include "../Game/Chrono/SlimeTentacleRules.h"
#include "../Game/Chrono/DevourMorphRules.h"
#include "../Game/Chrono/RoomRules.h"
#include <chrono>
#include <fstream>
class SlimeLifeValidationScene final : public Game::GameScene {
	Engine::WindowDX* window_ = nullptr;
	std::chrono::steady_clock::time_point start_;
	float elapsed_ = 0;
	int frames_ = 0;
	std::ofstream trace_;
	int devourPhase_ = 0, devourPounces_ = 0, devourMaxCombo_ = 0, devourTarget_ = -1;
	float devourClock_ = 0, devourMass_ = 0, devourLeft_ = 0, devourLastAge_ = 0, devourBaseY_ = 0, devourLift_ = 0;
	int devourAbsorbed_ = 0;
	int devourLagStage_ = -1;
	std::array<float, 3> devourLag_{};
	float devourLagWorst_ = 0;
	// 家を飲む確認 (phase 5) で一時的に片付けた物 (確認のあとに戻す)
	std::vector<size_t> devourCleared_;
	int devourGrounded_ = 0, devourFrames_ = 0;
	Game::Chrono::DevourPick devourPrevPick_{}, devourTracePick_{};
	bool devourTraced_ = true;
	bool devourValid_ = true, devourBounced_ = false, devourWasPouncing_ = false, devourFresh_ = true;
	std::array<bool, 5> devourImages_{};
	std::string devourNotes_;
	// 部屋のステージ: 時間と開始の大きさ、アイテムを拾って使うと一時的に変形し、最初の目標でクリア
	int roomPhase_ = 0;
	float roomClock_ = 0;
	bool roomValid_ = true, roomMouthPlaced_ = false, roomMouthSeen_ = false;
	std::string roomNotes_;
	// 部屋のカメラの確認 (--slime-life-room-camera): 家具の脚・壁際・部屋の角・間の壁など、カメラが遮られやすい所に体を置き、
	// 体を後ろから見るカメラを撮る。カメラと体の距離と、薄くなった物の数を記録する
	int cameraShot_ = 0;
	float cameraClock_ = 0;
	std::string cameraNotes_;
	void UpdateRoomCamera() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		// 名前・体の位置・向き (カメラはこの向きの後ろに付く)・見下ろす角度
		struct Shot {
			const char* name;
			Vec at;
			float yaw, pitch;
		};
		static const Shot shots[] = {
		    {"bed-corner", {-286, 0, -286}, .785f, .3f},  // ベッドの下の部屋の角。カメラは角の方へ下がる
		    {"bed-leg", {-55, 0, -175}, .785f, .3f},      // ベッドの脚の手前。カメラは脚の方へ下がる
		    {"under-bed", {-150, 0, -215}, 3.1416f, .3f}, // ベッドの下から外を見る
		    {"wall-back", {0, 0, -292}, 0, .3f},          // 壁を背にする
		    {"room-corner", {288, 0, -288}, -.785f, .3f}, // 部屋の角
		    {"desk-leg", {165, 0, -221}, -.785f, .3f},    // 机の脚の手前
		    {"bookshelf", {250, 0, -60}, -1.5708f, .3f},  // 本棚を背にする
		    {"partition", {150, 0, 70}, 0, .3f},          // 和室から、間の壁を背にする
		    {"rug-props", {60, 0, -100}, .5f, .2f},       // 小物の多い所
		    {"bowling-lane", {0, 0, -8}, 3.1416f, .25f},  // ボウリングのレーンの向こう側 (カメラはレーンの上)
		    {"kotatsu-up", {-205, 0, 176}, 1.5708f, .3f}, // こたつ布団の坂を登る向き
		    {"kotatsu-side", {-205, 0, 176}, 0, .3f}};    // こたつ布団の坂を横切る向き
		constexpr int count = int(std::size(shots));
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		const auto& shot = shots[size_t(cameraShot_)];
		input.yaw = shot.yaw;
		input.pitch = shot.pitch;
		if (cameraClock_ == 0) {
			Vec at = shot.at;
			at.y = RoomGround(at + Vec{0, 1, 0}) + 1.25f;
			body.at = at;
			body.forward = body.liquidForward = {std::sin(shot.yaw), 0, std::cos(shot.yaw)};
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {at.x, at.y, at.z};
		}
		float before = life.age;
		Game::GameScene::Update();
		++frames_;
		cameraClock_ += std::max(life.age - before, 1.f / 120);
		if (cameraClock_ >= 1.6f) {
			const auto eye = GetCamera().Position();
			const float gap = std::sqrt(
			    (eye.x - body.at.x) * (eye.x - body.at.x) + (eye.y - body.at.y) * (eye.y - body.at.y) +
			    (eye.z - body.at.z) * (eye.z - body.at.z));
			int faded = 0;
			for (auto e : GetComponents().view<CameraOccluder>())
				if (GetComponents().get<CameraOccluder>(e).opacity < .5f)
					++faded;
			char line[256];
			snprintf(
			    line, sizeof(line), "%s camera=(%.1f,%.1f,%.1f) body=(%.1f,%.1f,%.1f) gap=%.1f faded=%d", shot.name,
			    eye.x, eye.y, eye.z, body.at.x, body.at.y, body.at.z, gap, faded);
			cameraNotes_ += line;
			// 坂の上: 体の底が地面からどれだけ離れているか (坂の上側と下側の半分ごと、低い方から 5% の粒子)。
			// 0 に近いほど坂に沿っている (負は地面に埋まり、正は浮いている)。こたつ布団の坂は +x の向きに上る
			if (std::string(shot.name).rfind("kotatsu", 0) == 0) {
				const auto& snapshot = GetRenderer()->GetFluidBodySnapshot();
				std::vector<float> up, down;
				for (const auto& offset : snapshot.offsets) {
					const Vec p{snapshot.core.x + offset.x, snapshot.core.y + offset.y, snapshot.core.z + offset.z};
					if (std::abs(p.x - body.at.x) > 20 || std::abs(p.z - body.at.z) > 20)
						continue;
					(p.x > body.at.x ? up : down).push_back(p.y - RoomGround(p + Vec{0, 30, 0}));
				}
				auto bottom = [](std::vector<float>& v) {
					if (v.empty())
						return 0.f;
					std::sort(v.begin(), v.end());
					return v[v.size() / 20];
				};
				char slope[96];
				snprintf(slope, sizeof(slope), " bottom_uphill=%.2f bottom_downhill=%.2f", bottom(up), bottom(down));
				cameraNotes_ += slope;
			}
			cameraNotes_ += "\n";
			std::wstring path = L"tests/out/room-camera-";
			for (const char* c = shot.name; *c; ++c)
				path += wchar_t(*c);
			path += L".png";
			Capture(path.c_str());
			cameraClock_ = 0;
			if (++cameraShot_ >= count) {
				std::ofstream("tests/out/room-camera.txt") << cameraNotes_;
				PostQuitMessage(0);
			}
		}
		if (frames_ > 4000) {
			std::ofstream("tests/out/room-camera.txt") << "timeout\n" << cameraNotes_;
			PostQuitMessage(2);
		}
	}
	// 坂の町の確認 (--slime-life-slope-tour): 道・石段・境内・家の並びを、体の大きさを変えながら撮る (tests/out/slope-tour-*.png)。
	// 体の高さと地面の高さ・置いた物の数を slope-tour.txt に書く
	int slopeShot_ = 0;
	float slopeClock_ = 0;
	std::string slopeNotes_;
	void UpdateSlopeTour() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		// 名前・位置 (メートル)・向き・見下ろす角度・体の大きさ (cm)
		struct Shot {
			const char* name;
			float x, z, yaw, pitch, cm;
		};
		static const Shot shots[] = {
		    // 石段のまわり (段差の角の斜めの三角形・地面の下が見える穴がないか。大きくする撮影より前に撮る: 大きい体から縮めた直後は液体が広がったまま)
		    {"shrine-stairs-low", -54, 67, -.67f, .2f, 113},
		    {"shrine-stairs-mid", -62, 78, -.67f, .3f, 113},
		    {"shrine-stairs-top", -71, 91, 2.47f, .55f, 113},
		    {"tera-stairs", 21, 18, 0, .25f, 113},
		    {"roji-stairs", -39, -48, 0, .25f, 113},
		    {"roji2-stairs", 40, -57, 0, .25f, 113},
		    {"start", -8, -96.7f, 0, .3f, 100},
		    {"dandanzaka-low", -14, -75, -.67f, .25f, 150},
		    {"shotengai", -40, -57, 1.5708f, .3f, 200},
		    {"sento", 12, -61, 3.1416f, .3f, 300},
		    {"lots-east", 30, -2, -1.5708f, .35f, 300},
		    {"dandanzaka-bend", -55, -38, -.41f, .3f, 300},
		    {"park", 62, 6, 0, .35f, 300},
		    {"temple-gate", 22, 30, 0, .3f, 400},
		    {"temple", 24, 47, 3.1416f, .4f, 600},
		    {"dandanzaka-top", -30, 50, -.9f, .3f, 400},
		    {"shrine", -73, 96, -.63f, .35f, 800},
		    {"tsuzura", 114, -18, -1.5708f, .45f, 400},
		    {"plateau", 85, 85, 0, .45f, 1500},
		    {"overview", 0, -115, 0, .7f, 9000},
		    // 視点を一番下げる (床の近くまで下がるか・坂の途中で地面に埋まらないか)・町の端の塀
		    {"low-valley", -8, -96.7f, 0, -.7f, 100},
		    {"low-slope", -30, -55, -.9f, -.7f, 300},
		    {"border-west", -112, -10, -1.5708f, .6f, 500},
		    {"border-corner", -105, 108, -2.356f, .65f, 1200}};
		constexpr int count = int(std::size(shots));
		const float m = SlopeData().unit;
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		if (frames_ == 40) {
			char line[160];
			snprintf(
			    line, sizeof(line), "props=%zu lots=%zu roads=%zu stairs=%zu\n", life.props.size(),
			    SlopeData().lots.size(), SlopeData().roads.size(), SlopeData().stairs.size());
			slopeNotes_ += line;
			// 地面に触れる部分ごとの丸: 鳥居の柱の間 (中心) を体が通れるか。鳥居に一番近い物の丸を書く
			size_t withFeet = 0;
			for (const auto& prop : life.props)
				withFeet += prop.feet.empty() ? 0 : 1;
			const Vec torii{-73 * m, 0, 93.5f * m};
			const LifeCollectible* gate = nullptr;
			float best = 1e9f;
			for (const auto& prop : life.props) {
				const float d = std::hypot(prop.at.x - torii.x, prop.at.z - torii.z);
				if (d < best && !prop.feet.empty()) {
					best = d;
					gate = &prop;
				}
			}
			snprintf(line, sizeof(line), "feet props=%zu\n", withFeet);
			slopeNotes_ += line;
			// 正面のある物の向き: 正面の 0.9m 先が 0.5m より高い段 (擁壁) になっている物の数 (後ろ向きに置き直したあとも残る物)
			{
				int fronted = 0, walled = 0;
				std::string examples;
				for (auto e : GetComponents().view<TransformComponent, NameComponent>()) {
					const auto& name = GetComponents().get<NameComponent>(e).name;
					bool hasFront = false;
					for (const auto& id : SlopeData().fronts)
						hasFront |= name == "Chrono Lowpoly " + id;
					if (!hasFront)
						continue;
					++fronted;
					const auto& t = GetComponents().get<TransformComponent>(e);
					const Vec at{t.translate.x, 0, t.translate.z};
					const Vec probe = at + Vec{std::sin(t.rotate.y), 0, std::cos(t.rotate.y)} * (.9f * m);
					if (SlopeGround(probe) - SlopeGround(at) > .5f * m) {
						++walled;
						if (examples.size() < 200)
							examples += " " + name.substr(15) + "(" + std::to_string(int(at.x / m)) + "," +
							            std::to_string(int(at.z / m)) + ")";
					}
				}
				slopeNotes_ +=
				    "fronts=" + std::to_string(fronted) + " facing_wall=" + std::to_string(walled) + examples + "\n";
			}
			// 物の大きさの分布 (覆うのに要る半径 m ごとの数と成長の量の合計)。育つ段階ごとに食べごろの物が足りているか
			{
				const float edges[] = {.15f, .3f, .5f, .8f, 1.2f, 2, 3, 5, 1e9f};
				int binCount[9] = {};
				float reward[9] = {};
				for (const auto& prop : life.props) {
					if (prop.reward <= 0)
						continue;
					const float r = prop.radius / m;
					int b = 0;
					while (r > edges[b])
						++b;
					++binCount[b];
					reward[b] += prop.reward;
				}
				std::string text = "sizes";
				for (int b = 0; b < 9; ++b) {
					char cell[64];
					snprintf(cell, sizeof(cell), " <%.2gm:%d/%.0f", edges[b], binCount[b], reward[b]);
					text += cell;
				}
				slopeNotes_ += text + "\n";
			}
			// 物の一覧 (育ち方の見積もり scripts/terrain/simulate_growth.py に使う)
			{
				std::ofstream csv("tests/out/slope-props.csv");
				csv << "x,y,z,radius,reward\n";
				for (const auto& prop : life.props)
					if (prop.reward > 0)
						csv << prop.at.x / m << "," << prop.at.y / m << "," << prop.at.z / m << "," << prop.radius / m
						    << "," << prop.reward << "\n";
			}
			{
				const auto [routes, dancing] = chrono->DevourMoverCounts();
				slopeNotes_ += "movers routes=" + std::to_string(routes) + " dancing=" + std::to_string(dancing) + "\n";
			}
			if (gate) {
				float clear = 1e9f;
				for (const auto& f : gate->feet) {
					clear = std::min(clear, std::hypot(gate->at.x + f.x - torii.x, gate->at.z + f.z - torii.z) - f.y);
					snprintf(line, sizeof(line), "torii foot (%.1f,%.1f) r=%.2f\n", f.x / m, f.z / m, f.y / m);
					slopeNotes_ += line;
				}
				snprintf(line, sizeof(line), "torii center clear=%.2fm whole-foot=%.2fm\n", clear / m, gate->foot / m);
				slopeNotes_ += line;
			}
		}
		const auto& shot = shots[size_t(slopeShot_)];
		input.yaw = shot.yaw;
		input.pitch = shot.pitch;
		if (slopeClock_ == 0) {
			Vec at{shot.x * m, 0, shot.z * m};
			at.y = SlopeGround(at) + 1.25f;
			body.at = at;
			const DevourStage* slope = &DevourStages().back();
			for (const auto& s : DevourStages())
				if (s.map == "slope")
					slope = &s;
			body.mass = DevourGoalMass(*slope, shot.cm);
			body.forward = body.liquidForward = {std::sin(shot.yaw), 0, std::cos(shot.yaw)};
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {at.x, at.y, at.z};
		}
		float before = life.age;
		Game::GameScene::Update();
		++frames_;
		slopeClock_ += std::max(life.age - before, 1.f / 120);
		if (slopeClock_ >= 2.f) {
			const auto eye = GetCamera().Position();
			char line[256];
			snprintf(
			    line, sizeof(line), "%s body=(%.1f,%.1f,%.1f) ground=%.1f camera=(%.1f,%.1f,%.1f) camera_ground=%.1f\n",
			    shot.name, body.at.x, body.at.y, body.at.z, SlopeGround(body.at), eye.x, eye.y, eye.z,
			    SlopeGround({eye.x, 0, eye.z}));
			slopeNotes_ += line;
			std::wstring path = L"tests/out/slope-tour-";
			for (const char* c = shot.name; *c; ++c)
				path += wchar_t(*c);
			path += L".png";
			Capture(path.c_str());
			slopeClock_ = 0;
			if (++slopeShot_ >= count) {
				std::ofstream("tests/out/slope-tour.txt") << slopeNotes_;
				PostQuitMessage(0);
			}
		}
		if (frames_ > 6000) {
			std::ofstream("tests/out/slope-tour.txt") << "timeout\n" << slopeNotes_;
			PostQuitMessage(2);
		}
	}
	// 坂の町の乗り物の確認 (--slime-life-slope-ride): 電柱を登って電線を走る・こいのぼりで滑空する・電柱が飲まれると電線が消える。
	// 結果は tests/out/slope-ride.txt、途中の画面は tests/out/slope-ride-*.png
	int rideStep_ = 0;
	float rideClock_ = 0, rideLift_ = 0, rideDist_ = 0, rideAir_ = 0;
	std::string rideNotes_;
	Game::Chrono::Vec rideStart_{};
	bool rideSeen_ = false, rideFresh_ = true;
	void UpdateSlopeRide() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		const float m = SlopeData().unit;
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		const auto views = chrono->TownGimmicks();
		auto place = [&](Vec at) {
			at.y = SlopeGround(at) + 1.25f;
			body.at = at;
			body.action = LifeAction::Idle;
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {at.x, at.y, at.z};
			chrono->RequestFluidReseed();
		};
		auto finish = [&](int code) {
			std::ofstream("tests/out/slope-ride.txt") << rideNotes_;
			PostQuitMessage(code);
		};
		static std::set<std::wstring> taken;
		auto shot = [&](float at, const wchar_t* name) {
			if (rideClock_ >= at && taken.insert(name).second) {
				std::wstring path = std::wstring(L"tests/out/slope-ride-") + name + L".png";
				Capture(path.c_str());
			}
		};
		if (frames_ == 40) {
			int wires = 0, gliders = 0;
			for (const auto& v : views) {
				wires += v.kind == int(GimmickKind::Wire);
				gliders += v.kind == int(GimmickKind::Glide);
			}
			rideNotes_ += "poles=" + std::to_string(wires) + " edges=" + std::to_string(chrono->TownWireEdges(false)) +
			              " gliders=" + std::to_string(gliders) + "\n";
		}
		// 体の高さ (地面から) と、始めの位置からの水平の距離を記録する
		auto track = [&]() {
			const bool riding = chrono->GimmickRidingNow();
			rideSeen_ |= riding;
			if (riding) {
				rideLift_ = std::max(rideLift_, (body.at.y - SlopeGround(body.at)) / m);
				rideAir_ += 1.f / 60;
			}
			Vec d = body.at - rideStart_;
			d.y = 0;
			rideDist_ = std::max(rideDist_, Length(d) / m);
		};
		auto reset = [&]() {
			rideClock_ = 0;
			rideLift_ = rideDist_ = rideAir_ = 0;
			rideSeen_ = false;
			rideFresh_ = true;
			++rideStep_;
		};
		float before = life.age;
		if (rideStep_ == 0) {
			// 電線: 電線が一番多い電柱の下から登り、一番近い隣の電柱の方へスティックを倒して走る。3.5 秒で飛び降りる
			static Vec toward{0, 0, 1};
			if (rideFresh_) {
				rideFresh_ = false;
				const ChronoSystem::TownGimmickView* pick = nullptr;
				for (const auto& v : views)
					if (v.kind == int(GimmickKind::Wire) && !v.done && (!pick || v.edges > pick->edges))
						pick = &v;
				if (!pick) {
					rideNotes_ += "wire none\n";
					finish(2);
					return;
				}
				float best = 1e9f;
				for (const auto& v : views)
					if (v.kind == int(GimmickKind::Wire) && &v != pick) {
						Vec d = v.at - pick->at;
						d.y = 0;
						if (Length(d) > 1 && Length(d) < best) {
							best = Length(d);
							toward = Unit(d);
						}
					}
				place(pick->at + Vec{.8f * m, 0, 0});
				rideStart_ = body.at;
			}
			// (1.5 秒までは柱の上で止まって、体が見えるかを撮る)
			input.yaw = std::atan2(toward.x, toward.z);
			input.pitch = .35f;
			input.attack = rideClock_ < .05f || (rideClock_ > 4.5f && rideClock_ < 4.55f);
			input.move = {0, 0, rideClock_ > 1.5f && rideClock_ < 4.5f ? 1.f : 0.f};
			shot(.12f, L"wire-start");
			shot(.3f, L"wire-climb");
			shot(4.8f, L"wire-fall");
			shot(1.2f, L"wire-top");
			if (rideClock_ > 1.2f && taken.insert(L"debug-top").second) {
				const auto eye = GetCamera().Position();
				char why[260];
				snprintf(
				    why, sizeof(why),
				    "wire_top body=(%.1f,%.1f,%.1f) eye=(%.1f,%.1f,%.1f) ground=%.1f shape=%d riding=%d mass=%.0f",
				    body.at.x / m, body.at.y / m, body.at.z / m, eye.x / m, eye.y / m, eye.z / m,
				    SlopeGround(body.at) / m, chrono->GimmickRideShape(), int(chrono->GimmickRidingNow()), body.mass);
				rideNotes_ += why;
				rideNotes_ += "\n";
			}
			shot(2.2f, L"wire-run");
			shot(2.7f, L"wire-mid");
			shot(3.5f, L"wire-far");
			Game::GameScene::Update();
			++frames_;
			rideClock_ += std::max(life.age - before, 1.f / 120);
			track();
			if (rideClock_ > 8) {
				const bool landed = !chrono->GimmickRidingNow();
				char line[200];
				snprintf(
				    line, sizeof(line), "wire rode=%d lift=%.1fm dist=%.1fm landed=%d\n", int(rideSeen_), rideLift_,
				    rideDist_, int(landed));
				rideNotes_ += line;
				reset();
			}
			return;
		}
		if (rideStep_ == 1) {
			// こいのぼりの前に、スタートのおやつを飲んでおく (こいのぼりの形の中に物が収まるかを見る)
			if (rideFresh_) {
				rideFresh_ = false;
				place(Vec{-8 * m, 0, -96.7f * m});
				body.forward = {0, 0, 1};
			}
			input.yaw = 0;
			input.pitch = .3f;
			input.move = {0, 0, 1};
			input.absorb = std::fmod(rideClock_, .45f) < .12f;
			Game::GameScene::Update();
			++frames_;
			rideClock_ += std::max(life.age - before, 1.f / 120);
			if (rideClock_ > 4) {
				rideNotes_ += "ate=" + std::to_string(life.absorbed) + "\n";
				reset();
			}
			return;
		}
		if (rideStep_ == 2) {
			// こいのぼり: 西の丘のこいのぼりから町の真ん中へ向いて飛ぶ。2.5 秒から右へ曲がり、4.5 秒から押し下げ、5.5 秒から引き上げる
			if (rideFresh_) {
				rideFresh_ = false;
				const ChronoSystem::TownGimmickView* pick = nullptr;
				float best = 1e9f;
				const Vec hill{-118 * m, 0, 40 * m};
				for (const auto& v : views)
					if (v.kind == int(GimmickKind::Glide)) {
						Vec d = v.at - hill;
						d.y = 0;
						if (Length(d) < best) {
							best = Length(d);
							pick = &v;
						}
					}
				if (!pick) {
					rideNotes_ += "glide none\n";
					finish(2);
					return;
				}
				Vec spot = pick->at + Vec{1.6f * m, 0, 0};
				float level = 1e9f;
				for (int k = 0; k < 8; ++k) {
					const float a = 6.2831853f * float(k) / 8;
					const Vec at = pick->at + Vec{std::cos(a), 0, std::sin(a)} * (1.6f * m);
					const float gap = std::abs(SlopeGround(at) - pick->at.y);
					if (gap < level) {
						level = gap;
						spot = at;
					}
				}
				place(spot);
				rideStart_ = body.at;
				body.forward = {1, 0, 0};
			}
			const bool riding = chrono->GimmickRidingNow();
			// (カメラは体の向きについていく: ゲームではこいのぼりがカメラを回す)
			input.yaw = rideClock_ < .1f ? 1.5708f : std::atan2(body.forward.x, body.forward.z);
			input.pitch = .3f;
			input.attack = rideClock_ < .05f;
			input.move = {
			    rideClock_ > 2.5f && rideClock_ < 4.5f ? 1.f : 0.f, 0,
			    rideClock_ > 4.5f && rideClock_ < 5.5f   ? 1.f
			    : rideClock_ > 5.5f && rideClock_ < 6.5f ? -1.f
			                                             : 0.f};
			if (rideClock_ > .5f && rideClock_ - .5f < 1.f / 30 && !rideSeen_) {
				char why[220];
				snprintf(
				    why, sizeof(why), "glide_debug body=(%.0f,%.0f,%.0f) ground=%.1f action=%d riding=%d\n",
				    body.at.x / m, body.at.y / m, body.at.z / m, SlopeGround(body.at) / m, int(body.action),
				    int(riding));
				rideNotes_ += why;
				for (const auto& v : views)
					if (v.kind == int(GimmickKind::Glide)) {
						snprintf(
						    why, sizeof(why), "  glider at=(%.0f,%.1f,%.0f) done=%d\n", v.at.x / m, v.at.y / m,
						    v.at.z / m, int(v.done));
						rideNotes_ += why;
					}
			}
			shot(1.2f, L"glide-launch");
			shot(3.5f, L"glide-turn");
			shot(6.f, L"glide-climb");
			Game::GameScene::Update();
			++frames_;
			rideClock_ += std::max(life.age - before, 1.f / 120);
			track();
			if ((rideSeen_ && !riding && rideClock_ > 1) || rideClock_ > 40) {
				char line[200];
				snprintf(
				    line, sizeof(line), "glide rode=%d lift=%.1fm dist=%.1fm air=%.1fs time=%.1fs\n", int(rideSeen_),
				    rideLift_, rideDist_, rideAir_, rideClock_);
				rideNotes_ += line;
				Capture(L"tests/out/slope-ride-glide-land.png");
				reset();
			}
			return;
		}
		if (rideStep_ == 3) {
			// 電柱を飲む: 8m の体では覆えず、飲まれた (物が消えた) 電柱のギミックは止まり、その電線が消える
			if (rideFresh_) {
				rideFresh_ = false;
				const ChronoSystem::TownGimmickView* pick = nullptr;
				for (const auto& v : views)
					if (v.kind == int(GimmickKind::Wire) && !v.done && v.edges > 0) {
						pick = &v;
						break;
					}
				if (!pick) {
					rideNotes_ += "eat none\n";
					finish(2);
					return;
				}
				int index = -1;
				for (size_t i = 0; i < life.props.size(); ++i)
					if (life.props[i].visual == pick->anchor)
						index = int(i);
				if (index < 0) {
					rideNotes_ += "eat noprop\n";
					finish(2);
					return;
				}
				const DevourStage* slope = &DevourStages().back();
				for (const auto& s : DevourStages())
					if (s.map == "slope")
						slope = &s;
				const bool cannot = !DevourCanCover(DevourGoalMass(*slope, 800), life.props[size_t(index)].radius),
				           can = DevourCanCover(DevourGoalMass(*slope, 1300), life.props[size_t(index)].radius);
				const int edgesBefore = chrono->TownWireEdges(true);
				life.props[size_t(index)].active = false;
				rideStart_ = pick->at;
				rideNotes_ += "pole 8m_cannot=" + std::to_string(cannot) + " 13m_can=" + std::to_string(can) +
				              " edges_before=" + std::to_string(edgesBefore);
			}
			Game::GameScene::Update();
			++frames_;
			rideClock_ += std::max(life.age - before, 1.f / 120);
			if (rideClock_ > .3f) {
				bool done = false;
				int edges = -1;
				for (const auto& v : chrono->TownGimmicks())
					if (v.kind == int(GimmickKind::Wire) && Length(v.at - rideStart_) < .01f) {
						done = v.done;
						edges = v.edges;
					}
				rideNotes_ += " after=" + std::to_string(chrono->TownWireEdges(true)) +
				              " done=" + std::to_string(done) + " pole_edges=" + std::to_string(edges) + "\n";
				finish(0);
			}
			return;
		}
	}
	// おみこしとこまの確認 (--slime-life-slope-festival): 3m の体でおみこしを担いで歩き、子分が物を持ってきて飲むか。
	// 2m の体でこまを回し、跳ね返りながら飲むか。結果は tests/out/slope-festival.txt、画面は slope-festival-*.png
	int festStep_ = 0;
	float festClock_ = 0;
	bool festFresh_ = true, festSeen_ = false;
	int festAte_ = 0;
	size_t festHelpers_ = 0;
	float festDist_ = 0;
	Game::Chrono::Vec festLast_{};
	std::string festNotes_;
	void UpdateSlopeFestival() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		const float m = SlopeData().unit;
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		const auto views = chrono->TownGimmicks();
		const auto& stage = chrono->DevourStageInfo();
		auto place = [&](Vec at) {
			at.y = SlopeGround(at) + 1.25f;
			body.at = at;
			body.action = LifeAction::Idle;
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {at.x, at.y, at.z};
			chrono->RequestFluidReseed();
		};
		auto finish = [&](int code) {
			std::ofstream("tests/out/slope-festival.txt") << festNotes_;
			PostQuitMessage(code);
		};
		static std::set<std::wstring> taken;
		auto shot = [&](float at, const wchar_t* name) {
			if (festClock_ >= at && taken.insert(name).second) {
				std::wstring path = std::wstring(L"tests/out/slope-festival-") + name + L".png";
				Capture(path.c_str());
			}
		};
		auto pick = [&](GimmickKind kind, Vec around) -> const ChronoSystem::TownGimmickView* {
			const ChronoSystem::TownGimmickView* best = nullptr;
			float d = 1e9f;
			for (const auto& v : views)
				if (v.kind == int(kind)) {
					Vec e = v.at - around;
					e.y = 0;
					if (Length(e) < d) {
						d = Length(e);
						best = &v;
					}
				}
			return best;
		};
		auto begin = [&](GimmickKind kind, Vec around, float cm, float yaw) {
			const auto* g = pick(kind, around);
			if (!g) {
				festNotes_ += "none\n";
				finish(2);
				return false;
			}
			body.mass = body.health = body.maxHealth = DevourGoalMass(stage, cm);
			++life.revision;
			place(g->at);
			festLast_ = body.at;
			festDist_ = 0;
			festAte_ = int(life.absorbed);
			festHelpers_ = 0;
			festSeen_ = false;
			char line[160];
			snprintf(line, sizeof(line), "ring at=(%.1f,%.1f,%.1f)m ", g->at.x / m, g->at.y / m, g->at.z / m);
			festNotes_ += line;
			(void)yaw;
			return true;
		};
		float before = life.age;
		if (festStep_ == 0) {
			// おみこし: お寺の前の輪で担ぎ、2 秒から 10 秒まで前へ歩く
			if (festFresh_) {
				festFresh_ = false;
				if (!begin(GimmickKind::Mikoshi, Vec{18.5f * m, 0, 37.5f * m}, 300, 3.1416f))
					return;
			}
			input.yaw = 3.1416f;
			input.pitch = .35f;
			input.attack = festClock_ < .05f;
			input.move = {0, 0, festClock_ > 2 && festClock_ < 10 ? 1.f : 0.f};
			shot(1.2f, L"mikoshi-form");
			shot(3.f, L"mikoshi-walk");
			shot(6.f, L"mikoshi-helpers");
			shot(9.f, L"mikoshi-late");
			Game::GameScene::Update();
			++frames_;
			festClock_ += std::max(life.age - before, 1.f / 120);
			festSeen_ |= chrono->GimmickRidingNow();
			festHelpers_ = std::max(festHelpers_, chrono->MikoshiHelperCount());
			{
				Vec d = body.at - festLast_;
				d.y = 0;
				festDist_ += Length(d) / m;
				festLast_ = body.at;
			}
			if (festClock_ > 18) {
				char line[220];
				snprintf(
				    line, sizeof(line),
				    "mikoshi rode=%d ended=%d helpers_max=%zu helpers_left=%zu ate=%d walked=%.1fm\n", int(festSeen_),
				    int(!chrono->GimmickRidingNow()), festHelpers_, chrono->MikoshiHelperCount(),
				    int(life.absorbed) - festAte_, festDist_);
				festNotes_ += line;
				festClock_ = 0;
				festFresh_ = true;
				++festStep_;
			}
			return;
		}
		if (festStep_ == 1) {
			// こま: 商店街の輪で回し、まっすぐ放す (曲げない)
			if (festFresh_) {
				festFresh_ = false;
				if (!begin(GimmickKind::Koma, Vec{-4 * m, 0, -66 * m}, 200, 0))
					return;
			}
			input.yaw = -1.5708f;
			input.pitch = .35f;
			input.attack = festClock_ < .05f;
			shot(1.f, L"koma-form");
			shot(2.5f, L"koma-run");
			shot(5.f, L"koma-bounce");
			shot(8.f, L"koma-late");
			Game::GameScene::Update();
			++frames_;
			festClock_ += std::max(life.age - before, 1.f / 120);
			festSeen_ |= chrono->GimmickRidingNow();
			if (chrono->GimmickRidingNow())
				festHelpers_ = size_t(std::max(int(festHelpers_), chrono->GimmickRideBounces()));
			{
				Vec d = body.at - festLast_;
				d.y = 0;
				festDist_ += Length(d) / m;
				festLast_ = body.at;
			}
			if (festClock_ > 14) {
				char line[220];
				snprintf(
				    line, sizeof(line), "koma rode=%d ended=%d bounces=%zu ate=%d moved=%.1fm\n", int(festSeen_),
				    int(!chrono->GimmickRidingNow()), festHelpers_, int(life.absorbed) - festAte_, festDist_);
				festNotes_ += line;
				festClock_ = 0;
				festFresh_ = true;
				++festStep_;
			}
			return;
		}
		// 消火栓の放水は一旦はずした (slope_town.json の rides.hydrant が空) ので、置かれていなければここで終える
		if (festStep_ == 2 && !pick(GimmickKind::Hose, Vec{-17 * m, 0, 40 * m})) {
			festNotes_ += "hose skipped (no hydrant)\n";
			finish(0);
			return;
		}
		if (festStep_ == 2) {
			// 消火栓: 上の段の下り坂の消火栓で、5 秒まで坂の下りへ撃ち (押し流す)、そのあと振り返って反対へ撃つ (引き寄せる)
			if (festFresh_) {
				festFresh_ = false;
				festWashMax_ = festMistMax_ = 0;
				festRainbowMax_ = 0;
				if (!begin(GimmickKind::Hose, Vec{-17 * m, 0, 40 * m}, 250, 0))
					return;
			}
			// (肩越しの照準: 5 秒までは坂の下りを狙い、そのあと振り返って上りを狙う)
			const float down = std::atan2(.38f, -.92f);
			input.yaw = festClock_ < 5 ? down : down + 3.1416f;
			input.pitch = festClock_ < 5 ? .1f : .3f;
			input.attack = festClock_ < .05f;
			if (festClock_ > 2.5f && taken.insert(L"hose-debug").second) {
				const Vec im = chrono->HoseImpact(), nz = chrono->HoseNozzle();
				char why[200];
				snprintf(
				    why, sizeof(why), "hose_debug nozzle=(%.0f,%.0f,%.0f) impact=(%.0f,%.0f,%.0f) lob=%d", nz.x, nz.y,
				    nz.z, im.x, im.y, im.z, int(chrono->HoseLob()));
				festNotes_ += why;
				festNotes_ += " ";
			}
			if (festClock_ > 4.f && taken.insert(L"hose-water").second) {
				festNotes_ += "water_cells=" + std::to_string(chrono->HoseWaterCells()) + "\n";
			}
			shot(1.2f, L"hose-form");
			shot(2.5f, L"hose-wash");
			shot(4.5f, L"hose-rainbow");
			shot(4.f, L"hose-top");
			shot(7.5f, L"hose-pull");
			shot(10.5f, L"hose-late");
			Game::GameScene::Update();
			++frames_;
			festClock_ += std::max(life.age - before, 1.f / 120);
			festSeen_ |= chrono->GimmickRidingNow();
			festWashMax_ = std::max(festWashMax_, chrono->HoseWashCount());
			festMistMax_ = std::max(festMistMax_, chrono->HoseMistCount());
			festRainbowMax_ = std::max(festRainbowMax_, chrono->HoseRainbow());
			if (festClock_ > 15) {
				char line[260];
				snprintf(
				    line, sizeof(line), "hose rode=%d ended=%d washing_max=%zu mist_max=%zu rainbow=%.2f ate=%d\n",
				    int(festSeen_), int(!chrono->GimmickRidingNow()), festWashMax_, festMistMax_, festRainbowMax_,
				    int(life.absorbed) - festAte_);
				festNotes_ += line;
				finish(0);
			}
			return;
		}
	}
	size_t festWashMax_ = 0, festMistMax_ = 0;
	float festRainbowMax_ = 0;
	// 電線で折り返すときの重さ (--slime-life-slope-wire-perf): 電柱を登って隣の電柱へ走り、2.5 秒ごとにスティックを逆へ倒して折り返す。
	// コマごとの時間 (ミリ秒)・体の向きが前のコマからどれだけ回ったか・体の速さを tests/out/slope-wire-perf.csv に書き、
	// 折り返した直後 (向きが 90 度以上回ったコマから 0.4 秒) とそれ以外の平均と最大を slope-wire-perf.txt に書く
	float wirePerfClock_ = 0;
	std::string wirePerfCsv_;
	std::chrono::steady_clock::time_point wirePerfLast_;
	Game::Chrono::Vec wirePerfForward_{0, 0, 1}, wirePerfToward_{0, 0, 1};
	float wirePerfFlip_ = -1, wirePerfSum_[2] = {}, wirePerfMax_[2] = {}, wirePerfCamMax_[2] = {};
	int wirePerfCount_[2] = {}, wirePerfFlips_ = 0, wirePerfStalls_ = 0;
	Game::Chrono::Vec wirePerfEye_{};
	void UpdateWirePerf() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		const float m = SlopeData().unit;
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			wirePerfLast_ = std::chrono::steady_clock::now();
			return;
		}
		if (frames_ == 40) {
			const auto views = chrono->TownGimmicks();
			const ChronoSystem::TownGimmickView* pick = nullptr;
			for (const auto& v : views)
				if (v.kind == int(GimmickKind::Wire) && (!pick || v.edges > pick->edges))
					pick = &v;
			if (!pick) {
				std::ofstream("tests/out/slope-wire-perf.txt") << "no wire\n";
				PostQuitMessage(2);
				return;
			}
			float best = 1e9f;
			for (const auto& v : views)
				if (v.kind == int(GimmickKind::Wire) && &v != pick) {
					Vec d = v.at - pick->at;
					d.y = 0;
					if (Length(d) > 1 && Length(d) < best) {
						best = Length(d);
						wirePerfToward_ = Unit(d);
					}
				}
			Vec at = pick->at + Vec{.8f * m, 0, 0};
			at.y = SlopeGround(at) + 1.25f;
			body.at = at;
			GetComponents().get<Player>(player).velocity = {};
			GetComponents().get<TransformComponent>(player).translate = {at.x, at.y, at.z};
			chrono->RequestFluidReseed();
			wirePerfCsv_ = "t,ms,turn_deg,speed,riding\n";
		}
		// (カメラは最初の向きのまま。スティックを前・後ろと 2.5 秒ごとに入れ替える)
		const float phase = std::fmod(std::max(0.f, wirePerfClock_ - 1.f), 5.f);
		const float stick = wirePerfClock_ < 1 ? 0.f : phase < 2.5f ? 1.f : -1.f;
		input.yaw = std::atan2(wirePerfToward_.x, wirePerfToward_.z);
		input.pitch = .35f;
		input.attack = wirePerfClock_ < .05f;
		input.move = {0, 0, stick};
		const Vec before = body.at;
		const float age = life.age;
		Game::GameScene::Update();
		++frames_;
		const float dt = std::max(life.age - age, 1.f / 120);
		wirePerfClock_ += dt;
		const auto now = std::chrono::steady_clock::now();
		const float ms = std::chrono::duration<float, std::milli>(now - wirePerfLast_).count();
		wirePerfLast_ = now;
		Vec f = body.at - before;
		f.y = 0;
		f = Length(f) > .05f ? Unit(f) : wirePerfForward_;
		const float turn = std::acos(std::clamp(Dot(f, wirePerfForward_), -1.f, 1.f)) * 57.2958f;
		wirePerfForward_ = f;
		if (turn > 90) {
			wirePerfFlip_ = wirePerfClock_;
			++wirePerfFlips_;
		}
		const bool riding = chrono->GimmickRidingNow();
		// (カメラが 1 秒にどれだけ動いたか・体が電線の速さの 7 割より遅く進んだコマ = 引っかかったコマ)
		const auto eyeNow = GetCamera().Position();
		const Vec eye{eyeNow.x, eyeNow.y, eyeNow.z};
		const float camStep = Length(eye - wirePerfEye_) / dt / m;
		wirePerfEye_ = eye;
		const float speed = Length(body.at - before) / dt / m;
		if (wirePerfClock_ > 2.2f && riding) {
			const int k = wirePerfFlip_ >= 0 && wirePerfClock_ - wirePerfFlip_ < .4f ? 1 : 0;
			wirePerfSum_[k] += ms;
			wirePerfMax_[k] = std::max(wirePerfMax_[k], ms);
			wirePerfCamMax_[k] = std::max(wirePerfCamMax_[k], camStep);
			++wirePerfCount_[k];
			if (speed > .5f && speed < WireSpeed / m * .7f)
				++wirePerfStalls_;
		}
		char line[120];
		snprintf(line, sizeof(line), "%.3f,%.1f,%.0f,%.1f,%d\n", wirePerfClock_, ms, turn, speed, int(riding));
		wirePerfCsv_ += line;
		if (wirePerfClock_ > 16) {
			std::ofstream("tests/out/slope-wire-perf.csv") << wirePerfCsv_;
			char text[300];
			snprintf(
			    text, sizeof(text),
			    "flips=%d stalls=%d cam_after_flip=%.1fm/s cam_other=%.1fm/s after_flip avg=%.1fms max=%.1fms (%d frames) other avg=%.1fms max=%.1fms (%d frames)\n",
			    wirePerfFlips_, wirePerfStalls_, wirePerfCamMax_[1], wirePerfCamMax_[0],
			    wirePerfSum_[1] / std::max(1, wirePerfCount_[1]), wirePerfMax_[1], wirePerfCount_[1],
			    wirePerfSum_[0] / std::max(1, wirePerfCount_[0]), wirePerfMax_[0], wirePerfCount_[0]);
			std::ofstream("tests/out/slope-wire-perf.txt") << text;
			PostQuitMessage(0);
		}
	}
	// 坂の町の重さとカメラの確認 (--slime-life-slope-perf): 決まった場所に体を置き、決まった入力で数秒動かして、
	// 1 コマの時間 (平均と最大)・カメラの揺れ (コマごとの位置の 2 階差分の平均)・カメラと地面の高さの差の最小を slope-perf.txt に書く
	int perfShot_ = 0;
	float perfClock_ = 0;
	std::string perfNotes_;
	std::chrono::steady_clock::time_point perfLast_;
	double perfSum_ = 0, perfMax_ = 0;
	int perfFrames_ = 0;
	std::vector<Game::Chrono::Vec> perfCamera_, perfBody_;
	float perfLow_ = 1e9f;
	// 重さの内訳: ゲームの更新 (CPU) の時間と、GPU の段ごとの時間 (流体のプロファイラ) の合計
	double perfUpdate_ = 0, perfUpdateMax_ = 0, perfGpuMax_ = 0, perfGpuTotal_ = 0, perfSubsteps_ = 0;
	std::array<double, Engine::Renderer::kFluidProfileStageCount> perfGpu_{};
	int perfGpuFrames_ = 0;
	void UpdateSlopePerf() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		// 名前・位置 (メートル)・向き・カメラの上下の角度・前へ進む入力・体の大きさ (cm)
		struct Shot {
			const char* name;
			float x, z, yaw, pitch, move, cm;
		};
		static const Shot shots[] = {
		    {"stairs-up", -53.6f, 67.9f, -.667f, .3f, 1, 100}, {"stairs-up-big", -53.6f, 67.9f, -.667f, .3f, 1, 250},
		    {"road-still", -8, -96.7f, 0, .3f, 0, 100},        {"road-walk", -8, -96.7f, 0, .3f, 1, 100},
		    {"slope-walk", -14, -75, -.67f, .3f, 1, 150},      {"pit-still", 7.5f, -88, 0, .3f, 0, 100},
		    {"pit-push", 7.5f, -88, 0, .3f, 1, 100},           {"river-push", -40, -116, 0, .3f, 1, 100},
		    {"low-road", -8, -96.7f, 0, -.7f, 0, 100},         {"low-slope-up", -14, -75, -.67f, -.7f, 0, 150},
		    {"low-slope-down", -30, -55, 2.5f, -.7f, 0, 150},  {"low-walk", -14, -75, -.67f, -.7f, 1, 150}};
		constexpr int count = int(std::size(shots));
		const float m = SlopeData().unit;
		constexpr float settle = 1, measure = 4;
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			Engine::Renderer::GetInstance()->SetFluidProfilerEnabled(true);
			return;
		}
		const auto& shot = shots[size_t(perfShot_)];
		input.yaw = shot.yaw;
		input.pitch = shot.pitch;
		if (perfClock_ == 0) {
			Vec at{shot.x * m, 0, shot.z * m};
			at.y = SlopeGround(at) + 1.25f;
			body.at = at;
			const DevourStage* slope = &DevourStages().back();
			for (const auto& s : DevourStages())
				if (s.map == "slope")
					slope = &s;
			body.mass = DevourGoalMass(*slope, shot.cm);
			body.forward = body.liquidForward = {std::sin(shot.yaw), 0, std::cos(shot.yaw)};
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {at.x, at.y, at.z};
			perfSum_ = perfMax_ = 0;
			perfFrames_ = 0;
			perfCamera_.clear();
			perfBody_.clear();
			perfLow_ = 1e9f;
			perfLast_ = std::chrono::steady_clock::now();
			perfUpdate_ = perfUpdateMax_ = perfGpuMax_ = perfGpuTotal_ = perfSubsteps_ = 0;
			perfGpu_ = {};
			perfGpuFrames_ = 0;
		}
		if (perfClock_ > settle)
			input.move = {0, 0, shot.move};
		float before = life.age;
		const auto updateStart = std::chrono::steady_clock::now();
		Game::GameScene::Update();
		++frames_;
		perfClock_ += std::max(life.age - before, 1.f / 120);
		const auto now = std::chrono::steady_clock::now();
		const double ms = std::chrono::duration<double, std::milli>(now - perfLast_).count();
		perfLast_ = now;
		if (perfClock_ > settle) {
			const double updateMs = std::chrono::duration<double, std::milli>(now - updateStart).count();
			perfUpdate_ += updateMs;
			perfUpdateMax_ = std::max(perfUpdateMax_, updateMs);
			const auto& stats = Engine::Renderer::GetInstance()->GetFluidProfileStats();
			if (stats.valid && stats.framesSinceSample == 0) {
				for (size_t k = 0; k < perfGpu_.size(); ++k)
					perfGpu_[k] += stats.lastMs[k];
				perfGpuTotal_ += stats.totalMs;
				perfGpuMax_ = std::max(perfGpuMax_, double(stats.lastMs[Engine::Renderer::SceneRender]));
				perfSubsteps_ += stats.substeps;
				++perfGpuFrames_;
			}
			perfSum_ += ms;
			perfMax_ = std::max(perfMax_, ms);
			++perfFrames_;
			const auto eye = GetCamera().Position();
			perfCamera_.push_back({eye.x, eye.y, eye.z});
			perfBody_.push_back(body.at);
			perfLow_ = std::min(perfLow_, eye.y - SlopeGround({eye.x, 0, eye.z}));
		}
		if (perfClock_ >= settle + measure) {
			// カメラの揺れ: 位置の 2 階差分 (なめらかに動けば小さい)。高さだけの揺れも別に出す
			double shake = 0, shakeY = 0, bodyShake = 0, bodyShakeY = 0;
			for (size_t i = 2; i < perfCamera_.size(); ++i) {
				const Vec a = perfCamera_[i - 2], b = perfCamera_[i - 1], c = perfCamera_[i];
				const Vec d = c - b * 2.f + a;
				shake += Length(d);
				shakeY += std::abs(d.y);
				const Vec e = perfBody_[i] - perfBody_[i - 1] * 2.f + perfBody_[i - 2];
				bodyShake += Length(e);
				bodyShakeY += std::abs(e.y);
			}
			const double n = double(std::max<size_t>(1, perfCamera_.size() - 2));
			char line[320];
			snprintf(
			    line, sizeof(line),
			    "%s frame_ms=%.1f max_ms=%.1f shake=%.3f shake_y=%.3f body_shake=%.3f body_shake_y=%.3f camera_low=%.2f body=(%.0f,%.0f,%.0f) ground=%.1f\n",
			    shot.name, perfSum_ / std::max(1, perfFrames_), perfMax_, shake / n, shakeY / n, bodyShake / n,
			    bodyShakeY / n, perfLow_, body.at.x, body.at.y, body.at.z, SlopeGround(body.at));
			perfNotes_ += line;
			{
				static const char* names[] = {"sim",       "shapes", "splat",   "filter",      "raymarch", "impostors",
				                              "fshadow",   "scene",  "sshadow", "rt_accel",    "rt_trace", "rt_denoise",
				                              "dlss_prep", "dlss",   "rt_comp", "screen_water"};
				const double g = std::max(1, perfGpuFrames_);
				std::string text = "  update_ms=" + std::to_string(perfUpdate_ / std::max(1, perfFrames_)) +
				                   " update_max=" + std::to_string(perfUpdateMax_) +
				                   " gpu_frame_max=" + std::to_string(perfGpuMax_) +
				                   " gpu_total=" + std::to_string(perfGpuTotal_ / g) +
				                   " substeps=" + std::to_string(perfSubsteps_ / g);
				for (size_t k = 0; k < perfGpu_.size(); ++k) {
					char cell[48];
					snprintf(cell, sizeof(cell), " %s=%.2f", names[k], perfGpu_[k] / g);
					text += cell;
				}
				{
					const auto& lod = Engine::Renderer::GetInstance()->GetSceneryLodStats();
					char cell[200];
					snprintf(
					    cell, sizeof(cell), " | shadow draws=%u inst_calls=%u inst=%u tris=%.0fk visible_tris=%.0fk",
					    lod.shadowDraws, lod.shadowInstancedCalls, lod.shadowInstances,
					    double(lod.shadowAllIndices) / 3000., double(lod.visibleIndices) / 3000.);
					text += cell;
					const auto* rd = Engine::Renderer::GetInstance();
					snprintf(
					    cell, sizeof(cell), " | particles=%u boxes=%zu spheres=%zu", rd->gpuFluidActiveParticleCount_,
					    rd->gpuFluidAABBs_.size(), rd->gpuFluidSplashSpheres_.size());
					text += cell;
				}
				perfNotes_ += text + "\n";
			}
			{
				std::string csv = "tests/out/slope-perf-" + std::string(shot.name) + ".csv";
				std::ofstream trace(csv);
				trace << "cx,cy,cz,bx,by,bz" << std::endl;
				for (size_t i = 0; i < perfCamera_.size(); ++i)
					trace << perfCamera_[i].x << "," << perfCamera_[i].y << "," << perfCamera_[i].z << ","
					      << perfBody_[i].x << "," << perfBody_[i].y << "," << perfBody_[i].z << std::endl;
			}
			std::wstring path = L"tests/out/slope-perf-";
			for (const char* c = shot.name; *c; ++c)
				path += wchar_t(*c);
			path += L".png";
			Capture(path.c_str());
			perfClock_ = 0;
			if (++perfShot_ >= count) {
				std::ofstream("tests/out/slope-perf.txt") << perfNotes_;
				PostQuitMessage(0);
			}
		}
		if (frames_ > 20000) {
			std::ofstream("tests/out/slope-perf.txt") << "timeout\n" << perfNotes_;
			PostQuitMessage(2);
		}
	}
	// 消火栓の放水の重さ (--slime-life-slope-hose-perf): 消火栓の輪で、乗る前・近くへ撃つ・遠くへ撃つ・撃ち終わって水が残る間の
	// 4 つの区間で、1 コマの時間 (平均と最大)・GPU の流体の各段の時間・放水の CPU の時間・流体を描く升の大きさを
	// tests/out/slope-hose-perf.txt に書き、区間ごとの画面を slope-hose-perf-*.png に撮る
	int hosePerfPhase_ = -1;
	float hosePerfClock_ = 0;
	std::string hosePerfNotes_;
	std::chrono::steady_clock::time_point hosePerfLast_;
	double hosePerfCpu_ = 0;
	struct HosePerfSum {
		double wall = 0, wallMax = 0, hose = 0, cell = 0, substeps = 0;
		size_t boxes = 0, spheres = 0;
		std::array<double, Engine::Renderer::kFluidProfileStageCount> gpu{};
		int frames = 0, gpuFrames = 0;
		uint32_t particles = 0;
	};
	HosePerfSum hosePerfSum_{};
	bool hosePerfLook_ = false;
	void UpdateHosePerf() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		const float m = SlopeData().unit;
		auto* renderer = Engine::Renderer::GetInstance();
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			hosePerfLast_ = std::chrono::steady_clock::now();
			renderer->SetFluidProfilerEnabled(true);
			return;
		}
		// 区間: 名前・始まり・測り始め・終わり (乗った時からの秒。乗る前は -3 から)
		struct Phase {
			const char* name;
			float begin, measure, end;
		};
		static const Phase phases[] = {
		    {"idle", -3, -2, 0}, {"near", 0, 1.5f, 4.5f}, {"far", 4.5f, 6.5f, 9.5f}, {"after", 9.5f, 10.5f, 13.5f}};
		if (hosePerfPhase_ < 0) {
			const auto views = chrono->TownGimmicks();
			const ChronoSystem::TownGimmickView* best = nullptr;
			float d = 1e9f;
			for (const auto& v : views)
				if (v.kind == int(GimmickKind::Hose)) {
					Vec e = v.at - Vec{-17 * m, 0, 40 * m};
					e.y = 0;
					if (Length(e) < d) {
						d = Length(e);
						best = &v;
					}
				}
			if (!best) {
				std::ofstream("tests/out/slope-hose-perf.txt") << "no hydrant\n";
				PostQuitMessage(2);
				return;
			}
			const DevourStage* slope = &DevourStages().back();
			for (const auto& s : DevourStages())
				if (s.map == "slope")
					slope = &s;
			body.mass = body.health = body.maxHealth = DevourGoalMass(*slope, 250);
			++life.revision;
			Vec at = best->at;
			at.y = SlopeGround(at) + 1.25f;
			body.at = at;
			body.action = LifeAction::Idle;
			GetComponents().get<Player>(player).velocity = {};
			GetComponents().get<TransformComponent>(player).translate = {at.x, at.y, at.z};
			chrono->RequestFluidReseed();
			hosePerfPhase_ = 0;
			hosePerfClock_ = -3;
			hosePerfSum_ = {};
			hosePerfCpu_ = chrono->HoseCpuMs();
		}
		const auto& phase = phases[size_t(hosePerfPhase_)];
		// 入力: 0 秒で乗る、近くは下向き・4.5 秒からは水平に近く (遠く) を照準で狙う、9.5 秒でやめる
		const float down = std::atan2(.38f, -.92f);
		input.yaw = down;
		input.pitch = hosePerfClock_ < 4.5f ? .4f : .12f;
		input.attack =
		    (hosePerfClock_ >= 0 && hosePerfClock_ < .05f) || (hosePerfClock_ >= 9.5f && hosePerfClock_ < 9.55f);
		const float before = life.age;
		Game::GameScene::Update();
		++frames_;
		hosePerfClock_ += std::max(life.age - before, 1.f / 120);
		// 着いた水を低い所から近くで見る (撮るだけ。時間は測る区間の外)
		if (hosePerfClock_ > 2.6f && hosePerfClock_ < 3.2f) {
			const Vec at = chrono->HoseImpact(), from = chrono->HoseNozzle();
			Vec d = at - from;
			d.y = 0;
			d = Length(d) > .1f ? Unit(d) : Vec{0, 0, 1};
			const Vec side{d.z, 0, -d.x};
			const Vec eye = at - d * 18.f + side * 10.f + Vec{0, 9, 0};
			const Vec to = at - eye;
			GetCamera().SetPosition(eye.x, eye.y, eye.z);
			GetCamera().SetRotation(std::atan2(-to.y, std::sqrt(to.x * to.x + to.z * to.z)), std::atan2(to.x, to.z), 0);
			if (hosePerfClock_ > 3.1f && !hosePerfLook_) {
				hosePerfLook_ = true;
				Capture(L"tests/out/slope-hose-perf-look.png");
			}
		}
		const auto now = std::chrono::steady_clock::now();
		const double ms = std::chrono::duration<double, std::milli>(now - hosePerfLast_).count();
		hosePerfLast_ = now;
		const double cpu = chrono->HoseCpuMs();
		const double hoseMs = cpu - hosePerfCpu_;
		hosePerfCpu_ = cpu;
		if (hosePerfClock_ >= phase.measure) {
			auto& s = hosePerfSum_;
			s.wall += ms;
			s.wallMax = std::max(s.wallMax, ms);
			s.hose += hoseMs;
			s.cell += renderer->volumePreviousVoxelSize_;
			s.particles = std::max(s.particles, renderer->gpuFluidActiveParticleCount_);
			++s.frames;
			const auto& stats = renderer->GetFluidProfileStats();
			if (stats.valid && stats.framesSinceSample == 0) {
				for (size_t k = 0; k < s.gpu.size(); ++k)
					s.gpu[k] += stats.lastMs[k];
				s.substeps += stats.substeps;
				++s.gpuFrames;
			}
			s.boxes = std::max(s.boxes, renderer->gpuFluidAABBs_.size());
			s.spheres = std::max(s.spheres, renderer->gpuFluidSplashSpheres_.size());
		}
		if (hosePerfClock_ >= phase.end) {
			const auto& s = hosePerfSum_;
			const double n = std::max(1, s.frames), g = std::max(1, s.gpuFrames);
			char line[600];
			snprintf(
			    line, sizeof(line),
			    "%s frame_ms=%.2f max_ms=%.1f hose_cpu_ms=%.3f cell=%.3f particles=%u gpu: sim=%.2f shapes=%.2f splat=%.2f filter=%.2f raymarch=%.2f impostors=%.2f shadow=%.2f screen_water=%.2f scene=%.2f substeps=%.2f boxes=%zu spheres=%zu riding=%d\n",
			    phase.name, s.wall / n, s.wallMax, s.hose / n, s.cell / n, s.particles,
			    s.gpu[Engine::Renderer::FluidSimulation] / g, s.gpu[Engine::Renderer::FluidShapes] / g,
			    s.gpu[Engine::Renderer::FluidSplat] / g, s.gpu[Engine::Renderer::FluidFiltering] / g,
			    s.gpu[Engine::Renderer::FluidRaymarch] / g, s.gpu[Engine::Renderer::FluidImpostors] / g,
			    s.gpu[Engine::Renderer::FluidShadow] / g, s.gpu[Engine::Renderer::FluidScreenWater] / g,
			    s.gpu[Engine::Renderer::SceneRender] / g, s.substeps / g, s.boxes, s.spheres,
			    int(chrono->GimmickRidingNow()));
			hosePerfNotes_ += line;
			std::wstring path = L"tests/out/slope-hose-perf-";
			for (const char* c = phase.name; *c; ++c)
				path += wchar_t(*c);
			path += L".png";
			Capture(path.c_str());
			// 消火栓の水の粒のうち、町の地面より 1 以上下に沈んだもの・地面の上にあるもの・空中のもの
			{
				int sunk = 0, ground = 0, air = 0;
				for (const auto& q : renderer->GetHydrantWaterSnapshot().particles) {
					if (q.at.y < -500)
						continue;
					const float floor = SlopeGround({q.at.x, 0, q.at.z});
					if (q.at.y < floor - 1)
						++sunk;
					else if (q.at.y < floor + 1.5f)
						++ground;
					else
						++air;
				}
				hosePerfNotes_ += "  water sunk=" + std::to_string(sunk) + " ground=" + std::to_string(ground) +
				                  " air=" + std::to_string(air) + "\n";
			}
			hosePerfSum_ = {};
			if (++hosePerfPhase_ >= int(std::size(phases))) {
				std::ofstream("tests/out/slope-hose-perf.txt") << hosePerfNotes_;
				PostQuitMessage(0);
			}
		}
		if (frames_ > 20000) {
			std::ofstream("tests/out/slope-hose-perf.txt") << "timeout\n" << hosePerfNotes_;
			PostQuitMessage(2);
		}
	}
	// 大きい物の手前で開く口の確認 (--slime-life-room-mouth): 10cm の体を、覆える中で大きい物の手前に置いて進ませ、
	// 口を開けている間の何コマかを撮る。口の幕に穴が開いていないかを画像で見る
	int mouthTarget_ = -1, mouthCase_ = 0, mouthShot_ = 0;
	float mouthClock_ = 0;
	std::string mouthNotes_;
	std::vector<int> mouthTargets_;
	void UpdateRoomMouth() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.pitch = .25f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		const auto& stage = DevourStages()[size_t(chrono->DevourStageIndex())];
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		if (mouthTargets_.empty()) {
			body.mass = body.health = body.maxHealth = DevourGoalMass(stage, 10);
			++life.revision;
			// 覆える物のうち、体に比べて大きい物 (.55〜.8) を大きい順に、場所が散るように選ぶ
			const float r = DevourBodyRadius(body.mass);
			std::vector<std::pair<float, int>> list;
			for (size_t i = 0; i < life.props.size(); ++i) {
				const auto& prop = life.props[i];
				if (!prop.active || prop.sealed || prop.beacon || !DevourCanCover(body.mass, prop.radius) ||
				    prop.radius < r * .55f)
					continue;
				list.push_back({-prop.radius, int(i)});
			}
			std::sort(list.begin(), list.end());
			for (const auto& [radius, i] : list) {
				bool apart = true;
				for (int k : mouthTargets_)
					if (LifeDistance(life.props[size_t(k)].at, life.props[size_t(i)].at) < 40)
						apart = false;
				if (apart)
					mouthTargets_.push_back(i);
				if (mouthTargets_.size() >= 5)
					break;
			}
			if (mouthTargets_.empty()) {
				std::ofstream("tests/out/room-mouth.txt") << "FAIL no target";
				PostQuitMessage(2);
				return;
			}
		}
		const int target = mouthTargets_[size_t(mouthCase_)];
		const auto& prop = life.props[size_t(target)];
		const float r = DevourBodyRadius(body.mass);
		if (mouthClock_ == 0) {
			Vec at = prop.at + Vec{0, 0, -3.2f * r};
			at.y = RoomGround(at + Vec{0, 1, 0}) + 1.25f;
			body.at = at;
			body.forward = body.liquidForward = {0, 0, 1};
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {at.x, at.y, at.z};
			mouthShot_ = 0;
		}
		input.yaw = 0;
		input.move = {0, 0, mouthClock_ > .4f ? 1.f : 0.f};
		const float before = life.age;
		Game::GameScene::Update();
		++frames_;
		mouthClock_ += std::max(life.age - before, 1.f / 120);
		// 口を開け始めてからの 4 コマ (口の時間で撮る)
		static constexpr float ages[] = {.2f, .28f, .36f, .5f};
		const float bite = chrono->DevourBiteAge();
		if (mouthShot_ < 4 && bite >= ages[mouthShot_]) {
			std::wstring path =
			    L"tests/out/room-mouth-" + std::to_wstring(mouthCase_) + L"-" + std::to_wstring(mouthShot_) + L".png";
			Capture(path.c_str());
			char line[200];
			snprintf(
			    line, sizeof(line), " case%d(r=%.2f ratio=%.2f at=%.0f,%.0f,%.0f bite=%.2f)", mouthCase_, prop.radius,
			    prop.radius / (r * DevourCoverRatio), prop.at.x, prop.at.y, prop.at.z, bite);
			if (mouthShot_ == 0)
				mouthNotes_ += line;
			++mouthShot_;
		}
		if (mouthClock_ > 3.f) {
			mouthClock_ = 0;
			if (++mouthCase_ >= int(mouthTargets_.size())) {
				std::ofstream("tests/out/room-mouth.txt") << "DONE" << mouthNotes_;
				PostQuitMessage(0);
			}
		}
		if (frames_ > 8000) {
			std::ofstream("tests/out/room-mouth.txt") << "FAIL timeout" << mouthNotes_;
			PostQuitMessage(2);
		}
	}
	// ベッドの確認 (--slime-life-room-bed): 本の階段のいちばん上からベッドの奥へ進み、ベッドの上に乗れるか (18cm と 10cm)。
	// ベッドの上に置いた物が掛け布団と枕の面より下に埋もれていないかも調べる
	int bedCase_ = 0;
	float bedClock_ = 0;
	std::string bedNotes_;
	bool bedValid_ = true;
	void UpdateRoomBed() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.pitch = .35f;
		input.yaw = 3.1416f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		const auto& stage = DevourStages()[size_t(chrono->DevourStageIndex())];
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		static constexpr float sizes[] = {18, 10};
		// 枕の上と、枕の横の掛け布団の上: カメラの向きを変えても、乗っているベッドが透けないか (8.6cm)
		struct PillowCase {
			const char* name;
			Vec at;
			float yaw;
		};
		static const PillowCase pillows[] = {
		    {"pillow-x", {-266, 63.2f, -240}, 1.5708f},
		    {"pillow-z", {-266, 63.2f, -250}, 0},
		    {"pillow-back", {-266, 63.2f, -240}, -1.5708f},
		    {"quilt-by-pillow", {-222, RoomQuiltTop, -240}, -1.5708f}};
		const int climbs = int(std::size(sizes)), total = climbs + int(std::size(pillows));
		if (bedCase_ >= climbs) {
			const auto& c = pillows[size_t(bedCase_ - climbs)];
			input.yaw = c.yaw;
			if (bedClock_ == 0) {
				body.mass = body.health = body.maxHealth = DevourGoalMass(stage, 8.6f);
				++life.revision;
				const Vec at = c.at + Vec{0, 1.25f, 0};
				body.at = at;
				body.forward = body.liquidForward = {std::sin(c.yaw), 0, std::cos(c.yaw)};
				GetComponents().get<Player>(player).velocity = {};
				auto& t = GetComponents().get<TransformComponent>(player);
				t.translate = {at.x, at.y, at.z};
			}
			const float before = life.age;
			Game::GameScene::Update();
			++frames_;
			bedClock_ += std::max(life.age - before, 1.f / 120);
			if (bedClock_ >= 1.5f) {
				float opacity = 1;
				for (auto e : GetComponents().view<CameraOccluder, NameComponent>())
					if (GetComponents().get<NameComponent>(e).name == "Chrono Lowpoly bed")
						opacity = GetComponents().get<CameraOccluder>(e).opacity;
				const bool solid = opacity > .9f;
				char line[160];
				snprintf(line, sizeof(line), " %s_bed_opacity=%.2f", c.name, opacity);
				bedNotes_ += line;
				bedValid_ &= solid;
				std::wstring path = L"tests/out/room-bed-";
				for (const char* p = c.name; *p; ++p)
					path += wchar_t(*p);
				path += L".png";
				Capture(path.c_str());
				bedClock_ = 0;
				if (++bedCase_ >= total) {
					std::ofstream("tests/out/room-bed.txt") << (bedValid_ ? "PASS" : "FAIL") << bedNotes_;
					PostQuitMessage(bedValid_ ? 0 : 2);
				}
			}
			if (frames_ > 6000) {
				std::ofstream("tests/out/room-bed.txt") << "FAIL timeout" << bedNotes_;
				PostQuitMessage(2);
			}
			return;
		}
		if (bedClock_ == 0) {
			// ベッドの上に置かれた物の数 (埋もれていないかは撮った画像で見る)
			if (bedCase_ == 0) {
				int onBed = 0;
				for (const auto& prop : life.props) {
					if (!prop.active || prop.visual == 0xffffffffu)
						continue;
					if (prop.at.x < -300 || prop.at.x > -60 || prop.at.z < -300 || prop.at.z > -180 || prop.at.y < 40)
						continue;
					++onBed;
				}
				bedNotes_ += " bed_props=" + std::to_string(onBed);
				bedValid_ &= onBed > 0;
			}
			body.mass = body.health = body.maxHealth = DevourGoalMass(stage, sizes[bedCase_]);
			++life.revision;
			const Vec at{-180, RoomBedTop + 1.25f, -172};
			body.at = at;
			body.forward = body.liquidForward = {0, 0, -1};
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {at.x, at.y, at.z};
		}
		input.move = {0, 0, bedClock_ > .4f ? 1.f : 0.f};
		const float before = life.age;
		Game::GameScene::Update();
		++frames_;
		bedClock_ += std::max(life.age - before, 1.f / 120);
		if (bedClock_ >= 3.5f) {
			const bool onBed = body.at.z < -190 && body.at.y > RoomMattressTop;
			char line[160];
			snprintf(
			    line, sizeof(line), " climb%.0fcm=%d(at=%.1f,%.1f,%.1f)", sizes[bedCase_], int(onBed), body.at.x,
			    body.at.y, body.at.z);
			bedNotes_ += line;
			bedValid_ &= onBed;
			std::wstring path = L"tests/out/room-bed-" + std::to_wstring(bedCase_) + L".png";
			Capture(path.c_str());
			bedClock_ = 0;
			++bedCase_;
		}
		if (frames_ > 4000) {
			std::ofstream("tests/out/room-bed.txt") << "FAIL timeout" << bedNotes_;
			PostQuitMessage(2);
		}
	}
	// 部屋の乗れる面の高さの確認 (--slime-life-room-surfaces): 高さの規則 (RoomSurfaces) の面ごとに数点で、
	// 実際に置いた家具と床の板のメッシュへ真上から線を下ろし、一番上の面の高さと規則の高さを比べる。
	// 規則より見た目が高いと、その面に乗った体が見た目の面に埋まる (飲める小物は面の上に置いた物なので除く)
	void UpdateRoomSurfaces() {
		using namespace Game;
		using namespace Game::Chrono;
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		auto& reg = GetComponents();
		auto* render = Engine::Renderer::GetInstance();
		auto* chrono = GetChronoSystem();
		std::set<uint32_t> pickups;
		for (const auto& prop : chrono->LifeState().props)
			if (prop.visual != 0xffffffffu)
				pickups.insert(prop.visual);
		// 置いた物の三角形 (ワールド座標)
		struct Mesh {
			std::string name;
			Vec lo, hi;
			std::vector<std::array<Vec, 3>> tris;
		};
		std::vector<Mesh> meshes;
		for (auto e : reg.view<MeshRendererComponent, TransformComponent, NameComponent>()) {
			const auto& name = reg.get<NameComponent>(e).name;
			if (name.rfind("Chrono Lowpoly", 0) != 0 && name.rfind("Chrono Room", 0) != 0)
				continue;
			if (pickups.count(uint32_t(e)))
				continue;
			const auto& mr = reg.get<MeshRendererComponent>(e);
			if (!mr.enabled || !mr.modelHandle)
				continue;
			auto* model = render->GetModel(mr.modelHandle);
			if (!model)
				continue;
			const auto& t = reg.get<TransformComponent>(e);
			const auto world = DirectX::XMMatrixScaling(t.scale.x, t.scale.y, t.scale.z) *
			                   DirectX::XMMatrixRotationRollPitchYaw(t.rotate.x, t.rotate.y, t.rotate.z) *
			                   DirectX::XMMatrixTranslation(t.translate.x, t.translate.y, t.translate.z);
			const auto& data = model->GetData();
			std::vector<Vec> points;
			for (const auto& v : data.vertices) {
				DirectX::XMFLOAT3 p;
				DirectX::XMStoreFloat3(
				    &p, DirectX::XMVector3TransformCoord(
				            DirectX::XMVectorSet(v.position.x, v.position.y, v.position.z, 1), world));
				points.push_back({p.x, p.y, p.z});
			}
			Mesh m;
			m.name = name;
			m.lo = {1e9f, 1e9f, 1e9f};
			m.hi = {-1e9f, -1e9f, -1e9f};
			for (const auto& p : points) {
				m.lo = {std::min(m.lo.x, p.x), std::min(m.lo.y, p.y), std::min(m.lo.z, p.z)};
				m.hi = {std::max(m.hi.x, p.x), std::max(m.hi.y, p.y), std::max(m.hi.z, p.z)};
			}
			if (data.indices.empty())
				for (size_t i = 0; i + 2 < points.size(); i += 3)
					m.tris.push_back({points[i], points[i + 1], points[i + 2]});
			else
				for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
					m.tris.push_back(
					    {points[data.indices[i]], points[data.indices[i + 1]], points[data.indices[i + 2]]});
			meshes.push_back(std::move(m));
		}
		// 真上からの線が当たる一番上の面 (limit より上は天井や照明なので数えない)。当たった物の名前も返す
		auto topAt = [&](float x, float z, float limit, std::string& hit) {
			float best = -1e9f;
			for (const auto& m : meshes) {
				if (x < m.lo.x || x > m.hi.x || z < m.lo.z || z > m.hi.z || m.lo.y > limit)
					continue;
				for (const auto& tri : m.tris) {
					const Vec a = tri[0], b = tri[1], c = tri[2];
					const float d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
					if (std::abs(d) < 1e-6f)
						continue;
					const float u = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d,
					            v = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d, w = 1 - u - v;
					if (u < 0 || v < 0 || w < 0)
						continue;
					const float y = u * a.y + v * b.y + w * c.y;
					if (y <= limit && y > best) {
						best = y;
						hit = m.name;
					}
				}
			}
			return best;
		};
		std::ofstream report("tests/out/room-surfaces.txt");
		int worst = 0;
		for (size_t s = 0; s < RoomSurfaces.size(); ++s) {
			const auto& f = RoomSurfaces[s].foot;
			for (float u : {.5f, .25f, .75f})
				for (float v : {.5f, .25f, .75f}) {
					if ((u == .5f) != (v == .5f))
						continue;
					const float x = f.x0 + (f.x1 - f.x0) * u, z = f.z0 + (f.z1 - f.z0) * v;
					const float rule = RoomTop({x, 0, z});
					std::string hit;
					const float top = topAt(x, z, rule + 12, hit);
					const float diff = top - rule;
					char line[200];
					snprintf(
					    line, sizeof(line), "surface%02zu at=(%.0f,%.0f) rule=%.2f mesh=%.2f diff=%+.2f %s\n", s, x, z,
					    rule, top, diff, hit.c_str());
					report << line;
					if (diff > .3f)
						++worst;
				}
		}
		report << "sinking_points=" << worst << "\n";
		// 丸みのある面 (こたつ布団・座布団) の縁から中心への断面
		struct Profile {
			const char* name;
			Vec from, to;
		};
		for (const auto& p :
		     {Profile{"kotatsu", {-238, 0, 176}, {-120, 0, 176}}, Profile{"zabuton", {-299, 0, 176}, {-262, 0, 176}}}) {
			report << "profile " << p.name << ":";
			for (int i = 0; i <= 16; ++i) {
				const Vec at = Lerp(p.from, p.to, float(i) / 16);
				std::string hit;
				const float rule = RoomTop(at), top = topAt(at.x, at.z, rule + 12, hit);
				char cell[64];
				snprintf(cell, sizeof(cell), " %.0f:%.1f/%.1f", Length(at - p.from), rule, top);
				report << cell;
			}
			report << "\n";
		}
		report.close();
		PostQuitMessage(0);
	}
	// 部屋のチュートリアルの後の流れの確認 (--slime-life-room-intro): ベッドの下から出たら目標の発表 → ステージの紹介 →
	// 収納ケースの上からスタート。それぞれを撮り、スタートの位置と時間の進み方を記録する
	float introClock_ = 0;
	int introShot_ = 0;
	std::string introNotes_;
	bool introOut_ = false;
	void UpdateRoomIntro() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.intro = true;
		input.pitch = .3f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		auto& clock = chrono->DevourClockState();
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		// ベッドの下から出す (段差を越えた所へ置く)
		if (!introOut_) {
			introOut_ = true;
			body.at = {-84, 1.25f, -160};
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {body.at.x, body.at.y, body.at.z};
		}
		const float before = life.age, timeBefore = clock.left;
		Game::GameScene::Update();
		++frames_;
		introClock_ += std::max(life.age - before, 1.f / 120);
		auto note = [&](const char* name) {
			char line[200];
			snprintf(
			    line, sizeof(line), " %s(t=%.1f body=%.1f,%.1f,%.1f left=%.1f)", name, introClock_, body.at.x,
			    body.at.y, body.at.z, clock.left);
			introNotes_ += line;
		};
		// 目標の発表 (1 秒)・紹介の 3 つのカット (発表は 8 秒で自動で進む)・スタートの後
		struct Moment {
			float at;
			const char* name;
			const wchar_t* path;
		};
		static const Moment moments[] = {{1.5f, "briefing", L"tests/out/room-intro-briefing.png"},
		                                 {10.f, "tour-room", L"tests/out/room-intro-tour1.png"},
		                                 {12.8f, "tour-tatami", L"tests/out/room-intro-tour2.png"},
		                                 {15.5f, "tour-start", L"tests/out/room-intro-tour3.png"},
		                                 {18.f, "countdown", L"tests/out/room-intro-countdown.png"},
		                                 {20.3f, "started", L"tests/out/room-intro-start.png"},
		                                 {23.f, "playing", L"tests/out/room-intro-playing.png"}};
		if (introShot_ < int(std::size(moments)) && introClock_ >= moments[size_t(introShot_)].at) {
			note(moments[size_t(introShot_)].name);
			Capture(moments[size_t(introShot_)].path);
			++introShot_;
		}
		// 発表と紹介の間は時間が止まり、スタートの後は進む
		if (introShot_ == int(std::size(moments))) {
			const bool onCase = body.at.x > RoomExitX0 && body.at.x < -60 && body.at.z > -192 && body.at.z < -180 &&
			                    body.at.y > RoomCaseHeight;
			const bool running = clock.left < CurrentStageTime(chrono) - 1 && timeBefore > clock.left - 1;
			std::ofstream("tests/out/room-intro.txt") << (onCase && running ? "PASS" : "FAIL") << " on_case=" << onCase
			                                          << " running=" << running << introNotes_;
			PostQuitMessage(onCase && running ? 0 : 2);
			++introShot_;
		}
		if (frames_ > 6000) {
			std::ofstream("tests/out/room-intro.txt") << "FAIL timeout" << introNotes_;
			PostQuitMessage(2);
		}
	}
	// 坂の町の最初の流れの確認 (--slime-life-slope-intro --devour-stage=1): 目標の発表 → ステージの紹介 (3 カット) → カウントダウン → スタート。
	// それぞれを撮り、発表と紹介の間は時間が止まり、スタートの後は体がスタートの位置のまま時間が進むことを slope-intro.txt に書く
	void UpdateSlopeIntro() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.intro = true;
		input.pitch = .3f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		auto& clock = chrono->DevourClockState();
		if (frames_ == 0)
			introNotes_.clear();
		const float before = life.age, timeBefore = clock.left;
		Game::GameScene::Update();
		++frames_;
		introClock_ += std::max(life.age - before, 1.f / 120);
		const float m = SlopeData().unit;
		const Vec spawn = DevourStages()[size_t(chrono->DevourStageIndex())].spawn;
		auto note = [&](const char* name) {
			char line[200];
			snprintf(
			    line, sizeof(line), " %s(t=%.1f body=%.1f,%.1f,%.1fm left=%.1f)", name, introClock_, body.at.x / m,
			    body.at.y / m, body.at.z / m, clock.left);
			introNotes_ += line;
		};
		struct Moment {
			float at;
			const char* name;
			const wchar_t* path;
		};
		static const Moment moments[] = {{1.5f, "briefing", L"tests/out/slope-intro-briefing.png"},
		                                 {9.6f, "tour-valley", L"tests/out/slope-intro-tour1.png"},
		                                 {12.5f, "tour-shrine", L"tests/out/slope-intro-tour2.png"},
		                                 {15.3f, "tour-start", L"tests/out/slope-intro-tour3.png"},
		                                 {17.5f, "countdown", L"tests/out/slope-intro-countdown.png"},
		                                 {20.3f, "started", L"tests/out/slope-intro-start.png"},
		                                 {23.f, "playing", L"tests/out/slope-intro-playing.png"}};
		// 発表と紹介の間 (カウントダウンの終わりまで) は時間が止まる
		if (introClock_ > .5f && introClock_ < 19.f && clock.left < timeBefore - 1e-4f)
			introHeldBroken_ = true;
		if (introShot_ < int(std::size(moments)) && introClock_ >= moments[size_t(introShot_)].at) {
			note(moments[size_t(introShot_)].name);
			Capture(moments[size_t(introShot_)].path);
			++introShot_;
		}
		if (introShot_ == int(std::size(moments))) {
			Vec d = body.at - spawn;
			d.y = 0;
			const bool atStart = Length(d) < 3 * m;
			const float total = CurrentStageTime(chrono);
			const bool running = clock.left < total - 1 && clock.left > total - 6;
			const bool ok = atStart && running && !introHeldBroken_;
			std::ofstream("tests/out/slope-intro.txt")
			    << (ok ? "PASS" : "FAIL") << " at_start=" << atStart << " running=" << running
			    << " held=" << !introHeldBroken_ << introNotes_;
			PostQuitMessage(ok ? 0 : 2);
			++introShot_;
		}
		if (frames_ > 8000) {
			std::ofstream("tests/out/slope-intro.txt") << "FAIL timeout" << introNotes_;
			PostQuitMessage(2);
		}
	}
	bool introHeldBroken_ = false;
	// 坂の上の作動範囲の膜 (--slime-life-slope-rings --devour-stage=1): 縁の地面の高さの差が一番大きい輪を 2 つ選び、
	// 横の低い所から撮る (膜の下の端が地面に沿っているかを見る)。高さの差を slope-rings.txt に書く
	int ringShot_ = -1;
	float ringClock_ = 0;
	std::vector<Game::Chrono::Vec> ringAt_;
	std::string ringNotes_;
	void UpdateSlopeRings() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.pitch = .3f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		const float m = SlopeData().unit;
		if (frames_ < 40) {
			++frames_;
			Game::GameScene::Update();
			return;
		}
		if (ringShot_ < 0) {
			struct Pick {
				float drop;
				Vec at;
			};
			std::vector<Pick> picks;
			for (const auto& v : chrono->TownGimmicks()) {
				if (v.kind != int(GimmickKind::Mikoshi) && v.kind != int(GimmickKind::Koma))
					continue;
				float lo = 1e9f, hi = -1e9f;
				for (int i = 0; i < 24; ++i) {
					const float a = 6.2831853f * float(i) / 24;
					const float h = SlopeWalkGround(v.at + Vec{std::cos(a), 0, std::sin(a)} * (12 * m));
					lo = std::min(lo, h);
					hi = std::max(hi, h);
				}
				picks.push_back({hi - lo, v.at});
			}
			std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.drop > b.drop; });
			for (size_t i = 0; i < picks.size() && i < 2; ++i) {
				ringAt_.push_back(picks[i].at);
				char line[120];
				snprintf(
				    line, sizeof(line), "ring at=(%.0f,%.0f)m drop=%.2fm\n", picks[i].at.x / m, picks[i].at.z / m,
				    picks[i].drop / m);
				ringNotes_ += line;
			}
			ringShot_ = 0;
			ringClock_ = 0;
			if (ringAt_.empty()) {
				std::ofstream("tests/out/slope-rings.txt") << "no rings\n";
				PostQuitMessage(2);
				return;
			}
		}
		const Vec at = ringAt_[size_t(ringShot_)];
		// 体は輪から離して置く (膜は出たまま)
		{
			Vec away = at + Vec{0, 0, -40 * m};
			away.y = SlopeGround(away) + 1.25f;
			body.at = away;
			GetComponents().get<Player>(player).velocity = {};
			GetComponents().get<TransformComponent>(player).translate = {away.x, away.y, away.z};
		}
		Game::GameScene::Update();
		++frames_;
		ringClock_ += 1.f / 60;
		// 坂の下り側の低い所から、輪の縁を横に見る
		Vec down{
		    -(SlopeGround(at + Vec{3, 0, 0}) - SlopeGround(at - Vec{3, 0, 0})), 0,
		    -(SlopeGround(at + Vec{0, 0, 3}) - SlopeGround(at - Vec{0, 0, 3}))};
		down = Length(down) > .01f ? Unit(down) : Vec{1, 0, 0};
		const Vec side{down.z, 0, -down.x};
		Vec eye = at + down * (30 * m) + side * (14 * m);
		eye.y = std::max(SlopeGround(eye), SlopeGround(at)) + 4 * m;
		const Vec focus{at.x, SlopeGround(at) + 1 * m, at.z};
		const Vec to = focus - eye;
		GetCamera().SetPosition(eye.x, eye.y, eye.z);
		GetCamera().SetRotation(std::atan2(-to.y, std::sqrt(to.x * to.x + to.z * to.z)), std::atan2(to.x, to.z), 0);
		if (ringClock_ > 1.f) {
			std::wstring path = L"tests/out/slope-ring-" + std::to_wstring(ringShot_) + L".png";
			Capture(path.c_str());
			ringClock_ = 0;
			if (++ringShot_ >= int(ringAt_.size())) {
				std::ofstream("tests/out/slope-rings.txt") << ringNotes_;
				PostQuitMessage(0);
			}
		}
	}
	static float CurrentStageTime(Game::ChronoSystem* chrono) {
		return Game::Chrono::DevourStages()[size_t(chrono->DevourStageIndex())].time;
	}
	void UpdateRoom() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.pitch = .3f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		auto& clock = chrono->DevourClockState();
		const auto& stage = DevourStages()[0];
		auto place = [&](Vec at) {
			body.at = at;
			body.at.y = 1.25f;
			GetComponents().get<Player>(player).velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {body.at.x, body.at.y, body.at.z};
		};
		auto step = [&](bool ok, const char* note) {
			roomValid_ &= ok;
			roomNotes_ += std::string(" ") + note + "=" + std::to_string(ok);
			++roomPhase_;
			roomClock_ = 0;
		};
		if (frames_ > 40 && roomPhase_ == 0) {
			bool closed = chrono->MoveWithinCity({-200, 1, -158}, {-200, 1, -150}, 1).z < -152 &&
			              chrono->MoveWithinCity({-70, 1, 144}, {-70, 1, 160}, 1).z < 150;
			{
				char why[160];
				snprintf(
				    why, sizeof(why), " start_detail(closed=%d stage=%d left=%.1f action=%d)", int(closed),
				    chrono->DevourStageIndex(), clock.left, int(body.action));
				roomNotes_ += why;
			}
			step(
			    closed && chrono->DevourStageIndex() == 0 && clock.left > stage.time - 10 && clock.left <= stage.time &&
			        body.action == LifeAction::Idle,
			    "start");
			Capture(L"tests/out/slime-life-room-start.png");
		} else if (roomPhase_ == 1) {
			input.attack = roomClock_ > .1f && roomClock_ < .2f;
			if (roomClock_ > .4f) {
				step(body.action == LifeAction::Idle, "no_item_no_morph");
				roomPhase_ = 10;
			}
		} else if (roomPhase_ == 10) {
			if (roomClock_ < .05f) {
				body.mass = body.health = body.maxHealth = 300;
				++life.revision;
			}
			input.move = {0, 0, 1};
			if (roomClock_ > 2.5f && roomClock_ < 2.55f)
				Capture(L"tests/out/slime-life-room-bite.png");
			if (roomClock_ > 5) {
				step(life.absorbed >= 4, "auto_eat");
				Capture(L"tests/out/slime-life-room-meals.png");
				roomPhase_ = 11;
			}
		}
		// 大きい物へ触れると、口を大きく開ける
		else if (roomPhase_ == 11) {
			input.yaw = 1.5708f;
			input.pitch = .15f;
			if (roomClock_ < .05f && !roomMouthPlaced_) {
				roomMouthPlaced_ = true;
				float best = 1e9f, r = DevourBodyRadius(body.mass);
				int target = -1;
				for (size_t i = 0; i < life.props.size(); ++i) {
					const auto& prop = life.props[i];
					if (!prop.active || prop.sealed || prop.beacon || !DevourCanCover(body.mass, prop.radius) ||
					    prop.radius < r * .55f)
						continue;
					float d = LifeDistance(prop.at, body.at);
					if (d < best) {
						best = d;
						target = int(i);
					}
				}
				roomValid_ &= target >= 0;
				if (target >= 0)
					place(life.props[size_t(target)].at + Vec{0, 0, -3.2f * r});
			}
			input.move = {roomClock_ > .5f ? -1.f : 0.f, 0, 0};
			if (roomClock_ > .7f && chrono->DevourBiteAge() > DevourLandAt - .03f && !roomMouthSeen_) {
				roomMouthSeen_ = true;
				Capture(L"tests/out/slime-life-room-mouth.png");
			}
			if (roomClock_ > 2.5f) {
				step(roomMouthSeen_, "mouth");
				roomPhase_ = 13;
			}
		}
		// 坂を登ってテーブルの上へ行ける
		else if (roomPhase_ == 13) {
			input.yaw = 1.5708f;
			input.pitch = .3f;
			if (roomClock_ < .05f)
				place({-70, 0, -10});
			input.move = {0, 0, roomClock_ > .2f ? 1.f : 0.f};
			if (roomClock_ > 7 && roomClock_ < 7.05f)
				Capture(L"tests/out/slime-life-room-table.png");
			if (roomClock_ > 7.2f) {
				step(body.at.y > 26, "table_climb");
				roomPhase_ = 2;
			}
		} else if (roomPhase_ == 2) {
			if (roomClock_ < .05f) {
				place(stage.itemAt + Vec{0, 0, -20});
				body.forward = body.liquidForward = {0, 0, 1};
			}
			input.yaw = 0;
			if (roomClock_ > .1f && roomClock_ < .15f)
				Capture(L"tests/out/slime-life-room-item.png");
			if (roomClock_ > .15f)
				input.move = {0, 0, 1};
			if (roomClock_ > 1.5f)
				step(chrono->DevourItemReady(), "item_taken");
		} else if (roomPhase_ == 3) {
			input.attack = roomClock_ > .1f && roomClock_ < .2f;
			if (roomClock_ > 1) {
				step(body.action == LifeAction::Vacuum && !chrono->DevourItemReady(), "morph");
				Capture(L"tests/out/slime-life-room-vacuum.png");
			}
		} else if (roomPhase_ == 4) {
			if (roomClock_ < .05f)
				chrono->DevourSetMorphLeft(.2f);
			if (roomClock_ > 1)
				step(body.action == LifeAction::Idle || body.action == LifeAction::Restore, "morph_ends");
		} else if (roomPhase_ == 5) {
			if (roomClock_ < .05f) {
				body.mass = body.health = body.maxHealth = DevourGoalMass(stage, stage.goals[0]) + 1;
				++life.revision;
			}
			if (roomClock_ > .5f) {
				step(chrono->DevourRank() == 1 && !chrono->Finished(), "clear_keeps_going");
				Capture(L"tests/out/slime-life-room-clear.png");
				clock.left = .05f;
			}
		} else if (roomPhase_ == 6 && roomClock_ > 1) {
			bool ok = chrono->Finished();
			// プレイ記録が書かれ、結果と時刻が入っている
			{
				std::ifstream log(chrono->DevourLogPath());
				std::string text((std::istreambuf_iterator<char>(log)), {});
				bool logged = text.find("\"result\": \"time_up\"") != std::string::npos &&
				              text.find("\"goalTimes\"") != std::string::npos &&
				              text.find("\"samples\"") != std::string::npos &&
				              std::filesystem::exists("tests/out/playlogs/summary.csv");
				roomValid_ &= logged;
				roomNotes_ += " play_log=" + std::to_string(logged);
			}
			roomValid_ &= ok;
			Capture(L"tests/out/slime-life-room-result.png");
			std::ofstream report("tests/out/slime-life-room.txt");
			report << (roomValid_ ? "PASS" : "FAIL") << roomNotes_ << " finished=" << ok;
			report.flush();
			PostQuitMessage(roomValid_ ? 0 : 2);
			return;
		}
		float before = life.age;
		Game::GameScene::Update();
		++frames_;
		if (roomPhase_ > 0)
			roomClock_ += std::max(life.age - before, 1.f / 120);
		if (frames_ > 3000) {
			std::ofstream("tests/out/slime-life-room.txt") << "FAIL timeout phase=" << roomPhase_ << roomNotes_;
			PostQuitMessage(2);
		}
	}
	void UpdateDevour() {
		GetChronoSystem()->DevourUseAllForms();
		using namespace Game;
		using namespace Game::Chrono;
		// 進み具合 (遅すぎるときの調査用)
		if (frames_ % 60 == 0) {
			trace_ << "devour frame=" << frames_ << " phase=" << devourPhase_
			       << " real=" << std::chrono::duration<float>(std::chrono::steady_clock::now() - start_).count()
			       << '\n';
			trace_.flush();
		}
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.pitch = .33f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		auto& controller = GetComponents().get<Player>(player);
		auto& clock = chrono->DevourClockState();
		auto setMass = [&](float mass) {
			body.mass = body.health = body.maxHealth = mass;
			body.action = LifeAction::Idle;
			body.actionAge = 0;
			++life.revision;
		};
		// 物の手前 distance に置き、そちらを向かせる
		auto placeBefore = [&](Vec at, float distance) {
			Vec from = at + Vec{0, 0, -distance};
			from.y = at.y - 1.2f + 1.25f;
			body.at = from;
			body.forward = body.liquidForward = {0, 0, 1};
			controller.velocity = {};
			auto& t = GetComponents().get<TransformComponent>(player);
			t.translate = {from.x, from.y, from.z};
		};
		// 近くの覆える (覆えない) 物で、moreThan より大きい物を探す
		// 坂の町: 物の手前 (-z の向き) の地面が物の足元と同じ高さか (placeBefore で体を置く所。段の上や下に置くと飛びかかりが届かない)
		auto levelBefore = [&](const LifeCollectible& prop, float distance) {
			if (DevourStages()[1].map != "slope")
				return true;
			const float base = SlopeGround(prop.at);
			for (float s = 0; s <= distance; s += 2)
				if (std::abs(SlopeGround(prop.at - Vec{0, 0, s}) - base) > 1)
					return false;
			return true;
		};
		auto nearest = [&](float limit, bool fits, float moreThan = 0, float lessThan = 1e9f) {
			int best = -1;
			float d = limit;
			for (size_t i = 0; i < life.props.size(); ++i) {
				const auto& prop = life.props[i];
				if (!prop.active || prop.visual == 0xffffffffu || prop.reach > 0 || prop.radius <= moreThan ||
				    prop.radius > lessThan || DevourCanCover(body.mass, prop.radius) != fits || !levelBefore(prop, 40))
					continue;
				// (歩き回る物は、置いた所から離れていくので狙わない)
				if (chrono->DevourRouteMover(prop.visual))
					continue;
				// (手前 (placeBefore で体を置く -z の側) にほかの物がある物は狙わない。物が増えて、手前の別の物を先に飲んでいた)
				bool crowded = false;
				for (size_t j = 0; j < life.props.size() && !crowded; ++j) {
					if (j == i || !life.props[j].active)
						continue;
					const Vec gap = life.props[j].at - prop.at;
					crowded = gap.z > -10 && gap.z < 1.5f && std::abs(gap.x) < 3;
				}
				if (crowded)
					continue;
				float distance = LifeDistance(prop.at, body.at);
				if (distance < d) {
					d = distance;
					best = int(i);
				}
			}
			return best;
		};
		bool pouncing = body.action == LifeAction::Engulf;
		if (pouncing && (!devourWasPouncing_ || body.actionAge < devourLastAge_)) {
			devourTracePick_ = devourPrevPick_;
			devourTraced_ = false;
		}
		// 調査用: 飛びかかりごとに、選んだ目標と覆い終えたときの結果を記録する
		if (pouncing && !devourTraced_ && body.actionAge > DevourCoverEnd) {
			devourTraced_ = true;
			const auto& pick = devourTracePick_;
			trace_ << "pounce phase=" << devourPhase_ << " mass=" << body.mass << " target=" << pick.index
			       << " fits=" << pick.fits;
			if (pick.index >= 0) {
				const auto& prop = life.props[size_t(pick.index)];
				trace_ << " radius=" << prop.radius << " eaten=" << !prop.active
				       << " gap=" << LifeDistance(prop.at, body.at) << " dy=" << prop.at.y - body.at.y
				       << " can_cover=" << DevourCanCover(body.mass, prop.radius)
				       << " under=" << DevourUnderCover(body.at, prop.at, body.mass, prop.radius, prop.reach)
				       << " sealed=" << prop.sealed << " target=" << devourTarget_;
			}
			trace_ << '\n';
		}
		devourPrevPick_ = DevourChooseTarget(body.at, body.forward, body.mass, life.props);
		if (pouncing && (!devourWasPouncing_ || body.actionAge < devourLastAge_))
			++devourPounces_;
		devourWasPouncing_ = pouncing;
		devourLastAge_ = body.actionAge;
		devourMaxCombo_ = std::max(devourMaxCombo_, clock.combo);
		auto next = [&]() {
			devourNotes_ += " |p" + std::to_string(devourPhase_) + ":a" + std::to_string(life.absorbed) + "t" +
			                std::to_string(chrono->DevourRank()) + "l" + std::to_string(int(clock.left)) + "f" +
			                std::to_string(chrono->Finished()) + "m" + std::to_string(int(body.mass));
			++devourPhase_;
			devourClock_ = 0;
			devourFresh_ = true;
		};
		float before = life.age;
		const int phaseAtStart = devourPhase_;
		// 実際の遊び方: 開始位置から前へ進みながら吸収ボタンを連打して、ちゃんと飲み込めるか
		if (devourPhase_ == 0 && frames_ > 30) {
			if (frames_ == 31) {
				devourClock_ = 0;
				devourAbsorbed_ = life.absorbed;
				devourPounces_ = 0;
				devourGrounded_ = devourFrames_ = 0;
			}
			input.move = {0, 0, devourClock_ < 6 ? 1.f : 0.f};
			input.absorb = std::fmod(devourClock_, .45f) < .12f;
			++devourFrames_;
			if (controller.grounded)
				++devourGrounded_;
			if (devourClock_ > 8) {
				bool ok = life.absorbed >= devourAbsorbed_ + 2;
				devourValid_ &= ok;
				devourNotes_ += " play_pounces=" + std::to_string(devourPounces_) +
				                " play_eaten=" + std::to_string(life.absorbed - devourAbsorbed_) +
				                " grounded=" + std::to_string(devourGrounded_ * 100 / std::max(1, devourFrames_)) + "%";
				next();
				devourPhase_ = 9;
			}
		} else if (devourPhase_ == 9) {
			devourValid_ &=
			    chrono->DevourMode() && chrono->LifeEnemies().empty() && clock.left > DevourStages()[1].time - 12;
			devourTarget_ = nearest(400, true);
			devourValid_ &= devourTarget_ >= 0;
			if (devourTarget_ >= 0) {
				body.action = LifeAction::Idle;
				body.actionAge = 0;
				placeBefore(life.props[size_t(devourTarget_)].at, 8);
				devourMass_ = body.mass;
				devourAbsorbed_ = life.absorbed;
			}
			devourNotes_ += " no_enemies=" + std::to_string(chrono->LifeEnemies().empty());
			next();
			devourPhase_ = 1;
		}
		// 1 回飛びかかる: 跳ぶ・覆う・満足の画像を撮り、飲み込んで大きくなったか
		// (カメラを横へ回してから跳び、包み込みの形を横から撮る)
		else if (devourPhase_ == 1) {
			input.yaw = 1.5708f;
			input.pitch = .12f;
			input.absorb = devourClock_ > .8f && devourClock_ < .85f;
			const wchar_t* images[] = {
			    L"tests/out/slime-life-devour-leap.png", L"tests/out/slime-life-devour-cover.png",
			    L"tests/out/slime-life-devour-settle.png"};
			const float at[] = {DevourWindup + DevourLeap * .6f, DevourLandAt + .03f, DevourCoverEnd + .12f};
			for (int i = 0; i < 3; ++i)
				if (pouncing && body.actionAge >= at[i] && !devourImages_[size_t(i)]) {
					Capture(images[i]);
					devourImages_[size_t(i)] = true;
				}
			if (devourClock_ > 1.9f) {
				bool ok = devourTarget_ >= 0 && life.absorbed > devourAbsorbed_ && body.mass > devourMass_ &&
				          !life.props[size_t(devourTarget_)].active;
				devourValid_ &= ok;
				devourNotes_ += " single=" + std::to_string(ok);
				next();
			}
		}
		// 押し続けると、近くの物へ次々に飛びかかる
		else if (devourPhase_ == 2) {
			input.absorb = true;
			if (devourFresh_) {
				devourAbsorbed_ = life.absorbed;
				devourPounces_ = 0;
			}
			int target = nearest(60, true);
			if (target >= 0 && (body.action != LifeAction::Engulf || body.actionAge >= DevourCoverEnd)) {
				Vec d = life.props[size_t(target)].at - body.at;
				d.y = 0;
				if (Length(d) > .1f)
					body.forward = body.liquidForward = Unit(d);
			}
			if (devourClock_ > 5) {
				bool ok = devourPounces_ >= 3 && devourPounces_ <= 6;
				devourValid_ &= ok;
				devourNotes_ += " held_pounces=" + std::to_string(devourPounces_) +
				                " held_eaten=" + std::to_string(life.absorbed - devourAbsorbed_) +
				                " max_combo=" + std::to_string(devourMaxCombo_);
				next();
				devourPhase_ = 20;
			}
		}
		// 連打しても空へ上っていかない (何も無い所へ何度も飛びかかり、高さが変わらないか)
		else if (devourPhase_ == 20) {
			if (devourFresh_) {
				devourBaseY_ = body.at.y;
				devourLift_ = 0;
			}
			input.absorb = (frames_ / 4) % 2 == 0;
			devourLift_ = std::max(devourLift_, body.at.y - devourBaseY_);
			if (devourClock_ > 3) {
				bool ok = devourLift_ < 1.5f;
				devourValid_ &= ok;
				devourNotes_ += " mash_lift=" + std::to_string(devourLift_);
				next();
				devourPhase_ = 3;
			}
		}
		// 大きすぎる車には弾かれる
		else if (devourPhase_ == 3) {
			if (devourFresh_) {
				setMass(300);
				devourTarget_ =
				    nearest(3000, false, 10, 20); // (車ほどの大きさ。家は足元が広く、飛びかかりの届く所に置けない)
				devourValid_ &= devourTarget_ >= 0;
				if (devourTarget_ < 0) {
					next();
					return;
				}
				placeBefore(life.props[size_t(devourTarget_)].at, 16);
				// 手前の小物を選ばないよう、近くの他の物は片付けておく
				for (size_t i = 0; i < life.props.size(); ++i)
					if (int(i) != devourTarget_ && LifeDistance(life.props[i].at, body.at) < 40)
						life.props[i].active = false;
			}
			input.absorb = devourClock_ > .1f && devourClock_ < .3f;
			if (devourClock_ > .1f && pouncing && body.actionAge > DevourLandAt + .02f &&
			    body.actionAge < DevourCoverEnd && !devourImages_[3]) {
				Capture(L"tests/out/slime-life-devour-bounce.png");
				devourImages_[3] = true;
				devourBounced_ = Dot(Vec{controller.velocity.x, 0, controller.velocity.z}, body.forward) < 0;
			}
			if (devourClock_ > 1.2f) {
				bool ok = devourBounced_ && life.props[size_t(devourTarget_)].active;
				devourValid_ &= ok;
				devourNotes_ += " bounce=" + std::to_string(ok);
				next();
			}
		}
		// 大きくなれば車を丸のみにでき、体の中に見える
		else if (devourPhase_ == 4) {
			if (devourFresh_) {
				setMass(8000);
				devourTarget_ = nearest(3000, true, 10);
				devourValid_ &= devourTarget_ >= 0;
				if (devourTarget_ < 0) {
					next();
					return;
				}
				placeBefore(life.props[size_t(devourTarget_)].at, 40);
				// 車の手前の小物を先に飲まないよう、近くの他の物は片付けておく (坂の町は道ぞいに小物が多い)
				for (size_t i = 0; i < life.props.size(); ++i)
					if (int(i) != devourTarget_ && LifeDistance(life.props[i].at, body.at) < 45)
						life.props[i].active = false;
			}
			input.absorb = devourClock_ > .4f && devourClock_ < .6f;
			if (devourClock_ > .4f && pouncing && body.actionAge > DevourCoverEnd + .15f && !devourImages_[4]) {
				Capture(L"tests/out/slime-life-devour-car.png");
				devourImages_[4] = true;
			}
			if (devourClock_ > 2) {
				bool ok = !life.props[size_t(devourTarget_)].active;
				devourValid_ &= ok;
				devourNotes_ += " car=" + std::to_string(ok);
				next();
			}
		}
		// 家 (建物) も大きさが足りれば丸のみにでき、跡を通れる
		else if (devourPhase_ == 5) {
			if (devourFresh_) {
				devourTarget_ = -1;
				float best = 1e9f;
				for (size_t i = 0; i < life.props.size(); ++i) {
					const auto& prop = life.props[i];
					if (!prop.active || std::max(prop.reach, prop.foot) < 10 || prop.radius > 40)
						continue;
					float d = LifeDistance(prop.at, body.at);
					if (d < best) {
						best = d;
						devourTarget_ = int(i);
					}
				}
				devourValid_ &= devourTarget_ >= 0;
				if (devourTarget_ < 0) {
					next();
					return;
				}
				const auto& house = life.props[size_t(devourTarget_)];
				setMass(DevourMassToCover(house.radius) * 1.05f);
				placeBefore(house.at, std::max(house.reach, house.foot) + 3 * LifeScale(body.mass) + 4);
				// 家の前の塀や庭の小物を先に飲まないよう、近くの他の物は片付けておく (坂の町は家ごとに塀と小物がある)
				for (size_t i = 0; i < life.props.size(); ++i)
					if (int(i) != devourTarget_ &&
					    std::min(LifeDistance(life.props[i].at, house.at), LifeDistance(life.props[i].at, body.at)) <
					        std::max(house.reach, house.foot) + 40) {
						life.props[i].active = false;
						devourCleared_.push_back(i);
					}
			}
			input.absorb = devourClock_ > .4f && devourClock_ < .6f;
			if (devourClock_ > 1.6f) {
				const auto& house = life.props[size_t(devourTarget_)];
				// 飲み込めて、跡の真ん中を通れる
				const bool eaten = !house.active,
				           pass = chrono->MoveWithinCity(house.at, house.at + Vec{1, 0, 0}, 1).x > house.at.x + .5f;
				bool ok = eaten && pass;
				devourValid_ &= ok;
				for (size_t i : devourCleared_)
					if (i < life.props.size())
						life.props[i].active = true;
				devourCleared_.clear();
				devourNotes_ += " house=" + std::to_string(ok) + " house_eaten=" + std::to_string(eaten) +
				                " house_pass=" + std::to_string(pass) +
				                " house_dy=" + std::to_string(int(house.at.y - body.at.y));
				Capture(L"tests/out/slime-life-devour-house.png");
				next();
				devourPhase_ = 21;
			}
		}
		// 建物を背にすると、カメラは手前へ寄らず、間の建物が透ける
		else if (devourPhase_ == 21) {
			input.yaw = 0;
			if (devourFresh_) {
				setMass(150);
				devourTarget_ = -1;
				float best = 1e9f;
				// (家ほどの大きさの物だけ。坂の町の桜や柿の木は根元が太くても、枝の中にカメラが入るだけで建物の確認にならない)
				for (size_t i = 0; i < life.props.size(); ++i) {
					const auto& prop = life.props[i];
					if (!prop.active || std::max(prop.reach, prop.foot) < 10 || prop.radius > 40 || prop.radius < 24)
						continue;
					// (坂の町は、家の背中の地面が家の敷地と同じ高さの物だけ。段の上から見下ろすと、家は体とカメラの間に入らない)
					if (DevourStages()[1].map == "slope" &&
					    std::abs(
					        SlopeGround(prop.at + Vec{0, 0, std::max(prop.reach, prop.foot) + 8}) -
					        (prop.at.y - 1.2f)) > 1)
						continue;
					float d = LifeDistance(prop.at, body.at);
					if (d < best) {
						best = d;
						devourTarget_ = int(i);
					}
				}
				devourValid_ &= devourTarget_ >= 0;
				if (devourTarget_ < 0) {
					next();
					devourPhase_ = 6;
					return;
				}
				// 建物の北の壁のすぐ外に置き、南 (建物の側) から見る
				const auto& house = life.props[size_t(devourTarget_)];
				Vec at = house.at + Vec{0, 0, std::max(house.reach, house.foot) + 8};
				// (坂の町は家ごとに地面の高さが違うので、家の背中の地面に置く)
				at.y = DevourStages()[1].map == "slope" ? SlopeGround(at) + 1.25f : 1.25f;
				body.at = at;
				body.forward = body.liquidForward = {0, 0, 1};
				controller.velocity = {};
				auto& t = GetComponents().get<TransformComponent>(player);
				t.translate = {at.x, at.y, at.z};
			}
			if (devourClock_ > 1.5f) {
				auto camera = GetCamera().Position();
				float distance = LifeDistance({camera.x, camera.y, camera.z}, body.at);
				float faded = 1;
				for (auto e : GetComponents().view<CameraOccluder>())
					faded = std::min(faded, GetComponents().get<CameraOccluder>(e).opacity);
				bool ok = distance > 12 && faded < .5f;
				devourValid_ &= ok;
				devourNotes_ += " see_through=" + std::to_string(ok) + " cam_dist=" + std::to_string(int(distance)) +
				                " faded=" + std::to_string(faded);
				{
					char where[200];
					snprintf(
					    where, sizeof(where), " cam=(%.0f,%.0f,%.0f) body=(%.0f,%.0f,%.0f)", camera.x, camera.y,
					    camera.z, body.at.x, body.at.y, body.at.z);
					devourNotes_ += where;
					if (DevourStages()[1].map == "slope") {
						snprintf(
						    where, sizeof(where), " ground_cam=%.0f ground_body=%.0f",
						    SlopeGround({camera.x, 0, camera.z}), SlopeGround(body.at));
						devourNotes_ += where;
					}
				}
				Capture(L"tests/out/slime-life-devour-seethrough.png");
				next();
				devourPhase_ = 23;
			}
		}
		// ハンマー: 大きすぎる建物を叩くと砕けて、飲める欠片が散らばる
		else if (devourPhase_ == 23) {
			input.yaw = 0;
			input.morphForm = 1;
			if (devourFresh_) {
				devourTarget_ = -1;
				float best = 1e9f;
				for (size_t i = 0; i < life.props.size(); ++i) {
					const auto& prop = life.props[i];
					if (!prop.active || std::max(prop.reach, prop.foot) < 10 || prop.radius > 40)
						continue;
					float d = LifeDistance(prop.at, body.at);
					if (d < best) {
						best = d;
						devourTarget_ = int(i);
					}
				}
				devourValid_ &= devourTarget_ >= 0;
				if (devourTarget_ < 0) {
					next();
					devourPhase_ = 24;
					return;
				}
				const auto& house = life.props[size_t(devourTarget_)];
				setMass(DevourMassToShatter(house.radius) * 1.1f);
				placeBefore(house.at, std::max(house.reach, house.foot) + 2 * LifeScale(body.mass));
				devourAbsorbed_ = int(life.props.size());
			}
			input.morphNow = devourClock_ < .05f;
			input.attack = devourClock_ > .5f;
			if (devourClock_ > 2.5f) {
				const auto& house = life.props[size_t(devourTarget_)];
				bool ok = !house.active && int(life.props.size()) == devourAbsorbed_ + DevourShardCount &&
				          DevourCanCover(body.mass, life.props.back().radius);
				devourValid_ &= ok;
				devourNotes_ += " hammer_shatter=" + std::to_string(ok);
				Capture(L"tests/out/slime-life-devour-shatter.png");
				next();
				devourPhase_ = 24;
			}
		}
		// タイヤ: 転がるだけで、普段は沈まない大きさの物を通り道で吸い込む
		else if (devourPhase_ == 24) {
			input.yaw = 0;
			input.morphForm = 2;
			if (devourFresh_) {
				setMass(3000);
				body.tireUnlocked = true;
				devourTarget_ = -1;
				float best = 1e9f;
				for (size_t i = 0; i < life.props.size(); ++i) {
					const auto& prop = life.props[i];
					if (!prop.active || prop.reach > 0 || prop.visual == 0xffffffffu ||
					    !DevourCanCover(body.mass, prop.radius))
						continue;
					// (歩き回る物と、転がる道 (手前の -z の側) に覆えない物がある物は狙わない)
					if (chrono->DevourRouteMover(prop.visual) || !levelBefore(prop, 4 * LifeScale(body.mass) + 2))
						continue;
					const float lane = 4 * LifeScale(body.mass);
					bool blocked = false;
					for (const auto& other : life.props) {
						if (&other == &prop || !other.active || DevourCanCover(body.mass, other.radius))
							continue;
						const Vec gap = other.at - prop.at;
						blocked |= gap.z > -lane - other.radius && gap.z < other.radius &&
						           std::abs(gap.x) < lane * .6f + other.radius;
					}
					if (blocked)
						continue;
					float d = LifeDistance(prop.at, body.at);
					if (d < best) {
						best = d;
						devourTarget_ = int(i);
					}
				}
				devourValid_ &= devourTarget_ >= 0;
				if (devourTarget_ < 0) {
					next();
					devourPhase_ = 6;
					return;
				}
				placeBefore(life.props[size_t(devourTarget_)].at, 4 * LifeScale(body.mass));
			}
			input.morphNow = devourClock_ < .05f;
			input.move = {0, 0, devourClock_ > .5f ? 1.f : 0.f};
			if (devourClock_ > 3) {
				const auto& target = life.props[size_t(devourTarget_)];
				bool ok = !target.active;
				devourValid_ &= ok;
				devourNotes_ += " tire_sink=" + std::to_string(ok);
				if (!ok) {
					char why[200];
					snprintf(
					    why, sizeof(why), " tire_target=(%.0f,%.0f,%.0f) r=%.1f body=(%.0f,%.0f,%.0f) action=%d",
					    target.at.x, target.at.y, target.at.z, target.radius, body.at.x, body.at.y, body.at.z,
					    int(body.action));
					devourNotes_ += why;
				}
				next();
				devourPhase_ = 27;
			}
		}
		// 掃除機: 押している間、前の小物を吸い込む
		else if (devourPhase_ == 27) {
			input.yaw = devourClock_ > 1.6f ? 1.5708f : 0;
			input.pitch = .2f;
			input.morphForm = 3;
			if (devourFresh_) {
				setMass(3000);
				body.vacuumUnlocked = true;
				devourTarget_ = -1;
				float best = 1e9f;
				for (size_t i = 0; i < life.props.size(); ++i) {
					const auto& prop = life.props[i];
					if (!prop.active || prop.sealed || prop.reach > 0 || !DevourCanCover(body.mass, prop.radius))
						continue;
					float d = LifeDistance(prop.at, body.at);
					if (d < best) {
						best = d;
						devourTarget_ = int(i);
					}
				}
				devourValid_ &= devourTarget_ >= 0;
				if (devourTarget_ < 0) {
					next();
					devourPhase_ = 28;
					return;
				}
				placeBefore(life.props[size_t(devourTarget_)].at, 14 * LifeScale(body.mass));
				devourAbsorbed_ = life.absorbed;
			}
			input.morphNow = devourClock_ < .05f;
			input.attack = devourClock_ > .5f && devourClock_ < 1.5f;
			input.move = {0, 0, devourClock_ > .9f && devourClock_ < 1.5f ? 1.f : 0.f};
			if (devourClock_ > 1.4f && devourClock_ < 1.45f)
				Capture(L"tests/out/slime-life-devour-vacuum-back.png");
			if (devourClock_ > 1.72f && devourNotes_.find("vacuum_eaten") == std::string::npos) {
				bool ok = life.absorbed > devourAbsorbed_;
				devourValid_ &= ok;
				Capture(L"tests/out/slime-life-devour-vacuum-side.png");
				devourNotes_ += " vacuum_eaten=" + std::to_string(life.absorbed - devourAbsorbed_);
			}
			if (devourClock_ > 2.2f) {
				next();
				devourPhase_ = 28;
			}
		}
		// バネ: 溜めて離すと建物を飛び越えて、向こうの道に着地する
		else if (devourPhase_ == 28) {
			input.yaw = devourClock_ > .4f ? 1.5708f : 0;
			input.pitch = .2f;
			input.morphForm = 4;
			if (devourFresh_) {
				devourTarget_ = -1;
				float best = 1e9f;
				for (size_t i = 0; i < life.props.size(); ++i) {
					const auto& prop = life.props[i];
					if (!prop.active || prop.sealed || std::max(prop.reach, prop.foot) < 10 || prop.radius > 40)
						continue;
					float d = LifeDistance(prop.at, body.at);
					if (d < best) {
						best = d;
						devourTarget_ = int(i);
					}
				}
				devourValid_ &= devourTarget_ >= 0;
				if (devourTarget_ < 0) {
					next();
					devourPhase_ = 6;
					return;
				}
				setMass(6000);
				body.springUnlocked = true;
				const auto& house = life.props[size_t(devourTarget_)];
				placeBefore(house.at, std::max(house.reach, house.foot) + 2 * LifeScale(body.mass));
				devourBaseY_ = body.at.y;
				devourLift_ = 0;
				devourMass_ = body.at.z;
			}
			input.morphNow = devourClock_ < .05f;
			input.attack = devourClock_ > .6f && devourClock_ < 1.4f;
			input.move = devourClock_ > 1.3f && devourClock_ < 1.5f ? Vec{-1, 0, 0} : Vec{};
			if (devourClock_ > 1.2f && devourClock_ < 1.25f)
				Capture(L"tests/out/slime-life-devour-spring-charge.png");
			devourLift_ = std::max(devourLift_, body.at.y - devourBaseY_);
			if (devourClock_ > .52f && devourClock_ < .57f)
				Capture(L"tests/out/slime-life-devour-spring-idle.png");
			if (devourClock_ > 1.75f && devourClock_ < 1.8f)
				Capture(L"tests/out/slime-life-devour-spring-air.png");
			if (devourClock_ > 3.4f) {
				const auto& house =
				    life.props[size_t(devourTarget_)]; // (着地は地面の上。坂の町は段の高さが違う所へ降りることがある)
				const float ground = DevourStages()[1].map == "slope" ? SlopeGround(body.at) : devourBaseY_ - 1.25f;
				const bool landed = std::abs(body.at.y - 1.25f - ground) < 2;
				bool ok = devourLift_ > DevourSpringRise(6000, DevourSpringMinPower) * .8f && house.active &&
				          body.at.z > devourMass_ + 20 && landed;
				devourValid_ &= ok;
				devourNotes_ += " spring_hop=" + std::to_string(ok) + " landed=" + std::to_string(landed) +
				                " house_left=" + std::to_string(house.active) +
				                " lift=" + std::to_string(int(devourLift_)) +
				                " moved=" + std::to_string(int(body.at.z - devourMass_));
				Capture(L"tests/out/slime-life-devour-spring-land.png");
				next();
				devourPhase_ = 6;
			}
		}
		// 大きくなっても遠くまで見え、動いても体が見える (撮った画面で確かめる)
		else if (devourPhase_ == 22) {
			input.yaw = .7f;
			if (devourFresh_) {
				setMass(1.2e6f);
				Vec at{0, 1.25f, 0};
				body.at = at;
				controller.velocity = {};
				auto& t = GetComponents().get<TransformComponent>(player);
				t.translate = {at.x, at.y, at.z};
			}
			if (devourClock_ > 2 && devourClock_ < 6)
				input.move = {0, 0, 1};
			if (devourClock_ > 2.5f && devourNotes_.find("big_still") == std::string::npos) {
				Capture(L"tests/out/slime-life-devour-big-still.png");
				devourNotes_ += " big_still";
			}
			if (devourClock_ > 5.5f) {
				Capture(L"tests/out/slime-life-devour-big-moving.png");
				auto camera = GetCamera().Position();
				devourNotes_ +=
				    " big_cam=" + std::to_string(int(LifeDistance({camera.x, camera.y, camera.z}, body.at))) +
				    " big_at=" + std::to_string(int(body.at.x)) + "," + std::to_string(int(body.at.z));
				next();
				devourPhase_ = 25;
			}
		}
		// 大きくなっても液体の体が中心から置いていかれない: 歩く・飛びかかる・タイヤの突進で、粒子の重心のずれを体の半径で測る
		else if (devourPhase_ == 25) {
			constexpr float masses[] = {8000, 120000, 1.2e6f};
			input.yaw = .7f;
			int stage = std::min(2, int(devourClock_ / 9));
			float t = devourClock_ - float(stage) * 9;
			if (stage != devourLagStage_) {
				if (devourLagStage_ >= 0)
					devourNotes_ += " lag" + std::to_string(int(masses[devourLagStage_])) + "=" +
					                std::to_string(devourLag_[0]) + "/" + std::to_string(devourLag_[1]) + "/" +
					                std::to_string(devourLag_[2]);
				devourLagStage_ = stage;
				devourLag_ = {};
				setMass(masses[stage]);
				body.tireUnlocked = true;
				Vec at{0, 1.25f, 0};
				body.at = at;
				controller.velocity = {};
				auto& tr = GetComponents().get<TransformComponent>(player);
				tr.translate = {at.x, at.y, at.z};
			}
			input.move = {0, 0, 1};
			input.morphForm = 2;
			input.absorb = t > 2.5f && t < 5.5f;
			input.morphNow = t > 6 && t < 6.05f;
			input.attack = t > 6.8f && t < 7.8f;
			const auto& snapshot = GetRenderer()->GetFluidBodySnapshot();
			if (t > .6f && !snapshot.offsets.empty()) {
				Vec mean{};
				for (auto o : snapshot.offsets) {
					mean.x += o.x;
					mean.z += o.z;
				}
				mean = mean * (1.f / float(snapshot.offsets.size()));
				float lag = std::sqrt(mean.x * mean.x + mean.z * mean.z) / DevourBodyRadius(body.mass);
				auto& slot = devourLag_[t < 2.5f ? 0 : t < 6 ? 1 : 2];
				slot = std::max(slot, lag);
				devourLagWorst_ = std::max(devourLagWorst_, lag);
			}
			if (devourClock_ > 27) {
				devourNotes_ += " lag" + std::to_string(int(masses[2])) + "=" + std::to_string(devourLag_[0]) + "/" +
				                std::to_string(devourLag_[1]) + "/" + std::to_string(devourLag_[2]);
				devourValid_ &= devourLagWorst_ < 1;
				setMass(masses[0]);
				next();
				devourPhase_ = 7;
			}
		}
		// 時間は減り、目標に届くとランクが上がる
		else if (devourPhase_ == 6) {
			const auto& stage = DevourStages()[1];
			if (devourFresh_)
				devourLeft_ = clock.left;
			if (devourClock_ > 1 && devourNotes_.find("ticking") == std::string::npos) {
				bool ticking = clock.left < devourLeft_ - .8f && clock.left > devourLeft_ - 1.3f;
				setMass(DevourGoalMass(stage, stage.goals[0]) + 1);
				devourNotes_ += " ticking=" + std::to_string(ticking);
				devourValid_ &= ticking;
			}
			if (devourClock_ > 1.4f) {
				bool ok = chrono->DevourRank() >= 1;
				devourValid_ &= ok;
				devourNotes_ += " rank_up=" + std::to_string(ok);
				next();
				devourPhase_ = 22;
			}
		}
		// S ランクでも終わらず、時間切れで終わって結果を出す
		else if (devourPhase_ == 7) {
			const auto& stage = DevourStages()[1];
			if (devourFresh_)
				setMass(DevourGoalMass(stage, stage.goals.back()) + 1);
			if (devourClock_ > .3f && devourNotes_.find("keeps_going") == std::string::npos) {
				bool ok = chrono->DevourRank() == int(stage.goals.size()) && !chrono->Finished();
				devourValid_ &= ok;
				devourNotes_ += " keeps_going=" + std::to_string(ok);
				clock.left = .05f;
			}
			if (devourClock_ > 1.5f) {
				bool ok = chrono->Finished();
				devourValid_ &= ok;
				Capture(L"tests/out/slime-life-devour-timeup.png");
				devourNotes_ += " timeup=" + std::to_string(ok);
				next();
			}
		} else if (devourPhase_ == 8) {
			std::ofstream report("tests/out/slime-life-devour.txt");
			report << (devourValid_ ? "PASS" : "FAIL") << devourNotes_ << " mass=" << body.mass
			       << " absorbed=" << life.absorbed << " left=" << clock.left << '\n';
			report.flush();
			PostQuitMessage(devourValid_ ? 0 : 2);
			return;
		}
		Game::GameScene::Update();
		++frames_;
		if (devourPhase_ == phaseAtStart) {
			devourFresh_ = false;
			devourClock_ += std::max(life.age - before, 1.f / 120);
		}
		if (frames_ > 20000) {
			std::ofstream report("tests/out/slime-life-devour.txt");
			report << "FAIL timeout phase=" << devourPhase_ << devourNotes_ << '\n';
			PostQuitMessage(2);
		}
	}
	// 物差しの UI のスライムの撮影: 本物の GPU 流体を、同じ瞬間に黄と青の背景で 2 回撮る。
	// 差から透明度を取り出す (Docs/UIDesign/chiritsumo/matte_slime.py)。idle は待機、roll は右へ転がる
	int spriteFrame_ = 0, spritePose_ = 0, spriteStep_ = 0;
	bool spriteHidden_ = false;
	uint32_t spriteCube_ = 0, spriteWhite_ = 0;
	Engine::Vector4 spriteBack_{.96f, .77f, 0, 1};
	static constexpr int SpriteIdle = 12, SpriteRoll = 10;
	void UpdateSpriteCapture() {
		using namespace Game;
		using namespace Game::Chrono;
		auto* render = Engine::Renderer::GetInstance();
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.pitch = .08f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		chrono->DevourClockState().left = 170;
		GetComponents().get<Player>(player).invincible = 60;
		if (!spriteHidden_) {
			spriteHidden_ = true;
			spriteCube_ = render->LoadObjMesh("Resources/Models/cube/cube.obj");
			spriteWhite_ = render->LoadTexture2D("Resources/Textures/white1x1.png");
			std::filesystem::create_directories("tests/out/sprite");
		}
		if (++spriteFrame_ % 30 == 1)
			for (auto [e, mr] : const_cast<entt::registry&>(GetRegistry()).view<MeshRendererComponent>().each())
				mr.enabled = false;
		// 周りの物に反応して口を開かないよう、飲める物を無くす
		for (auto& prop : life.props)
			prop.sealed = true;
		const bool rolling = spritePose_ >= SpriteIdle;
		if (spritePose_ >= SpriteIdle + SpriteRoll) {
			std::ofstream("tests/out/sprite/done.txt") << "PASS";
			PostQuitMessage(0);
			return;
		}
		// 待機の間は顔をカメラへ向ける。転がる間は右へ動かす
		if (!rolling)
			body.forward = body.liquidForward = {0, 0, -1};
		else
			input.move = {1, 0, 0};
		const int warm = spritePose_ == 0 ? 150 : spritePose_ == SpriteIdle ? 60 : rolling ? 4 : 6;
		if (spriteStep_ < warm) {
			render->SetRogueWorldFrozen(false);
			spriteBack_ = {.96f, .77f, 0, 1};
			Game::GameScene::Update();
			++spriteStep_;
			return;
		}
		render->SetRogueWorldFrozen(true);
		const int local = spriteStep_ - warm;
		++spriteStep_;
		wchar_t path[96];
		const wchar_t* kind = rolling ? L"roll" : L"idle";
		const int index = rolling ? spritePose_ - SpriteIdle : spritePose_;
		if (local == 5) {
			swprintf_s(path, L"tests/out/sprite/%s_%02d_a.png", kind, index);
			Capture(path);
			spriteBack_ = {.02f, .2f, .92f, 1};
		}
		if (local == 11) {
			swprintf_s(path, L"tests/out/sprite/%s_%02d_b.png", kind, index);
			Capture(path);
			++spritePose_;
			spriteStep_ = 0;
		}
	}
	// 終了の演出の撮影: 物を実際に飲ませて育ててから時間切れにして、巻尺で測るまでを連続で撮る
	float resultPlay_ = 0;
	bool hudShot_ = false;
	int resultShot_ = 0;
	std::chrono::steady_clock::time_point resultFinished_{};
	void UpdateResultCapture() {
		using namespace Game;
		using namespace Game::Chrono;
		auto player = FindObjectByName("Player");
		auto& input = GetComponents().get<ControlFrame>(player);
		input = {};
		input.pitch = .3f;
		auto* chrono = GetChronoSystem();
		auto& life = const_cast<SlimeLife&>(chrono->LifeState());
		auto& body = life.bodies[0];
		auto& clock = chrono->DevourClockState();
		GetComponents().get<Player>(player).invincible = 60;
		if (!chrono->Finished()) {
			const auto& stage = chrono->DevourStageInfo();
			if (frames_ == 60) {
				const wchar_t* goal = wcsstr(GetCommandLineW(), L"--result-goal=");
				const int index = goal ? _wtoi(goal + 14) : 0;
				const float size =
				    goal ? stage.goals[std::min<size_t>(size_t(std::max(index, 0)), stage.goals.size() - 1)] * 1.02f
				         : stage.goals.front() * .9f;
				if (index >= 0) {
					body.mass = body.health = body.maxHealth = DevourGoalMass(stage, size);
					++life.revision;
				}
			}
			if (frames_ > 60) {
				input.move = {0, 0, 1};
				input.yaw = resultPlay_ * .5f;
				resultPlay_ += 1.f / 60;
			}
			if (resultPlay_ > 6 && !hudShot_) {
				hudShot_ = true;
				Capture(L"tests/out/hud-play.png");
			}
			if (resultPlay_ > 9) {
				// --result-all-kinds: 終わる直前に、ステージの物を種類ごとに 1 つずつ飲みこんだことにする (升に全部の種類を並べる)
				if (clock.left > .01f && wcsstr(GetCommandLineW(), L"--result-all-kinds"))
					chrono->RecordAllKindsForTest(GetRegistry());
				clock.left = .01f;
			}
			if (frames_ > 40000) {
				PostQuitMessage(2);
				return;
			}
			resultFinished_ = std::chrono::steady_clock::now();
			Game::GameScene::Update();
			++frames_;
			return;
		}
		Game::GameScene::Update();
		++frames_;
		const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - resultFinished_).count();
		// 測るまでと、体の中へ入って飲みこんだ物を並べるまで。最後の 1 枚は升を 1 つ大きく見せる
		// (--result-all-kinds では升が多いので、全部が並び終わる 18 秒の 1 枚を足す)
		const bool allKinds = wcsstr(GetCommandLineW(), L"--result-all-kinds") != nullptr;
		const float at[] = {.12f,
		                    .4f,
		                    .75f,
		                    1.1f,
		                    1.5f,
		                    2.f,
		                    2.8f,
		                    3.3f,
		                    3.9f,
		                    4.6f,
		                    6.f,
		                    9.f,
		                    allKinds ? 18.f : 10.5f,
		                    allKinds ? 19.5f : 10.6f};
		const int shots = allKinds ? 14 : 13;
		if (resultShot_ == shots - 1 && age >= at[resultShot_] - .6f)
			chrono->OpenShelfDetailForTest(0);
		if (resultShot_ < shots && age >= at[resultShot_]) {
			wchar_t path[64];
			swprintf_s(path, L"tests/out/result-seq-%d.png", resultShot_++);
			Capture(path);
		}
		if (resultShot_ >= shots) {
			std::ofstream("tests/out/result-seq.txt") << "PASS absorbed=" << life.absorbed << " mass=" << body.mass
			                                          << " shelf=" << chrono->ShelfCellCountForTest();
			// 結果からタイトルへの切り替えも見るときは、外から画面を撮るので終わらない
			if (wcsstr(GetCommandLineW(), L"--slime-result-to-title"))
				Engine::SceneManager::GetInstance()->RequestCoveredChange("Title");
			else
				PostQuitMessage(0);
		}
	}
	void Capture(const wchar_t* path) {
		DirectX::ScratchImage image;
		auto result = DirectX::CaptureTexture(
		    window_->Queue(), window_->GetCurrentBackBufferResource(), false, image, D3D12_RESOURCE_STATE_PRESENT,
		    D3D12_RESOURCE_STATE_PRESENT);
		if (SUCCEEDED(result))
			result = DirectX::SaveToWICFile(
			    *image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path);
		trace_ << "capture=" << result << '\n';
	}

public:
	void Initialize(Engine::WindowDX* window, const Engine::SceneParameters&) override {
		window_ = window;
		Engine::SceneParameters params;
		params.stagePath = Game::Data::MainStagePath();
		Game::GameScene::Initialize(window, params);
		GetComponents().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));
		start_ = std::chrono::steady_clock::now();
		trace_.open("tests/out/slime-life-trace.txt");
		auto* renderer = Engine::Renderer::GetInstance();
		auto settings = renderer->GetGraphicsSettings();
		settings.rtShadows = settings.rtReflections = settings.rtIndirect = false;
		renderer->ApplyGraphicsSettings(settings);
	}
	void Update() override {
		// 撮影用の起動引数と、部屋のテスト。それ以外はステージ 2 (坂の町、吸収モード) のテスト
		if (wcsstr(GetCommandLineW(), L"--slime-sprite-capture")) {
			UpdateSpriteCapture();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-result-capture")) {
			UpdateResultCapture();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-room-camera")) {
			UpdateRoomCamera();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-slope-tour")) {
			UpdateSlopeTour();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-slope-perf")) {
			UpdateSlopePerf();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-slope-ride")) {
			UpdateSlopeRide();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-slope-festival")) {
			UpdateSlopeFestival();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-slope-wire-perf")) {
			UpdateWirePerf();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-slope-hose-perf")) {
			UpdateHosePerf();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-room-intro")) {
			UpdateRoomIntro();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-slope-intro")) {
			UpdateSlopeIntro();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-slope-rings")) {
			UpdateSlopeRings();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-room-mouth")) {
			UpdateRoomMouth();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-room-bed")) {
			UpdateRoomBed();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-room-surfaces")) {
			UpdateRoomSurfaces();
			return;
		}
		if (wcsstr(GetCommandLineW(), L"--slime-life-room")) {
			UpdateRoom();
			return;
		}
		UpdateDevour();
	}
	void Draw() override {
		// スライムの撮影: 背景の壁と床 (世界の物は隠してある)
		if (spriteCube_) {
			auto* render = Engine::Renderer::GetInstance();
			auto& body = GetChronoSystem()->LifeState().bodies[0];
			GetCamera().SetProjection(.34f, 1266.f / 729.f, .1f, 2000);
			Engine::Transform wall;
			wall.translate = {body.at.x, 100, body.at.z + 60};
			wall.scale = {600, 300, 1};
			render->DrawMesh(spriteCube_, spriteWhite_, wall, spriteBack_);
			Engine::Transform floor;
			floor.translate = {body.at.x, -20, body.at.z};
			floor.scale = {600, 20.02f, 600};
			render->DrawMesh(spriteCube_, spriteWhite_, floor, spriteBack_);
		}
		Game::GameScene::Draw();
	}
};
