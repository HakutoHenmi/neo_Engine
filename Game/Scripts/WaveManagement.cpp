#include "WaveManagement.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "EnemySpawnerScript.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include <cmath>
#include <iostream>

#include "PhaseSystemScript.h"

using json = nlohmann::json;

namespace Game {

int WaveManagement::currentWave_ = -1;

void WaveManagement::Start(entt::entity entity, GameScene* scene) {
	if (!scene) return;

	managerEntity_ = entity;
	currentWave_ = -1;
	previousWave_ = -2;

	// 名前に基づいてエンティティを解決
	enemySpawners_.clear();
	for (const auto& waveNames : enemySpawnerNames_) {
		std::vector<entt::entity> waveSpawners;
		for (const auto& name : waveNames) {
			entt::entity e = scene->FindObjectByName(name);
			if (scene->GetRegistry().valid(e)) {
				if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
					sc->enabled = false;
				}
				waveSpawners.push_back(e);
			}
		}
		enemySpawners_.push_back(waveSpawners);
	}
}

void WaveManagement::Update(entt::entity entity, GameScene* scene, float /*dt*/) {
	cachedScene_ = scene;
	managerEntity_ = entity;

	#if defined(USE_IMGUI) && !defined(NDEBUG) 
	auto* renderer = Engine::Renderer::GetInstance();
	bool shouldDrawPreview = false;
	if (scene) {
		if (!scene->IsPlaying()) {
			shouldDrawPreview = true;
		} else if (PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
			shouldDrawPreview = true;
		}
	}

	if (renderer && shouldDrawPreview) {
		for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
			for (entt::entity spawnerEntity : enemySpawners_[wi]) {
				if (scene->GetRegistry().valid(spawnerEntity)) {
					// スポナーの位置にプレビューを描画
					if (auto* tc = scene->GetRegistry().try_get<TransformComponent>(spawnerEntity)) {
						Engine::Matrix4x4 wm = scene->GetWorldMatrix(static_cast<int>(spawnerEntity));
						Engine::Vector3 p = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };

						float s = 0.5f;
						Engine::Vector4 c = { 1.0f, 0.5f, 0.0f, 1.0f };
						// 底面
						renderer->DrawLine3D({p.x - s, p.y - s, p.z - s}, {p.x + s, p.y - s, p.z - s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y - s, p.z - s}, {p.x + s, p.y - s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y - s, p.z + s}, {p.x - s, p.y - s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x - s, p.y - s, p.z + s}, {p.x - s, p.y - s, p.z - s}, c, true);
						// 上面
						renderer->DrawLine3D({p.x - s, p.y + s, p.z - s}, {p.x + s, p.y + s, p.z - s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y + s, p.z - s}, {p.x + s, p.y + s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y + s, p.z + s}, {p.x - s, p.y + s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x - s, p.y + s, p.z + s}, {p.x - s, p.y + s, p.z - s}, c, true);
						// 側面
						renderer->DrawLine3D({p.x - s, p.y - s, p.z - s}, {p.x - s, p.y + s, p.z - s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y - s, p.z - s}, {p.x + s, p.y + s, p.z - s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y - s, p.z + s}, {p.x + s, p.y + s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x - s, p.y - s, p.z + s}, {p.x - s, p.y + s, p.z + s}, c, true);

						// EnemySpawnerScript の詳細なプレビューも描画する
						if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(spawnerEntity)) {
							for (auto& entry : sc->scripts) {
								if (entry.scriptPath == "EnemySpawnerScript" && entry.instance) {
									static_cast<EnemySpawnerScript*>(entry.instance.get())->DrawSpawnPreview({p.x, p.y, p.z});
								}
							}
						}
					}
				}
			}
		}
	}
	#endif

	if (currentWave_ != previousWave_) {
		if (scene->IsPlaying() && currentWave_ == -1) {
			// 準備フェーズなどに移行した場合、残存している敵を全て消去する
			const auto& enemies = scene->GetEntitiesByTag(TagType::Enemy);
			std::vector<entt::entity> toDestroy(enemies.begin(), enemies.end());
			for (auto e : toDestroy) {
				if (scene->GetRegistry().valid(e)) {
					scene->DestroyObject(static_cast<uint32_t>(e));
				}
			}
		}

		SpawnSpanner(currentWave_, scene);
	}
	previousWave_ = currentWave_;
}

void WaveManagement::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void WaveManagement::SpawnSpanner(int currentWave, GameScene* scene) {
	if (!scene) return;

	// すべてのスポナーを無効化する
	for (auto& waveSpawners : enemySpawners_) {
		for (entt::entity e : waveSpawners) {
			if (scene->GetRegistry().valid(e)) {
				if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
					sc->enabled = false;
				}
			}
		}
	}

	if (currentWave < 0 || currentWave >= static_cast<int>(enemySpawners_.size())) return;

	// 現在のウェーブのスポナーだけを有効化する
	for (entt::entity e : enemySpawners_[currentWave]) {
		if (scene->GetRegistry().valid(e)) {
			if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
				sc->enabled = true;
				for (auto& entry : sc->scripts) {
					if (entry.instance) {
						entry.instance->Start(e, scene);
					}
				}
			}
		}
	}
}

#if defined(USE_IMGUI) && !defined(NDEBUG)
void WaveManagement::OnEditorUI() {
	if (!cachedScene_) {
		auto* sm = Engine::SceneManager::GetInstance();
		if (sm && sm->Current()) {
			cachedScene_ = dynamic_cast<GameScene*>(sm->Current());
		}
	}

	if (!cachedScene_) {
		ImGui::Text("シーンがキャッシュされていません。再生するかエディタで更新してください。");
		return;
	}

	// エディタ起動時やプレイ終了時など、エンティティのリストが未構築・無効な場合に名前から復元する
	if (enemySpawners_.size() != enemySpawnerNames_.size()) {
		enemySpawners_.resize(enemySpawnerNames_.size());
	}
	for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
		if (enemySpawners_[wi].size() != enemySpawnerNames_[wi].size()) {
			enemySpawners_[wi].resize(enemySpawnerNames_[wi].size(), static_cast<entt::entity>(entt::null));
		}
		for (size_t si = 0; si < enemySpawners_[wi].size(); ++si) {
			if (!cachedScene_->GetRegistry().valid(enemySpawners_[wi][si])) {
				enemySpawners_[wi][si] = cachedScene_->FindObjectByName(enemySpawnerNames_[wi][si]);
			}
		}
	}

	ImGui::SeparatorText("ウェーブ管理 (Wave Management)");

	if (ImGui::Button("ウェーブを追加 (Add Wave)")) {
		enemySpawners_.push_back({});
		enemySpawnerNames_.push_back({});
	}

	for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
		ImGui::PushID(static_cast<int>(wi));
		bool isNodeOpen = ImGui::TreeNodeEx(("Wave " + std::to_string(wi + 1)).c_str(), ImGuiTreeNodeFlags_DefaultOpen);

		if (isNodeOpen) {
			if (ImGui::Button("スポナーを追加 (Add Spawner)")) {
				std::string name = "Spawner_W" + std::to_string(wi + 1) + "_" + std::to_string(enemySpawners_[wi].size() + 1);
				entt::entity spawner = cachedScene_->CreateEntity(name);

				if (auto* tc = cachedScene_->GetRegistry().try_get<TransformComponent>(spawner)) {
					tc->translate = {0, 0, 0};
				}

				auto& sc = cachedScene_->GetRegistry().emplace<ScriptComponent>(spawner);
				sc.scripts.push_back({"EnemySpawnerScript", "", nullptr});
				sc.enabled = true;

				enemySpawners_[wi].push_back(spawner);
				enemySpawnerNames_[wi].push_back(name);
			}
			ImGui::SameLine();
			if (ImGui::Button("ウェーブを削除")) {
				// スポナーエンティティは破棄せずリストからのみ外す（必要ならDestroyObjectを呼ぶ）
				enemySpawners_.erase(enemySpawners_.begin() + wi);
				enemySpawnerNames_.erase(enemySpawnerNames_.begin() + wi);
				ImGui::TreePop();
				ImGui::PopID();
				--wi;
				continue;
			}

			// スポナーリストの描画
			for (size_t si = 0; si < enemySpawners_[wi].size(); ++si) {
				entt::entity spawner = enemySpawners_[wi][si];
				if (!cachedScene_->GetRegistry().valid(spawner)) continue;

				auto* nc = cachedScene_->GetRegistry().try_get<NameComponent>(spawner);
				std::string sname = nc ? nc->name : "Spawner";

				ImGui::PushID(static_cast<int>(si));
				bool isSpawnerNodeOpen = ImGui::TreeNode(sname.c_str());

				ImGui::SameLine();
				if (ImGui::Button("選択(Select)")) {
					cachedScene_->SetSelectedEntity(spawner);
					cachedScene_->GetSelectedEntities().clear();
					cachedScene_->GetSelectedEntities().insert(spawner);
				}

				if (isSpawnerNodeOpen) {
					// 座標設定
					if (auto* tc = cachedScene_->GetRegistry().try_get<TransformComponent>(spawner)) {
						ImGui::DragFloat3("Position", &tc->translate.x, 0.1f);
					}

					if (ImGui::Button("スポナーを削除")) {
						cachedScene_->DestroyObject(static_cast<uint32_t>(spawner));
						enemySpawners_[wi].erase(enemySpawners_[wi].begin() + si);
						enemySpawnerNames_[wi].erase(enemySpawnerNames_[wi].begin() + si);
						ImGui::TreePop();
						ImGui::PopID();
						--si;
						continue;
					}

					// EnemySpawnerScript の設定を呼び出す
					if (auto* sc = cachedScene_->GetRegistry().try_get<ScriptComponent>(spawner)) {
						for (auto& entry : sc->scripts) {
							if (entry.scriptPath == "EnemySpawnerScript") {
								if (!entry.instance) {
									entry.instance = ScriptEngine::GetInstance()->CreateScript(entry.scriptPath);
									if (entry.instance) {
										entry.instance->DeserializeParameters(entry.parameterData);
									}
								}
								if (entry.instance) {
									entry.instance->OnEditorUI();
									entry.parameterData = entry.instance->SerializeParameters();
								}
							}
						}
					}

					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}
#else
void WaveManagement::OnEditorUI() {}
#endif

std::string WaveManagement::SerializeParameters() {
	json j;

	// 最新のスポナー名リストをエディタUI上で構築・維持しているので、そのまま保存する
	// (ここでキャッシュされたシーンやエンティティを基に再構築すると、プレイ終了時に無効化されて消えてしまう問題を防止)
	j["spawners"] = enemySpawnerNames_;
	return j.dump();
}

void WaveManagement::DeserializeParameters(const std::string& data) {
	if (data.empty()) return;
	try {
		json j = json::parse(data);
		if (j.contains("spawners")) {
			enemySpawnerNames_ = j["spawners"].get<std::vector<std::vector<std::string>>>();
		}
	} catch (const std::exception& e) {
		std::cerr << "WaveManagement Deserialize Error: " << e.what() << "\n";
	}
}

REGISTER_SCRIPT(WaveManagement);

} // namespace Game