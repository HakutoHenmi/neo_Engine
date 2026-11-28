// GameSceneUpdate.cpp
// GameScene::Update の各手順: タグの同期、PLAY 中の入力 (ポーズ・設定画面)、アニメーション、
// 生成と破棄の反映、ライトとパーティクルエミッターの更新。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
#include "../../Engine/Core/Input.h"
#include "../../Engine/Core/JobSystem.h"
#include "../../Engine/Scene/SceneManager.h"
#include "../Chrono/WeaponCraftRules.h"
#include "../Systems/ChronoSystem.h"
#include "../UI/GameUI.h"
#include "../UI/GraphicsUI.h"
#include <algorithm>

namespace Game {

using namespace GameSceneConstants;

// タグのキャッシュを遅れて同期する (生成直後と破棄時に予約したもの)
void GameScene::FlushPendingTags() {
	// タグのキャッシュを遅れて同期する (生成直後と破棄時に予約したもの)
	// 予約があるときだけ処理する
	if (!pendingTagRemoved_.empty() || !pendingTagSync_.empty()) {
		// 1. 削除・変更予定のエンティティを全タグの一覧から取り除く
		std::vector<entt::entity> toRemove = std::move(pendingTagRemoved_);
		// 同期待ちのエンティティはタグが変わった可能性があるので、古い一覧から一旦消しておく
		for (auto e : pendingTagSync_) {
			toRemove.push_back(e);
		}

		// それぞれをすべてのタグの一覧から消す
		for (auto e : toRemove) {
			for (auto& pair : tagCache_) {
				auto& vec = pair.second;
				// すべてのタグの一覧からそのエンティティを消す
				vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
			}
		}

		// 2. 最新のタグで一覧に入れ直す
		std::vector<entt::entity> toSync = std::move(pendingTagSync_);
		// まだ存在するものだけ入れ直す
		for (auto e : toSync) {
			if (registry_.valid(e)) {
				SyncTag(e);
			}
		}
	}
}

// PLAY 中の入力: F7 でグラフィック設定、ポーズメニュー、Esc / START でポーズ。
// グラフィック設定やポーズメニューを処理したフレームは true を返す (Update はそこで終わる)
bool GameScene::HandlePlayInput(float dt, bool stageClear) {
	// PLAY 中の入力: F7 でグラフィック設定、ポーズメニュー、Esc / START でポーズ
	if (isPlaying_) {
		// F7: グラフィック設定画面を開閉する (開いている間はポーズ)
		if (!stageClear && UI::Pressed(DIK_F7)) {
			SetPaused(true);
			graphicsOpen_ = !graphicsOpen_;
			return true;
		}
		// グラフィック設定画面: 変更があれば設定ファイルへ保存する
		if (!stageClear && isPaused_ && graphicsOpen_) {
			// UI の描画先 (エディタの Game ビューならマウス座標を上書きする)
			UI::Canvas ui(renderer_, ctx_.viewportSize.x, ctx_.viewportSize.y);
			if (ctx_.useOverrideMouse)
				ui.SetPointer(ctx_.overrideMouseX, ctx_.overrideMouseY);
			// 変更前の設定
			auto previous = renderer_->GetGraphicsSettings();
			// 入力を処理し、戻る操作なら画面を閉じる
			if (UI::UpdateGraphics(ui, *renderer_))
				graphicsOpen_ = false;
			// 変更後の設定
			const auto& current = renderer_->GetGraphicsSettings();
			// 効果のどれかが変わったら保存し、失敗したら覚えておく
			if (previous.bloom != current.bloom || previous.lensFlare != current.lensFlare ||
			    previous.grading != current.grading || previous.ambientOcclusion != current.ambientOcclusion ||
			    previous.motionBlur != current.motionBlur || previous.dof != current.dof ||
			    previous.exposure != current.exposure || previous.dlssQuality != current.dlssQuality ||
			    previous.rtShadows != current.rtShadows || previous.rtReflections != current.rtReflections ||
			    previous.rtIndirect != current.rtIndirect)
				graphicsSaveFailed_ = !renderer_->SaveGraphicsSettings();
			return true;
		}
		// ポーズメニューのボタン入力 (グラフィック設定 / タイトルへ / 再開)
		if (!stageClear && isPaused_ && chronoSystem_ && chronoSystem_->DevourMode()) {
			// 吸収モードのポーズ: 上下で選び、Enter / クリック / A で決定する
			UI::Canvas ui(renderer_, ctx_.viewportSize.x, ctx_.viewportSize.y);
			if (ctx_.useOverrideMouse)
				ui.SetPointer(ctx_.overrideMouseX, ctx_.overrideMouseY);
			auto* input = Engine::Input::GetInstance();
			int chosen = UI::Scale::UpdatePause(ui, pauseMenu_);
			if (UI::Pressed(DIK_G) || UI::PadPressed(XINPUT_GAMEPAD_BACK)) {
				chosen = UI::Scale::kPauseGraphics;
			}
			if (input->Trigger(DIK_TAB)) {
				chosen = UI::Scale::kPauseTitle;
			}
			if (input->Trigger(DIK_ESCAPE) || input->PadTrigger(XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START)) {
				chosen = UI::Scale::kPauseResume;
			}
			if (chosen == UI::Scale::kPauseGraphics) {
				graphicsOpen_ = true;
				return true;
			}
			if (chosen == UI::Scale::kPauseTitle) {
				Engine::SceneManager::GetInstance()->RequestCoveredChange("Title");
				return true;
			}
			if (chosen == UI::Scale::kPauseResume) {
				UI::PlayUISound(UI::UISound::Confirm);
				SetPaused(false);
				input->LatchPad();
				return true;
			}
		} else if (!stageClear && isPaused_) {
			// UI の描画先 (エディタの Game ビューならマウス座標を上書きする)
			UI::Canvas ui(renderer_, ctx_.viewportSize.x, ctx_.viewportSize.y);
			if (ctx_.useOverrideMouse)
				ui.SetPointer(ctx_.overrideMouseX, ctx_.overrideMouseY);
			// グラフィック設定ボタン・G・パッドの BACK で設定画面を開く
			if (ui.Click(UI::GraphicsButton) || UI::Pressed(DIK_G) || UI::PadPressed(XINPUT_GAMEPAD_BACK)) {
				graphicsOpen_ = true;
				return true;
			}
			auto* input = Engine::Input::GetInstance();
			// タイトルへ戻るボタンか Tab でタイトルへ
			if (ui.Click(UI::Title) || input->Trigger(DIK_TAB)) {
				Engine::SceneManager::GetInstance()->RequestCoveredChange("Title");
				return true;
			}
			// 再開ボタン・Enter・パッドの B でポーズを解く (同じ押下を他で使わないよう止める)
			if (ui.Click(UI::Resume) || input->Trigger(DIK_RETURN) ||
			    input->PadTrigger(XINPUT_GAMEPAD_B)) {
				UI::PlayUISound(UI::UISound::Confirm);
				SetPaused(false);
				input->LatchPad();
			}
		}
		// Esc (0x01 = DIK_ESCAPE) / START でポーズを切り替える
		if (!stageClear && (Engine::Input::GetInstance()->Trigger(0x01) ||
		                    Engine::Input::GetInstance()->PadTrigger(XINPUT_GAMEPAD_START))) {
			SetPaused(!isPaused_);
			Engine::Input::GetInstance()->LatchPad();
		}

		// ステージクリア中はカーソルを出す。通常プレイ中はプレイ時間を計測する
		if (stageClear) {
			Engine::WindowDX::SetCursorVisible(true);
		} else if (!isPaused_) {
			playTime_ += dt;

			// PLAY 中はマウスカーソルを画面中央に固定する (メニューを開いていない場合)
			// (変形武器を作る画面が開いている間は固定しない。マウスで材料を選ぶ)
			if (!Chrono::CraftScreenOpen() && dx_ && dx_->GetHwnd()) {
				// 画面中央をスクリーン座標に直してカーソルを置く
				POINT center = {(LONG)Engine::WindowDX::kW / 2, (LONG)Engine::WindowDX::kH / 2};
				ClientToScreen(dx_->GetHwnd(), &center);
				SetCursorPos(center.x, center.y);
			}
		}
	}
	return false;
}

// アニメーション (エンジン側の処理なのでシーンに残している)
void GameScene::UpdateAnimations(float dt) {
	// アニメーション (エンジン側の処理なのでシーンに残している)
	// アニメーションの再生時刻を進める (エンティティごとに並列で処理する)
	auto animView = registry_.view<AnimatorComponent, MeshRendererComponent>();
	// PLAY 中でポーズしていなければ、対象のエンティティを集める
	if (isPlaying_ && !isPaused_) {
		std::vector<entt::entity> animEntities;
		animView.each([&](entt::entity entity, auto&, auto&) { animEntities.push_back(entity); });

		// 64 件ずつ別のスレッドで処理する
		if (!animEntities.empty()) {
			Engine::JobSystem::Dispatch(
			    static_cast<uint32_t>(animEntities.size()), kAnimationJobBatchSize, [&](uint32_t i) {
				    // このエンティティのアニメーションとモデル
				    auto entity = animEntities[i];
				    auto& anim = registry_.get<AnimatorComponent>(entity);
				    auto& meshWrapper = registry_.get<MeshRendererComponent>(entity);

				    // 有効で再生中のものだけ進める
				    if (anim.enabled && anim.isPlaying) {
					    const float animDt = dt;
					    // 1 秒 60 フレームとして、再生の速さを掛けて進める
					    anim.time += animDt * kAnimationFramesPerSecond * anim.speed;
					    // クロスフェード中は残り時間を減らし、直前のクリップも進める
					    if (anim.crossfadeTimer > 0.0f) {
						    anim.crossfadeTimer -= animDt;
						    if (anim.crossfadeTimer < 0.0f)
							    anim.crossfadeTimer = 0.0f;
						    anim.prevTime += animDt * kAnimationFramesPerSecond * anim.speed;
					    }

					    // クリップの長さを超えたら、ループならはじめから・ループでなければ最後で止める
					    auto* m = renderer_->GetModel(meshWrapper.modelHandle);
					    if (m) {
						    // モデルのアニメーションの一覧から、再生中のクリップを探す
						    const auto& data = m->GetData();
						    for (const auto& a : data.animations) {
							    if (a.name == anim.currentAnimation) {
								    if (anim.time > a.duration) {
									    if (anim.loop)
										    anim.time = std::fmod(anim.time, a.duration);
									    else {
										    anim.time = a.duration;
										    anim.isPlaying = false;
									    }
								    }
							    }
							    // クロスフェード中の直前のクリップも同様に処理する
							    if (anim.crossfadeTimer > 0.0f && a.name == anim.prevAnimation) {
								    if (anim.prevTime > a.duration) {
									    if (anim.prevLoop)
										    anim.prevTime = std::fmod(anim.prevTime, a.duration);
									    else
										    anim.prevTime = a.duration;
								    }
							    }
						    }
					    }
				    }
			    });
			// すべての処理が終わるまで待つ
			Engine::JobSystem::Wait();
		}
	}
}

// 予約された生成 (弾など) と破棄を反映する
void GameScene::FlushPendingSpawnsAndDestroys() {
	// 予約された生成 (弾など) と破棄を反映する
	// 生成・破棄の予約を取り出す (ロック中に取り出すだけにして、削除はロックの外で行う)
	std::vector<entt::entity> destroysToProcess;
	{
		// 生成・破棄の予約を守るロック
		std::lock_guard<std::mutex> lock(spawnMutex_);

		if (!pendingSpawns_.storage<entt::entity>().empty()) {
			// 生成は直接 registry_.create() で行うので、ここの予約は使っていない (空にするだけ)
			pendingSpawns_.clear();
		}

		if (!pendingDestroys_.empty()) {
			// 同じエンティティの重複を除いて二重に破棄しない
			// 並べ替えて重複を消す
			std::sort(pendingDestroys_.begin(), pendingDestroys_.end());
			pendingDestroys_.erase(
			    std::unique(pendingDestroys_.begin(), pendingDestroys_.end()), pendingDestroys_.end());

			// 処理する一覧へ移す
			destroysToProcess = std::move(pendingDestroys_);
			pendingDestroys_.clear();
		}
	}

	// 破棄はロックの外で行う (OnDestroy の中から DestroyObject が呼ばれてもデッドロックしないように)
	for (auto id : destroysToProcess) {
		// まだ存在するものだけ削除する
		if (registry_.valid(id)) {
			RemoveEntity(id);
		}
	}
}

// ライト (描画の設定なのでシーンに残している)
void GameScene::ApplyLights() {
	// ライト (描画の設定なのでシーンに残している)
	// ライトの反映: 平行光源1つ・点光源・スポットライトをレンダラーへ渡す
	if (renderer_) {
		// 使った点光源・スポットライトの数と、平行光源があったか
		int plCount = 0;
		int slCount = 0;
		bool hasDirLight = false;

		// 平行光源
		auto dirLightView = registry_.view<DirectionalLightComponent, TransformComponent>();
		// 平行光源は最初に見つかった1つだけ使う。向きは回転行列の Z 軸
		dirLightView.each([&](auto, const DirectionalLightComponent& dl, const TransformComponent& tc) {
			if (dl.enabled && !hasDirLight) {
				// 回転から向きを求め、色に強さを掛ける
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 color = {
				    dl.color.x * dl.intensity, dl.color.y * dl.intensity, dl.color.z * dl.intensity};
				renderer_->SetDirectionalLight(dir, color, true);
				hasDirLight = true;
			}
		});

		auto plView = registry_.view<PointLightComponent, TransformComponent>();
		// 点光源は上限数まで
		plView.each([&](auto, const PointLightComponent& pl, const TransformComponent& tc) {
			if (pl.enabled && plCount < Engine::Renderer::kMaxPointLights) {
				// 位置・色 (強さを掛ける)・減衰
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {
				    pl.color.x * pl.intensity, pl.color.y * pl.intensity, pl.color.z * pl.intensity};
				Engine::Vector3 atten = {pl.atten.x, pl.atten.y, pl.atten.z};
				renderer_->SetPointLight(plCount, pos, color, pl.range, atten, true);
				plCount++;
			}
		});

		// スポットライトも上限数まで (向きは回転行列の Z 軸)
		auto slView = registry_.view<SpotLightComponent, TransformComponent>();
		slView.each([&](auto, const SpotLightComponent& sl, const TransformComponent& tc) {
			if (sl.enabled && slCount < Engine::Renderer::kMaxSpotLights) {
				// 回転から向きを求める
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				// 位置・色 (強さを掛ける)・減衰
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {
				    sl.color.x * sl.intensity, sl.color.y * sl.intensity, sl.color.z * sl.intensity};
				Engine::Vector3 atten = {sl.atten.x, sl.atten.y, sl.atten.z};
				// 範囲と、内側・外側の角度 (cos) も渡す
				renderer_->SetSpotLight(slCount, pos, dir, color, sl.range, sl.innerCos, sl.outerCos, atten, true);
				slCount++;
			}
		});

		// 使わなかった枠はライトを無効にしておく
		if (!hasDirLight) {
			renderer_->SetDirectionalLight({0, -1, 0}, {0, 0, 0}, false);
		}
		for (int i = plCount; i < Engine::Renderer::kMaxPointLights; ++i) {
			renderer_->SetPointLight(i, {0, 0, 0}, {0, 0, 0}, 0, {1, 0, 0}, false);
		}
		for (int i = slCount; i < Engine::Renderer::kMaxSpotLights; ++i) {
			renderer_->SetSpotLight(i, {0, 0, 0}, {0, -1, 0}, {0, 0, 0}, 0, 0.0f, 0.0f, {1, 0, 0}, false);
		}
	}
}

// パーティクルエミッターの更新
void GameScene::UpdateParticleEmitters(float dt) {
	// パーティクルエミッターの更新
	auto peView = registry_.view<ParticleEmitterComponent, TransformComponent, NameComponent>();
	// 初回にエミッターを初期化し (保存された設定ファイルがあれば読み込む)、位置を合わせて更新する
	peView.each([&](auto, ParticleEmitterComponent& pe, const TransformComponent& tc, const NameComponent& nc) {
		// 無効なら何もしない
		if (!pe.enabled)
			return;

		// まだ初期化していなければ、名前を付けて作る
		if (!pe.isInitialized && renderer_) {
			pe.emitter.Initialize(*renderer_, nc.name + "_Emitter");
			// 設定ファイルがあれば読み込む
			if (!pe.assetPath.empty()) {
				pe.emitter.LoadFromJson(pe.assetPath);
			}
			pe.isInitialized = true;
		}

		// エミッターをエンティティの位置へ動かし、時間を進める
		pe.emitter.params.position = {tc.translate.x, tc.translate.y, tc.translate.z};
		pe.emitter.Update(dt);
	});
}

} // namespace Game
