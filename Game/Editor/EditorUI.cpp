// EditorUI.cpp
// ゲーム開発用エディタ (ImGui) の実装。
// 主な構成:
//   - シーンの JSON 読み書き (RestoreSceneFromJson / SerializeEntity)
//   - Undo/Redo・Console・コピー/貼り付け
//   - Unity 風の画面 (メインメニュー・ツールバー・ステータスバー・ドック配置)
//   - Scene / Game ビュー (エディタカメラ・ギズモ・クリック選択)
//   - Hierarchy・Inspector・Project・Animation・Lighting・Console の各ウィンドウ
//   - GameScene 以外のシーン (タイトルなど) を包む表示とシーンランチャー
#include "EditorUI.h"
// UTF-8 とワイド文字のパス変換、ImGui (内部 API はドック配置などに使う)
#include "../../Engine/PathUtils.h"
#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
// スクリプトの基底・開始と停止・Inspector での追加と削除
#include "../Scripts/IScript.h" // 循環参照を避けるため先に読み込む
#include "../Scripts/ScriptLifecycle.h"
#include "../Scripts/ScriptEdits.h"
// 値が無いかもしれないことを表す std::optional
#include <optional>
// エディタから参照するゲーム側のシーンとシステム
#include "../Scenes/GameScene.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/RiverSystem.h" 
#include "../Systems/PlayerActionSystem.h" 
#include "../Systems/EnemyAISystem.h"      
#include "../Systems/UISystem.h"    
#include "../Scripts/ScriptEngine.h" 
// MSVC の警告 4865 (列挙型の基になる型の変更) を抑止する
#ifdef _MSC_VER
#pragma warning(disable: 4865)
#endif

// 音・流体の水槽・シーン管理・入力・ウィンドウ (エンジン側)
#include "Audio.h"
#include <tuple>
#include "../../Engine/FluidTank.h"
#include "SceneManager.h"
#include "Input.h"
#include "WindowDX.h"
// 標準ライブラリ
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <string>
// ファイル選択ダイアログ (GetOpenFileName) と JSON ライブラリ
#include <Windows.h>
#include <commdlg.h>
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
// JSON を短く json と書く
using json = nlohmann::json;

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {
// std::filesystem を短く fs と書く
namespace fs = std::filesystem;

// 現在開いているシーンファイル (保存先の既定値)
// 空のときは本編のシーンファイル (game.json の mainStage) に保存する
std::string EditorUI::currentScenePath;

// 英字を小文字にする (大文字小文字を区別しない検索用)
static std::string ToLower(std::string s) {
	// 1 文字ずつ小文字にする
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// strcpy_s は入力がバッファより長いと invalid parameter で即終了するため、切り詰めてコピーする
template <size_t N>
static void CopyToBuffer(char (&buffer)[N], const std::string& text) {
	// 長すぎれば切り詰めてコピーする
	strncpy_s(buffer, N, text.c_str(), _TRUNCATE);
}

// Inspector の各コンポーネント欄を ID で分離する。"Speed" "Loop" "Color" などの同名ウィジェットが
// 別コンポーネントにもあり、ID が衝突して片方しか操作できなかった
struct ImGuiIdScope {
	// 作ったときに ID を積み、消えるときに外す
	explicit ImGuiIdScope(const char* id) { ImGui::PushID(id); }
	~ImGuiIdScope() { ImGui::PopID(); }
	// コピーはできない
	ImGuiIdScope(const ImGuiIdScope&) = delete;
	ImGuiIdScope& operator=(const ImGuiIdScope&) = delete;
};

// JSON の数値配列を float 配列へ読み込む。要素不足・型違いは読み飛ばして既定値を残す
// (const json の operator[] は範囲外アクセスが未定義動作になるため、必ずここを経由する)
static bool ReadFloats(const json& o, const char* key, float* out, size_t count) {
	// キーを探し、配列で要素が足りているか調べる
	auto it = o.find(key);
	if (it == o.end() || !it->is_array() || it->size() < count) return false;
	// すべて数値か調べる
	for (size_t i = 0; i < count; ++i) {
		if (!(*it)[i].is_number()) return false;
	}
	// 数値なら書き込む
	for (size_t i = 0; i < count; ++i) out[i] = (*it)[i].get<float>();
	return true;
}

// 保存された id を読む。数値以外 (壊れたファイル) は null 扱い
static uint32_t ReadEntityId(const json& o, const char* key) {
	// キーを探し、符号なしの数値でなければ null
	auto it = o.find(key);
	if (it == o.end() || !it->is_number_unsigned()) return static_cast<uint32_t>(entt::null);
	return it->get<uint32_t>();
}

// 保存された名前を読む (無ければ "Object")
static std::string ReadName(const json& o) {
	// キーを探し、文字列ならその値
	auto it = o.find("name");
	return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string("Object");
}

// コンポーネントの復元 (定義は後ろ)
static void RestoreComponents(GameScene* scene, const json& obj, entt::entity entity);

// シーンの復元 (ID を対応付けて親子関係を解決する)
// 1オブジェクトの破損でシーン全体の読み込みが止まらないよう、オブジェクト単位で例外を捕捉する
static std::vector<entt::entity> RestoreSceneFromJson(GameScene* scene, const json& j, bool append) {
	// シーンか JSON が無ければ何もしない
	if (!scene || !j.is_object()) return {};
	// このシーンの ECS
	auto& reg = scene->GetComponents();
	// 追加でなければ、今のシーンを空にしてから読み込む
	if (!append) {
		OutputDebugStringA("[EditorUI] RestoreSceneFromJson: Clearing registry (FULL RELOAD)\n");
		scene->ClearScene(); // reg.clear() ではなく ClearScene を使い、予約や各システムの状態もまとめて消す
		scene->SelectEntity(entt::null);
	}

	// 描画設定 (ポストエフェクトの有無・環境光) の復元
	if (j.contains("settings") && j["settings"].is_object()) {
		// ポストエフェクトの有無と環境光を描画の設定へ反映する
		auto& s = j["settings"];
		auto* r = Engine::Renderer::GetInstance();
		if (s.contains("postProcessEnabled") && s["postProcessEnabled"].is_boolean()) r->SetPostProcessEnabled(s["postProcessEnabled"].get<bool>());
		Engine::Vector3 ambient{};
		if (ReadFloats(s, "ambientColor", &ambient.x, 3)) r->SetAmbientColor(ambient);
	}

	// objects の配列が無ければ何もしない
	if (!j.contains("objects") || !j["objects"].is_array()) return {};
	const json& objects = j["objects"];

	// 保存時の ID → 新しいエンティティ / 配列の順のエンティティ
	std::map<uint32_t, entt::entity> idMap;
	std::vector<entt::entity> entitiesInJson;

	// 1回目: エンティティを作り、保存時の ID と新しいエンティティを対応付ける
	for (const auto& obj : objects) {
		// オブジェクトでない要素は null を入れて順番を保つ
		if (!obj.is_object()) {
			entitiesInJson.push_back(entt::null);
			continue;
		}
		// 名前を付けて作る
		entt::entity newEntity = scene->CreateEntity(ReadName(obj));
		entitiesInJson.push_back(newEntity);
		// 保存時の ID があれば対応付ける
		uint32_t savedId = ReadEntityId(obj, "id");
		if (savedId != (uint32_t)entt::null) idMap[savedId] = newEntity;
	}

	// 2回目: コンポーネントの復元と親子関係の接続
	for (size_t i = 0; i < objects.size(); ++i) {
		// この要素と、1 回目で作ったエンティティ
		const auto& obj = objects[i];
		entt::entity entity = entitiesInJson[i];
		if (entity == entt::null) continue;

		// 親子関係のコンポーネント
		auto& hc = reg.get_or_emplace<HierarchyComponent>(entity);
		uint32_t savedParentId = ReadEntityId(obj, "parentId");
		// EnTT ではエンティティ 0 も正しい親になれる (単独の水槽など)。
		// 存在しない古い ID と自分自身を親にする指定は外し、正しい 0 は残す
		auto parentIt = idMap.find(savedParentId);
		hc.parentId = (parentIt != idMap.end() && parentIt->second != entity) ? parentIt->second : entt::null;

		// コンポーネントを復元する (失敗したら警告だけ出して続ける)
		try {
			RestoreComponents(scene, obj, entity);
		} catch (const std::exception& e) {
			EditorUI::LogWarning("Object '" + ReadName(obj) + "' was partially restored: " + e.what());
		}
	}
	return entitiesInJson;
}

// 1つのエンティティの名前・トランスフォーム・ロック状態と各コンポーネントを JSON から復元する
static void RestoreComponents(GameScene* scene, const json& obj, entt::entity entity) {
	// このシーンの ECS
	auto& reg = scene->GetComponents();
	{
		// 名前と、位置・回転・大きさ
		(void)reg.get_or_emplace<NameComponent>(entity, ReadName(obj));
		auto& tc = reg.get_or_emplace<TransformComponent>(entity);
		ReadFloats(obj, "translate", &tc.translate.x, 3);
		ReadFloats(obj, "rotate", &tc.rotate.x, 3);
		ReadFloats(obj, "scale", &tc.scale.x, 3);

		// Hierarchy でのロック
		reg.get_or_emplace<EditorStateComponent>(entity).locked = obj.value("locked", false);

		// "components" の各要素を type ごとに対応するコンポーネントへ読み込む。壊れた要素は読み飛ばす
		if (obj.contains("components") && obj["components"].is_array()) {
			for (auto& comp : obj["components"]) {
				if (!comp.is_object()) continue;
				// コンポーネントの種類
				std::string type = comp.value("type", "");
				try {
				// enabled は全コンポーネント共通
				bool en = comp.value("enabled", true);

				// メッシュ描画: モデル・テクスチャ・シェーダー・色・UV・反射。読み込んだパスからハンドルを作り直す
				if (type == "MeshRenderer") {
					// 見た目のコンポーネント
					auto& c = reg.get_or_emplace<MeshRendererComponent>(entity);
					c.enabled = en;
					// モデル・テクスチャ・シェーダー (コンポーネントに無ければ、旧形式のオブジェクト直下の値を使う)
					c.modelPath = comp.value("modelPath", "");
					if (c.modelPath.empty()) c.modelPath = obj.value("modelPath", "");
					c.texturePath = comp.value("texturePath", "");
					if (c.texturePath.empty()) c.texturePath = obj.value("texturePath", "");
					c.shaderName = comp.value("shaderName", "");
					if (c.shaderName.empty()) c.shaderName = obj.value("shaderName", "Default");

					// 色・UV の繰り返しとずらし
					ReadFloats(comp, "color", &c.color.x, 4);
					ReadFloats(comp, "uvTiling", &c.uvTiling.x, 2);
					ReadFloats(comp, "uvOffset", &c.uvOffset.x, 2);
					// 反射の強さと、空を映すか
					if (comp.contains("reflectivity")) c.reflectivity = comp["reflectivity"].get<float>();
					if (comp.contains("useCubemap")) c.useCubemap = comp["useCubemap"].get<bool>();
					// パスからモデルとテクスチャを読み込む
					if (!c.modelPath.empty()) c.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(c.modelPath);
					if (!c.texturePath.empty()) c.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(c.texturePath);
				// 箱の当たり判定
				} else if (type == "BoxCollider") {
					auto& c = reg.get_or_emplace<BoxColliderComponent>(entity);
					c.enabled = en;
					// 中心・大きさ・すり抜けるか
					ReadFloats(comp, "center", &c.center.x, 3);
					ReadFloats(comp, "size", &c.size.x, 3);
					c.isTrigger = comp.value("isTrigger", false);
				// 物理挙動
				} else if (type == "Rigidbody") {
					auto& c = reg.get_or_emplace<RigidbodyComponent>(entity);
					c.enabled = en;
					// 速度・重力・自分で動かすか
					ReadFloats(comp, "velocity", &c.velocity.x, 3);
					c.useGravity = comp.value("useGravity", true); c.isKinematic = comp.value("isKinematic", false);
				// スクリプト: クラス名から実体を作り、保存されたパラメータを渡す
				} else if (type == "Script") {
					auto& c = reg.get_or_emplace<ScriptComponent>(entity);
					c.enabled = en;
					// 新しい形式: スクリプトの一覧
					if (comp.contains("scripts") && comp["scripts"].is_array()) {
						for (auto& s : comp["scripts"]) {
							// クラス名と保存されたパラメータ
							std::string scriptPath = s.value("path", "");
							std::string paramData = s.value("param", "");
							
							// 欄を作り、クラス名から実体を作る
							ScriptEntry entry;
							entry.scriptPath = scriptPath;
							entry.parameterData = paramData;
							entry.instance = ScriptEngine::GetInstance()->CreateScript(scriptPath);
							entry.isStarted = false;
							
							// 実体があればパラメータを読み込む (ログにも残す)
							if (entry.instance && !entry.parameterData.empty()) {
								char logBuf2[2048];
								sprintf_s(logBuf2, "[EditorUI] Immediate Restore: Applying params to %s: %s\n", scriptPath.c_str(), paramData.c_str());
								OutputDebugStringA(logBuf2);
								entry.instance->DeserializeParameters(entry.parameterData);
							}
							
							// 一覧に追加する
							c.scripts.push_back(entry);
						}
					} else {
						// 旧形式 (スクリプト1つだけの書式) との互換
						std::string path = comp.value("scriptPath", "");
						if (!path.empty()) {
							std::string paramData = comp.value("parameterData", "");
							ScriptEntry entry;
							entry.scriptPath = path;
							entry.parameterData = paramData;
							entry.instance = ScriptEngine::GetInstance()->CreateScript(path);
							entry.isStarted = false;
							
							if (entry.instance && !entry.parameterData.empty()) {
								entry.instance->DeserializeParameters(entry.parameterData);
							}
							
							c.scripts.push_back(entry);
						}
					}
				// 汎用の変数 (数値・文字列)
				} else if (type == "Variable") {
					auto& c = reg.get_or_emplace<VariableComponent>(entity);
					c.enabled = en;
					// 数値と文字列の一覧
					if (comp.contains("values")) for (auto& [k, v] : comp["values"].items()) c.values[k] = v;
					if (comp.contains("strings")) for (auto& [k, v] : comp["strings"].items()) c.strings[k] = v;
				// ライト (平行光源・点光源・スポットライト)
				} else if (type == "DirectionalLight") {
					auto& c = reg.get_or_emplace<DirectionalLightComponent>(entity);
					// 色と強さ
					c.enabled = en; ReadFloats(comp, "color", &c.color.x, 3);
					c.intensity = comp.value("intensity", 1.0f);
				// 点光源: 色・強さ・届く範囲
				} else if (type == "PointLight") {
					auto& c = reg.get_or_emplace<PointLightComponent>(entity);
					c.enabled = en; ReadFloats(comp, "color", &c.color.x, 3);
					c.intensity = comp.value("intensity", 1.0f); c.range = comp.value("range", 10.0f);
				// スポットライト: 色・強さ・届く範囲
				} else if (type == "SpotLight") {
					auto& c = reg.get_or_emplace<SpotLightComponent>(entity);
					c.enabled = en; ReadFloats(comp, "color", &c.color.x, 3);
					c.intensity = comp.value("intensity", 1.0f); c.range = comp.value("range", 10.0f);
				// 体力
				} else if (type == "Health") {
					auto& c = reg.get_or_emplace<HealthComponent>(entity);
					// 体力・最大体力・死んでいるか
					c.enabled = en; c.SetHp(comp.value("hp", 100.0f)); c.SetMaxHp(comp.value("maxHp", 100.0f)); c.SetDead(comp.value("isDead", false));
				// タグ
				} else if (type == "Tag") {
					auto& c = reg.get_or_emplace<TagComponent>(entity);
					// タグの文字列を列挙値にする
					c.enabled = en; c.tag = StringToTag(comp.value("tag", "Untagged"));
				// 音の発信源
				} else if (type == "AudioSource") {
					auto& c = reg.get_or_emplace<AudioSourceComponent>(entity);
					// 音のファイル・音量・ループ・開始時に鳴らすか
					c.enabled = en; c.soundPath = comp.value("soundPath", ""); c.volume = comp.value("volume", 1.0f);
					c.loop = comp.value("loop", false); c.playOnStart = comp.value("playOnStart", false);
				// 旧アクションゲーム用: 入力・移動・カメラ追従
				} else if (type == "PlayerInput") {
					reg.get_or_emplace<PlayerInputComponent>(entity).enabled = en;
				// 移動の速さ・ジャンプ力・重力など
				} else if (type == "CharacterMovement") {
					auto& c = reg.get_or_emplace<CharacterMovementComponent>(entity);
					c.enabled = en; c.speed = comp.value("speed", 5.0f); c.jumpPower = comp.value("jumpPower", 6.0f); c.gravity = comp.value("gravity", 9.8f); c.heightOffset = comp.value("heightOffset", 1.0f);
				// カメラ追従の距離・高さ・なめらかさ
				} else if (type == "CameraTarget") {
					auto& c = reg.get_or_emplace<CameraTargetComponent>(entity);
					c.enabled = en; c.distance = comp.value("distance", 10.0f); c.height = comp.value("height", 3.0f); c.smoothSpeed = comp.value("smoothSpeed", 5.0f);
				// 川 (制御点の一覧)
				} else if (type == "River") {
					auto& c = reg.get_or_emplace<RiverComponent>(entity);
					// 幅・流れの速さ・UV の倍率・テクスチャ
					c.enabled = en; c.width = comp.value("width", 5.0f); c.flowSpeed = comp.value("flowSpeed", 1.0f);
					c.uvScale = comp.value("uvScale", 1.0f);
					c.texturePath = comp.value("texturePath", "Resources/Water/water.png");
					// 制御点を読み込み直す (3 要素の配列だけ)
					if (comp.contains("points") && comp["points"].is_array()) {
						c.points.clear();
						for (auto& pt : comp["points"]) {
							if (pt.is_array() && pt.size() >= 3)
								c.points.push_back({pt[0], pt[1], pt[2]});
						}
					}
				// 頭上の HP バー
				} else if (type == "WorldSpaceUI") {
					auto& c = reg.get_or_emplace<WorldSpaceUIComponent>(entity);
					// HP バーとダメージの数字を出すか
					c.enabled = en; c.showHealthBar = comp.value("showHealthBar", true);
					c.showDamageNumbers = comp.value("showDamageNumbers", true);
					// 頭上のずれと、バーの大きさ
					ReadFloats(comp, "offset", &c.offset.x, 3);
					c.barWidth = comp.value("barWidth", 60.0f);
					c.barHeight = comp.value("barHeight", 6.0f);
				// GPU メッシュコライダー (パスが無ければ描画用モデルのパスを使う)
				} else if (type == "GpuMeshCollider") {
					auto& c = reg.get_or_emplace<GpuMeshColliderComponent>(entity);
					c.enabled = en;
					// 当たり判定に使うモデルのパス
					c.meshPath = comp.value("meshPath", "");
					if (c.meshPath.empty()) c.meshPath = obj.value("modelPath", "");
					// すり抜けるか / パスからモデルを読み込む
					c.isTrigger = comp.value("isTrigger", false);
					if (!c.meshPath.empty()) c.meshHandle = Engine::Renderer::GetInstance()->LoadObjMesh(c.meshPath);
				// アニメーター
				} else if (type == "Animator") {
					auto& c = reg.get_or_emplace<AnimatorComponent>(entity);
					// 再生中のクリップ・速さ・再生中か・ループ
					c.enabled = en; c.currentAnimation = comp.value("currentAnimation", ""); c.speed = comp.value("speed", 1.0f);
					c.isPlaying = comp.value("isPlaying", false); c.loop = comp.value("loop", true);
				// 流体エミッター
				} else if (type == "FluidEmitter") {
					auto& c = reg.get_or_emplace<FluidEmitterComponent>(entity);
					c.enabled = en;
					// 1 フレームの数・水を残すか・合計の上限
					c.emitCountPerFrame = comp.value("emitCountPerFrame", 80);
					c.retainWater = comp.value("retainWater", false);
					c.particleLimit = (std::max)(0, comp.value("particleLimit", 0));
					// 色・初速・流体の種類
					ReadFloats(comp, "color", &c.color.x, 4);
					ReadFloats(comp, "velocity", &c.velocity.x, 3);
					c.fluidType = comp.value("fluidType", 1.0f);
				// パーティクルエミッター: 発生・寿命・速度・大きさ・色・回転・見た目の設定
				} else if (type == "ParticleEmitter") {
					auto& c = reg.get_or_emplace<ParticleEmitterComponent>(entity);
					// 設定ファイルのパス
					c.enabled = en; c.assetPath = comp.value("assetPath", "");
					
					// 発生の数・形・寿命
					auto& p = c.emitter.params;
					if (comp.contains("emitRate")) p.emitRate = comp["emitRate"].get<float>();
					if (comp.contains("burstCount")) p.burstCount = comp["burstCount"].get<int>();
					if (comp.contains("shape")) p.shape = static_cast<Engine::EmissionShape>(comp["shape"].get<int>());
					if (comp.contains("shapeRadius")) p.shapeRadius = comp["shapeRadius"].get<float>();
					if (comp.contains("shapeAngle")) p.shapeAngle = comp["shapeAngle"].get<float>();
					if (comp.contains("lifeTime")) p.lifeTime = comp["lifeTime"].get<float>();
					if (comp.contains("lifeTimeVariance")) p.lifeTimeVariance = comp["lifeTimeVariance"].get<float>();
					
					// 速度・ばらつき・加速度・減衰
					ReadFloats(comp, "startVelocity", &p.startVelocity.x, 3);
					ReadFloats(comp, "velocityVariance", &p.velocityVariance.x, 3);
					ReadFloats(comp, "acceleration", &p.acceleration.x, 3);
					if (comp.contains("damping")) p.damping = comp["damping"].get<float>();
					
					// 大きさ (始まりと終わり、ばらつき)
					ReadFloats(comp, "startSize", &p.startSize.x, 3);
					ReadFloats(comp, "startSizeVariance", &p.startSizeVariance.x, 3);
					ReadFloats(comp, "endSize", &p.endSize.x, 3);
					ReadFloats(comp, "endSizeVariance", &p.endSizeVariance.x, 3);
						
					// 色 (始まりと終わり)
					ReadFloats(comp, "startColor", &p.startColor.x, 4);
					ReadFloats(comp, "endColor", &p.endColor.x, 4);
						
					// 回転の速さとばらつき
					ReadFloats(comp, "angularVelocity", &p.angularVelocity.x, 3);
					ReadFloats(comp, "angularVelocityVariance", &p.angularVelocityVariance.x, 3);
						
					// テクスチャ・シェーダー・ビルボード・加算合成・UV アニメーション
					if (comp.contains("texturePath")) p.texturePath = comp["texturePath"].get<std::string>();
					if (comp.contains("shaderName")) p.shaderName = comp["shaderName"].get<std::string>();
					if (comp.contains("useBillboard")) p.useBillboard = comp["useBillboard"].get<bool>();
					if (comp.contains("isAdditive")) p.isAdditive = comp["isAdditive"].get<bool>();
					if (comp.contains("useUvAnim")) p.useUvAnim = comp["useUvAnim"].get<bool>();
					if (comp.contains("uvAnimCols")) p.uvAnimCols = comp["uvAnimCols"].get<int>();
					if (comp.contains("uvAnimRows")) p.uvAnimRows = comp["uvAnimRows"].get<int>();
					if (comp.contains("uvAnimFps")) p.uvAnimFps = comp["uvAnimFps"].get<float>();
					
					c.isInitialized = false; // 読み込んだ設定で必ず初期化し直す
				// UI: 配置・画像・文字・ボタン
				} else if (type == "RectTransform") {
					auto& c = reg.get_or_emplace<RectTransformComponent>(entity);
					// 位置・大きさ・基準点・回転の中心・回転
					c.enabled = en; ReadFloats(comp, "pos", &c.pos.x, 2);
					ReadFloats(comp, "size", &c.size.x, 2);
					ReadFloats(comp, "anchor", &c.anchor.x, 2);
					ReadFloats(comp, "pivot", &c.pivot.x, 2);
					c.rotation = comp.value("rotation", 0.0f);
				// 画像: テクスチャ・色・重なり順・9 分割の境界
				} else if (type == "UIImage") {
					auto& c = reg.get_or_emplace<UIImageComponent>(entity);
					c.enabled = en; c.texturePath = comp.value("texturePath", "");
					ReadFloats(comp, "color", &c.color.x, 4);
					c.layer = comp.value("layer", 0);
					c.is9Slice = comp.value("is9Slice", false);
					c.borderTop = comp.value("borderTop", 10.0f);
					c.borderBottom = comp.value("borderBottom", 10.0f);
					c.borderLeft = comp.value("borderLeft", 10.0f);
					c.borderRight = comp.value("borderRight", 10.0f);
					// UI のテクスチャは色が変わらないよう sRGB 変換をせずに読み込む
					if (!c.texturePath.empty()) c.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(c.texturePath, false);
				// 文字: 内容・大きさ・フォント・色
				} else if (type == "UIText") {
					auto& c = reg.get_or_emplace<UITextComponent>(entity);
					c.enabled = en; 
					c.text = comp.value("text", ""); 
					c.fontSize = comp.value("fontSize", 24.0f);
					c.fontPath = comp.value("fontPath", "C:\\Windows\\Fonts\\msgothic.ttc");
					ReadFloats(comp, "color", &c.color.x, 4);
				// ボタン: 通常・乗ったとき・押したときの色、押したときの処理名、当たり範囲
				} else if (type == "UIButton") {
					auto& c = reg.get_or_emplace<UIButtonComponent>(entity);
					c.enabled = en;
					ReadFloats(comp, "normalColor", &c.normalColor.x, 4);
					ReadFloats(comp, "hoverColor", &c.hoverColor.x, 4);
					ReadFloats(comp, "pressedColor", &c.pressedColor.x, 4);
					c.onClickCallback = comp.value("onClickCallback", "");
					ReadFloats(comp, "hitboxOffset", &c.hitboxOffset.x, 2);
					ReadFloats(comp, "hitboxScale", &c.hitboxScale.x, 2);
				// 音を聞く位置
				} else if (type == "AudioListener") {
					reg.get_or_emplace<AudioListenerComponent>(entity).enabled = en;
				// 旧アクションゲーム用: 攻撃判定・食らい判定・プレイヤーの行動・敵の AI
				} else if (type == "Hitbox") {
					auto& c = reg.get_or_emplace<HitboxComponent>(entity);
					c.enabled = en;
					// 中心・大きさ・ダメージ・有効か・タグ
					ReadFloats(comp, "center", &c.center.x, 3);
					ReadFloats(comp, "size", &c.size.x, 3);
					c.damage = comp.value("damage", 10.0f);
					c.isActive = comp.value("isActive", false);
					if (comp.contains("tag")) c.tag = StringToTag(comp.value("tag", "Default"));
				// 食らい判定: 中心・大きさ・ダメージ倍率・タグ
				} else if (type == "Hurtbox") {
					auto& c = reg.get_or_emplace<HurtboxComponent>(entity);
					c.enabled = en;
					ReadFloats(comp, "center", &c.center.x, 3);
					ReadFloats(comp, "size", &c.size.x, 3);
					c.damageMultiplier = comp.value("damageMultiplier", 1.0f);
					if (comp.contains("tag")) c.tag = StringToTag(comp.value("tag", "Default"));
				// プレイヤーの行動: 回避の時間と速さ
				} else if (type == "PlayerAction") {
					auto& c = reg.get_or_emplace<PlayerActionComponent>(entity);
					c.enabled = en;
					c.dodgeDuration = comp.value("dodgeDuration", 0.4f);
					c.dodgeSpeed = comp.value("dodgeSpeed", 15.0f);
				// 敵の AI: 攻撃の間隔・溜め・攻撃・待ち・怯みの時間
				} else if (type == "EnemyAI") {
					auto& c = reg.get_or_emplace<EnemyAIComponent>(entity);
					c.enabled = en;
					c.attackInterval = comp.value("attackInterval", 3.0f);
					c.windUpDuration = comp.value("windUpDuration", 0.6f);
					c.attackDuration = comp.value("attackDuration", 1.0f);
					c.cooldownDuration = comp.value("cooldownDuration", 1.0f);
					c.stunDuration = comp.value("stunDuration", 1.5f);
					// 追跡の速さと範囲、攻撃の範囲
					c.chaseSpeed = comp.value("chaseSpeed", 4.0f);
					c.chaseRange = comp.value("chaseRange", 15.0f);
					c.attackRange = comp.value("attackRange", 15.0f);
					// 跳ねる敵の設定と、気付く範囲
					c.hopper = comp.value("hopper", false);
					c.hopInterval = comp.value("hopInterval", 0.72f);
					c.hopHeight = comp.value("hopHeight", 1.65f);
					c.aggroRange = comp.value("aggroRange", 28.0f);
				// モーション (キーフレームアニメーション): クリップごとのキーフレーム
				} else if (type == "Motion") {
					auto& c = reg.get_or_emplace<MotionComponent>(entity);
					c.enabled = en;
					if (comp.contains("clips") && comp["clips"].is_object()) {
						// クリップを読み込み直す
						c.clips.clear();
						for (auto& [name, clipJ] : comp["clips"].items()) {
							// クリップの名前・長さ・ループ
							MotionComponent::MotionClip clip;
							clip.name = name;
							clip.totalDuration = clipJ.value("totalDuration", 1.0f);
							clip.loop = clipJ.value("loop", false);
							if (clipJ.contains("keyframes") && clipJ["keyframes"].is_array()) {
								// キーフレームの時刻・位置・回転・大きさ
								for (auto& kf : clipJ["keyframes"]) {
									MotionComponent::Keyframe k;
									k.time = kf.value("time", 0.0f);
									ReadFloats(kf, "translate", &k.translate.x, 3);
									ReadFloats(kf, "rotate", &k.rotate.x, 3);
									ReadFloats(kf, "scale", &k.scale.x, 3);
									clip.keyframes.push_back(k);
								}
							}
							// 名前で登録する
							c.clips[name] = clip;
						}
					} else if (comp.contains("keyframes")) {
						// 旧形式 (クリップ1つだけの書式) との互換
						MotionComponent::MotionClip clip;
						// Default という名前のクリップにする
						clip.name = "Default";
						clip.totalDuration = comp.value("totalDuration", 1.0f);
						clip.loop = comp.value("loop", true);
						// キーフレームを読み込む
						for (auto& kf : comp["keyframes"]) {
							MotionComponent::Keyframe k;
							k.time = kf.value("time", 0.0f);
							ReadFloats(kf, "translate", &k.translate.x, 3);
							ReadFloats(kf, "rotate", &k.rotate.x, 3);
							ReadFloats(kf, "scale", &k.scale.x, 3);
							clip.keyframes.push_back(k);
						}
						c.clips["Default"] = clip;
					}
					// 再生するクリップ
					c.activeClip = comp.value("activeClip", "Default");
				}
				// この要素だけ読み飛ばし、警告を出して次へ進む
				} catch (const std::exception& e) {
					EditorUI::LogWarning("Skipped broken component '" + type + "' on '" + ReadName(obj) + "': " + e.what());
				}
			}
		}
	}
}

// エディタ全体の静的な状態
// Undo / Redo の履歴 (最大 100 件)
static std::deque<UndoCommand> undoStack;
static std::deque<UndoCommand> redoStack;
static constexpr size_t kMaxUndoDepth = 100;
// Game ビューの縦横比の選択肢 (0 は自由、-1 は自動)
static int currentAspect = 0;
static const float aspectValues[] = { 0.0f, 16.0f/9.0f, 4.0f/3.0f, 1.0f/1.0f, -1.0f };
static const char* aspects[] = { "Free", "16:9", "4:3", "1:1", "Auto" };

// 変形ツールの状態と Console のログ (最大 500 行)、エディタの経過時間
EditorUI::GizmoState EditorUI::gizmo_;
static std::deque<LogEntry> consoleLog;
static constexpr size_t kMaxConsoleLines = 500;
static float globalTime = 0.0f;


// 変形ツールのドラッグ開始時の値 (Undo 用)
static ImVec2 gizmoDragStartMouse = {};
static DirectX::XMFLOAT3 gizmoStartTranslate;
static DirectX::XMFLOAT3 gizmoStartRotate;
static DirectX::XMFLOAT3 gizmoStartScale;
// Inspector で値を変え始めたときの値 (Undo 用)
static DirectX::XMFLOAT3 s_inspectorStartTranslate;
static DirectX::XMFLOAT3 s_inspectorStartRotate;
static DirectX::XMFLOAT3 s_inspectorStartScale;
// 以前の機能で使っていた状態 (現在は未使用)
static bool objectDragging = false;
static std::vector<entt::entity> clipboardEntities; 
static bool s_riverPlaceMode = false;             
static int s_riverPlaceCompIdx = 0;               

// UI 部品 (RectTransform) のドラッグ操作の状態
static bool uiDragging = false;
static int uiDragHandle = -1; 
static DirectX::XMFLOAT2 uiDragStartPos = {};
static DirectX::XMFLOAT2 uiDragStartSize = {};
static DirectX::XMFLOAT2 uiDragStartHitOffset = {};
static DirectX::XMFLOAT2 uiDragStartHitScale = {};
static bool uiHoveredAny = false; 
static int uiHoveredHandle = -1;

// Game ビューに表示した画像の画面上の範囲
static ImVec2 gameImageMin = {};
static ImVec2 gameImageMax = {};

// 連番 ID の生成 (現在は未使用) とアイコンのテクスチャ
static uint32_t nextObjectId = 1;
EditorUI::Icons EditorUI::s_icons;
static uint32_t GenerateId() { return nextObjectId++; }

// 複製したエンティティの名前を作る: 末尾の "_番号" や " (Copy)" を外し、同じ名前の最大番号 + 1 を付ける
static std::string GenerateCopyName(const std::string& baseName, SceneComponents& registry) {
	// 末尾の " (Copy)" をすべて外す
	std::string base = baseName;
	while (base.size() > 7 && base.substr(base.size() - 7) == " (Copy)")
		base = base.substr(0, base.size() - 7);
	{
		// 末尾が "_数字" なら外す
		auto pos = base.rfind('_');
		if (pos != std::string::npos && pos + 1 < base.size()) {
			bool allDigit = true;
			for (size_t i = pos + 1; i < base.size(); ++i)
				if (!isdigit((unsigned char)base[i])) {
					allDigit = false;
					break;
				}
			if (allDigit)
				base = base.substr(0, pos);
		}
	}
	// 空になったら Object にする
	if (base.empty()) base = "Object";
	// 同じ名前で "_数字" が付いたものの最大の番号を探す
	int maxNum = 0;
	auto view = registry.view<NameComponent>();
	for (auto entity : view) {
		const auto& name = view.get<NameComponent>(entity).name;
		if (name.size() > base.size() + 1 && name.substr(0, base.size()) == base && name[base.size()] == '_') {
			// "_" より後ろが数字だけか
			std::string numPart = name.substr(base.size() + 1);
			bool allDigit = true;
			for (char c : numPart) if (!isdigit((unsigned char)c)) { allDigit = false; break; }
			// 10桁以上は int に収まらず std::stoi が例外を投げるので連番として扱わない
			if (allDigit && !numPart.empty() && numPart.size() <= 9) {
				int n = std::stoi(numPart);
				if (n > maxNum) maxNum = n;
			}
		}
	}
	// 最大の番号 + 1 を付ける
	return base + "_" + std::to_string(maxNum + 1);
}

// 取り消しとやり直し
// Undo できる操作を記録する (新しい操作をしたら Redo の履歴は消す)
void EditorUI::PushUndo(const UndoCommand& cmd) {
	// 古いものから消して最大件数を保つ
	undoStack.push_back(cmd);
	if (undoStack.size() > kMaxUndoDepth) undoStack.pop_front();
	redoStack.clear();
}
// 最後の操作を取り消す
void EditorUI::Undo() {
	// 履歴が無ければ何もしない
	if (undoStack.empty()) return;
	// 最後の操作を取り出し、取り消して Redo の履歴へ移す
	auto c = undoStack.back();
	undoStack.pop_back();
	c.undo();
	redoStack.push_back(c);
}
// 取り消した操作をやり直す
void EditorUI::Redo() {
	// 履歴が無ければ何もしない
	if (redoStack.empty()) return;
	// 最後に取り消した操作を取り出し、やり直して Undo の履歴へ戻す
	auto c = redoStack.back();
	redoStack.pop_back();
	c.redo();
	undoStack.push_back(c);
}

// Console (ログの表示)
// Console にログを追加する (情報・警告・エラー)
void EditorUI::Log(const std::string& msg) {
	// 時刻付きで追加し、古いものから消して最大行数を保つ
	consoleLog.push_back({LogLevel::Info, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}
void EditorUI::LogWarning(const std::string& msg) {
	consoleLog.push_back({LogLevel::Warning, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}
void EditorUI::LogError(const std::string& msg) {
	consoleLog.push_back({LogLevel::Error, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}

// Game ビューの画像の範囲を返す
ImVec2 EditorUI::GetGameImageMin() { return gameImageMin; }
ImVec2 EditorUI::GetGameImageMax() { return gameImageMax; }

// JSON への書き出し
// 文字列を JSON 用にエスケープする (引用符・バックスラッシュ・制御文字)
static std::string EscapeJson(const std::string& s) {
	// 結果の文字列
	std::string o;
	for (char c : s) {
		switch (c) {
		case '"':  o += "\\\""; break;
		case '\\': o += "\\\\"; break;
		case '\b': o += "\\b";  break;
		case '\f': o += "\\f";  break;
		case '\n': o += "\\n";  break;
		case '\r': o += "\\r";  break;
		case '\t': o += "\\t";  break;
		default:
			// その他の制御文字は \u で書く
			if (static_cast<unsigned char>(c) < 32) {
				char buf[8];
				sprintf_s(buf, "\\u%04x", static_cast<int>(c));
				o += buf;
			} else {
				o += c;
			}
		}
	}
	return o;
}

// 1つのエンティティを JSON の文字列にする (ID・親 ID・名前・ロック・トランスフォーム・各コンポーネント)
static std::string SerializeEntity(SceneComponents& registry, entt::entity entity) {
	// 書き込み先
	std::stringstream ss;
	ss << "    {\n";
	uint32_t id = static_cast<uint32_t>(entt::to_entity(entity)); // バージョン情報を除いた番号だけを ID として保存する
	ss << "      \"id\": " << id << ",\n";
	
	// 親の ID (親がいなければ null)
	uint32_t parentId = static_cast<uint32_t>(entt::null);
	if (auto* hc = registry.try_get<HierarchyComponent>(entity)) {
		if (registry.valid(hc->parentId)) {
			parentId = static_cast<uint32_t>(entt::to_entity(hc->parentId));
		}
	}
	ss << "      \"parentId\": " << parentId << ",\n";

	// 名前 (無ければ Object)
	std::string name = "Object";
	if (auto* nc = registry.try_get<NameComponent>(entity)) name = nc->name;
	ss << "      \"name\": \"" << EscapeJson(name) << "\",\n";

	// Hierarchy でのロック
	bool locked = false;
	if (auto* es = registry.try_get<EditorStateComponent>(entity)) locked = es->locked;
	ss << "      \"locked\": " << (locked ? "true" : "false") << ",\n";

	// 位置・回転・大きさ
	auto* tc = registry.try_get<TransformComponent>(entity);
	if (tc) {
		ss << "      \"translate\": [" << tc->translate.x << ", " << tc->translate.y << ", " << tc->translate.z << "],\n";
		ss << "      \"rotate\": [" << tc->rotate.x << ", " << tc->rotate.y << ", " << tc->rotate.z << "],\n";
		ss << "      \"scale\": [" << tc->scale.x << ", " << tc->scale.y << ", " << tc->scale.z << "],\n";
	} else {
		ss << "      \"translate\": [0,0,0], \"rotate\": [0,0,0], \"scale\": [1,1,1],\n";
	}

	// 持っているコンポーネントを順に書き出す
	ss << "      \"components\": [\n";
	// 最初の要素の前にはカンマを付けない
	bool first = true;
	auto addComma = [&]() { if (!first) ss << ",\n"; first = false; };

	// 見た目: モデル・テクスチャ・シェーダー・色・UV・反射
	if (auto* cp = registry.try_get<MeshRendererComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"MeshRenderer\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"modelPath\": \"" << EscapeJson(cp->modelPath) << "\", \"texturePath\": \""
		   << EscapeJson(cp->texturePath) << "\", \"shaderName\": \"" << EscapeJson(cp->shaderName) << "\", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "," << cp->color.w << "], \"uvTiling\": [" << cp->uvTiling.x << "," << cp->uvTiling.y << "], \"uvOffset\": [" << cp->uvOffset.x << "," << cp->uvOffset.y << "], \"reflectivity\": " << cp->reflectivity << ", \"useCubemap\": " << (cp->useCubemap ? "true" : "false") << "}";
	}
	// 箱の当たり判定
	if (auto* cp = registry.try_get<BoxColliderComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"BoxCollider\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"center\": [" << cp->center.x << "," << cp->center.y << "," << cp->center.z << "], \"size\": ["
		   << cp->size.x << "," << cp->size.y << "," << cp->size.z << "], \"isTrigger\": " << (cp->isTrigger ? "true" : "false") << "}";
	}
	// 物理挙動
	if (auto* cp = registry.try_get<RigidbodyComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Rigidbody\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"velocity\": [" << cp->velocity.x << "," << cp->velocity.y << "," << cp->velocity.z
		   << "], \"useGravity\": " << (cp->useGravity ? "true" : "false") << ", \"isKinematic\": " << (cp->isKinematic ? "true" : "false") << "}";
	}
	// 音の発信源
	if (auto* cp = registry.try_get<AudioSourceComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"AudioSource\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"soundPath\": \"" << EscapeJson(cp->soundPath) << "\", \"volume\": " << cp->volume
		   << ", \"loop\": " << (cp->loop ? "true" : "false") << ", \"playOnStart\": " << (cp->playOnStart ? "true" : "false") << "}";
	}
	// スクリプトの一覧 (実体があれば今のパラメータを書き出す)
	if (auto* cp = registry.try_get<ScriptComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Script\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"scripts\": [";
		for (size_t i = 0; i < cp->scripts.size(); ++i) {
			auto& entry = cp->scripts[i];
			std::string params = entry.parameterData;
			if (entry.instance) params = entry.instance->SerializeParameters();
			
			// 書き出す内容をログに残す
			char logBuf[2048];
			sprintf_s(logBuf, "[EditorUI] SerializeEntity [Script]: %s, params: %s (instance=%p)\n", entry.scriptPath.c_str(), params.c_str(), entry.instance.get());
			OutputDebugStringA(logBuf);

			// クラス名とパラメータ
			ss << "{\"path\": \"" << EscapeJson(entry.scriptPath) << "\", \"param\": \"" << EscapeJson(params) << "\"}";
			if (i < cp->scripts.size() - 1) ss << ", ";
		}
		ss << "]}";
	}
	// ライト (平行光源・点光源・スポットライト)
	if (auto* cp = registry.try_get<DirectionalLightComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"DirectionalLight\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "], \"intensity\": " << cp->intensity << "}";
	}
	if (auto* cp = registry.try_get<PointLightComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"PointLight\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "], \"intensity\": " << cp->intensity << ", \"range\": " << cp->range << "}";
	}
	if (auto* cp = registry.try_get<SpotLightComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"SpotLight\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "], \"intensity\": " << cp->intensity << ", \"range\": " << cp->range << "}";
	}
	// 体力
	if (auto* cp = registry.try_get<HealthComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Health\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"hp\": " << cp->Hp() << ", \"maxHp\": " << cp->MaxHp() << ", \"isDead\": " << (cp->IsDead() ? "true" : "false") << "}";
	}
	// タグ
	if (auto* cp = registry.try_get<TagComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Tag\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"tag\": \"" << EscapeJson(TagToString(cp->tag)) << "\"}";
	}
	// 旧アクションゲーム用: 入力・移動・カメラ追従
	if (auto* cp = registry.try_get<PlayerInputComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"PlayerInput\", \"enabled\": " << (cp->enabled ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<CharacterMovementComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"CharacterMovement\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"speed\": " << cp->speed << ", \"jumpPower\": " << cp->jumpPower << ", \"gravity\": " << cp->gravity << ", \"heightOffset\": " << cp->heightOffset << "}";
	}
	if (auto* cp = registry.try_get<CameraTargetComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"CameraTarget\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"distance\": " << cp->distance << ", \"height\": " << cp->height << ", \"smoothSpeed\": " << cp->smoothSpeed << "}";
	}
	// 川 (制御点の一覧)
	if (auto* cp = registry.try_get<RiverComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"River\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"width\": " << cp->width << ", \"flowSpeed\": " << cp->flowSpeed << ", \"uvScale\": " << cp->uvScale << ", \"texturePath\": \"" << EscapeJson(cp->texturePath) << "\", \"points\": [";
		for (size_t pi = 0; pi < cp->points.size(); ++pi) {
			if (pi > 0) ss << ", ";
			ss << "[" << cp->points[pi].x << "," << cp->points[pi].y << "," << cp->points[pi].z << "]";
		}
		ss << "]}";
	}
	// 頭上の HP バー
	if (auto* cp = registry.try_get<WorldSpaceUIComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"WorldSpaceUI\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"showHealthBar\": " << (cp->showHealthBar ? "true" : "false") << ", \"showDamageNumbers\": " << (cp->showDamageNumbers ? "true" : "false") << ", \"offset\": [" << cp->offset.x << "," << cp->offset.y << "," << cp->offset.z << "], \"barWidth\": " << cp->barWidth << ", \"barHeight\": " << cp->barHeight << "}";
	}
	// GPU メッシュコライダー
	if (auto* cp = registry.try_get<GpuMeshColliderComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"GpuMeshCollider\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"meshPath\": \"" << EscapeJson(cp->meshPath) << "\", \"isTrigger\": " << (cp->isTrigger ? "true" : "false") << "}";
	}
	// アニメーター
	if (auto* cp = registry.try_get<AnimatorComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Animator\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"currentAnimation\": \"" << EscapeJson(cp->currentAnimation)
		   << "\", \"speed\": " << cp->speed << ", \"isPlaying\": " << (cp->isPlaying ? "true" : "false") << ", \"loop\": " << (cp->loop ? "true" : "false") << "}";
	}
	// 汎用の変数 (数値と文字列)
	if (auto* cp = registry.try_get<VariableComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Variable\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"values\": {";
		bool firstVar = true;
		for (auto& [k, v] : cp->values) {
			if (!firstVar) ss << ",";
			ss << "\"" << EscapeJson(k) << "\": " << v;
			firstVar = false;
		}
		ss << "}, \"strings\": {";
		firstVar = true;
		for (auto& [k, v] : cp->strings) {
			if (!firstVar) ss << ",";
			ss << "\"" << EscapeJson(k) << "\": \"" << EscapeJson(v) << "\"";
			firstVar = false;
		}
		ss << "}}";
	}
	// パーティクルエミッターの設定一式
	if (auto* cp = registry.try_get<ParticleEmitterComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"ParticleEmitter\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"assetPath\": \"" << EscapeJson(cp->assetPath) << "\"";
		
		auto& p = cp->emitter.params;
		ss << ", \"emitRate\": " << p.emitRate << ", \"burstCount\": " << p.burstCount;
		ss << ", \"shape\": " << static_cast<int>(p.shape) << ", \"shapeRadius\": " << p.shapeRadius << ", \"shapeAngle\": " << p.shapeAngle;
		ss << ", \"lifeTime\": " << p.lifeTime << ", \"lifeTimeVariance\": " << p.lifeTimeVariance;
		ss << ", \"startVelocity\": [" << p.startVelocity.x << "," << p.startVelocity.y << "," << p.startVelocity.z << "]";
		ss << ", \"velocityVariance\": [" << p.velocityVariance.x << "," << p.velocityVariance.y << "," << p.velocityVariance.z << "]";
		ss << ", \"acceleration\": [" << p.acceleration.x << "," << p.acceleration.y << "," << p.acceleration.z << "]";
		ss << ", \"damping\": " << p.damping;
		ss << ", \"startSize\": [" << p.startSize.x << "," << p.startSize.y << "," << p.startSize.z << "]";
		ss << ", \"startSizeVariance\": [" << p.startSizeVariance.x << "," << p.startSizeVariance.y << "," << p.startSizeVariance.z << "]";
		ss << ", \"endSize\": [" << p.endSize.x << "," << p.endSize.y << "," << p.endSize.z << "]";
		ss << ", \"endSizeVariance\": [" << p.endSizeVariance.x << "," << p.endSizeVariance.y << "," << p.endSizeVariance.z << "]";
		ss << ", \"startColor\": [" << p.startColor.x << "," << p.startColor.y << "," << p.startColor.z << "," << p.startColor.w << "]";
		ss << ", \"endColor\": [" << p.endColor.x << "," << p.endColor.y << "," << p.endColor.z << "," << p.endColor.w << "]";
		ss << ", \"angularVelocity\": [" << p.angularVelocity.x << "," << p.angularVelocity.y << "," << p.angularVelocity.z << "]";
		ss << ", \"angularVelocityVariance\": [" << p.angularVelocityVariance.x << "," << p.angularVelocityVariance.y << "," << p.angularVelocityVariance.z << "]";
		ss << ", \"texturePath\": \"" << EscapeJson(p.texturePath) << "\"";
		ss << ", \"shaderName\": \"" << EscapeJson(p.shaderName) << "\"";
		ss << ", \"useBillboard\": " << (p.useBillboard ? "true" : "false");
		ss << ", \"isAdditive\": " << (p.isAdditive ? "true" : "false");
		ss << ", \"useUvAnim\": " << (p.useUvAnim ? "true" : "false");
		ss << ", \"uvAnimCols\": " << p.uvAnimCols << ", \"uvAnimRows\": " << p.uvAnimRows << ", \"uvAnimFps\": " << p.uvAnimFps;
		
		ss << "}";
	}
	// 流体エミッター
	if (auto* fec = registry.try_get<FluidEmitterComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"FluidEmitter\", \"enabled\": " << (fec->enabled ? "true" : "false")
		   << ", \"emitCountPerFrame\": " << fec->emitCountPerFrame
		   << ", \"retainWater\": " << (fec->retainWater ? "true" : "false")
		   << ", \"particleLimit\": " << fec->particleLimit
		   << ", \"color\": [" << fec->color.x << "," << fec->color.y << "," << fec->color.z << "," << fec->color.w << "]"
		   << ", \"velocity\": [" << fec->velocity.x << "," << fec->velocity.y << "," << fec->velocity.z << "]"
		   << ", \"fluidType\": " << fec->fluidType << "}";
	}
	// UI: 配置・画像・文字・ボタン
	if (auto* cp = registry.try_get<RectTransformComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"RectTransform\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"pos\": [" << cp->pos.x << "," << cp->pos.y << "], \"size\": [" << cp->size.x << "," << cp->size.y << "], \"anchor\": [" << cp->anchor.x << "," << cp->anchor.y << "], \"pivot\": [" << cp->pivot.x << "," << cp->pivot.y << "], \"rotation\": " << cp->rotation << "}";
	}
	if (auto* cp = registry.try_get<UIImageComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIImage\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"texturePath\": \"" << EscapeJson(cp->texturePath) << "\", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "," << cp->color.w << "], \"layer\": " << cp->layer << ", \"is9Slice\": " << (cp->is9Slice ? "true" : "false") << ", \"borderTop\": " << cp->borderTop << ", \"borderBottom\": " << cp->borderBottom << ", \"borderLeft\": " << cp->borderLeft << ", \"borderRight\": " << cp->borderRight << "}";
	}
	if (auto* cp = registry.try_get<UITextComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIText\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"text\": \"" << EscapeJson(cp->text) << "\", \"fontSize\": " << cp->fontSize << ", \"fontPath\": \"" << EscapeJson(cp->fontPath) << "\", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "," << cp->color.w << "]}";
	}
	if (auto* cp = registry.try_get<UIButtonComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIButton\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"normalColor\": [" << cp->normalColor.x << "," << cp->normalColor.y << "," << cp->normalColor.z << "," << cp->normalColor.w << "], \"hoverColor\": [" << cp->hoverColor.x << "," << cp->hoverColor.y << "," << cp->hoverColor.z << "," << cp->hoverColor.w << "], \"pressedColor\": [" << cp->pressedColor.x << "," << cp->pressedColor.y << "," << cp->pressedColor.z << "," << cp->pressedColor.w << "], \"onClickCallback\": \"" << EscapeJson(cp->onClickCallback) << "\", \"hitboxOffset\": [" << cp->hitboxOffset.x << "," << cp->hitboxOffset.y << "], \"hitboxScale\": [" << cp->hitboxScale.x << "," << cp->hitboxScale.y << "]}";
	}
	// 音を聞く位置
	if (auto* cp = registry.try_get<AudioListenerComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"AudioListener\", \"enabled\": " << (cp->enabled ? "true" : "false") << "}";
	}
	// 旧アクションゲーム用: 攻撃判定・食らい判定・プレイヤーの行動・敵の AI
	if (auto* cp = registry.try_get<HitboxComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Hitbox\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"center\": [" << cp->center.x << "," << cp->center.y << "," << cp->center.z << "], \"size\": [" << cp->size.x << "," << cp->size.y << "," << cp->size.z << "], \"damage\": " << cp->damage << ", \"isActive\": " << (cp->isActive ? "true" : "false") << ", \"tag\": \"" << EscapeJson(TagToString(cp->tag)) << "\"}";
	}
	if (auto* cp = registry.try_get<HurtboxComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Hurtbox\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"center\": [" << cp->center.x << "," << cp->center.y << "," << cp->center.z << "], \"size\": [" << cp->size.x << "," << cp->size.y << "," << cp->size.z << "], \"damageMultiplier\": " << cp->damageMultiplier << ", \"tag\": \"" << EscapeJson(TagToString(cp->tag)) << "\"}";
	}
	if (auto* cp = registry.try_get<PlayerActionComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"PlayerAction\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"dodgeDuration\": " << cp->dodgeDuration << ", \"dodgeSpeed\": " << cp->dodgeSpeed << "}";
	}
	if (auto* cp = registry.try_get<EnemyAIComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"EnemyAI\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"attackInterval\": " << cp->attackInterval << ", \"windUpDuration\": " << cp->windUpDuration << ", \"attackDuration\": " << cp->attackDuration << ", \"cooldownDuration\": " << cp->cooldownDuration << ", \"stunDuration\": " << cp->stunDuration << ", \"chaseSpeed\": " << cp->chaseSpeed << ", \"chaseRange\": " << cp->chaseRange << ", \"attackRange\": " << cp->attackRange << ", \"hopper\": " << (cp->hopper ? "true" : "false") << ", \"hopInterval\": " << cp->hopInterval << ", \"hopHeight\": " << cp->hopHeight << ", \"aggroRange\": " << cp->aggroRange << "}";
	}
	// モーション: クリップごとのキーフレーム
	if (auto* cp = registry.try_get<MotionComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Motion\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"activeClip\": \"" << EscapeJson(cp->activeClip) << "\", \"clips\": {\n";
		bool firstClip = true;
		for (auto& [clipName, clip] : cp->clips) {
			if (!firstClip) ss << ",\n";
			firstClip = false;
			ss << "          \"" << EscapeJson(clipName) << "\": {\"totalDuration\": " << clip.totalDuration << ", \"loop\": " << (clip.loop ? "true" : "false") << ", \"keyframes\": [\n";
			for (size_t i = 0; i < clip.keyframes.size(); ++i) {
				auto& kf = clip.keyframes[i];
				ss << "            {\"time\": " << kf.time << ", \"translate\": [" << kf.translate.x << "," << kf.translate.y << "," << kf.translate.z << "], \"rotate\": [" << kf.rotate.x << "," << kf.rotate.y << "," << kf.rotate.z << "], \"scale\": [" << kf.scale.x << "," << kf.scale.y << "," << kf.scale.z << "]}";
				if (i < clip.keyframes.size() - 1) ss << ",\n";
			}
			ss << "\n          ]}";
		}
		ss << "\n        }}";
	}

	// 配列とオブジェクトを閉じる
	ss << "\n      ]\n";
	ss << "    }";
	return ss.str();
}

// シーン全体を JSON の文字列にする (描画設定と全エンティティ)
std::string EditorUI::SaveToMemory(GameScene* scene) {
	// シーンが無ければ空
	if (!scene) return "";
	std::stringstream ss;
	// 描画の設定 (ポストエフェクトの有無・環境光・周辺減光・ノイズ)
	ss << "{\n  \"settings\": {\n";
	auto* r = Engine::Renderer::GetInstance();
	auto pp = r->GetPostProcessParams();
	ss << "    \"postProcessEnabled\": " << (r->GetPostProcessEnabled() ? "true" : "false") << ",\n";
	auto ambient = r->GetLightCB().ambientColor;
	ss << "    \"ambientColor\": [" << ambient.x << ", " << ambient.y << ", " << ambient.z << "],\n";
	ss << "    \"vignette\": " << pp.vignette << ",\n";
	ss << "    \"noiseStrength\": " << pp.noiseStrength << "\n";
	ss << "  },\n";
	// エンティティの一覧
	ss << "  \"objects\": [\n";

	// 名前を持つエンティティを集めて、順に書き出す
	auto& registry = scene->GetComponents();
	auto view = registry.view<NameComponent>();
	std::vector<entt::entity> sortedEntities;
	view.each([&](entt::entity e, const NameComponent&) { sortedEntities.push_back(e); });
	for (size_t i = 0; i < sortedEntities.size(); ++i) {
		if (i > 0) ss << ",\n";
		ss << SerializeEntity(registry, sortedEntities[i]);
	}
	// 閉じる
	ss << "\n  ]\n}\n";
	return ss.str();
}

// コピーしたエンティティの JSON (Edit > Copy)
static std::string s_clipboardJson;

// エンティティの JSON から1つ復元する
static entt::entity RestoreEntitySnapshot(GameScene* scene, const std::string& snapshot) {
	try {
		// 1 つだけの objects 配列として読み込む (追加モード)
		auto created = RestoreSceneFromJson(scene, json::parse("{\"objects\": [" + snapshot + "]}"), true);
		if (!created.empty()) return created[0];
	} catch (const std::exception& e) {
		// 失敗したらエラーを出す
		EditorUI::LogError(std::string("Failed to restore entity: ") + e.what());
	}
	return entt::null;
}

// 生成/削除の Undo。復元したエンティティは別IDになるため、最新IDを共有して追跡する
// (古いIDを保持したままだと Redo→Undo で何も消えなくなる)
static UndoCommand MakeSpawnUndo(GameScene* scene, const std::string& description, const std::string& snapshot, entt::entity current, bool existsAfterRedo) {
	// 今のエンティティを共有して覚える
	auto live = std::make_shared<entt::entity>(current);
	// 消す処理: 今のエンティティを破棄予約する
	std::function<void()> destroy = [scene, live]() {
		if (*live != entt::null) scene->DestroyObject(static_cast<uint32_t>(*live));
		// 選択中なら選択を外す
		if (scene->GetSelectedEntity() == *live) scene->SelectEntity(entt::null);
		*live = entt::null;
	};
	// 戻す処理: 保存した JSON から作り直し、新しいエンティティを覚える
	std::function<void()> restore = [scene, live, snapshot]() { *live = RestoreEntitySnapshot(scene, snapshot); };
	// Redo 後に存在する操作 (生成・貼り付け) か、消える操作 (削除) かで、取り消しとやり直しを入れ替える
	return existsAfterRedo ? UndoCommand{description, destroy, restore} : UndoCommand{description, restore, destroy};
}

// 選択中のエンティティをコピーする
static void ExecuteCopy(GameScene* scene) {
	// シーンが無ければ何もしない
	if (!scene) return;
	// 選択中のエンティティ (無いか消えていれば何もしない)
	auto ent = scene->GetSelectedEntity();
	if (ent == entt::null || !scene->GetComponents().valid(ent)) return;
	// JSON にして覚える
	s_clipboardJson = SerializeEntity(scene->GetComponents(), ent);
	EditorUI::Log("Copied Entity to Clipboard.");
}

// コピーしたエンティティを貼り付ける (名前に番号を付け、少しずらして置く)
static void ExecutePaste(GameScene* scene) {
	// シーンかコピーした内容が無ければ何もしない
	if (!scene || s_clipboardJson.empty()) return;
	try {
		// 1 つだけの objects 配列として読み込む (追加モード)
		json j = json::parse("{\"objects\": [" + s_clipboardJson + "]}");
		auto createdEnts = RestoreSceneFromJson(scene, j, true);
		if (!createdEnts.empty() && createdEnts[0] != entt::null) {
			// 作ったエンティティ
			auto e = createdEnts[0];
			auto& reg = scene->GetComponents();
			// 名前に番号を付ける
			if (reg.all_of<NameComponent>(e)) {
				auto& nc = reg.get<NameComponent>(e);
				nc.name = GenerateCopyName(nc.name, reg);
			}
			// 元の位置から少しずらす
			if (reg.all_of<TransformComponent>(e)) {
				auto& tc = reg.get<TransformComponent>(e);
				tc.translate.x += 0.5f;
				tc.translate.y += 0.5f;
				tc.translate.z -= 0.5f;
			}
			// 選択し、Undo を登録してログに残す
			scene->SelectEntity(e);
			
			EditorUI::PushUndo(MakeSpawnUndo(scene, "Paste Entity", SerializeEntity(reg, e), e, true));
			EditorUI::Log("Pasted Entity: " + (reg.all_of<NameComponent>(e) ? reg.get<NameComponent>(e).name : "Unknown"));
		}
	// 失敗したらエラーを出す
	} catch (...) { EditorUI::LogError("Failed to paste entity"); }
}

// 複製 (コピー → 貼り付け)
static void ExecuteDuplicate(GameScene* scene) {
	if (!scene) return;
	ExecuteCopy(scene);
	ExecutePaste(scene);
}

// シーンをファイルに保存する (パスを省略すると現在のシーンファイル)
void EditorUI::SaveScene(GameScene* scene, const std::string& path) {
	// シーンが無ければ何もしない
	if (!scene) return;
	// PLAY 中は保存しない (実行中に作られた物まで保存されるため)
	if (scene->GetIsPlaying()) {
		LogWarning("Stop play mode before saving the scene (runtime objects would be saved).");
		return;
	}
	// 保存先 (指定があれば現在のシーンファイルも変える)
	std::string targetPath = path;
	if (targetPath.empty()) targetPath = currentScenePath.empty() ? Data::MainStagePath() : currentScenePath;
	else currentScenePath = targetPath;

	// プロジェクトのルートを基準にした絶対パス
	std::string absPath = GetUnifiedProjectPath(targetPath);
	OutputDebugStringA(("[EditorUI] Saving scene to: " + absPath + "\n").c_str());
	
	// シーンを JSON の文字列にする
	std::string content = SaveToMemory(scene);
	if (content.empty()) { LogError("Save failed: empty content"); return; }

	// 上書きで開いて書き込む
	std::ofstream f(Engine::PathUtils::FromUTF8(absPath), std::ios::out | std::ios::trunc);
	if (!f.is_open()) { LogError("Save failed (cannot open): " + absPath); return; }
	f << content;
	f.flush(); // 確実にディスクへ書き出す
	// 書き込みに失敗したらエラーを出す
	if (f.bad() || f.fail()) {
		LogError("Save failed (write error): " + absPath);
		f.close();
		return;
	}
	f.close();
	Log("Scene saved: " + absPath);
}

// ユーティリティとファイルダイアログ


// Windows の「開く」ダイアログ (選ばれたパスを UTF-8 で返す。キャンセル時は空)
static std::string OpenFileDialog(const char* filter) {
	// ダイアログの設定 (パスを入れるバッファ・絞り込み・存在するファイルだけ)
	OPENFILENAMEW ofn;
	WCHAR szFile[260] = {0};
	std::wstring wfilter = Engine::PathUtils::FromUTF8(filter);
	ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
	ofn.lpstrFilter = wfilter.c_str();
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	// 選ばれたら UTF-8 のパスで返す
	if (GetOpenFileNameW(&ofn) == TRUE) return Engine::PathUtils::ToUTF8(ofn.lpstrFile);
	return "";
}

// Windows の「名前を付けて保存」ダイアログ
static std::string SaveFileDialog(const char* filter, const char* defExt) {
	// ダイアログの設定 (パスを入れるバッファ・絞り込み・既定の拡張子・上書きの確認)
	OPENFILENAMEW ofn;
	WCHAR szFile[260] = {0};
	std::wstring wfilter = Engine::PathUtils::FromUTF8(filter);
	std::wstring wdefExt = Engine::PathUtils::FromUTF8(defExt);
	ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
	ofn.lpstrFilter = wfilter.c_str();
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = wdefExt.c_str();
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	// 選ばれたら UTF-8 のパスで返す
	if (GetSaveFileNameW(&ofn) == TRUE) return Engine::PathUtils::ToUTF8(ofn.lpstrFile);
	return "";
}

// シーンファイルを読み込む (append が true なら今のシーンに追加する)
static void LoadSceneInternal(GameScene* scene, const std::string& path, bool append) {
	// シーンが無ければ何もしない
	if (!scene) return;
	// プロジェクトのルートを基準にした絶対パス
	std::string absPath = EditorUI::GetUnifiedProjectPath(path);
	OutputDebugStringA(("[EditorUI] LoadSceneInternal: " + absPath + "\n").c_str());

	// 開けなければ、指定されたパスのままでもう一度試す
	std::ifstream f(Engine::PathUtils::FromUTF8(absPath));
	if (!f.is_open()) {
		absPath = path; f.open(Engine::PathUtils::FromUTF8(absPath));
		if (!f.is_open()) {
			// それでも開けなければエラーを出す
			EditorUI::LogError("Load failed: " + absPath);
			MessageBoxA(NULL, ("Failed to open scene file:\n" + absPath).c_str(), "Load Error", MB_OK | MB_ICONERROR);
			return;
		}
	}
	json j;
	try {
		// JSON を読み込み、シーンを復元する
		f >> j;
		RestoreSceneFromJson(scene, j, append);
		EditorUI::Log((append ? "Scene appended: " : "Scene loaded: ") + absPath);
	} catch (const std::exception& e) {
		// 読み込みに失敗したらエラーを出す
		std::string msg = "JSON Parse Error in " + absPath + ": " + std::string(e.what());
		EditorUI::LogError(msg);
		MessageBoxA(NULL, msg.c_str(), "JSON Error", MB_OK | MB_ICONERROR);
	}
}

// シーンを開く / シーンを追加で読み込む
void EditorUI::LoadScene(GameScene* scene, const std::string& path) {
	// 保存先を覚えてから読み込む
	currentScenePath = path;
	(void)LoadSceneInternal(scene, path, false);
}
void EditorUI::AddScene(GameScene* scene, const std::string& path) { (void)LoadSceneInternal(scene, path, true); }

// 文字列からシーンを復元する (PLAY 停止時の巻き戻しに使う)
void EditorUI::LoadFromMemory(GameScene* scene, const std::string& data) {
	// シーンが無ければ何もしない
	if (!scene) return;
	json j;
	OutputDebugStringA(("[EditorUI] LoadFromMemory: data size = " + std::to_string(data.size()) + "\n").c_str());
	try {
		// JSON を読み込み、今のシーンを置き換える
		j = json::parse(data);
		(void)RestoreSceneFromJson(scene, j, false);
	} catch (const std::exception& e) {
		LogError("Failed to restore memory snapshot: " + std::string(e.what()));
	}
}
// プレハブを読み込んで配置する ("objects" 形式・"prefab" キー・単一エンティティのどれでも可)
std::vector<entt::entity> EditorUI::LoadPrefab(GameScene* scene, const std::string& path) {
	// シーンが無ければ何もしない
	if (!scene) return {};
	// プロジェクトのルートを基準にした絶対パス (開けなければ指定のまま)
	std::string absPath = GetUnifiedProjectPath(path);
	std::ifstream f(Engine::PathUtils::FromUTF8(absPath));
	if (!f.is_open()) {
		absPath = path; f.open(Engine::PathUtils::FromUTF8(absPath));
		if (!f.is_open()) { LogError("Prefab load failed: " + absPath); return {}; }
	}
	json j;
	// JSON を読み込む
	try { f >> j; } catch (...) { LogError("Prefab JSON error: " + path); return {}; }
	
	// 作ったエンティティ
	std::vector<entt::entity> createdEntities;
	try {
		if (j.contains("objects") && j["objects"].is_array()) {
			createdEntities = RestoreSceneFromJson(scene, j, true); // 追加で読み込む
		} else {
			// "prefab" ルートキー、または単一エンティティのプレハブ
			// objects 配列に包んでから読み込む
			json wrapper = json::object();
			wrapper["objects"] = json::array();
			wrapper["objects"].push_back(j.contains("prefab") ? j["prefab"] : j);
			createdEntities = RestoreSceneFromJson(scene, wrapper, true);
		}
	} catch (const std::exception& e) {
		LogError("Prefab restore error: " + path + ": " + e.what());
	}
	// 復元に失敗したオブジェクトの穴 (entt::null) は呼び出し側に返さない
	createdEntities.erase(std::remove(createdEntities.begin(), createdEntities.end(), entt::entity{entt::null}), createdEntities.end());

	// 結果をログに残す
	if (createdEntities.empty()) {
		LogError("Prefab created no entities: " + absPath);
	} else {
		Log("Prefab loaded: " + absPath + " (" + std::to_string(createdEntities.size()) + " entities)");
	}
	return createdEntities;
}

// プロジェクトのルートを基準にした絶対パスへ変換する (絶対パスはそのまま)
std::string EditorUI::GetUnifiedProjectPath(const std::string& path) {
	// 空なら空
	if (path.empty()) return "";
	// UTF-8 の文字列を直接 fs::path に渡すと Windows で文字化けするので FromUTF8 を通す
	if (std::filesystem::path(Engine::PathUtils::FromUTF8(path)).is_absolute()) return path;

	// プロジェクトのルート
	std::string root = Engine::PathUtils::GetRootPath();
	std::filesystem::path combined = std::filesystem::path(Engine::PathUtils::FromUTF8(root)) / Engine::PathUtils::FromUTF8(path);
	// UTF-8 に戻し、区切りを / にそろえる
	std::string res = Engine::PathUtils::ToUTF8(combined.wstring());
	std::replace(res.begin(), res.end(), '\\', '/');
	return res;
}

// 初期化: Project ウィンドウのアイコンを読み込む
void EditorUI::Initialize(Engine::Renderer* renderer) {
	// 描画の窓口が無ければ何もしない
	if (!renderer) return;
	// エディタのアイコンは色が変わらないよう sRGB 変換をせずに読み込む
	s_icons.folder = renderer->LoadTexture2D("Resources/Editor/Icons/folder.png", false);
	s_icons.file = renderer->LoadTexture2D("Resources/Editor/Icons/file.png", false);
	s_icons.model = renderer->LoadTexture2D("Resources/Editor/Icons/model.png", false);
	s_icons.prefab = renderer->LoadTexture2D("Resources/Editor/Icons/prefab.png", false);
	s_icons.audio = renderer->LoadTexture2D("Resources/Editor/Icons/audio.png", false);
	s_icons.script = renderer->LoadTexture2D("Resources/Editor/Icons/script.png", false);
}

// メイン UI (Fluid Sandbox のウィンドウは別ファイル)
#include "FluidSandbox.inl"

// ====== Scene launcher (Unity の Build Settings / シーン切り替えに相当) ======
namespace {
// ランチャーの1項目 (グループ名・表示名・シーン名・シーンファイル・説明)。Resources/Data/editor.json の sceneLauncher から読む
struct SceneLaunch {
	std::string group;
	std::string label;
	std::string key;       // SceneManager に登録された名前
	std::string stagePath; // GameScene に読み込ませるシーンファイル (空なら本編のシーン)
	std::string note;
};
// editor.json の中身 (シーンランチャーの項目と、エディタで包んで表示するシーン)
struct EditorData { std::vector<SceneLaunch> launches; std::vector<std::string> hostedScenes; };
const EditorData& EditorConfig() {
	static const EditorData data = [] {
		const auto& json = Data::Load("editor.json");
		EditorData out;
		for (const auto& item : Data::Require(json, "sceneLauncher", "editor.json")) {
			out.launches.push_back({item.at("group").get<std::string>(), item.at("label").get<std::string>(), item.at("scene").get<std::string>(),
				item.at("stage").get<std::string>(), item.at("note").get<std::string>()});
		}
		out.hostedScenes = Data::ReadStrings(Data::Require(json, "hostedScenes", "editor.json"));
		return out;
	}();
	return data;
}

// 今開いているシーンがこの項目か (同じシーン名で、シーンファイルも一致するか)
bool IsCurrentLaunch(const SceneLaunch& launch) {
	// シーン名が違えば違う
	auto* manager = Engine::SceneManager::GetInstance();
	if (!manager || manager->CurrentName() != launch.key) return false;
	// シーンファイルを比べる (空の項目は本編のステージ)
	const std::string& stage = manager->CurrentParameters().stagePath;
	if (launch.stagePath.empty()) return Data::IsMainStage(stage);
	return stage.find(launch.stagePath) != std::string::npos;
}

// シーンを切り替える (切り替え中は受け付けない)
void LaunchScene(const std::string& key, const std::string& stagePath) {
	// 未登録のシーンや切り替え中は何もしない
	auto* manager = Engine::SceneManager::GetInstance();
	if (!manager || !manager->Has(key) || manager->InTransition()) return;
	// シーンファイルを渡して切り替え、ログに残す
	Engine::SceneParameters params;
	params.stagePath = stagePath;
	manager->RequestChange(key, params);
	EditorUI::Log("Open scene: " + key + (stagePath.empty() ? "" : " (" + stagePath + ")"));
}

// ツールバーに表示する現在のシーン名
std::string CurrentSceneLabel() {
	// ランチャーの項目にあればその表示名
	for (const auto& launch : EditorConfig().launches) {
		if (IsCurrentLaunch(launch)) return launch.label;
	}
	// 無ければシーン名と、シーンファイルの名前
	auto* manager = Engine::SceneManager::GetInstance();
	if (!manager) return "Scene";
	std::string label = manager->CurrentName();
	const std::string& stage = manager->CurrentParameters().stagePath;
	if (!stage.empty()) label += " (" + Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(stage)).filename().wstring()) + ")";
	return label;
}

// メニューとツールバーのドロップダウンで共通の中身
void DrawSceneLaunchItems() {
	// 直前のグループ名 (変わったら見出しを出す)
	const char* group = nullptr;
	for (const auto& launch : EditorConfig().launches) {
		if (!group || std::string(group) != launch.group) {
			if (group) ImGui::Spacing();
			group = launch.group.c_str();
			ImGui::TextDisabled("%s", group);
		}
		// 選んだらそのシーンを開く
		if (ImGui::Selectable(launch.label.c_str(), IsCurrentLaunch(launch))) LaunchScene(launch.key, launch.stagePath);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", launch.note.c_str());
	}
	// Resources/Scenes の JSON はゲームシーンとして開く
	ImGui::Spacing();
	ImGui::TextDisabled("Scene Files");
	// フォルダーが無くても例外を出さないためのエラーの受け取り先
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(Engine::PathUtils::FromUTF8(EditorUI::GetUnifiedProjectPath("Resources/Scenes")), ec)) {
		// JSON 以外は飛ばす
		if (entry.path().extension() != L".json") continue;
		const std::string fileName = Engine::PathUtils::ToUTF8(entry.path().filename().wstring());
		const std::string path = "Resources/Scenes/" + fileName;
		auto* manager = Engine::SceneManager::GetInstance();
		// 今開いているシーンファイルか
		const bool current = manager && manager->CurrentName() == "Game" && manager->CurrentParameters().stagePath.find(fileName) != std::string::npos;
		if (ImGui::Selectable(fileName.c_str(), current)) LaunchScene("Game", path);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open %s in the game scene", path.c_str());
	}
}

// メインメニューの「Scenes」
void DrawSceneLaunchMenu() {
	// メニューを開いていなければ何もしない
	if (!ImGui::BeginMenu("Scenes")) return;
	DrawSceneLaunchItems();
	ImGui::Separator();
	// 今のシーンを読み込み直す
	auto* manager = Engine::SceneManager::GetInstance();
	if (ImGui::MenuItem("Restart Current Scene", "Ctrl+R") && manager) LaunchScene(manager->CurrentName().c_str(), manager->CurrentParameters().stagePath);
	ImGui::EndMenu();
}

// ツールバーのシーン切り替えドロップダウン
void DrawSceneLaunchCombo() {
	ImGui::SetNextItemWidth(170.0f);
	// 今のシーン名を表示する
	const std::string label = CurrentSceneLabel();
	if (ImGui::BeginCombo("##SceneLauncher", label.c_str(), ImGuiComboFlags_HeightLarge)) {
		DrawSceneLaunchItems();
		ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open another scene (Scenes menu)");
}

// Ctrl+R: 今のシーンを読み込み直す
void HandleSceneShortcuts() {
	// 文字入力中・Ctrl が押されていない・R が押されていないなら何もしない
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput || !io.KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_R, false)) return;
	if (auto* manager = Engine::SceneManager::GetInstance()) LaunchScene(manager->CurrentName().c_str(), manager->CurrentParameters().stagePath);
}

// GameScene 以外 (タイトル・セレクト・デモ) で編集できない欄に出す説明
void DrawCodeSceneNotice(const char* what) {
	// この欄はゲームシーンでのみ使えることを説明する
	ImGui::TextWrapped("%s is available in game scenes.", what);
	ImGui::Spacing();
	ImGui::TextDisabled("This scene is built in code (UI / demo). Use the Game view to try it, or open a game scene:");
	// 街のステージを開くボタン
	if (ImGui::Button("Open City Stage")) LaunchScene("Game", "");
}
} // namespace

// ====== エディタの外枠 (Unity 風) ======
// 画面構成: メインメニュー / ツールバー (ツール・Scene⇔Game・Play/Pause/Step) / ドックスペース / ステータスバー
// 作成メニュー (定義は後ろ)
static void DrawCreateEntityMenu(GameScene* scene);
// Scene ビューでカメラ操作中か・カメラの移動速度
static bool s_sceneCameraNavigating = false;
static float s_sceneCameraSpeed = 20.0f;
// エディタのカメラを初期位置に置いたか・選択中の物へカメラを寄せる要求
static bool s_editorCameraPlaced = false;
static bool s_frameSelectedRequest = false;

namespace {

// Window メニューで開閉できる各ウィンドウの表示状態
struct EditorWindows {
	bool hierarchy = true;
	bool inspector = true;
	bool project = true;
	bool console = true;
	bool animation = true;
	bool lighting = true;
	// Particle Editor と Fluid Sandbox はプレビュー描画でレンダラーのカメラや流体を使うため、必要な時だけ開く
	bool particleEditor = false;
	bool fluidSandbox = false;
	bool fluidProfiler = false;
};
EditorWindows s_windows;
// Maximize On Play / Stats / レイアウトの再構築 / プロファイラの一時停止 の状態
bool s_maximizeOnPlay = false;
bool s_showStats = false;
bool s_resetLayoutRequested = false;
bool s_fluidProfilerPaused = false;
// Scene / Game タブの切り替え要求
GameScene::EditorView s_requestedView = GameScene::EditorView::Game;
bool s_viewRequested = false;
const char* s_pendingFocusWindow = nullptr; // レイアウト再構築後、ウィンドウが作られてから前面に出す

// ウィンドウ名 (ドック配置と Window メニューで共通に使う)
constexpr const char* kSceneWindow = "Scene";
constexpr const char* kGameWindow = "Game";
constexpr const char* kHierarchyWindow = "Hierarchy";
constexpr const char* kInspectorWindow = "Inspector";
constexpr const char* kProjectWindow = "Project";
constexpr const char* kConsoleWindow = "Console";
constexpr const char* kAnimationWindow = "Animation";
constexpr const char* kLightingWindow = "Lighting";
constexpr const char* kParticleWindow = "Particle Editor";
constexpr const char* kFluidSandboxWindow = "Fluid Sandbox";

// Scene / Game タブを前面に出す (ウィンドウの Begin より前に呼ぶ)
// 要求を覚えておく
void RequestView(GameScene::EditorView view) {
	s_requestedView = view;
	s_viewRequested = true;
}

// ツールバーのアイコンの種類 (再生・停止・一時停止・1 フレーム送り)
enum class ToolbarIcon { Play, Stop, Pause, Step };

// フォントに依存しないよう、記号は図形で描く
bool IconButton(const char* id, ToolbarIcon icon, bool active, const char* tooltip) {
	// ボタンの大きさと位置
	const float size = ImGui::GetFrameHeight();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	// 見えないボタンを置き、押されたか・乗っているかを調べる
	const bool pressed = ImGui::InvisibleButton(id, ImVec2(size * 1.5f, size));
	const bool hovered = ImGui::IsItemHovered();
	// 背景 (押されている・乗っている・通常で色を変える)
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImU32 bg = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
	draw->AddRectFilled(pos, ImVec2(pos.x + size * 1.5f, pos.y + size), bg, 3.0f);
	// 記号の色 (有効なら青)
	const ImU32 fg = active ? IM_COL32(120, 200, 255, 255) : IM_COL32(225, 225, 225, 255);
	// 記号の中心と大きさ
	const ImVec2 c(pos.x + size * 0.75f, pos.y + size * 0.5f);
	const float r = size * 0.28f;
	switch (icon) {
	case ToolbarIcon::Play:
		// 右向きの三角
		draw->AddTriangleFilled(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x - r * 0.8f, c.y + r), ImVec2(c.x + r, c.y), fg);
		break;
	case ToolbarIcon::Stop:
		// 四角
		draw->AddRectFilled(ImVec2(c.x - r * 0.85f, c.y - r * 0.85f), ImVec2(c.x + r * 0.85f, c.y + r * 0.85f), fg);
		break;
	case ToolbarIcon::Pause:
		// 縦の 2 本線
		draw->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x - r * 0.25f, c.y + r), fg);
		draw->AddRectFilled(ImVec2(c.x + r * 0.25f, c.y - r), ImVec2(c.x + r * 0.8f, c.y + r), fg);
		break;
	case ToolbarIcon::Step:
		// 三角と縦線
		draw->AddTriangleFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r, c.y + r), ImVec2(c.x + r * 0.45f, c.y), fg);
		draw->AddRectFilled(ImVec2(c.x + r * 0.5f, c.y - r), ImVec2(c.x + r * 0.9f, c.y + r), fg);
		break;
	}
	if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
	return pressed;
}

// 選択状態を持つテキストボタン (ツール切り替え・Scene/Game 切り替え用)
bool ToggleButton(const char* label, bool active, const char* tooltip = nullptr) {
	// 選択中は押された色にする
	if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	const bool pressed = ImGui::Button(label);
	if (active) ImGui::PopStyleColor();
	if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
	return pressed;
}

// PLAY 中はエディタ全体を色付けして、編集が停止時に戻ることを分かりやすくする (Unity の Play Mode Tint)
int PushPlayModeTint(bool playing, bool paused) {
	if (!playing) return 0;
	// 一時停止中は黄色っぽく、再生中は赤っぽくする
	const ImVec4 tint = paused ? ImVec4(0.30f, 0.26f, 0.12f, 1.0f) : ImVec4(0.28f, 0.14f, 0.14f, 1.0f);
	// 色を変える部分 (背景・メニュー・タイトル・タブ)
	const ImGuiCol targets[] = {ImGuiCol_WindowBg, ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_Tab, ImGuiCol_TabSelected};
	for (ImGuiCol col : targets) {
		ImVec4 c = ImGui::GetStyleColorVec4(col);
		// 元の色に 25% だけ混ぜる
		ImGui::PushStyleColor(col, ImVec4(c.x * 0.75f + tint.x * 0.25f, c.y * 0.75f + tint.y * 0.25f, c.z * 0.75f + tint.z * 0.25f, c.w));
	}
	// 変えた色の数 (後で同じ数だけ戻す)
	return IM_ARRAYSIZE(targets);
}

// 既定のレイアウト: 左 Hierarchy / 中央 Scene・Game / 下 Project・Console / 右 Inspector など
void BuildDefaultLayout(ImGuiID dockspace, const ImVec2& size, const std::vector<EditorExtension>& extensions) {
	// 今の配置を消して作り直す
	ImGui::DockBuilderRemoveNode(dockspace);
	ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspace, size);
	// 右・左・下に分ける (残りが中央)
	ImGuiID center = dockspace;
	ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
	ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
	// 各ウィンドウを置く
	ImGui::DockBuilderDockWindow(kSceneWindow, center);
	ImGui::DockBuilderDockWindow(kGameWindow, center);
	ImGui::DockBuilderDockWindow(kHierarchyWindow, left);
	ImGui::DockBuilderDockWindow(kProjectWindow, bottom);
	ImGui::DockBuilderDockWindow(kConsoleWindow, bottom);
	ImGui::DockBuilderDockWindow(kAnimationWindow, bottom);
	ImGui::DockBuilderDockWindow(kInspectorWindow, right);
	ImGui::DockBuilderDockWindow(kLightingWindow, right);
	ImGui::DockBuilderDockWindow(kParticleWindow, right);
	ImGui::DockBuilderDockWindow(kFluidSandboxWindow, right);
	// 拡張のウィンドウも右側に置く
	for (const auto& ext : extensions) ImGui::DockBuilderDockWindow(ext.windowName.c_str(), right);
	// 配置を確定する
	ImGui::DockBuilderFinish(dockspace);
	// 右側は Inspector を前面にする (ウィンドウがまだ無いので次フレーム以降に適用)
	s_pendingFocusWindow = kInspectorWindow;
}

// Console のログを種類ごとに数える
int CountLogs(LogLevel level) {
	int count = 0;
	// 指定の種類のものを数える
	for (const auto& log : consoleLog) count += log.level == level ? 1 : 0;
	return count;
}

} // namespace

// Scene ビューに切り替えて選択物を映す (Hierarchy のダブルクリック・F キー・Edit メニュー)
void EditorUI::FrameSelectedInScene() {
	// Scene タブを前面に出し、次のフレームでカメラを寄せる
	RequestView(GameScene::EditorView::Scene);
	s_frameSelectedRequest = true;
}

// メインメニュー (File / Edit / Scenes / GameObject / Window)
void EditorUI::DrawMainMenu(Engine::Renderer* renderer, GameScene* gameScene) {
	// ファイル選択の絞り込み (JSON とすべてのファイル)
	static const char* kSceneFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
	// PLAY 中か
	const bool playing = gameScene->GetIsPlaying();
	if (ImGui::BeginMenu("File")) {
		// PLAY 中は保存しない: ゲームが生成した一時オブジェクトまでシーンに書き込まれてしまうため
		ImGui::BeginDisabled(playing);
		// 新しいシーン: 空にして選択を外し、保存先を既定に戻す
		if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
			gameScene->ClearScene();
			gameScene->SelectEntity(entt::null);
			currentScenePath = Data::MainStagePath();
		}
		ImGui::Separator();
		// シーンを開く / 追加で読み込む
		if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
			std::string path = OpenFileDialog(kSceneFilter);
			if (!path.empty()) LoadScene(gameScene, path);
		}
		if (ImGui::MenuItem("Append Scene...", "Ctrl+Shift+O")) {
			std::string path = OpenFileDialog(kSceneFilter);
			if (!path.empty()) AddScene(gameScene, path);
		}
		// Resources/Scenes の JSON を一覧にして開く
		if (ImGui::BeginMenu("Scenes")) {
			std::string sceneDir = "Resources/Scenes";
			std::error_code ec;
			bool any = false;
			// 日本語名のファイルで path::string() が例外を投げるため、UTF-8 へ明示変換する
			for (const auto& entry : fs::directory_iterator(Engine::PathUtils::FromUTF8(GetUnifiedProjectPath(sceneDir)), ec)) {
				// JSON 以外は飛ばす
				if (entry.path().extension() != L".json") continue;
				std::string fileName = Engine::PathUtils::ToUTF8(entry.path().filename().wstring());
				std::string fullPath = sceneDir + "/" + fileName;
				if (ImGui::MenuItem(fileName.c_str(), nullptr, currentScenePath == fullPath)) LoadScene(gameScene, fullPath);
				any = true;
			}
			// 見つからなければその旨を出す
			if (!any) ImGui::TextDisabled("No scenes in %s", sceneDir.c_str());
			ImGui::EndMenu();
		}
		ImGui::Separator();
		// 上書き保存 / 名前を付けて保存
		if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene(gameScene);
		if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
			std::string path = SaveFileDialog(kSceneFilter, "json");
			if (!path.empty()) SaveScene(gameScene, path);
		}
		ImGui::EndDisabled();
		if (playing) ImGui::TextDisabled("Stop play mode to save or load scenes");
		ImGui::EndMenu();
	}
	// Edit: Undo・コピー/貼り付け・Frame Selected・Play/Pause/Step
	if (ImGui::BeginMenu("Edit")) {
		// Undo / Redo (PLAY 中は無効)
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !playing)) Undo();
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !playing)) Redo();
		ImGui::Separator();
		// コピー・貼り付け・複製
		if (ImGui::MenuItem("Copy", "Ctrl+C")) ExecuteCopy(gameScene);
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, !s_clipboardJson.empty() && !playing)) ExecutePaste(gameScene);
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !playing)) ExecuteDuplicate(gameScene);
		ImGui::Separator();
		// 選択中の物へカメラを寄せる
		if (ImGui::MenuItem("Frame Selected", "F")) FrameSelectedInScene();
		ImGui::Separator();
		// Play / Stop・Pause・Step
		if (ImGui::MenuItem(playing ? "Stop" : "Play", "Ctrl+P")) gameScene->SetIsPlaying(!playing);
		if (ImGui::MenuItem("Pause", "Ctrl+Shift+P", gameScene->IsEditorPaused(), playing)) gameScene->SetEditorPaused(!gameScene->IsEditorPaused());
		if (ImGui::MenuItem("Step", "Ctrl+Alt+P", false, playing)) {
			gameScene->SetEditorPaused(true);
			gameScene->StepEditorFrame();
		}
		ImGui::EndMenu();
	}
	DrawSceneLaunchMenu();
	// GameObject: 生成と Move To View (PLAY 中は無効)
	if (ImGui::BeginMenu("GameObject")) {
		ImGui::BeginDisabled(playing);
		DrawCreateEntityMenu(gameScene);
		ImGui::Separator();
		// 選択中の物が位置を持っていれば、カメラの前へ移せる
		const auto selected = gameScene->GetSelectedEntity();
		const bool hasTransform = selected != entt::null && gameScene->GetComponents().valid(selected) && gameScene->GetComponents().all_of<TransformComponent>(selected);
		if (ImGui::MenuItem("Move To View", "Ctrl+Alt+F", false, hasTransform)) MoveSelectedToView(gameScene);
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}
	// Window: Scene/Game の切り替え、各ウィンドウの開閉、分析ツール、レイアウトのリセット
	if (ImGui::BeginMenu("Window")) {
		// Scene / Game タブの切り替え
		if (ImGui::MenuItem("Scene", "Ctrl+1")) RequestView(GameScene::EditorView::Scene);
		if (ImGui::MenuItem("Game", "Ctrl+2")) RequestView(GameScene::EditorView::Game);
		ImGui::Separator();
		// 各ウィンドウの開閉
		ImGui::MenuItem(kHierarchyWindow, nullptr, &s_windows.hierarchy);
		ImGui::MenuItem(kInspectorWindow, nullptr, &s_windows.inspector);
		ImGui::MenuItem(kProjectWindow, nullptr, &s_windows.project);
		ImGui::MenuItem(kConsoleWindow, nullptr, &s_windows.console);
		ImGui::MenuItem(kAnimationWindow, nullptr, &s_windows.animation);
		ImGui::MenuItem(kLightingWindow, nullptr, &s_windows.lighting);
		ImGui::MenuItem(kParticleWindow, nullptr, &s_windows.particleEditor);
		ImGui::MenuItem(kFluidSandboxWindow, nullptr, &s_windows.fluidSandbox);
		// 拡張のウィンドウがあれば、その開閉も並べる
		if (!Extensions().empty()) {
			ImGui::Separator();
			for (auto& ext : Extensions()) ImGui::MenuItem(ext.windowName.c_str(), nullptr, &ext.open);
		}
		ImGui::Separator();
		// 分析ツール: 流体のプロファイラと、流体の表示モードの切り替え
		if (ImGui::BeginMenu("Analysis")) {
			ImGui::MenuItem("Fluid Profiler", nullptr, &s_windows.fluidProfiler);
			if (ImGui::BeginMenu("Fluid Debug View")) {
				if (renderer) {
					// 流体のボリュームが使えるか
					ImGui::TextDisabled("%s", renderer->IsFluidVolumeReady() ? "3D volume active" : "Fallback (volume init failed)");
					// 表示モード (材質・材質の段階・法線・厚さ・計算範囲)
					const char* modes[] = {"Material", "Material phases", "3D normals", "Thickness", "Domain / ray steps"};
					for (uint32_t mode = 0; mode < 5; ++mode) {
						if (ImGui::MenuItem(modes[mode], nullptr, renderer->GetFluidVolumeDebugMode() == mode)) renderer->SetFluidVolumeDebugMode(mode);
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		// レイアウトを既定に戻す
		if (ImGui::MenuItem("Reset Layout")) s_resetLayoutRequested = true;
		ImGui::EndMenu();
	}
}

// 画面上部のツールバー
void EditorUI::DrawToolbar(GameScene* gameScene, float height) {
	// メインのビューポートの上端に、スクロールしない帯として置く
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
	if (ImGui::BeginViewportSideBar("##MainToolbar", viewport, ImGuiDir_Up, height, flags)) {
		// PLAY 中か
		const bool playing = gameScene->GetIsPlaying();
		// 左: 変形ツール (W/E/R) と Scene / Game の切り替え
		if (ToggleButton("Move", gizmo_.Mode() == GizmoMode::Translate, "Move tool (W)")) gizmo_.SetMode(GizmoMode::Translate);
		ImGui::SameLine(0, 2);
		if (ToggleButton("Rotate", gizmo_.Mode() == GizmoMode::Rotate, "Rotate tool (E)")) gizmo_.SetMode(GizmoMode::Rotate);
		ImGui::SameLine(0, 2);
		if (ToggleButton("Scale", gizmo_.Mode() == GizmoMode::Scale, "Scale tool (R)")) gizmo_.SetMode(GizmoMode::Scale);
		ImGui::SameLine(0, 16);
		// Scene ビューを表示しているか
		const bool sceneActive = gameScene->GetEditorView() == GameScene::EditorView::Scene;
		if (ToggleButton("Scene", sceneActive, "Editor camera view (Ctrl+1)")) RequestView(GameScene::EditorView::Scene);
		ImGui::SameLine(0, 2);
		if (ToggleButton("Game", !sceneActive, "Player camera view (Ctrl+2)")) RequestView(GameScene::EditorView::Game);
		ImGui::SameLine(0, 16);
		// シーン切り替えのドロップダウン
		DrawSceneLaunchCombo();

		// 中央: Play / Pause / Step
		// ボタン 3 つ分の幅を求めて中央に置く
		const float buttonWidth = ImGui::GetFrameHeight() * 1.5f;
		const float groupWidth = buttonWidth * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, (ImGui::GetWindowWidth() - groupWidth) * 0.5f));
		// 再生 / 停止
		if (IconButton("##Play", playing ? ToolbarIcon::Stop : ToolbarIcon::Play, playing, playing ? "Stop (Ctrl+P)" : "Play (Ctrl+P)")) {
			gameScene->SetIsPlaying(!playing);
		}
		ImGui::SameLine();
		// 一時停止と 1 フレーム送りは PLAY 中だけ
		ImGui::BeginDisabled(!playing);
		if (IconButton("##Pause", ToolbarIcon::Pause, gameScene->IsEditorPaused(), "Pause (Ctrl+Shift+P)")) gameScene->SetEditorPaused(!gameScene->IsEditorPaused());
		ImGui::SameLine();
		if (IconButton("##Step", ToolbarIcon::Step, false, "Step one frame (Ctrl+Alt+P)")) {
			gameScene->SetEditorPaused(true);
			gameScene->StepEditorFrame();
		}
		ImGui::EndDisabled();

		// 右: Maximize On Play とレイアウト
		// 右端に置くための幅を求める
		const char* layoutLabel = "Reset Layout";
		const float rightWidth = ImGui::CalcTextSize("Maximize On Play").x + ImGui::CalcTextSize(layoutLabel).x + ImGui::GetFrameHeight() + 48.0f;
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - rightWidth));
		ImGui::Checkbox("Maximize On Play", &s_maximizeOnPlay);
		ImGui::SameLine();
		if (ImGui::Button(layoutLabel)) s_resetLayoutRequested = true;
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

// 画面下部のステータスバー
void EditorUI::DrawStatusBar(GameScene* gameScene, float height) {
	// メインのビューポートの下端に、スクロールしない帯として置く
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
	if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags)) {
		// 左: 最新のログ (クリックで Console を開く)
		if (!consoleLog.empty()) {
			// 最新のログ (エラーは赤、警告は黄、それ以外は薄く)
			const auto& last = consoleLog.back();
			const ImVec4 color = last.level == LogLevel::Error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) :
				last.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.35f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			if (ImGui::Selectable(last.message.c_str(), false, 0, ImVec2(ImGui::GetWindowWidth() * 0.55f, 0))) {
				s_windows.console = true;
				ImGui::SetWindowFocus(kConsoleWindow);
			}
			ImGui::PopStyleColor();
		}
		// 右: エラー/警告数・状態・FPS
		// 右側の文字 (エラー数・警告数・状態・物の数・FPS)
		char right[160];
		const char* state = !gameScene->GetIsPlaying() ? "Edit" : gameScene->IsEditorPaused() ? "Paused" : "Playing";
		snprintf(right, sizeof(right), "%d errors  %d warnings   |   %s   |   %d objects   |   %.0f FPS",
			CountLogs(LogLevel::Error), CountLogs(LogLevel::Warning), state,
			static_cast<int>(gameScene->GetComponents().view<NameComponent>().size()), ImGui::GetIO().Framerate);
		// 右寄せで書く
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - ImGui::CalcTextSize(right).x - 12.0f));
		ImGui::TextDisabled("%s", right);
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

// 選択中のエンティティを Scene カメラの前方 10 の位置へ移す (Undo 対応)
void EditorUI::MoveSelectedToView(GameScene* scene) {
	// 選択中の物 (無いか位置が無ければ何もしない)
	auto selected = scene->GetSelectedEntity();
	auto& reg = scene->GetComponents();
	if (selected == entt::null || !reg.valid(selected) || !reg.all_of<TransformComponent>(selected)) return;
	// エディタのカメラのワールド行列 (ビュー行列の逆行列)
	const auto& camera = scene->GetEditorCamera();
	DirectX::XMMATRIX world = DirectX::XMMatrixInverse(nullptr, camera.View());
	// カメラの位置と前方向
	DirectX::XMFLOAT3 position, forward;
	DirectX::XMStoreFloat3(&position, world.r[3]);
	DirectX::XMStoreFloat3(&forward, DirectX::XMVector3Normalize(world.r[2]));
	// 移す前の位置と、移す先
	auto& tc = reg.get<TransformComponent>(selected);
	const DirectX::XMFLOAT3 before = tc.translate;
	const DirectX::XMFLOAT3 after{position.x + forward.x * 10.0f, position.y + forward.y * 10.0f, position.z + forward.z * 10.0f};
	tc.translate = after;
	// Undo / Redo を登録する (前の位置に戻す / 移した位置にする)
	PushUndo({"Move To View",
		[scene, selected, before]() { auto& r = scene->GetComponents(); if (r.valid(selected) && r.all_of<TransformComponent>(selected)) r.get<TransformComponent>(selected).translate = before; },
		[scene, selected, after]() { auto& r = scene->GetComponents(); if (r.valid(selected) && r.all_of<TransformComponent>(selected)) r.get<TransformComponent>(selected).translate = after; }});
}

// キーボードショートカット (文字入力中は無効)
void EditorUI::HandleShortcuts(GameScene* gameScene) {
	ImGuiIO& io = ImGui::GetIO();
	// 文字入力中は何もしない
	if (io.WantTextInput) return;
	// ファイル選択の絞り込み
	static const char* kSceneFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
	// PLAY 中か
	const bool playing = gameScene->GetIsPlaying();
	// Play / Pause / Step は Unity と同じく Ctrl+P 系 (P 単体はゲームの操作キーと衝突するため廃止)
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
		// Ctrl+Alt+P: 1 フレーム送り / Ctrl+Shift+P: 一時停止 / Ctrl+P: 再生と停止
		if (io.KeyAlt) {
			if (playing) { gameScene->SetEditorPaused(true); gameScene->StepEditorFrame(); }
		} else if (io.KeyShift) {
			if (playing) gameScene->SetEditorPaused(!gameScene->IsEditorPaused());
		} else {
			gameScene->SetIsPlaying(!playing);
		}
		return;
	}
	// Ctrl+1 / Ctrl+2 で Scene / Game を切り替える
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_1, false)) RequestView(GameScene::EditorView::Scene);
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_2, false)) RequestView(GameScene::EditorView::Game);
	if (playing) return; // ここから下は編集操作 (PLAY 中はゲームの入力と衝突する)

	// 編集用 (PLAY 中は効かない): 保存・Undo・新規・開く・コピー/貼り付け/複製・Move To View、W/E/R のツール切り替え
	if (io.KeyCtrl) {
		// Ctrl+S: 上書き保存 (Shift 付きで名前を付けて保存)
		if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			if (io.KeyShift) {
				std::string path = SaveFileDialog(kSceneFilter, "json");
				if (!path.empty()) SaveScene(gameScene, path);
			} else {
				SaveScene(gameScene, currentScenePath);
			}
		}
		// Ctrl+Z / Ctrl+Y: Undo / Redo
		if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
		if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
		// Ctrl+N: 新しいシーン
		if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
			gameScene->ClearScene();
			gameScene->SelectEntity(entt::null);
			currentScenePath = Data::MainStagePath();
		}
		// Ctrl+O: 開く (Shift 付きで追加)
		if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
			std::string path = OpenFileDialog(kSceneFilter);
			if (!path.empty()) {
				if (io.KeyShift) AddScene(gameScene, path);
				else LoadScene(gameScene, path);
			}
		}
		// Ctrl+C / V / D: コピー・貼り付け・複製、Ctrl+Alt+F: カメラの前へ移す
		if (ImGui::IsKeyPressed(ImGuiKey_C, false)) ExecuteCopy(gameScene);
		if (ImGui::IsKeyPressed(ImGuiKey_V, false)) ExecutePaste(gameScene);
		if (ImGui::IsKeyPressed(ImGuiKey_D, false)) ExecuteDuplicate(gameScene);
		if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false)) MoveSelectedToView(gameScene);
	} else if (!s_sceneCameraNavigating) {
		// W / E / R: 移動・回転・拡大のツール (Scene カメラの操作中は除く)
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmo_.SetMode(GizmoMode::Translate);
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmo_.SetMode(GizmoMode::Rotate);
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmo_.SetMode(GizmoMode::Scale);
	}
}

// 毎フレームのエディタ描画 (GameScene 用)
void EditorUI::Show(Engine::Renderer* renderer, GameScene* gameScene) {
	// エディタの経過時間を進める
	globalTime += ImGui::GetIO().DeltaTime;
	// PLAY 中か、PLAY 中ならエディタ全体を色付けする
	const bool playing = gameScene->GetIsPlaying();
	const int tintColors = PushPlayModeTint(playing, gameScene->IsEditorPaused());

	// PLAY 中 (Game ビュー表示時) は ImGui のカーソルを消す
	// スライムの調合室を開いているか
	const bool slimeLabOpen = gameScene->GetChronoSystem() && gameScene->GetChronoSystem()->SlimeLabOpen();
	if (playing && !gameScene->IsPaused() && !gameScene->IsEditorPaused() && !gameScene->IsResultShowing() && !slimeLabOpen &&
		gameScene->GetEditorView() == GameScene::EditorView::Game) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
	} else {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
	}

	// ショートカットキーの処理
	HandleShortcuts(gameScene);
	HandleSceneShortcuts();
	// GameScene は自前でマウス座標を Game ビューへ換算する (ctx の override) ため、Input の換算は使わない
	if (auto* input = Engine::Input::GetInstance()) input->ClearPointerViewport();

	// メインメニュー・ツールバー・ステータスバー
	if (ImGui::BeginMainMenuBar()) {
		DrawMainMenu(renderer, gameScene);
		ImGui::EndMainMenuBar();
	}
	DrawToolbar(gameScene, ImGui::GetFrameHeight() + 8.0f);
	DrawStatusBar(gameScene, ImGui::GetFrameHeight() + 2.0f);

	// 作業領域いっぱいの、枠もタイトルも無い親ウィンドウを作る
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	const ImGuiWindowFlags mainFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	ImGui::Begin("EditorMain", nullptr, mainFlags);
	ImGui::PopStyleVar(3);

	// ドックスペースの ID
	const ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
	// 初回 (新しいウィンドウ構成の設定がまだ無い) か、Reset Layout が押されたら既定のレイアウトを組む
	if (s_resetLayoutRequested || ImGui::FindWindowSettingsByID(ImHashStr(kInspectorWindow)) == nullptr) {
		// ウィンドウの表示状態を初期値に戻し、既定の配置を組む
		s_windows = EditorWindows{};
		for (auto& ext : Extensions()) ext.open = true;
		BuildDefaultLayout(dockspaceId, viewport->WorkSize, Extensions());
		s_resetLayoutRequested = false;
	}
	// ドックスペースを置く
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	// 前面に出す予約があり、ウィンドウができていれば前面に出す
	if (s_pendingFocusWindow && ImGui::FindWindowByName(s_pendingFocusWindow)) {
		ImGui::SetWindowFocus(s_pendingFocusWindow);
		s_pendingFocusWindow = nullptr;
	}

	// Maximize On Play: PLAY 中は Game ビューだけを作業領域いっぱいに表示する
	const bool maximized = playing && s_maximizeOnPlay;
	// 最大化していないときだけ各ウィンドウを描く
	if (!maximized) {
		// Hierarchy と Console
		if (s_windows.hierarchy) ShowHierarchy(gameScene);
		if (s_windows.console) ShowConsole();
		// Inspector (PLAY 中は、停止すると元に戻ることを知らせる)
		if (s_windows.inspector) {
			if (ImGui::Begin(kInspectorWindow, &s_windows.inspector)) {
				if (playing) {
					ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Play mode: changes are reverted on Stop");
					ImGui::Separator();
				}
				ShowInspector(gameScene);
			}
			ImGui::End();
		}
		// Animation・Lighting・Particle Editor
		if (s_windows.animation) {
			if (ImGui::Begin(kAnimationWindow, &s_windows.animation)) ShowAnimationWindow(renderer, gameScene);
			ImGui::End();
		}
		if (s_windows.lighting) {
			if (ImGui::Begin(kLightingWindow, &s_windows.lighting)) ShowSceneSettings(renderer);
			ImGui::End();
		}
		if (s_windows.particleEditor) {
			if (ImGui::Begin(kParticleWindow, &s_windows.particleEditor)) gameScene->GetParticleEditor().DrawUI();
			ImGui::End();
		}
		// Fluid Sandbox を開く要求があれば開いて前面に出す
		if (fluidSandboxOpen) {
			s_windows.fluidSandbox = true;
			ImGui::SetNextWindowFocus();
			fluidSandboxOpen = false;
		}
		if (s_windows.fluidSandbox) {
			if (ImGui::Begin(kFluidSandboxWindow, &s_windows.fluidSandbox)) ShowFluidSandbox(renderer, gameScene);
			ImGui::End();
		}
		// Project
		if (s_windows.project) ShowProject(renderer, gameScene);
		// 拡張のウィンドウ
		for (auto& ext : Extensions()) {
			if (!ext.open || !ext.drawWindow) continue;
			if (ImGui::Begin(ext.windowName.c_str(), &ext.open)) ext.drawWindow(gameScene);
			ImGui::End();
		}
	}
	// Scene / Game のビュー
	ShowViewports(renderer, gameScene, maximized);

	ImGui::End(); // EditorMain

	// 流体のプロファイラ (開いていて一時停止していなければ計測する)
	if (s_windows.fluidProfiler) ShowFluidProfiler(renderer);
	if (renderer) renderer->SetFluidProfilerEnabled(s_windows.fluidProfiler && !s_fluidProfilerPaused);
	// PLAY 中の色付けを戻す
	ImGui::PopStyleColor(tintColors);
}

// 流体の GPU 処理時間を表示するプロファイラ
void EditorUI::ShowFluidProfiler(Engine::Renderer* renderer) {
	// 最初の大きさ
	ImGui::SetNextWindowSize(ImVec2(540, 420), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Fluid Profiler##Development", &s_windows.fluidProfiler)) {
		// 計測を止めるか
		ImGui::Checkbox("Pause capture", &s_fluidProfilerPaused);
		// 流体の計測結果
		const auto& profile = renderer->GetFluidProfileStats();
		if (s_fluidProfilerPaused) ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.32f, 1.0f),
			"Capture paused: the values below are from an earlier frame.");
		// 計測できない / まだ結果が無い / 結果の表示
		if (!profile.available) {
			ImGui::TextUnformatted("GPU timestamp queries are unavailable on this device.");
		} else if (!profile.valid) {
			ImGui::TextUnformatted("Waiting for a completed GPU frame...");
		} else {
			// 流体の合計時間
			ImGui::Text("Fluid GPU total: %.3f ms", profile.totalMs);
			ImGui::SameLine();
			ImGui::TextDisabled("(%u frames old)", profile.framesSinceSample);
			// ゲームの描画全体と、それ以外の時間
			if (profile.sampledStages[Engine::Renderer::SceneRender]) {
				const float sceneMs = profile.lastMs[Engine::Renderer::SceneRender];
				ImGui::Text("Game render GPU: %.3f ms   Other game passes: %.3f ms",
					sceneMs, (std::max)(0.0f, sceneMs - profile.totalMs));
			}
			// 粒子数・分割数・時間刻み・格子の作り直しの回数・ボリュームの大きさ・FPS・CPU の時間
			ImGui::Text("Particles: %u   Simulation substeps: %u   Simulated dt: %.2f ms",
				profile.particleSlots, profile.substeps, profile.simulatedDtMs);
			ImGui::Text("Spatial grid rebuilds: %u", profile.simulatedDtMs > 0.0f ? 1 + 4 * profile.substeps : 1);
			ImGui::Text("Volume: 176 x 64 x 176 at 0.56 units   LOD: 46.0 - 48.5 units");
			ImGui::Text("Frame rate: %.1f FPS (whole editor)", ImGui::GetIO().Framerate);
			ImGui::Text("CPU command recording: simulation %.3f ms, volume %.3f ms",
				profile.cpuSimulationMs, profile.cpuVolumeMs);
			ImGui::Separator();
			// 処理ごとの直近・平均・最大の時間
			ImGui::TextUnformatted("GPU pass                                Last       Mean       Peak (ms)");
			static const char* names[Engine::Renderer::kFluidProfileStageCount] = {
				"Particle simulation", "Shape reconstruction", "Density splat + resolve",
				"Filtering + occupancy", "Refraction + raymarch", "Distant impostors", "Fluid shadow", "Whole game render",
				"Scenery shadow", "RT acceleration", "RT lighting rays", "RT radiance filter",
				"DLSS depth + motion inputs", "DLSS super resolution", "RT HDR composite"
			};
			// 計測していない処理は inactive と出す
			for (uint32_t i = 0; i < Engine::Renderer::kFluidProfileStageCount; ++i) {
				if (!profile.sampledStages[i]) {
					ImGui::Text("%-30s    (inactive)", names[i]);
					continue;
				}
				ImGui::Text("%-30s %8.3f   %8.3f   %8.3f", names[i],
					profile.lastMs[i], profile.averageMs[i], profile.peakMs[i]);
			}
			ImGui::Separator();
			// 合計時間の折れ線グラフ
			ImGui::PlotLines("Fluid GPU total", profile.totalHistory.data(),
				static_cast<int>(profile.historyCount), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 100));
			ImGui::TextDisabled("Mean / peak: last %u completed samples. GPU time excludes CPU waits and UI.", profile.historyCount);
			ImGui::TextDisabled("Game render includes fluid; Other game passes is the difference. Editor UI and Present are excluded.");
		}
	}
	ImGui::End();
}

// ====== エディタ内で表示するシーン (タイトル / セレクト / デモ) ======
// App から呼ばれる: シーンに応じて、エディタで包むか各シーンの DrawEditor をそのまま呼ぶかを決める
void EditorUI::PresentScene(Engine::IScene& scene, const std::string& sceneName) {
	// プレイヤーが通るシーンとデモだけをエディタで包む。検証用シーンは独自の処理をそのまま使う
	// 包む対象のシーンなら、エディタで包んで表示する
	for (const auto& hosted : EditorConfig().hostedScenes) {
		if (sceneName == hosted) {
			ShowSceneHost(scene, sceneName);
			return;
		}
	}
	// それ以外は入力の換算を外し、シーン自身のエディタ描画を呼ぶ
	if (auto* input = Engine::Input::GetInstance()) input->ClearPointerViewport();
	scene.DrawEditor();
}

// GameScene 以外のシーン用のエディタ (メニュー・ツールバー・ステータスバー・Game ビュー)
void EditorUI::ShowSceneHost(Engine::IScene& scene, const std::string& sceneName) {
	// エディタの経過時間を進める
	globalTime += ImGui::GetIO().DeltaTime;
	// 描画の窓口
	auto* renderer = Engine::Renderer::GetInstance();
	// カーソルは常に矢印
	ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
	// Ctrl+R で読み込み直す
	HandleSceneShortcuts();

	// メインメニュー: Scenes と Window (このシーンで使える欄だけ)
	if (ImGui::BeginMainMenuBar()) {
		DrawSceneLaunchMenu();
		if (ImGui::BeginMenu("Window")) {
			ImGui::MenuItem(kProjectWindow, nullptr, &s_windows.project);
			ImGui::MenuItem(kConsoleWindow, nullptr, &s_windows.console);
			ImGui::MenuItem(kLightingWindow, nullptr, &s_windows.lighting);
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout")) s_resetLayoutRequested = true;
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	// ツールバー: シーン切り替え・再読み込み・最大化
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	// ツールバーの高さ
	const float toolbarHeight = ImGui::GetFrameHeight() + 8.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
	if (ImGui::BeginViewportSideBar("##MainToolbar", viewport, ImGuiDir_Up, toolbarHeight, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
		DrawSceneLaunchCombo();
		ImGui::SameLine();
		// 今のシーンを読み込み直すボタン
		if (ImGui::Button("Restart")) LaunchScene(sceneName.c_str(), Engine::SceneManager::GetInstance()->CurrentParameters().stagePath);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reload this scene (Ctrl+R)");
		ImGui::SameLine(0, 16);
		// 動いているシーン名
		ImGui::TextDisabled("Running: %s", sceneName.c_str());
		// 右端に Maximize のチェック
		const float rightWidth = ImGui::CalcTextSize("Maximize").x + ImGui::GetFrameHeight() + 24.0f;
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - rightWidth));
		ImGui::Checkbox("Maximize", &s_maximizeOnPlay);
	}
	ImGui::End();
	ImGui::PopStyleVar();

	// ステータスバー: 最新のログと、エラー数・警告数・シーン名・FPS
	const float statusHeight = ImGui::GetFrameHeight() + 2.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
	if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, statusHeight, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
		if (!consoleLog.empty()) ImGui::TextDisabled("%s", consoleLog.back().message.c_str());
		char right[128];
		snprintf(right, sizeof(right), "%d errors  %d warnings   |   %s   |   %.0f FPS",
			CountLogs(LogLevel::Error), CountLogs(LogLevel::Warning), sceneName.c_str(), ImGui::GetIO().Framerate);
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - ImGui::CalcTextSize(right).x - 12.0f));
		ImGui::TextDisabled("%s", right);
	}
	ImGui::End();
	ImGui::PopStyleVar();

	// 作業領域いっぱいの、枠もタイトルも無い親ウィンドウを作る
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("EditorMain", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
	ImGui::PopStyleVar(3);
	// ドックスペース (初回か Reset Layout のときは既定の配置を組む)
	const ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
	if (s_resetLayoutRequested || ImGui::FindWindowSettingsByID(ImHashStr(kInspectorWindow)) == nullptr) {
		s_windows = EditorWindows{};
		for (auto& ext : Extensions()) ext.open = true;
		BuildDefaultLayout(dockspaceId, viewport->WorkSize, Extensions());
		s_resetLayoutRequested = false;
	}
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

	// シーンが変わったら Game タブを前に出す (Scene ビューはゲームシーン専用)
	static std::string lastScene;
	static bool focusGame = false;
	if (lastScene != sceneName) {
		lastScene = sceneName;
		focusGame = true;
	}
	if (focusGame && ImGui::FindWindowByName(kGameWindow)) {
		ImGui::SetWindowFocus(kGameWindow);
		focusGame = false;
	}

	// 最大化していないときだけ各ウィンドウを出す
	if (!s_maximizeOnPlay) {
		// 既定レイアウトのドックが空にならないよう、ゲームシーン専用の欄も説明付きで出しておく
		if (ImGui::Begin(kSceneWindow)) DrawCodeSceneNotice("The Scene view");
		ImGui::End();
		if (s_windows.hierarchy) {
			if (ImGui::Begin(kHierarchyWindow, &s_windows.hierarchy)) DrawCodeSceneNotice("The Hierarchy");
			ImGui::End();
		}
		if (s_windows.inspector) {
			if (ImGui::Begin(kInspectorWindow, &s_windows.inspector)) DrawCodeSceneNotice("The Inspector");
			ImGui::End();
		}
		if (s_windows.animation) {
			if (ImGui::Begin(kAnimationWindow, &s_windows.animation)) DrawCodeSceneNotice("The Animation list");
			ImGui::End();
		}
		if (s_windows.lighting && renderer) {
			if (ImGui::Begin(kLightingWindow, &s_windows.lighting)) ShowSceneSettings(renderer);
			ImGui::End();
		}
		// Console と Project (Project はシーン無しで使う)
		if (s_windows.console) ShowConsole();
		if (s_windows.project) ShowProject(renderer, nullptr);
		// 拡張のウィンドウ
		for (auto& ext : Extensions()) {
			if (!ext.open || !ext.drawWindow) continue;
			if (ImGui::Begin(ext.windowName.c_str(), &ext.open)) ext.drawWindow(nullptr);
			ImGui::End();
		}
	}

	// Game ビュー: シーンの最終画像を表示し、マウスをその範囲で換算してゲーム UI を操作できるようにする
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	bool gameVisible;
	// 最大化なら作業領域全体に、そうでなければ Game ウィンドウに出す
	if (s_maximizeOnPlay) {
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		gameVisible = ImGui::Begin("##MaximizedHostGame", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
	} else {
		gameVisible = ImGui::Begin(kGameWindow);
	}
	// 見えていれば、縦横比と Stats の切り替えを上に並べる
	if (gameVisible && renderer) {
		ImGui::SetCursorPos(ImVec2(6.0f, 4.0f));
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::BeginCombo("##HostAspect", aspects[currentAspect])) {
			for (int n = 0; n < IM_ARRAYSIZE(aspects); n++) {
				if (ImGui::Selectable(aspects[n], currentAspect == n)) currentAspect = n;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Stats", &s_showStats);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

		// 使える領域
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float availW = (std::max)(avail.x, 1.0f), availH = (std::max)(avail.y, 1.0f);
		// 縦横比 (既定は内部解像度の比)
		float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
		if (currentAspect > 0 && aspectValues[currentAspect] > 0.0f) aspect = aspectValues[currentAspect];
		// 領域に収まる大きさ
		const float w = availW / availH > aspect ? availH * aspect : availW;
		const float h = availW / availH > aspect ? availH : availW / aspect;
		// 中央に置いて画像を表示する
		const ImVec2 cursor = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(cursor.x + (availW - w) * 0.5f, cursor.y + (availH - h) * 0.5f));
		ImGui::Image((ImTextureID)renderer->GetGameFinalSRV().ptr, ImVec2(w, h));
		// 画像の範囲を覚える
		gameImageMin = ImGui::GetItemRectMin();
		gameImageMax = ImGui::GetItemRectMax();
		// ImGui の座標はクライアント座標なので、そのまま Input の換算範囲に使える
		if (auto* input = Engine::Input::GetInstance()) input->SetPointerViewport(gameImageMin.x, gameImageMin.y, w, h);
		// Stats: FPS と表示の大きさを左上に重ねる
		if (s_showStats) {
			char stats[128];
			snprintf(stats, sizeof(stats), "%.1f FPS (%.2f ms)\nView: %.0f x %.0f", ImGui::GetIO().Framerate,
				1000.0f / (std::max)(ImGui::GetIO().Framerate, 1.0f), w, h);
			// 半透明の黒い板の上に文字を書く
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 at(gameImageMin.x + 8.0f, gameImageMin.y + 8.0f), size = ImGui::CalcTextSize(stats);
			draw->AddRectFilled(at, ImVec2(at.x + size.x + 16.0f, at.y + size.y + 12.0f), IM_COL32(0, 0, 0, 170), 4.0f);
			draw->AddText(ImVec2(at.x + 8.0f, at.y + 6.0f), IM_COL32(230, 230, 230, 255), stats);
		}
	}
	// Game ウィンドウを閉じる
	ImGui::End();
	ImGui::PopStyleVar();

	ImGui::End(); // EditorMain

	// シーン固有のツール (Assignment の GPU Particle Editor など)
	scene.DrawEditor();
}

// ====== Scene / Game のビュー ======
// 描画先は1枚だけなので、表示中のタブのカメラで描画する (Unity の Scene / Game タブ相当)。
// Scene : エディタカメラ。右ドラッグ + WASD/QE で飛行、ホイールで前後、中ボタンで平行移動、F で選択物へ
// Game  : ゲーム (プレイヤー) カメラ。実際のプレイ画面

// ビュー行列から位置と向きを取り出して、エディタカメラをゲームカメラの視点に合わせる
// (ゲーム側は LookAt で行列だけ設定することがあり、Rotation() が実際の向きと一致しないため)
static void AlignEditorCameraToGame(GameScene* scene) {
	// ゲームカメラのワールド行列 (ビュー行列の逆行列)
	DirectX::XMMATRIX world = DirectX::XMMatrixInverse(nullptr, scene->GetCamera().View());
	// 位置と前方向
	DirectX::XMFLOAT3 position, forward;
	DirectX::XMStoreFloat3(&position, world.r[3]);
	DirectX::XMStoreFloat3(&forward, DirectX::XMVector3Normalize(world.r[2]));
	// エディタのカメラに位置と向き (上下と左右の角度) を設定する
	auto& camera = scene->GetEditorCamera();
	camera.SetPosition(position);
	camera.SetRotation(-std::asin(std::clamp(forward.y, -1.0f, 1.0f)), std::atan2(forward.x, forward.z), 0.0f);
}

// Scene ビューのカメラ操作 (右ドラッグで飛行、ホイール、中ボタンで平行移動、F で選択物へ)
void EditorUI::UpdateSceneCamera(GameScene* scene, bool hovered) {
	ImGuiIO& io = ImGui::GetIO();
	// エディタのカメラ
	auto& camera = scene->GetEditorCamera();
	// 最初の 1 回だけゲームカメラの視点に合わせる
	if (!s_editorCameraPlaced) {
		AlignEditorCameraToGame(scene);
		s_editorCameraPlaced = true;
	}
	// 画像の上で右ボタンを押したら飛行を始め、離したら終わる
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) s_sceneCameraNavigating = true;
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) s_sceneCameraNavigating = false;

	// 今の位置と回転
	DirectX::XMFLOAT3 position = camera.Position();
	DirectX::XMFLOAT3 rotation = camera.Rotation();
	// カメラの向きの回転行列
	const DirectX::XMMATRIX basis = DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, 0.0f);
	// カメラから見た向きで移動する
	auto move = [&](DirectX::XMFLOAT3 local) {
		DirectX::XMFLOAT3 world;
		DirectX::XMStoreFloat3(&world, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&local), basis));
		position = {position.x + world.x, position.y + world.y, position.z + world.z};
	};
	// 変わったか / Shift で 4 倍速
	bool changed = false;
	const float boost = io.KeyShift ? 4.0f : 1.0f;

	if (s_sceneCameraNavigating) {
		// マウスで向きを変える (上下は ±89 度くらいまで)
		rotation.y += io.MouseDelta.x * 0.004f;
		rotation.x = std::clamp(rotation.x + io.MouseDelta.y * 0.004f, -1.55f, 1.55f);
		// 飛行中のホイールは速度の変更 (Unity と同じ)
		if (io.MouseWheel != 0.0f) s_sceneCameraSpeed = std::clamp(s_sceneCameraSpeed * (io.MouseWheel > 0 ? 1.25f : 0.8f), 1.0f, 2000.0f);
		// 1 フレームの移動量
		const float step = s_sceneCameraSpeed * boost * io.DeltaTime;
		DirectX::XMFLOAT3 local{0, 0, 0};
		// W/S で前後、D/A で左右、E/Q で上下
		if (ImGui::IsKeyDown(ImGuiKey_W)) local.z += step;
		if (ImGui::IsKeyDown(ImGuiKey_S)) local.z -= step;
		if (ImGui::IsKeyDown(ImGuiKey_D)) local.x += step;
		if (ImGui::IsKeyDown(ImGuiKey_A)) local.x -= step;
		if (ImGui::IsKeyDown(ImGuiKey_E)) local.y += step;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) local.y -= step;
		move(local);
		changed = true;
	} else if (hovered && io.MouseWheel != 0.0f) {
		// 飛行していないときのホイールは前後に動く
		move({0, 0, io.MouseWheel * s_sceneCameraSpeed * 0.25f * boost});
		changed = true;
	}
	if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
		// 中ボタンのドラッグで平行移動
		const float pan = s_sceneCameraSpeed * 0.01f * boost;
		move({-io.MouseDelta.x * pan, io.MouseDelta.y * pan, 0});
		changed = true;
	}
	// F: 選択中のオブジェクトを正面に捉える (Hierarchy のダブルクリックや Edit メニューからも要求される)
	const bool frameRequested = s_frameSelectedRequest || (hovered && !io.WantTextInput && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false));
	// 要求は 1 回で消す
	s_frameSelectedRequest = false;
	if (frameRequested) {
		// 選択中の物 (位置があるときだけ)
		auto selected = scene->GetSelectedEntity();
		if (selected != entt::null && scene->GetComponents().valid(selected) && scene->GetComponents().all_of<TransformComponent>(selected)) {
			// ワールド行列と大きさ
			const auto world = scene->GetWorldMatrix(static_cast<int>(selected));
			const auto& tc = scene->GetComponents().get<TransformComponent>(selected);
			const float size = (std::max)({std::abs(tc.scale.x), std::abs(tc.scale.y), std::abs(tc.scale.z), 1.0f});
			// カメラの前方向
			DirectX::XMFLOAT3 forward;
			DirectX::XMStoreFloat3(&forward, DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), basis));
			// 大きさに合わせた距離だけ後ろへ下がる
			const float distance = size * 4.0f + 4.0f;
			position = {world.m[3][0] - forward.x * distance, world.m[3][1] - forward.y * distance, world.m[3][2] - forward.z * distance};
			changed = true;
		}
	}
	// 変わったらカメラに反映する
	if (changed) {
		camera.SetRotation(rotation);
		camera.SetPosition(position);
	}
}

// Scene ビュー左上のツールバー (ゲームカメラに合わせる・移動速度・操作の案内)
void EditorUI::DrawSceneToolbar(GameScene* scene, const ImVec2& imageMin) {
	// 画像の左上に置く
	ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 6.0f, imageMin.y + 6.0f));
	// ボタンを半透明にする
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.12f, 0.75f));
	if (ImGui::SmallButton("Align to Game Camera")) AlignEditorCameraToGame(scene);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(110.0f);
	// 移動速度 (対数のスライダー)
	ImGui::SliderFloat("##CameraSpeed", &s_sceneCameraSpeed, 1.0f, 2000.0f, "speed %.0f", ImGuiSliderFlags_Logarithmic);
	ImGui::PopStyleColor();
	ImGui::SameLine();
	// 操作の案内
	ImGui::TextDisabled("RMB+WASD/QE  Wheel  MMB  F");
}

// もう一方のビューが描画中であることの案内と、こちらへ切り替えるボタン
static void DrawInactiveViewNotice(const char* active, bool& switchHere) {
	// 左上に案内を書く
	ImGui::SetCursorPos(ImVec2(16.0f, 16.0f));
	ImGui::TextWrapped("The %s view is being rendered. Only one view is rendered per frame.", active);
	ImGui::SetCursorPosX(16.0f);
	// 押されたらこちらへ切り替える
	switchHere = ImGui::Button("Render this view");
}

// Scene / Game ウィンドウの管理 (表示中のビューを決め、そのビューだけ描く)
void EditorUI::ShowViewports(Engine::Renderer* renderer, GameScene* gameScene, bool maximized) {
	// ビューの種類
	using View = GameScene::EditorView;
	// 表示中のビュー / PLAY 前のビュー / 前のフレームで Game が見えていたか / 前のフレームで PLAY 中だったか / 初回か / 固定する残りフレーム
	static View activeView = View::Game;
	static View viewBeforePlay = View::Scene;
	static bool gameVisibleLastFrame = true;
	static bool wasPlaying = false;
	static bool firstFrame = true;
	static int pinFrames = 0;

	// Unity と同様に PLAY 開始で Game タブへ、停止で元のタブへ戻す (起動時に既に PLAY 中でも Game から始める)
	// PLAY 中か
	const bool playing = gameScene->GetIsPlaying();
	if (playing != wasPlaying || firstFrame) {
		if (playing) {
			// PLAY 開始: 今のビューを覚えて Game へ
			if (!firstFrame) viewBeforePlay = activeView;
			RequestView(View::Game);
		// PLAY 停止: 元が Scene なら Scene へ戻す
		} else if (!firstFrame && viewBeforePlay == View::Scene) {
			RequestView(View::Scene);
		}
		wasPlaying = playing;
		firstFrame = false;
	}
	// ウィンドウがまだ作られていない (初回・レイアウト再構築直後) 間は要求を保持し、
	// タブの切り替えが反映されるまでの数フレームは表示中のビューを固定する
	if (s_viewRequested) {
		// 要求されたビューのウィンドウ名
		const char* name = s_requestedView == View::Scene ? kSceneWindow : kGameWindow;
		activeView = s_requestedView;
		pinFrames = 3;
		// ウィンドウがあれば前面に出し、要求を消す
		if (ImGui::FindWindowByName(name)) {
			ImGui::SetWindowFocus(name);
			s_viewRequested = false;
		}
	}
	// 固定中か
	const bool pinned = pinFrames > 0;
	if (pinFrames > 0) --pinFrames;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (maximized) {
		// Maximize On Play: ドックを使わず作業領域全体に Game ビューを出す
		// メインのビューポート
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		if (ImGui::Begin("##MaximizedGame", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
			activeView = View::Game;
			DrawViewport(renderer, gameScene, false);
		}
		ImGui::End();
		ImGui::PopStyleVar();
		// 表示中のビューを伝え、カーソルの強制表示を外す
		gameScene->SetEditorView(activeView);
		Engine::WindowDX::SetCursorForcedVisible(false);
		return;
	}
	// 初回は Game ウィンドウと同じ場所にタブとして並べる
	if (ImGuiWindow* game = ImGui::FindWindowByName("Game"); game && game->DockId != 0) {
		ImGui::SetNextWindowDockID(game->DockId, ImGuiCond_FirstUseEver);
	}
	// Scene ウィンドウ
	const bool sceneVisible = ImGui::Begin("Scene");
	if (sceneVisible) {
		bool switchHere = false;
		// Scene が表示中か、Game が隠れていれば Scene を描く
		if (activeView == View::Scene || (!gameVisibleLastFrame && !pinned)) {
			activeView = View::Scene;
			DrawViewport(renderer, gameScene, true);
		// そうでなければ案内を出す
		} else {
			DrawInactiveViewNotice("Game", switchHere);
			if (switchHere) activeView = View::Scene;
		}
	}
	ImGui::End();

	// Game ウィンドウ
	const bool gameVisible = ImGui::Begin("Game");
	if (gameVisible) {
		bool switchHere = false;
		// Game が表示中か、Scene が隠れていれば Game を描く
		if (activeView == View::Game || (!sceneVisible && !pinned)) {
			activeView = View::Game;
			DrawViewport(renderer, gameScene, false);
		// そうでなければ案内を出す
		} else {
			DrawInactiveViewNotice("Scene", switchHere);
			if (switchHere) activeView = View::Game;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
	gameVisibleLastFrame = gameVisible;

	gameScene->SetEditorView(activeView);
	// PLAY 中はゲームがカーソルを隠すが、Scene ビューを操作する間は表示する
	Engine::WindowDX::SetCursorForcedVisible(playing && activeView == View::Scene);
}

// ビューの中身: 画像・ツールバー・Stats・ドラッグ&ドロップ・ギズモ・UI の編集・クリック選択
void EditorUI::DrawViewport(Engine::Renderer* renderer, GameScene* gameScene, bool sceneView) {
	if (!sceneView) {
		// Game ビューのツールバー (Unity と同じく縦横比と Stats はここで切り替える)
		// 縦横比の選択
		ImGui::SetCursorPos(ImVec2(6.0f, 4.0f));
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::BeginCombo("##Aspect", aspects[currentAspect])) {
			for (int n = 0; n < IM_ARRAYSIZE(aspects); n++) {
				if (ImGui::Selectable(aspects[n], currentAspect == n)) currentAspect = n;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		// Stats と Maximize On Play
		ImGui::Checkbox("Stats", &s_showStats);
		ImGui::SameLine();
		ImGui::Checkbox("Maximize On Play", &s_maximizeOnPlay);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
	}
	
	// 使える領域
	ImVec2 av = ImGui::GetContentRegionAvail();
	// ドッキング直後やウィンドウを畳んだ時は 0 になり、以降の除算が NaN/Inf になる
	float tW = (std::max)(av.x, 1.0f), tH = (std::max)(av.y, 1.0f);
	
	// 縦横比を決めてカメラの投影を更新する
	// 内部解像度の縦横比
	float nativeAspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
	float targetAspect = nativeAspect; // 既定は内部解像度の比 (2D や 3D が潰れないように)
	
	// Game ビューで縦横比が指定されていればそれを使う
	if (!sceneView && currentAspect > 0 && aspectValues[currentAspect] > 0.0f) {
		targetAspect = aspectValues[currentAspect];
	}

	// 表示する大きさと、使える領域の縦横比
	float renderW = tW, renderH = tH;
	float currentAvailAspect = tW / tH;
	
	// 描画先 (固定サイズ) が伸び縮みしないよう、常に縦横比を保って余白を付ける
	if (currentAvailAspect > targetAspect) {
		renderW = tH * targetAspect;
		renderH = tH;
	} else {
		renderW = tW;
		renderH = tW / targetAspect;
	}
	// Scene ビューはエディタカメラ、Game ビューはゲームカメラ。Scene ビューの縦横比は常に描画先と同じにする
	Engine::Camera& viewCamera = sceneView ? gameScene->GetEditorCamera() : gameScene->GetCamera();
	viewCamera.SetProjection(0.7854f, targetAspect, 0.1f, sceneView ? 10000.0f : 1000.0f);
	
	// 画像を中央に置く
	ImVec2 cursorPadding((tW - renderW) * 0.5f, (tH - renderH) * 0.5f);
	ImVec2 currentCursorPos = ImGui::GetCursorPos();
	float targetIdxX = currentCursorPos.x + cursorPadding.x;
	float targetIdxY = currentCursorPos.y + cursorPadding.y;
	ImGui::SetCursorPos(ImVec2(targetIdxX, targetIdxY));
	
	// 描画した画像を表示する
	ImGui::Image((ImTextureID)renderer->GetGameFinalSRV().ptr, ImVec2(renderW, renderH));
	
	// 実際に画像を描いた画面上の範囲を取得する
	gameImageMin = ImGui::GetItemRectMin();
	gameImageMax = ImGui::GetItemRectMax();
	const bool imageHovered = ImGui::IsItemHovered();
	// Scene ビュー左上のツールバー。ここをクリックしてもピッキングしない
	const ImVec2 toolbarMax(gameImageMin.x + 420.0f, gameImageMin.y + ImGui::GetFrameHeightWithSpacing() + 8.0f);
	const bool overToolbar = sceneView && ImGui::IsMouseHoveringRect(gameImageMin, toolbarMax);
	if (sceneView) UpdateSceneCamera(gameScene, imageHovered && !overToolbar);
	
	// UISystem などが座標を換算できるよう、ビューの情報をシーンへ渡す
	// 画像の上のマウスの位置を、内部解像度の座標に換算する
	// 既存のピクセル中心の座標の対応関係を保つ
	ImVec2 mPos_img = ImGui::GetMousePos();
	const float internalMouseX = (mPos_img.x - gameImageMin.x) * (float)Engine::WindowDX::kW / renderW;
	const float internalMouseY = (mPos_img.y - gameImageMin.y) * (float)Engine::WindowDX::kH / renderH;
	// Scene ビューではゲーム内 UI (調合室のボタンなど) がマウスに反応しないよう、画面外の座標を渡す
	gameScene->SetEditorViewport({gameImageMin.x, gameImageMin.y}, {renderW, renderH},
		sceneView ? -100000.0f : internalMouseX, sceneView ? -100000.0f : internalMouseY);

	// ツールのオーバーレイを画像の上に描く (Stats・拡張のオーバーレイ)
	// Stats: FPS・物の数・表示と描画の大きさを左上に重ねる
	if (!sceneView && s_showStats) {
		const ImVec2 at(gameImageMin.x + 8.0f, gameImageMin.y + 8.0f);
		char stats[256];
		snprintf(stats, sizeof(stats), "%.1f FPS (%.2f ms)\nObjects: %d\nView: %.0f x %.0f (render %u x %u)",
			ImGui::GetIO().Framerate, 1000.0f / (std::max)(ImGui::GetIO().Framerate, 1.0f),
			static_cast<int>(gameScene->GetComponents().view<NameComponent>().size()), renderW, renderH,
			Engine::WindowDX::kW, Engine::WindowDX::kH);
		const ImVec2 textSize = ImGui::CalcTextSize(stats);
		ImDrawList* statsDraw = ImGui::GetWindowDrawList();
		statsDraw->AddRectFilled(at, ImVec2(at.x + textSize.x + 16.0f, at.y + textSize.y + 12.0f), IM_COL32(0, 0, 0, 170), 4.0f);
		statsDraw->AddText(ImVec2(at.x + 8.0f, at.y + 6.0f), IM_COL32(230, 230, 230, 255), stats);
	}
	// 拡張のオーバーレイ
	for (auto& ext : Extensions()) {
		if (ext.open && ext.drawViewportOverlay) ext.drawViewportOverlay(gameScene, gameImageMin, gameImageMax);
	}
	
	// Project からのドラッグ&ドロップ: プレハブは配置、モデルはメッシュ付きエンティティを作る
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
			// 渡されたパスと、その拡張子 (小文字)
			std::string sPath = (const char*)payload->Data;
			std::replace(sPath.begin(), sPath.end(), '\\', '/'); // 正規化
			const std::string ext = ToLower(Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(sPath)).extension().wstring()));
			// プレハブなら読み込んで、最初のものを選ぶ
			if (ext == ".prefab") {
				auto created = LoadPrefab(gameScene, sPath);
				if (!created.empty()) gameScene->SelectEntity(created.front());
			} else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") {
				// モデルなら、ファイル名を名前にして作り、メッシュと白いテクスチャを付ける
				auto e = gameScene->CreateEntity(Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(sPath)).stem().wstring()));
				auto& mr = gameScene->GetComponents().emplace<MeshRendererComponent>(e);
				mr.modelPath = sPath;
				mr.modelHandle = renderer->LoadObjMesh(sPath);
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
				gameScene->SelectEntity(e);
			}
		}
		ImGui::EndDragDropTarget();
	}
	// クリック選択とギズモ
	// ギズモに触れているか・PLAY 中か・選択中の物
	bool gizmoHovered = false;
	bool isPlaying = gameScene->GetIsPlaying();
	auto selectedEnt = gameScene->GetSelectedEntity();

	// TransformComponent を持たないエンティティ (UI だけの物など) で get<> が assert しないよう先に確認する
	// 3D の移動ギズモは Scene ビューだけ。Game ビューでは UI の配置だけ編集できる
	if (!isPlaying && selectedEnt != entt::null && gameScene->GetComponents().valid(selectedEnt) &&
		gameScene->GetComponents().all_of<TransformComponent>(selectedEnt)) {
		// このシーンの ECS と、選択中の物のモーションと位置
		auto& reg = gameScene->GetComponents();
		auto* mc = reg.try_get<MotionComponent>(selectedEnt);
		auto& tc = reg.get<TransformComponent>(selectedEnt);
		
		DirectX::XMFLOAT3 currentPos;
		// モーションのキーフレームを選択中なら、そのキーフレームの位置にギズモを出す
		if (mc && mc->selectedKeyframe >= 0 && mc->clips.count(mc->activeClip)) {
			auto& kfs = mc->clips.at(mc->activeClip).keyframes;
			if (mc->selectedKeyframe < (int)kfs.size()) {
				currentPos = kfs[mc->selectedKeyframe].translate;
			} else {
				currentPos = tc.translate;
			}
		} else {
			currentPos = tc.translate;
		}

		// 位置とカメラの行列
		DirectX::XMVECTOR objPos3D = DirectX::XMLoadFloat3(&currentPos);
		DirectX::XMMATRIX view = viewCamera.View();
		DirectX::XMMATRIX proj = viewCamera.Proj();
		
		// オブジェクトの位置を画面座標へ投影する
		DirectX::XMVECTOR projP = DirectX::XMVector3Project(objPos3D, 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
		// 画面上の位置と奥行き
		float px = DirectX::XMVectorGetX(projP) + gameImageMin.x;
		float py = DirectX::XMVectorGetY(projP) + gameImageMin.y;
		float pz = DirectX::XMVectorGetZ(projP);

		if (sceneView && pz > 0.0f && pz < 1.0f) { // カメラの後ろや遠すぎる場合は描かない
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			// ギズモの原点・軸の長さ・当たりの半径・マウスの位置
			ImVec2 origin(px, py);
			float gizmoLen = 60.0f;
			float hitRadius = 15.0f;
			ImVec2 mousePos = ImGui::GetMousePos();
			
			// X・Y・Z 軸の単位ベクトルを画面へ投影する
			DirectX::XMVECTOR pX = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(1,0,0,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			DirectX::XMVECTOR pY = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(0,1,0,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			DirectX::XMVECTOR pZ = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(0,0,1,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			
			// 原点から各軸の先への画面上の向き
			ImVec2 dirX(DirectX::XMVectorGetX(pX) + gameImageMin.x - px, DirectX::XMVectorGetY(pX) + gameImageMin.y - py);
			ImVec2 dirY(DirectX::XMVectorGetX(pY) + gameImageMin.x - px, DirectX::XMVectorGetY(pY) + gameImageMin.y - py);
			ImVec2 dirZ(DirectX::XMVectorGetX(pZ) + gameImageMin.x - px, DirectX::XMVectorGetY(pZ) + gameImageMin.y - py);
			
			// 画面上の向きとして正規化する
			auto normalize = [](ImVec2 v) { float len = std::sqrt(v.x*v.x + v.y*v.y); if (len>0) { v.x/=len; v.y/=len; } return v; };
			dirX = normalize(dirX); dirY = normalize(dirY); dirZ = normalize(dirZ);

			// 各軸の先端
			ImVec2 endX(origin.x + dirX.x * gizmoLen, origin.y + dirX.y * gizmoLen);
			ImVec2 endY(origin.x + dirY.x * gizmoLen, origin.y + dirY.y * gizmoLen);
			ImVec2 endZ(origin.x + dirZ.x * gizmoLen, origin.y + dirZ.y * gizmoLen);

			// 点から線分までの距離
			auto distToSegment = [](ImVec2 p, ImVec2 a, ImVec2 b) {
				ImVec2 pa(p.x - a.x, p.y - a.y), ba(b.x - a.x, b.y - a.y);
				float lenSq = ba.x*ba.x + ba.y*ba.y;
				float h = lenSq > 1e-6f ? std::clamp((pa.x*ba.x + pa.y*ba.y) / lenSq, 0.0f, 1.0f) : 0.0f;
				float dx = pa.x - ba.x * h, dy = pa.y - ba.y * h;
				return std::sqrt(dx*dx + dy*dy);
			};

			// 触れている軸 (ドラッグ中はその軸)
			int hoveredAxis = -1;
			if (!gizmo_.Dragging()) {
				if (distToSegment(mousePos, origin, endX) < hitRadius) hoveredAxis = 0;
				else if (distToSegment(mousePos, origin, endY) < hitRadius) hoveredAxis = 1;
				else if (distToSegment(mousePos, origin, endZ) < hitRadius) hoveredAxis = 2;
			} else {
				hoveredAxis = gizmo_.Axis();
			}

			// どれかの軸に触れていれば、背面のクリック選択をしない
			if (hoveredAxis != -1) gizmoHovered = true;

			// 入力: 軸の上で押したらドラッグ開始
			if (gizmoHovered && ImGui::IsMouseClicked(0)) {
				gizmo_.BeginDrag(hoveredAxis);
				gizmoDragStartMouse = mousePos;
				gizmoStartTranslate = currentPos;
				gizmoStartRotate = {0,0,0}; // キーフレームでは回転を使わない
				gizmoStartScale = {1,1,1};
			}

			if (gizmo_.Dragging()) {
				if (ImGui::IsMouseReleased(0)) {
					// Undo の記録
					// 今の値と、ドラッグを始めたときの値
					DirectX::XMFLOAT3 endT = tc.translate;
					DirectX::XMFLOAT3 endR = tc.rotate;
					DirectX::XMFLOAT3 endS = tc.scale;
					DirectX::XMFLOAT3 startT = gizmoStartTranslate;
					DirectX::XMFLOAT3 startR = gizmoStartRotate;
					DirectX::XMFLOAT3 startS = gizmoStartScale;
					entt::entity e = selectedEnt;

					// Undo / Redo を登録する (始めの値に戻す / 今の値にする)
					PushUndo({
						"Transform",
						[=, &reg = gameScene->GetComponents()]() {
							if (reg.valid(e)) {
								auto& t = reg.get<TransformComponent>(e);
								t.translate = startT; t.rotate = startR; t.scale = startS;
							}
						},
						[=, &reg = gameScene->GetComponents()]() {
							if (reg.valid(e)) {
								auto& t = reg.get<TransformComponent>(e);
								t.translate = endT; t.rotate = endR; t.scale = endS;
							}
						}
					});

					// ドラッグを終える
					gizmo_.EndDrag();
				} else if (ImGui::IsMouseDragging(0)) {
					// 前のフレームからのマウスの移動量
					ImVec2 delta = ImGui::GetMouseDragDelta(0);
					ImGui::ResetMouseDragDelta(0);
					
					// 軸の向きへの移動量 (画面上)
					ImVec2 axisDir = (gizmo_.Axis() == 0) ? dirX : ((gizmo_.Axis() == 1) ? dirY : dirZ);
					float projMovement = delta.x * axisDir.x + delta.y * axisDir.y;
					
					// ツールごとの簡易的な感度
					float sensitivity = (gizmo_.Mode() == GizmoMode::Rotate) ? 0.02f : 0.05f;
					if (gizmo_.Mode() == GizmoMode::Scale) sensitivity = 0.01f;

					// 動かす対象 (キーフレームを選択中ならその位置)
					DirectX::XMFLOAT3* targetPos = &reg.get<TransformComponent>(selectedEnt).translate;
					if (mc && mc->selectedKeyframe >= 0 && mc->clips.count(mc->activeClip)) {
						auto& kfs = mc->clips.at(mc->activeClip).keyframes;
						if (mc->selectedKeyframe < (int)kfs.size()) {
							targetPos = &kfs[mc->selectedKeyframe].translate;
						}
					}
					// 回転と大きさ (キーフレームでは使わない)
					auto* targetRot = &reg.get<TransformComponent>(selectedEnt).rotate;
					auto* targetScale = &reg.get<TransformComponent>(selectedEnt).scale;

					// 移動: 軸の方向へ動かす
					if (gizmo_.Mode() == GizmoMode::Translate) {
						if (gizmo_.Axis() == 0) targetPos->x += projMovement * sensitivity;
						if (gizmo_.Axis() == 1) targetPos->y -= projMovement * sensitivity; // 画面の Y は上下が逆
						if (gizmo_.Axis() == 2) targetPos->z += projMovement * sensitivity;
					// 回転: 軸のまわりに回す
					} else if (gizmo_.Mode() == GizmoMode::Rotate && !mc) {
						if (gizmo_.Axis() == 0) targetRot->x += projMovement * sensitivity;
						if (gizmo_.Axis() == 1) targetRot->y -= projMovement * sensitivity;
						if (gizmo_.Axis() == 2) targetRot->z += projMovement * sensitivity;
					// 拡大: 軸の方向へ大きくする
					} else if (gizmo_.Mode() == GizmoMode::Scale && !mc) {
						if (gizmo_.Axis() == 0) targetScale->x += projMovement * sensitivity;
						if (gizmo_.Axis() == 1) targetScale->y -= projMovement * sensitivity;
						if (gizmo_.Axis() == 2) targetScale->z += projMovement * sensitivity;
					}
				}
			}

			// 軸を描く (触れている軸は黄色)
			// 各軸の色 (X 赤・Y 緑・Z 青、触れている軸は黄色)
			ImU32 colX = (hoveredAxis == 0) ? IM_COL32(255,255,0,255) : IM_COL32(255,50,50,255);
			ImU32 colY = (hoveredAxis == 1) ? IM_COL32(255,255,0,255) : IM_COL32(50,255,50,255);
			ImU32 colZ = (hoveredAxis == 2) ? IM_COL32(255,255,0,255) : IM_COL32(50,50,255,255);

			// 軸の線
			drawList->AddLine(origin, endX, colX, 3.0f);
			drawList->AddLine(origin, endY, colY, 3.0f);
			drawList->AddLine(origin, endZ, colZ, 3.0f);
			
			// 移動は先端に三角、拡大は先端に四角
			if (gizmo_.Mode() == GizmoMode::Translate) {
				drawList->AddTriangleFilled(endX, ImVec2(endX.x - dirX.x*10 - dirX.y*5, endX.y - dirX.y*10 + dirX.x*5), ImVec2(endX.x - dirX.x*10 + dirX.y*5, endX.y - dirX.y*10 - dirX.x*5), colX);
				drawList->AddTriangleFilled(endY, ImVec2(endY.x - dirY.x*10 - dirY.y*5, endY.y - dirY.y*10 + dirY.x*5), ImVec2(endY.x - dirY.x*10 + dirY.y*5, endY.y - dirY.y*10 - dirY.x*5), colY);
				drawList->AddTriangleFilled(endZ, ImVec2(endZ.x - dirZ.x*10 - dirZ.y*5, endZ.y - dirZ.y*10 + dirZ.x*5), ImVec2(endZ.x - dirZ.x*10 + dirZ.y*5, endZ.y - dirZ.y*10 - dirZ.x*5), colZ);
			} else if (gizmo_.Mode() == GizmoMode::Scale) {
				drawList->AddRectFilled(ImVec2(endX.x-4, endX.y-4), ImVec2(endX.x+4, endX.y+4), colX);
				drawList->AddRectFilled(ImVec2(endY.x-4, endY.y-4), ImVec2(endY.x+4, endY.y+4), colY);
				drawList->AddRectFilled(ImVec2(endZ.x-4, endZ.y-4), ImVec2(endZ.x+4, endZ.y+4), colZ);
			} else { // 回転
			}

			// モーションのキーフレームの点をクリックして選ぶ
			if (mc && !gizmoHovered) {
				if (ImGui::IsMouseClicked(0)) {
					// クリックした位置
					mousePos = ImGui::GetMousePos();
					float sx = mousePos.x, sy = mousePos.y;
					
					// カメラの行列を取り直す
					view = viewCamera.View();
					proj = viewCamera.Proj();

					if (mc->clips.count(mc->activeClip)) {
						// キーフレームを順に画面へ投影し、15 ピクセル以内なら選ぶ
						auto& kfs = mc->clips.at(mc->activeClip).keyframes;
						for (int i = 0; i < (int)kfs.size(); ++i) {
							DirectX::XMVECTOR p3D = DirectX::XMLoadFloat3(&kfs[i].translate);
							projP = DirectX::XMVector3Project(p3D, 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
							px = DirectX::XMVectorGetX(projP) + gameImageMin.x;
							py = DirectX::XMVectorGetY(projP) + gameImageMin.y;
							pz = DirectX::XMVectorGetZ(projP);

							if (pz > 0.0f && pz < 1.0f) {
								float dx = sx - px, dy = sy - py;
								if (std::sqrt(dx*dx + dy*dy) < 15.0f) {
									mc->selectedKeyframe = i;
									gizmoHovered = true; // オブジェクトの選択より優先する
									break;
								}
							}
						}
					}
				}
			}
		}

		// UI 部品のギズモ
		// UI 部品 (RectTransform) の編集: 枠と、移動・拡縮・判定範囲のハンドル
		if (auto* rt = reg.try_get<RectTransformComponent>(selectedEnt)) {
			// 描画先とマウスの位置
			ImDrawList* uiDrawList = ImGui::GetWindowDrawList();
			ImVec2 mPos = ImGui::GetMousePos();
				// UI の画面上の矩形 (内部解像度の座標)
				UISystem::WorldRect wr = UISystem::CalculateWorldRect(selectedEnt, gameScene->GetRegistry(), (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH);
				
				// 内部解像度 (1920x1080) から表示上のピクセルへの倍率
				float scaleX = renderW / (float)Engine::WindowDX::kW;
				float scaleY = renderH / (float)Engine::WindowDX::kH;

				// 表示上の矩形
				ImVec2 pMin(gameImageMin.x + wr.x * scaleX, gameImageMin.y + wr.y * scaleY);
				ImVec2 pMax(pMin.x + wr.w * scaleX, pMin.y + wr.h * scaleY);
				
				// UI 本体の枠 (橙)
				uiDrawList->AddRect(pMin, pMax, IM_COL32(255, 140, 0, 255), 0.0f, 0, 2.0f);
				
				// UIButton があればクリック判定の範囲も表示する (水色)
				if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
					// 判定範囲の大きさと中心 (倍率とずれから求める)
					float hw = wr.w * btn->hitboxScale.x;
					float hh = wr.h * btn->hitboxScale.y;
					float cx = wr.x + wr.w * 0.5f + btn->hitboxOffset.x;
					float cy = wr.y + wr.h * 0.5f + btn->hitboxOffset.y;
					float hx = cx - hw * 0.5f;
					float hy = cy - hh * 0.5f;

					// 表示上の判定範囲
					ImVec2 hpMin(gameImageMin.x + hx * scaleX, gameImageMin.y + hy * scaleY);
					ImVec2 hpMax(hpMin.x + hw * scaleX, hpMin.y + hh * scaleY);
					uiDrawList->AddRect(hpMin, hpMax, IM_COL32(0, 255, 255, 255), 0.0f, 0, 1.0f);

					// 判定範囲の移動ハンドル (中央の丸)
					ImVec2 hCenter((hpMin.x + hpMax.x) * 0.5f, (hpMin.y + hpMax.y) * 0.5f);
					uiDrawList->AddCircleFilled(hCenter, 6.0f, IM_COL32(0, 255, 255, 255));
					
					mPos = ImGui::GetMousePos();
					// マウスとの距離
					float dx_h = mPos.x - hCenter.x;
					float dy_h = mPos.y - hCenter.y;
					float dToHCenter = std::sqrt(dx_h * dx_h + dy_h * dy_h);

					if (dToHCenter < 10.0f) {
						gizmoHovered = true; // ハンドルに触れている間は背面のクリック選択をしない
						if (!uiDragging && ImGui::IsMouseClicked(0)) {
							uiDragging = true;
							uiDragHandle = 10; // 10 = 判定範囲の位置
							uiDragStartHitOffset = btn->hitboxOffset;
						}
					}
					
					// 判定範囲の拡縮ハンドル (右下の丸)
					// 右下の角
					ImVec2 hScaleHandle = hpMax;
					uiDrawList->AddCircleFilled(hScaleHandle, 6.0f, IM_COL32(0, 255, 255, 255));
					// マウスとの距離
					float dx_s = mPos.x - hScaleHandle.x;
					float dy_s = mPos.y - hScaleHandle.y;
					float dToHScale = std::sqrt(dx_s * dx_s + dy_s * dy_s);
					if (dToHScale < 10.0f) {
						gizmoHovered = true;
						if (!uiDragging && ImGui::IsMouseClicked(0)) {
							uiDragging = true;
							uiDragHandle = 11; // 11 = 判定範囲の大きさ
							uiDragStartHitScale = btn->hitboxScale;
						}
					}
				}

				// UI 本体の移動ハンドル (左上)
				uiDrawList->AddCircleFilled(pMin, 8.0f, IM_COL32(255, 140, 0, 255));
				
				mPos = ImGui::GetMousePos();
				// マウスとの距離
				float dx_p = mPos.x - pMin.x;
				float dy_p = mPos.y - pMin.y;
				float dToPMin = std::sqrt(dx_p * dx_p + dy_p * dy_p);
				if (dToPMin < 12.0f) {
					gizmoHovered = true;
					if (!uiDragging && ImGui::IsMouseClicked(0)) {
						uiDragging = true;
						uiDragHandle = 0; // 0 = UI の位置
						uiDragStartPos = rt->pos;
					}
				}

				// UI 本体の拡縮ハンドル (右下)
				uiDrawList->AddCircleFilled(pMax, 8.0f, IM_COL32(255, 140, 0, 255));
				// マウスとの距離
				float dx_m = mPos.x - pMax.x;
				float dy_m = mPos.y - pMax.y;
				float dToPMax = std::sqrt(dx_m * dx_m + dy_m * dy_m);
				if (dToPMax < 12.0f) {
					gizmoHovered = true;
					if (!uiDragging && ImGui::IsMouseClicked(0)) {
						uiDragging = true;
						uiDragHandle = 1; // 1 = UI の大きさ
						uiDragStartSize = rt->size;
					}
				}

				if (uiDragging) {
					gizmoHovered = true;
					// ドラッグの移動量を内部解像度の座標にする
					ImVec2 delta = ImGui::GetMouseDragDelta(0);
					float dx = delta.x / scaleX;
					float dy = delta.y / scaleY;

					// ドラッグ中:ハンドルの種類に応じて位置・大きさ・判定範囲を変える
					if (uiDragHandle == 0) { // UI の位置
						rt->pos.x = uiDragStartPos.x + dx;
						rt->pos.y = uiDragStartPos.y + dy;
					} else if (uiDragHandle == 1) { // UI の大きさ
						rt->size.x = (std::max)(1.0f, uiDragStartSize.x + dx);
						rt->size.y = (std::max)(1.0f, uiDragStartSize.y + dy);
					} else if (uiDragHandle == 10) { // 判定範囲の位置
						if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
							btn->hitboxOffset.x = uiDragStartHitOffset.x + dx;
							btn->hitboxOffset.y = uiDragStartHitOffset.y + dy;
						}
					} else if (uiDragHandle == 11) { // 判定範囲の大きさ
						if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
							btn->hitboxScale.x = (std::max)(0.01f, uiDragStartHitScale.x + dx / wr.w);
							btn->hitboxScale.y = (std::max)(0.01f, uiDragStartHitScale.y + dy / wr.h);
						}
					}

					// 離したらドラッグを終える
					if (ImGui::IsMouseReleased(0)) {
						uiDragging = false;
						uiDragHandle = -1;
				}
			}
		}
	}

	// クリック選択 (PLAY 中は無効)
	if (!isPlaying && imageHovered && !overToolbar && !gizmoHovered && !s_sceneCameraNavigating) {
		// クリック選択: UI を優先し、無ければ 3D のメッシュにレイを当てて一番近いものを選ぶ
		if (ImGui::IsMouseClicked(0)) {
			// クリックした位置 (画像の左上が原点)
			ImVec2 mousePos = ImGui::GetMousePos();
			float sx = mousePos.x - gameImageMin.x;
			float sy = mousePos.y - gameImageMin.y;
			
			// 画像の中なら判定する
			if (sx >= 0 && sx <= renderW && sy >= 0 && sy <= renderH) {

				// 当たったもの
				entt::entity hitE = entt::null;
				
				// 1. UI を優先して判定する
				int maxLayer = -10000;
				// UI の矩形にマウスが入っているものを探す
				gameScene->GetComponents().view<RectTransformComponent>().each([&](entt::entity e, const RectTransformComponent&) {
					UISystem::WorldRect wr = UISystem::CalculateWorldRect(e, gameScene->GetRegistry(), (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH);
					if (internalMouseX >= wr.x && internalMouseX <= wr.x + wr.w &&
						internalMouseY >= wr.y && internalMouseY <= wr.y + wr.h) {
						
						// 重なり順 (画像があればその値)
						int layer = 0;
						if (auto* img = gameScene->GetComponents().try_get<UIImageComponent>(e)) {
							layer = img->layer;
						}
						// より手前（レイヤーが大きい）のUIを優先
						if (hitE == entt::null || layer >= maxLayer) {
							maxLayer = layer;
							hitE = e;
						}
					}
				});

				// 2. UI に当たらなければ 3D のメッシュを判定する
				if (hitE == entt::null && sceneView) {
					// クリックした位置からのレイ
					DirectX::XMVECTOR rayOrig, rayDir;
					ScreenToWorldRay(sx, sy, (float)renderW, (float)renderH, viewCamera.View(), viewCamera.Proj(), rayOrig, rayDir);
					
					// 一番近い距離
					float minD = FLT_MAX;
					gameScene->GetComponents().view<MeshRendererComponent, TransformComponent>().each([&](entt::entity e, const MeshRendererComponent& mr, [[maybe_unused]] const TransformComponent& tc) {
						// モデルが無ければ飛ばす
						if (mr.modelHandle == 0) return;
						auto* m = renderer->GetModel(mr.modelHandle);
						if (!m) return;

						float d; Engine::Vector3 p;
						// ロックされているオブジェクトはピッキング（クリック選択）させない
						if (auto* esc = gameScene->GetComponents().try_get<EditorStateComponent>(e)) {
							if (esc->locked) return;
						}

						// 当たったら、一番近いものを覚える
						if (m->RayCast(rayOrig, rayDir, gameScene->GetWorldMatrix(static_cast<int>(e)), d, p)) {
							if (d < minD) { minD = d; hitE = e; }
						}
					});
				}

				// 当たったものを選ぶ (無ければ選択を外す)
				if (hitE != entt::null) {
					gameScene->SelectEntity(hitE);
				} else {
					gameScene->SelectEntity(entt::null);
				}
			}
		}
	}

	// Scene ビューならツールバーを描く
	if (sceneView) DrawSceneToolbar(gameScene, gameImageMin);
}

// ====== ヒエラルキー ======
// 1000件を超えるオブジェクトを一列に並べると探せないため、次の3通りで整理して表示する。
//   Category : コンポーネント構成 (またはゲーム拡張の分類) ごとのフォルダ
//   Name     : 名前の単語を区切りにした自動フォルダ ("Chrono City prop xxx_01" → Chrono City / prop / xxx)
//   Flat     : 登録順 (従来表示)
// いずれも HierarchyComponent の親子関係は子ノードとして表示する。
namespace {

// Hierarchy の整理の仕方 (分類・名前・一列)
enum class HierarchyGrouping { Category, Name, Flat };

// Hierarchy の状態 (検索文字・整理の仕方・選択・削除・名前の変更・親の付け替えの予約)
struct HierarchyState {
	char search[128] = "";
	HierarchyGrouping grouping = HierarchyGrouping::Category;
	entt::entity lastSelected = entt::null;
	bool revealSelected = false;   // ビューポート等で選択が変わったらフォルダを開いてスクロールする
	bool collapseAll = false;
	entt::entity pendingDelete = entt::null;
	entt::entity renameRequest = entt::null;
	entt::entity renaming = entt::null;
	char renameBuffer[256] = "";
	entt::entity reparentChild = entt::null;
	entt::entity reparentTo = entt::null;
	bool reparentRequested = false;
};
HierarchyState s_hierarchy;

// フォルダーの木 (表示名・子フォルダー・中の物・数・選択中の物を含むか)
struct HierarchyFolder {
	std::string label;
	std::map<std::string, HierarchyFolder> folders;
	std::vector<entt::entity> leaves;
	int count = 0;
	bool containsSelected = false;
};

// 名前の末尾の連番 ("_01", " (Copy)") を除いてグループ化キーにする
std::string NameStem(std::string token) {
	// 末尾の " (Copy)" をすべて外す
	while (token.size() > 7 && token.compare(token.size() - 7, 7, " (Copy)") == 0) token.resize(token.size() - 7);
	// 末尾の数字を外し、その前の区切り (_ - .) も外す
	size_t end = token.size();
	while (end > 0 && std::isdigit(static_cast<unsigned char>(token[end - 1]))) --end;
	if (end < token.size() && end > 0 && (token[end - 1] == '_' || token[end - 1] == '-' || token[end - 1] == '.')) --end;
	// 全部数字なら元のまま
	if (end == 0) return token;
	token.resize(end);
	return token;
}

// 名前を空白で単語に分け、最後の単語から連番を外す (フォルダーの道筋に使う)
std::vector<std::string> NameTokens(const std::string& name) {
	std::vector<std::string> tokens;
	std::string current;
	// 空白ごとに区切る
	for (char c : name) {
		if (c == ' ') {
			if (!current.empty()) tokens.push_back(current);
			current.clear();
		} else {
			current += c;
		}
	}
	// 最後の単語を足す (空なら Object)
	if (!current.empty()) tokens.push_back(current);
	if (tokens.empty()) tokens.push_back("Object");
	tokens.back() = NameStem(tokens.back());
	return tokens;
}

// 標準の分類: 持っているコンポーネントから UI・ライト・プレイヤー・敵・エフェクトなどに振り分ける
const char* DefaultCategory(SceneComponents& reg, entt::entity e) {
	if (reg.any_of<RectTransformComponent, UIImageComponent, UITextComponent, UIButtonComponent, WorldSpaceUIComponent>(e)) return "UI";
	if (reg.any_of<DirectionalLightComponent, PointLightComponent, SpotLightComponent>(e)) return "Lights";
	if (reg.any_of<PlayerInputComponent, CharacterMovementComponent, CameraTargetComponent>(e)) return "Player";
	if (reg.any_of<EnemyAIComponent>(e)) return "Enemies";
	if (reg.any_of<ParticleEmitterComponent, FluidEmitterComponent>(e)) return "Effects";
	if (reg.any_of<AudioSourceComponent, AudioListenerComponent>(e)) return "Audio";
	if (reg.any_of<ScriptComponent>(e)) return "Scripts";
	if (reg.any_of<MeshRendererComponent>(e)) return "Meshes";
	if (reg.any_of<BoxColliderComponent, GpuMeshColliderComponent>(e)) return "Colliders";
	return "Other";
}

// ancestor が e 自身か、e の親をたどった先にあるか (深さ 256 で打ち切り)
bool IsAncestor(SceneComponents& reg, entt::entity ancestor, entt::entity e) {
	// 親をたどって ancestor を探す
	for (int depth = 0; depth < 256 && reg.valid(e); ++depth) {
		if (e == ancestor) return true;
		auto* hc = reg.try_get<HierarchyComponent>(e);
		if (!hc) return false;
		e = hc->parentId;
	}
	return false;
}

// フォルダ内の件数と、選択中の物を含むかを子フォルダまで集計する
void FinalizeFolder(HierarchyFolder& folder, const std::function<bool(entt::entity)>& isSelectedOrAncestor) {
	// このフォルダーの中の物の数と、選択中の物を含むか
	folder.count = static_cast<int>(folder.leaves.size());
	for (auto e : folder.leaves) folder.containsSelected |= isSelectedOrAncestor(e);
	// 子フォルダーの分も足す
	for (auto& [key, child] : folder.folders) {
		FinalizeFolder(child, isSelectedOrAncestor);
		folder.count += child.count;
		folder.containsSelected |= child.containsSelected;
	}
}

} // namespace

// 拡張の一覧 (関数内の static にして、静的初期化の順序に依存しないようにする)
std::vector<EditorExtension>& EditorUI::Extensions() {
	static std::vector<EditorExtension> extensions;
	return extensions;
}

void EditorUI::RegisterExtension(EditorExtension extension) {
	Extensions().push_back(std::move(extension));
}

// 生成メニュー (空・立方体・球・リング・円柱)
static void DrawCreateEntityMenu(GameScene* scene) {
	// このシーンの ECS と描画の窓口
	auto& registry = scene->GetComponents();
	auto* renderer = Engine::Renderer::GetInstance();
	// 名前を付けて作り、メッシュと白いテクスチャを付けて選ぶ
	auto createMesh = [&](const char* name, uint32_t handle, const std::string& modelPath) {
		auto e = scene->CreateEntity(name);
		auto& mr = registry.emplace<MeshRendererComponent>(e);
		mr.modelPath = modelPath;
		mr.modelHandle = handle;
		mr.texturePath = "Resources/Textures/white1x1.png";
		mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
		scene->SelectEntity(e);
	};
	// 空・立方体・球・リング・円柱
	if (ImGui::MenuItem("Create Empty")) scene->SelectEntity(scene->CreateEntity("Empty"));
	if (ImGui::MenuItem("Create Cube")) createMesh("Cube", renderer->LoadObjMesh("Resources/Models/cube/cube.obj"), "Resources/Models/cube/cube.obj");
	if (ImGui::MenuItem("Create Sphere")) createMesh("Sphere", renderer->LoadObjMesh("Resources/Models/player_ball/ball.obj"), "Resources/Models/player_ball/ball.obj");
	if (ImGui::MenuItem("Create Ring")) createMesh("Ring", renderer->CreateRingMesh(2.0f, 1.0f, 32), "");
	if (ImGui::MenuItem("Create Cylinder")) createMesh("Cylinder", renderer->CreateCylinderMesh(1.0f, 2.0f, 32), "");
}

// Hierarchy ウィンドウ
void EditorUI::ShowHierarchy(GameScene* scene) {
	// ウィンドウを開く (シーンが無ければすぐ閉じる)
	ImGui::Begin(kHierarchyWindow, &s_windows.hierarchy);
	if (!scene) {
		ImGui::End();
		return;
	}
	// このシーンの ECS と Hierarchy の状態
	auto& registry = scene->GetComponents();
	auto& st = s_hierarchy;
	// 選択が変わったら、その物が見えるようにフォルダーを開く
	const entt::entity selected = scene->GetSelectedEntity();
	if (selected != st.lastSelected) {
		st.revealSelected = selected != entt::null;
		st.lastSelected = selected;
	}

	// ツールバー (表示方法・すべて閉じる・生成・検索)
	// 整理の仕方の選択
	static const char* groupingNames[] = {"Category", "Name", "Flat"};
	int grouping = static_cast<int>(st.grouping);
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("Category").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f);
	if (ImGui::Combo("##Grouping", &grouping, groupingNames, IM_ARRAYSIZE(groupingNames))) st.grouping = static_cast<HierarchyGrouping>(grouping);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Category: by components / game rules\nName: folders from shared name words\nFlat: registration order");
	ImGui::SameLine();
	// すべて閉じる
	if (ImGui::Button("-")) st.collapseAll = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collapse all folders");
	ImGui::SameLine();
	// 生成メニュー
	if (ImGui::Button("+")) ImGui::OpenPopup("HierarchyCreate");
	if (ImGui::BeginPopup("HierarchyCreate")) {
		DrawCreateEntityMenu(scene);
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	// 検索欄 (小文字で比べる)
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##HierarchySearch", "Search...", st.search, sizeof(st.search));
	const std::string search = ToLower(st.search);

	// 名前を持つエンティティを集め、親子関係を整理する
	std::vector<entt::entity> all;
	std::unordered_map<entt::entity, std::vector<entt::entity>> children;
	for (auto e : registry.view<NameComponent>()) all.push_back(e);
	std::vector<entt::entity> roots;
	roots.reserve(all.size());
	// 親が有効ならその子に、そうでなければ一番上に入れる
	for (auto e : all) {
		auto* hc = registry.try_get<HierarchyComponent>(e);
		entt::entity parent = hc ? hc->parentId : entt::null;
		if (parent != entt::null && parent != e && registry.valid(parent) && registry.all_of<NameComponent>(parent)) children[parent].push_back(e);
		else roots.push_back(e);
	}
	// 物の数
	ImGui::TextDisabled("%d objects", static_cast<int>(all.size()));
	ImGui::Separator();

	// スクロールする木の領域
	ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), ImGuiChildFlags_None);

	const auto& extensions = Extensions();
	// 分類: 拡張の分類があればそれを使い、無ければ標準の分類
	auto categoryOf = [&](entt::entity e) -> std::string {
		for (const auto& ext : extensions) {
			if (!ext.classify) continue;
			std::string category = ext.classify(registry, e);
			if (!category.empty()) return category;
		}
		return DefaultCategory(registry, e);
	};

	// エンティティの行 (子があれば再帰的に描く)
	std::function<void(entt::entity, int)> drawEntity = [&](entt::entity e, int depth) {
		// 消えていれば何もしない
		if (!registry.valid(e)) return;
		ImGui::PushID(static_cast<int>(entt::to_integral(e)));
		// ロックの切り替えボタン (ロック中は L、それ以外は .)
		auto& esc = registry.get_or_emplace<EditorStateComponent>(e);
		ImGui::PushStyleColor(ImGuiCol_Text, esc.locked ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
		if (ImGui::SmallButton(esc.locked ? "L" : ".")) esc.locked = !esc.locked;
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip(esc.locked ? "Locked: cannot be picked in the viewport (click to unlock)" : "Click to lock (disable viewport picking)");
		ImGui::SameLine();

		// 子があるか (深すぎる場合は打ち切る)
		auto childIt = children.find(e);
		const bool hasChildren = childIt != children.end() && depth < 64;
		// 矢印かダブルクリックで開く、子が無ければ葉にする、選択中なら強調する
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (selected == e) flags |= ImGuiTreeNodeFlags_Selected;
		// 選択中の物の祖先なら開いておく
		if (st.revealSelected && hasChildren && selected != e && IsAncestor(registry, e, selected)) ImGui::SetNextItemOpen(true);
		const std::string& name = registry.get<NameComponent>(e).name;
		// 名前の行
		const bool open = ImGui::TreeNodeEx("##node", flags, "%s", name.c_str());
		// クリックで選ぶ (開閉の矢印は除く)
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
			scene->SelectEntity(e);
			st.lastSelected = e; // Hierarchy 内のクリックではスクロールさせない
		}
		// Unity と同じくダブルクリックで Scene ビューの視点を合わせる
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hasChildren) FrameSelectedInScene();
		// 選択中の物ならそこまでスクロールする
		if (st.revealSelected && selected == e) {
			ImGui::SetScrollHereY(0.4f);
			st.revealSelected = false;
		}

		// ドラッグで親子付け (ドロップ先が自分の子孫になる循環は拒否)
		// ドラッグ元: この物を運ぶ
		if (ImGui::BeginDragDropSource()) {
			entt::entity payload = e;
			ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &payload, sizeof(payload));
			ImGui::Text("Parent '%s' to...", name.c_str());
			ImGui::EndDragDropSource();
		}
		// ドロップ先: 運ばれた物をこの物の子にする (後でまとめて反映する)
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
				entt::entity child = *static_cast<const entt::entity*>(payload->Data);
				if (child != e && !IsAncestor(registry, child, e)) {
					st.reparentChild = child;
					st.reparentTo = e;
					st.reparentRequested = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		// 右クリックのメニュー: 名前の変更・カメラを寄せる・削除・親子解除・コピー/貼り付け/複製
		if (ImGui::BeginPopupContextItem("EntityContext")) {
			scene->SelectEntity(e);
			if (ImGui::MenuItem("Rename", "F2")) st.renameRequest = e;
			if (ImGui::MenuItem("Frame in Scene", "F")) FrameSelectedInScene();
			if (ImGui::MenuItem("Delete", "Del")) st.pendingDelete = e;
			if (ImGui::MenuItem("Unparent", nullptr, false, registry.all_of<HierarchyComponent>(e) && registry.get<HierarchyComponent>(e).parentId != entt::null)) {
				st.reparentChild = e;
				st.reparentTo = entt::null;
				st.reparentRequested = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Copy", "Ctrl+C")) ExecuteCopy(scene);
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, !s_clipboardJson.empty())) ExecutePaste(scene);
			if (ImGui::MenuItem("Duplicate", "Ctrl+D")) ExecuteDuplicate(scene);
			ImGui::EndPopup();
		}

		// 開いていれば子を描く
		if (open && hasChildren) {
			for (auto child : childIt->second) drawEntity(child, depth + 1);
			ImGui::TreePop();
		}
		ImGui::PopID();
	};

	// 選択中の物か、その祖先か
	auto isSelectedOrAncestor = [&](entt::entity e) { return selected != entt::null && IsAncestor(registry, e, selected); };

	std::function<void(HierarchyFolder&, const std::string&)> drawFolder = [&](HierarchyFolder& folder, const std::string& id) {
		// 1件だけのフォルダは作らない / 子フォルダ1つだけのフォルダは名前を連結する ("Chrono" + "City")
		// 子フォルダーを順に描く
		for (auto& [key, child] : folder.folders) {
			HierarchyFolder* node = &child;
			std::string label = key;
			while (node->leaves.empty() && node->folders.size() == 1) {
				auto& only = node->folders.begin()->second;
				label += " " + node->folders.begin()->first;
				node = &only;
			}
			// 1 件だけなら、フォルダーを作らずにその物を直接描く
			if (node->count == 1) {
				std::function<void(HierarchyFolder&)> drawSingle = [&](HierarchyFolder& f) {
					for (auto e : f.leaves) drawEntity(e, 0);
					for (auto& [k, c] : f.folders) drawSingle(c);
				};
				drawSingle(*node);
				continue;
			}
			// フォルダーの ID と、開閉の状態
			std::string nodeId = id + "/" + label;
			ImGui::PushID(nodeId.c_str());
			if (st.collapseAll) ImGui::SetNextItemOpen(false);
			else if (st.revealSelected && node->containsSelected) ImGui::SetNextItemOpen(true);
			// フォルダーは金色の文字で、名前と件数を出す
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.82f, 0.45f, 1.0f));
			const bool open = ImGui::TreeNodeEx("##folder", ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%d)", label.c_str(), node->count);
			ImGui::PopStyleColor();
			if (open) {
				drawFolder(*node, nodeId);
				// 子を持たない大量の葉は可視範囲だけ描画する
				bool uniformRows = true;
				for (auto e : node->leaves) {
					if (children.count(e)) { uniformRows = false; break; }
				}
				if (uniformRows && node->leaves.size() > 64 && !st.revealSelected) {
					ImGuiListClipper clipper;
					clipper.Begin(static_cast<int>(node->leaves.size()));
					while (clipper.Step()) {
						for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) drawEntity(node->leaves[i], 0);
					}
				} else {
					// 少なければすべて描く
					for (auto e : node->leaves) drawEntity(e, 0);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		// 一番上のフォルダーなら、そこの物も描く
		if (id.empty()) {
			for (auto e : folder.leaves) drawEntity(e, 0);
		}
	};

	if (!search.empty()) {
		// 検索中は一致したものを親子関係を問わず平坦に表示する
		// 名前に検索文字を含むものを集める
		std::vector<entt::entity> hits;
		for (auto e : all) {
			if (ToLower(registry.get<NameComponent>(e).name).find(search) != std::string::npos) hits.push_back(e);
		}
		ImGui::TextDisabled("%d match(es)", static_cast<int>(hits.size()));
		// 見えている行だけ描く
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(hits.size()));
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				auto e = hits[i];
				ImGui::PushID(static_cast<int>(entt::to_integral(e)));
				const std::string& name = registry.get<NameComponent>(e).name;
				// 選んだら選択する (Hierarchy 内なのでスクロールしない)
				if (ImGui::Selectable(name.c_str(), selected == e)) {
					scene->SelectEntity(e);
					st.lastSelected = e;
				}
				// 分類をツールチップで出す
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", categoryOf(e).c_str());
				ImGui::PopID();
			}
		}
	} else if (st.grouping == HierarchyGrouping::Flat) {
		// 一列: 一番上の物を順に描く
		for (auto e : roots) drawEntity(e, 0);
	} else {
		// 分類・名前: フォルダーの木を作る
		HierarchyFolder root;
		for (auto e : roots) {
			const std::string& name = registry.get<NameComponent>(e).name;
			HierarchyFolder* folder = &root;
			std::vector<std::string> path;
			if (st.grouping == HierarchyGrouping::Category) {
				path.push_back(categoryOf(e));
				// カテゴリ内でも 16 件を超える同名系統はさらに名前でまとめる
				for (auto& token : NameTokens(name)) path.push_back(token);
			} else {
				path = NameTokens(name);
			}
			// 道筋をたどってフォルダーを作り、物を入れる
			for (const auto& key : path) folder = &folder->folders[key];
			folder->leaves.push_back(e);
		}
		// 件数と選択中の物を集計する
		FinalizeFolder(root, isSelectedOrAncestor);
		if (st.grouping == HierarchyGrouping::Category) {
			// カテゴリは必ずフォルダとして表示し、中身だけ名前で畳む
			for (auto& [category, folder] : root.folders) {
				ImGui::PushID(category.c_str());
				if (st.collapseAll) ImGui::SetNextItemOpen(false);
				else if (st.revealSelected && folder.containsSelected) ImGui::SetNextItemOpen(true);
				// 分類の見出しは水色の枠付き
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.0f, 1.0f));
				const bool open = ImGui::TreeNodeEx("##category", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed, "%s  (%d)", category.c_str(), folder.count);
				ImGui::PopStyleColor();
				if (open) {
					// カテゴリ内の全員が同じ名前の系統 ("Chrono City prop ...") なら、その段は省いて中身を直接並べる
					HierarchyFolder* content = &folder;
					while (content->leaves.empty() && content->folders.size() == 1) content = &content->folders.begin()->second;
					drawFolder(*content, category);
					for (auto e : content->leaves) drawEntity(e, 0);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		} else {
			drawFolder(root, "");
		}
	}
	// すべて閉じる指定を消す
	st.collapseAll = false;
	// 展開とスクロールは選択が変わった1フレームだけ行う (以降はユーザーが自由に閉じられる)
	st.revealSelected = false;

	// 余白: 右クリックで生成メニュー / ドロップで親子解除
	// 残りの領域に見えないボタンを置く
	ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::InvisibleButton("HierarchyBackground", ImVec2((std::max)(avail.x, 1.0f), (std::max)(avail.y, 40.0f)));
	// ここにドロップしたら親子関係を外す
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
			st.reparentChild = *static_cast<const entt::entity*>(payload->Data);
			st.reparentTo = entt::null;
			st.reparentRequested = true;
		}
		ImGui::EndDragDropTarget();
	}
	// 右クリックで生成メニュー
	if (ImGui::BeginPopupContextItem("HierarchyBackgroundContext")) {
		DrawCreateEntityMenu(scene);
		ImGui::EndPopup();
	}
	ImGui::EndChild();

	// Delete / F2 / F キー (Hierarchy にフォーカスがあるときのみ)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && selected != entt::null) {
		// Delete: 削除 / F2: 名前の変更 / F: カメラを寄せる
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !scene->GetIsPlaying()) st.pendingDelete = selected;
		if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) st.renameRequest = selected;
		if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().KeyCtrl) FrameSelectedInScene();
	}
	// 名前の変更の要求があれば、今の名前を入れて窓を開く
	if (st.renameRequest != entt::null) {
		st.renaming = st.renameRequest;
		st.renameRequest = entt::null;
		if (registry.valid(st.renaming) && registry.all_of<NameComponent>(st.renaming)) {
			CopyToBuffer(st.renameBuffer, registry.get<NameComponent>(st.renaming).name);
			ImGui::OpenPopup("Rename Object");
		}
	}
	// 名前の変更の窓
	if (ImGui::BeginPopupModal("Rename Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(320.0f);
		const bool enter = ImGui::InputText("##RenameInput", st.renameBuffer, sizeof(st.renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		// Enter か OK で確定し、Undo を登録する
		if (enter || ImGui::Button("OK")) {
			if (registry.valid(st.renaming) && registry.all_of<NameComponent>(st.renaming)) {
				entt::entity target = st.renaming;
				std::string before = registry.get<NameComponent>(target).name, after = st.renameBuffer;
				registry.get<NameComponent>(target).name = after;
				PushUndo({"Rename",
					[scene, target, before]() { auto& r = scene->GetComponents(); if (r.valid(target) && r.all_of<NameComponent>(target)) r.get<NameComponent>(target).name = before; },
					[scene, target, after]() { auto& r = scene->GetComponents(); if (r.valid(target) && r.all_of<NameComponent>(target)) r.get<NameComponent>(target).name = after; }});
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		// Cancel か Esc で閉じる
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// --- 反復処理の外で構造を変更する ---
	if (st.reparentRequested) {
		st.reparentRequested = false;
		// 子と新しい親 (null なら親子解除)
		entt::entity child = st.reparentChild, parent = st.reparentTo;
		if (registry.valid(child) && (parent == entt::null || registry.valid(parent))) {
			// 親を付け替え、ワールド行列を計算し直し、Undo を登録する
			auto& hc = registry.get_or_emplace<HierarchyComponent>(child);
			entt::entity before = hc.parentId;
			hc.parentId = parent;
			scene->InvalidateWorldMatrices();
			PushUndo({"Reparent",
				[scene, child, before]() { auto& reg = scene->GetComponents(); if (reg.valid(child)) { reg.get_or_emplace<HierarchyComponent>(child).parentId = before; scene->InvalidateWorldMatrices(); } },
				[scene, child, parent]() { auto& reg = scene->GetComponents(); if (reg.valid(child)) { reg.get_or_emplace<HierarchyComponent>(child).parentId = parent; scene->InvalidateWorldMatrices(); } }});
		}
	}
	if (st.pendingDelete != entt::null) {
		// 削除の予約を取り出し、Undo を登録してから破棄する
		entt::entity e = st.pendingDelete;
		st.pendingDelete = entt::null;
		if (registry.valid(e)) {
			PushUndo(MakeSpawnUndo(scene, "Delete Entity", SerializeEntity(registry, e), e, false));
			scene->DestroyObject(static_cast<uint32_t>(e));
			if (scene->GetSelectedEntity() == e) scene->SelectEntity(entt::null);
		}
	}
	ImGui::End();
}

// フォルダ以下から指定拡張子のファイルを探し、Resources/ からの相対パスで返す
std::vector<std::string> EditorUI::GetAssetsInDir(const std::string& root, const std::vector<std::string>& extensions) {
	std::vector<std::string> assets;
	// プロジェクトのルートを基準にした絶対パス (フォルダーで無ければ空)
	std::string absRoot = GetUnifiedProjectPath(root);
	std::error_code ec;
	if (!fs::is_directory(Engine::PathUtils::FromUTF8(absRoot), ec)) return assets;

	// アクセスできないフォルダや日本語のファイル名があっても例外で落ちないよう error_code 版を使う
	fs::recursive_directory_iterator it(Engine::PathUtils::FromUTF8(absRoot), fs::directory_options::skip_permission_denied, ec);
	for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		// ファイルだけを調べる
		const auto& entry = *it;
		std::error_code fileError;
		if (entry.is_regular_file(fileError)) {
			// 拡張子 (小文字)
			std::string ext = ToLower(Engine::PathUtils::ToUTF8(entry.path().extension().wstring()));
			
			// 探している拡張子か
			bool match = false;
			for (const auto& targetExt : extensions) {
				if (ext == targetExt) { match = true; break; }
			}
			
			if (match) {
				// UTF-8 のパスにして、区切りを / にそろえる
				std::string path = Engine::PathUtils::ToUTF8(entry.path().wstring());
				std::replace(path.begin(), path.end(), '\\', '/');
				
				// 保存の形をそろえるため Resources/ からの相対パスにする
				size_t pos = path.find("/Resources/");
				if (pos != std::string::npos) {
					path = path.substr(pos + 1); // 先頭のスラッシュを除く
				} else {
					pos = path.find("Resources/");
					if (pos != std::string::npos) path = path.substr(pos);
				}
				assets.push_back(path);
			}
		}
	}
	return assets;
}

// アセットのパス入力欄 (直接入力・Project からのドラッグ&ドロップ・一覧からの選択)
bool EditorUI::AssetField(const char* label, std::string& path, const std::vector<std::string>& extensions) {
	// 変更されたか / 欄ごとに ID を分ける
	bool modified = false;
	ImGui::PushID(label);
	
	// 入力欄の文字
	char buf[512];
	CopyToBuffer(buf, path);
	
	// 右の ... ボタンの分だけ欄を狭くする
	float buttonSize = ImGui::GetFrameHeight();
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);
	
	// 直接入力
	if (ImGui::InputText(label, buf, sizeof(buf))) {
		path = buf;
		modified = true;
	}

	// ドラッグ&ドロップ
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
			// 落とされたパスと拡張子
			std::string droppedPath = (const char*)payload->Data;
			std::replace(droppedPath.begin(), droppedPath.end(), '\\', '/');
			
			std::string ext = ToLower(Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(droppedPath)).extension().wstring()));
			
			// 受け付ける拡張子なら反映する
			bool valid = false;
			for (const auto& e : extensions) { if (ext == e) { valid = true; break; } }
			
			if (valid) {
				path = droppedPath;
				modified = true;
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();
	// ... ボタンで一覧を開く
	if (ImGui::Button("...")) {
		ImGui::OpenPopup("AssetDropdown");
	}

	if (ImGui::BeginPopup("AssetDropdown")) {
		// 一覧は開いたときに作り直す
		static std::vector<std::string> assetList;
		if (ImGui::IsWindowAppearing()) {
			assetList = GetAssetsInDir("Resources", extensions);
		}

		// (None) で空にする
		if (ImGui::Selectable("(None)", path.empty())) {
			path = "";
			modified = true;
		}

		// 一覧から選ぶ
		for (const auto& a : assetList) {
			if (ImGui::Selectable(a.c_str(), path == a)) {
				path = a;
				modified = true;
			}
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();
	return modified;
}

// Inspector の補助
// Inspector を描いている間、PLAY 中かどうか
static bool s_inspectorPlaying = false;

// コンポーネントの追加・削除など構造を変えるボタン。PLAY 中は値の調整だけを許可する (停止時に元へ戻るため)
static bool StructureButton(const char* label) {
	// PLAY 中は押せないようにする
	ImGui::BeginDisabled(s_inspectorPlaying);
	const bool pressed = ImGui::Button(label);
	ImGui::EndDisabled();
	return pressed;
}

// Add Component の項目 (カテゴリ・表示名・持っているかの判定・追加する関数)
namespace {
struct ComponentEntry {
	const char* category;
	const char* name;
	bool (*has)(SceneComponents&, entt::entity);
	void (*add)(SceneComponents&, entt::entity);
};
// 持っているか / 追加する (共通の関数)
template <class T> bool HasComponent(SceneComponents& r, entt::entity e) { return r.all_of<T>(e); }
template <class T> void AddComponent(SceneComponents& r, entt::entity e) { std::ignore = r.get_or_emplace<T>(e); }

// GPU メッシュコライダーを追加する (描画用のメッシュがあればそれを判定にも使う)
void AddGpuMeshCollider(SceneComponents& r, entt::entity e) {
	auto& gmc = r.get_or_emplace<GpuMeshColliderComponent>(e);
	if (auto* mr = r.try_get<MeshRendererComponent>(e)) {
		gmc.meshPath = mr->modelPath;
		gmc.meshHandle = mr->modelHandle;
	}
}

// Legacy は旧アクションゲーム (缶・ボス戦) 用。現行のスライム (Chrono) モードでは処理されない
const ComponentEntry kComponentEntries[] = {
	// 描画: メッシュ・アニメーター・モーション
	{"Rendering", "Mesh Renderer", &HasComponent<MeshRendererComponent>, &AddComponent<MeshRendererComponent>},
	{"Rendering", "Animator", &HasComponent<AnimatorComponent>, &AddComponent<AnimatorComponent>},
	{"Rendering", "Motion (Keyframes)", &HasComponent<MotionComponent>, &AddComponent<MotionComponent>},
	// エフェクト: パーティクル・流体・川
	{"Effects", "Particle Emitter", &HasComponent<ParticleEmitterComponent>, &AddComponent<ParticleEmitterComponent>},
	{"Effects", "Fluid Emitter", &HasComponent<FluidEmitterComponent>, &AddComponent<FluidEmitterComponent>},
	{"Effects", "River", &HasComponent<RiverComponent>, &AddComponent<RiverComponent>},
	// 物理: 箱・剛体・GPU メッシュ
	{"Physics", "Box Collider", &HasComponent<BoxColliderComponent>, &AddComponent<BoxColliderComponent>},
	{"Physics", "Rigidbody", &HasComponent<RigidbodyComponent>, &AddComponent<RigidbodyComponent>},
	{"Physics", "GPU Mesh Collider", &HasComponent<GpuMeshColliderComponent>, &AddGpuMeshCollider},
	// ライト
	{"Lighting", "Directional Light", &HasComponent<DirectionalLightComponent>, &AddComponent<DirectionalLightComponent>},
	{"Lighting", "Point Light", &HasComponent<PointLightComponent>, &AddComponent<PointLightComponent>},
	{"Lighting", "Spot Light", &HasComponent<SpotLightComponent>, &AddComponent<SpotLightComponent>},
	// 音
	{"Audio", "Audio Source", &HasComponent<AudioSourceComponent>, &AddComponent<AudioSourceComponent>},
	{"Audio", "Audio Listener", &HasComponent<AudioListenerComponent>, &AddComponent<AudioListenerComponent>},
	// UI
	{"UI", "Rect Transform", &HasComponent<RectTransformComponent>, &AddComponent<RectTransformComponent>},
	{"UI", "UI Image", &HasComponent<UIImageComponent>, &AddComponent<UIImageComponent>},
	{"UI", "UI Text", &HasComponent<UITextComponent>, &AddComponent<UITextComponent>},
	{"UI", "UI Button", &HasComponent<UIButtonComponent>, &AddComponent<UIButtonComponent>},
	// スクリプト・変数・タグ・体力
	{"Scripting", "Script", &HasComponent<ScriptComponent>, &AddComponent<ScriptComponent>},
	{"Scripting", "Variables", &HasComponent<VariableComponent>, &AddComponent<VariableComponent>},
	{"Scripting", "Tag", &HasComponent<TagComponent>, &AddComponent<TagComponent>},
	{"Scripting", "Health", &HasComponent<HealthComponent>, &AddComponent<HealthComponent>},
	// 旧アクションゲーム用
	{"Legacy (old action game)", "Player Input", &HasComponent<PlayerInputComponent>, &AddComponent<PlayerInputComponent>},
	{"Legacy (old action game)", "Character Movement", &HasComponent<CharacterMovementComponent>, &AddComponent<CharacterMovementComponent>},
	{"Legacy (old action game)", "Camera Target", &HasComponent<CameraTargetComponent>, &AddComponent<CameraTargetComponent>},
	{"Legacy (old action game)", "Hitbox", &HasComponent<HitboxComponent>, &AddComponent<HitboxComponent>},
	{"Legacy (old action game)", "Hurtbox", &HasComponent<HurtboxComponent>, &AddComponent<HurtboxComponent>},
	{"Legacy (old action game)", "World Space UI", &HasComponent<WorldSpaceUIComponent>, &AddComponent<WorldSpaceUIComponent>},
};
} // namespace

// Unity と同じく検索欄付きの Add Component。検索中はカテゴリを畳まず一覧にする
void EditorUI::DrawAddComponentButton(SceneComponents& registry, entt::entity entity, bool playing) {
	// 検索文字
	static char search[64] = "";
	// ボタンの幅 (最大 260) と中央寄せ
	const float width = (std::min)(ImGui::GetContentRegionAvail().x, 260.0f);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
	ImGui::BeginDisabled(playing);
	// 押したら検索を空にして一覧を開く (PLAY 中は押せない)
	if (ImGui::Button("Add Component", ImVec2(width, 0))) {
		search[0] = '\0';
		ImGui::OpenPopup("AddComp");
	}
	ImGui::EndDisabled();
	if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop play mode to add components");
	// 一覧が開いていなければ終わる
	if (!ImGui::BeginPopup("AddComp")) return;
	// 開いたときは検索欄に入力できるようにする
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(240.0f);
	ImGui::InputTextWithHint("##ComponentSearch", "Search...", search, sizeof(search));
	ImGui::Separator();
	// 小文字で比べる
	const std::string needle = ToLower(search);
	// 1 項目: 持っていればチェック付きで押せない、押したら追加して閉じる
	auto drawEntry = [&](const ComponentEntry& entry) {
		const bool has = entry.has(registry, entity);
		if (ImGui::MenuItem(entry.name, nullptr, has, !has)) {
			entry.add(registry, entity);
			ImGui::CloseCurrentPopup();
		}
	};
	// 検索中: 名前に含むものだけを一列に並べる
	if (!needle.empty()) {
		int shown = 0;
		for (const auto& entry : kComponentEntries) {
			if (ToLower(entry.name).find(needle) == std::string::npos) continue;
			drawEntry(entry);
			++shown;
		}
		if (shown == 0) ImGui::TextDisabled("No component matches");
	} else {
		// 検索していないとき: カテゴリごとのサブメニューに分ける
		const char* current = nullptr;
		bool open = false;
		for (const auto& entry : kComponentEntries) {
			if (!current || std::string(current) != entry.category) {
				if (open) ImGui::EndMenu();
				current = entry.category;
				open = ImGui::BeginMenu(entry.category);
			}
			if (open) drawEntry(entry);
		}
		if (open) ImGui::EndMenu();
	}
	ImGui::EndPopup();
}

// Inspector: 選択中のエンティティの名前・トランスフォーム・各コンポーネントを編集する
void EditorUI::ShowInspector(GameScene* scene) {
	// PLAY 中か (構造を変えるボタンを無効にする)
	s_inspectorPlaying = scene && scene->GetIsPlaying();
	// ウィンドウの Begin は呼び出し側で行う
	if (!scene) return;
	// スクリプトの変更の予約 (描画の後にまとめて反映する)
	std::optional<ScriptEdit> pendingScriptEdit;
	// 選択中の物
	auto selected = scene->GetSelectedEntity();
	if (scene && selected != entt::null && scene->GetComponents().valid(selected)) {
		// 物ごとに ID を分ける
		ImGui::PushID((int)selected);
		auto entity = selected;
		auto& registry = scene->GetComponents();

		// 名前とロック
		if (auto* nc = registry.try_get<NameComponent>(entity)) {
			// 名前の入力欄
			char buf[256]; CopyToBuffer(buf, nc->name);
			ImGui::SetNextItemWidth(-130);
			if (ImGui::InputText("##Name", buf, sizeof(buf))) nc->name = buf;
			ImGui::SameLine();
			// ロック
			auto& esc = registry.get_or_emplace<EditorStateComponent>(entity);
			ImGui::Checkbox("Locked", &esc.locked);
		}
		// ID
		ImGui::TextDisabled("ID: %d", (uint32_t)entity);
		ImGui::SameLine();
		// プレハブとして保存する (Resources/Prefabs/名前.prefab)
		if (ImGui::Button("Save Prefab")) {
			// 名前 (無ければ object)
			std::string name = (registry.all_of<NameComponent>(entity) ? registry.get<NameComponent>(entity).name : "object");
			// ファイル名に使えない文字 (\\ / : * ? " < > |) を置き換える
			for (char& c : name) {
				if (std::string("\\/:*?\"<>|").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) c = '_';
			}
			// 空なら object
			if (name.empty()) name = "object";
			// 保存先のパスと、フォルダーの作成
			std::string p = "Resources/Prefabs/" + name + ".prefab";
			std::string fullPath = GetUnifiedProjectPath(p);
			std::error_code dirError;
			fs::create_directories(fs::path(Engine::PathUtils::FromUTF8(fullPath)).parent_path(), dirError);
			// prefab キーの下にエンティティの JSON を書く
			std::ofstream f(Engine::PathUtils::FromUTF8(fullPath));
			if (f.is_open()) {
				f << "{\n  \"prefab\":\n";
				f << SerializeEntity(registry, entity);
				f << "\n}\n";
				f.close();
				Log("Prefab saved: " + p);
			} else {
				LogError("Failed to save prefab: " + fullPath);
			}
		}

		// トランスフォーム (編集の開始と終了を記録して Undo できるようにする)
		if (auto* tc = registry.try_get<TransformComponent>(entity)) {
			ImGui::Separator();
			ImGui::Text("Transform");
			// 位置: 動かし始めの値を覚え、離したら Undo を登録する
			if (ImGui::DragFloat3("Pos", &tc->translate.x, 0.1f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartTranslate = tc->translate;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartTranslate;
				DirectX::XMFLOAT3 end = tc->translate;
				entt::entity e = entity;
				PushUndo({"Change Position",
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).translate = start; },
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).translate = end; }
				});
			}

			// 回転: 同様
			if (ImGui::DragFloat3("Rot", &tc->rotate.x, 0.01f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartRotate = tc->rotate;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartRotate;
				DirectX::XMFLOAT3 end = tc->rotate;
				entt::entity e = entity;
				PushUndo({"Change Rotation",
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).rotate = start; },
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).rotate = end; }
				});
			}

			// 大きさ: 同様
			if (ImGui::DragFloat3("Scale", &tc->scale.x, 0.01f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartScale = tc->scale;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartScale;
				DirectX::XMFLOAT3 end = tc->scale;
				entt::entity e = entity;
				PushUndo({"Change Scale",
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).scale = start; },
					[=, &reg = scene->GetComponents()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).scale = end; }
				});
			}
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
			// 各コンポーネントの編集欄 (コンポーネントごとに ID を分けて、同名の項目が干渉しないようにする)
			if (auto* cp = registry.try_get<MeshRendererComponent>(selected)) {
				ImGuiIdScope componentId("MeshRendererComponent");
				if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##MR", &cp->enabled);
					
					// モデル (変えたら読み込み直す)
					if (AssetField("Model Path", cp->modelPath, {".obj", ".fbx", ".gltf"})) {
						cp->modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(cp->modelPath);
					}

					// テクスチャ (変えたら読み込み直す)
					if (AssetField("Texture Path", cp->texturePath, {".png", ".jpg"})) {
						cp->textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(cp->texturePath);
					}

					// シェーダーの選択
					// 登録されているシェーダーの一覧
					const auto& shaders = Engine::Renderer::GetInstance()->GetShaderNames();
					std::string currentShader = cp->shaderName.empty() ? "Default" : cp->shaderName;
					if (ImGui::BeginCombo("Shader", currentShader.c_str())) {
						for (size_t i = 0; i < shaders.size(); ++i) {
							const auto& sName = shaders[i];
							ImGui::PushID((int)i);
							bool isSelected = (currentShader == sName);
							if (ImGui::Selectable(sName.c_str(), isSelected)) {
								cp->shaderName = sName;
							}
							if (isSelected) ImGui::SetItemDefaultFocus();
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}

					// 色・UV の繰り返しとずらし
					ImGui::ColorEdit4("Base Color", &cp->color.x);
					ImGui::DragFloat2("UV Tiling", &cp->uvTiling.x, 0.01f);
					ImGui::DragFloat2("UV Offset", &cp->uvOffset.x, 0.01f);

					// Reflection シェーダーのときは反射率の項目を出す
					if (cp->shaderName == "Reflection") {
						ImGui::SliderFloat("Reflectivity", &cp->reflectivity, 0.0f, 1.0f, "%.2f");
						ImGui::Checkbox("Use Cubemap", &cp->useCubemap);
					}

					// コンポーネントを外す (PLAY 中は押せない)
					if (StructureButton("Remove##MR")) registry.remove<MeshRendererComponent>(entity);
				}
			}
			// 箱の当たり判定
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				ImGuiIdScope componentId("BoxColliderComponent");
				if (ImGui::CollapsingHeader("BoxCollider", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・中心・大きさ・すり抜けるか
					ImGui::Checkbox("Enabled##BC", &bc->enabled);
					ImGui::DragFloat3("Center", &bc->center.x, 0.1f);
					ImGui::DragFloat3("Size", &bc->size.x, 0.1f);
					ImGui::Checkbox("Is Trigger", &bc->isTrigger);
					if (StructureButton("Remove##BC")) registry.remove<BoxColliderComponent>(entity);
				}
			}
			// GPU メッシュコライダー
			if (auto* gmc = registry.try_get<GpuMeshColliderComponent>(entity)) {
				ImGuiIdScope componentId("GpuMeshColliderComponent");
				if (ImGui::CollapsingHeader("GpuMeshCollider", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・メッシュ (変えたら読み込み直す)・すり抜けるか
					ImGui::Checkbox("Enabled##GMC", &gmc->enabled);
					if (AssetField("Mesh Path", gmc->meshPath, {".obj", ".fbx"})) {
						gmc->meshHandle = Engine::Renderer::GetInstance()->LoadObjMesh(gmc->meshPath);
					}
					ImGui::Checkbox("Is Trigger", &gmc->isTrigger);
					if (StructureButton("Remove##GMC")) registry.remove<GpuMeshColliderComponent>(entity);
				}
			}
			// 物理挙動
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
				ImGuiIdScope componentId("RigidbodyComponent");
				if (ImGui::CollapsingHeader("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・速度・重力・自分で動かすか
					ImGui::Checkbox("Enabled##RB", &rb->enabled);
					ImGui::DragFloat3("Velocity", &rb->velocity.x, 0.1f);
					ImGui::Checkbox("Use Gravity", &rb->useGravity);
					ImGui::Checkbox("Is Kinematic", &rb->isKinematic);
					if (StructureButton("Remove##RB")) registry.remove<RigidbodyComponent>(entity);
				}
			}
			// タグ (変更したらタグ検索のキャッシュも更新する)
			if (auto* tag = registry.try_get<TagComponent>(entity)) {
				ImGuiIdScope componentId("TagComponent");
				if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##Tag", &tag->enabled);
					
					// 今のタグと、タグの一覧
					const char* currentTagName = TagToString(tag->tag);
					if (ImGui::BeginCombo("Tag##TagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::VFX; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (tag->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) {
								tag->tag = t;
								scene->SyncTag(entity); // キャッシュ更新
							}
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					if (StructureButton("Remove##Tag")) registry.remove<TagComponent>(entity);
				}
			}
			// 音の発信源
			if (auto* as = registry.try_get<AudioSourceComponent>(entity)) {
				ImGuiIdScope componentId("AudioSourceComponent");
				if (ImGui::CollapsingHeader("AudioSource", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・音のファイル (変えたら読み込み直す)
					ImGui::Checkbox("Enabled##AS", &as->enabled);
					if (AssetField("Sound File", as->soundPath, {".wav", ".mp3"})) {
						as->soundHandle = Engine::Audio::GetInstance()->Load(as->soundPath);
					}
					// 音量・ループ・開始時に鳴らすか・3D か
					ImGui::DragFloat("Volume", &as->volume, 0.01f, 0, 1);
					ImGui::Checkbox("Loop", &as->loop);
					ImGui::Checkbox("Play on Start", &as->playOnStart);
					ImGui::Checkbox("Is 3D", &as->is3D);
					if (StructureButton("Remove##AS")) registry.remove<AudioSourceComponent>(entity);
				}
			}
			// 体力
			if (auto* hp = registry.try_get<HealthComponent>(entity)) {
				ImGuiIdScope componentId("HealthComponent");
				if (ImGui::CollapsingHeader("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##HP", &hp->enabled);
					// 体力・最大体力・死んでいるか
					float currentHp = hp->Hp(), maxHp = hp->MaxHp();
                    bool dead = hp->IsDead();
                    if (ImGui::DragFloat("HP", &currentHp, 1.0f, 0, maxHp)) hp->SetHp(currentHp);
                    if (ImGui::DragFloat("Max HP", &maxHp, 1.0f, 1, 10000)) hp->SetMaxHp(maxHp);
                    if (ImGui::Checkbox("Is Dead", &dead)) hp->SetDead(dead);
					if (StructureButton("Remove##HP")) registry.remove<HealthComponent>(entity);
				}
			}
			// モーション: クリップの選択・追加・削除、キーフレームの編集
			if (auto* mc = registry.try_get<MotionComponent>(entity)) {
				ImGuiIdScope componentId("MotionComponent");
				if (ImGui::CollapsingHeader("Motion Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##MC", &mc->enabled);
					
					// クリップの選択
					if (ImGui::BeginCombo("Active Clip", mc->activeClip.c_str())) {
						for (auto& [name, clip] : mc->clips) {
							bool isSelected = (mc->activeClip == name);
							if (ImGui::Selectable(name.c_str(), isSelected)) {
								mc->activeClip = name;
								mc->currentTime = 0.0f;
								mc->selectedKeyframe = -1;
							}
						}
						ImGui::EndCombo();
					}

					ImGui::SameLine();
					// 新しいクリップを作る窓を開く
					if (StructureButton("+##NewClip")) {
						ImGui::OpenPopup("NewClipPopup");
					}

					if (ImGui::BeginPopup("NewClipPopup")) {
						static char newClipBuf[64] = "NewClip";
						ImGui::InputText("Name", newClipBuf, sizeof(newClipBuf));
						// 最初と 1 秒後のキーフレームを入れて追加する
						if (ImGui::Button("Add")) {
							MotionComponent::MotionClip clip;
							clip.name = newClipBuf;
							clip.keyframes.push_back({0.00f, {0,0,0}, {0,0,0}, {1,1,1}});
							clip.keyframes.push_back({1.00f, {1,0,0}, {0,0,0}, {1,1,1}});
							mc->clips[newClipBuf] = clip;
							mc->activeClip = newClipBuf;
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}

					// クリップが 1 つしか無いときは削除できない
					ImGui::BeginDisabled(mc->clips.size() <= 1);
					// 今のクリップを消し、先頭のクリップを選ぶ
					if (StructureButton("Delete Clip") && mc->clips.size() > 1) {
						mc->clips.erase(mc->activeClip);
						mc->activeClip = mc->clips.begin()->first;
						mc->selectedKeyframe = -1;
						mc->currentTime = 0.0f;
					}
					ImGui::EndDisabled();

					ImGui::Separator();
					// 今のクリップの設定
					if (mc->clips.count(mc->activeClip)) {
						auto& clip = mc->clips[mc->activeClip];
						ImGui::Text("Clip: %s", clip.name.c_str());
						// 再生中か・現在の時刻・長さ・ループ
						ImGui::Checkbox("Playing", &mc->isPlaying);
						ImGui::DragFloat("Current Time", &mc->currentTime, 0.01f, 0, clip.totalDuration);
						ImGui::DragFloat("Duration", &clip.totalDuration, 0.1f, 0.1f, 100.0f);
						ImGui::Checkbox("Loop", &clip.loop);
						
						if (StructureButton("Add Keyframe")) {
							// 末尾の時刻に、最後のキーフレームと同じ値で追加する
							MotionComponent::Keyframe k;
							k.time = clip.totalDuration;
							if (!clip.keyframes.empty()) {
								k.translate = clip.keyframes.back().translate;
								k.rotate = clip.keyframes.back().rotate;
								k.scale = clip.keyframes.back().scale;
							} else {
								// キーフレームが無ければ原点・回転なし・大きさ 1
								k.translate = {0,0,0};
								k.rotate = {0,0,0};
								k.scale = {1,1,1};
							}
							// 末尾に追加する
							clip.keyframes.push_back(k);
						}
						
						ImGui::Separator();
						ImGui::Text("Keyframes:");
						// キーフレームの一覧 (選んだものだけ時刻・位置・回転・大きさを編集できる)
						for (int i = 0; i < (int)clip.keyframes.size(); ++i) {
							char label[32]; sprintf_s(label, "KF %d", i);
							if (ImGui::Selectable(label, mc->selectedKeyframe == i)) {
								mc->selectedKeyframe = i;
							}
							if (mc->selectedKeyframe == i) {
								ImGuiIdScope keyframeId(label);
								ImGui::Indent();
								ImGui::DragFloat("Time", &clip.keyframes[i].time, 0.01f, 0, clip.totalDuration);
								ImGui::DragFloat3("Pos", &clip.keyframes[i].translate.x, 0.1f);
								ImGui::DragFloat3("Rot", &clip.keyframes[i].rotate.x, 0.01f);
								ImGui::DragFloat3("Scale", &clip.keyframes[i].scale.x, 0.01f);
								// このキーフレームを消す
								if (StructureButton("Remove KF")) {
									clip.keyframes.erase(clip.keyframes.begin() + i);
									mc->selectedKeyframe = -1;
								}
								ImGui::Unindent();
							}
						}
					}

					// コンポーネントを外す
					if (StructureButton("Remove Component##MC")) registry.remove<MotionComponent>(entity);
				}
			}
			// スクリプト: クラスの選択・パラメータの編集・追加と削除 (変更は描画の後でまとめて反映する)
			if (auto* cp = registry.try_get<ScriptComponent>(selected)) {
				ImGuiIdScope componentId("ScriptComponent");
				if (ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##Scripts", &cp->enabled);
					
					// 登録されているスクリプトのクラス名
					auto scriptNames = ScriptEngine::GetInstance()->GetRegisteredScriptNames();

					// 付いているスクリプトを順に表示する
					for (int i = 0; i < (int)cp->scripts.size(); ++i) {
						auto& entry = cp->scripts[i];
						ImGui::PushID(i);
						std::string treeLabel = (entry.scriptPath.empty() ? "None" : entry.scriptPath) + "##Tree";
						if (ImGui::TreeNodeEx(treeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
							// 今のクラス名の番号
							int current = -1;
							for (int j = 0; j < (int)scriptNames.size(); ++j) {
								if (scriptNames[j] == entry.scriptPath) { current = j; break; }
							}

							std::string comboName = entry.scriptPath.empty() ? "None" : entry.scriptPath;
							// クラスの選択 (変えたら差し替えを予約する)
							if (ImGui::BeginCombo("Class", comboName.c_str())) {
								for (int j = 0; j < (int)scriptNames.size(); ++j) {
									bool isSelected = (current == j);
									if (ImGui::Selectable(scriptNames[j].c_str(), isSelected)) {
										if (entry.scriptPath != scriptNames[j]) {
											pendingScriptEdit = ScriptEdit{ScriptEditKind::Replace, selected,
												static_cast<size_t>(i), entry.scriptPath, entry.instance, scriptNames[j]};
										}
									}
								}
								ImGui::EndCombo();
							}

							// クラス名があるのに実体が無ければ作る
							if (!entry.instance && !entry.scriptPath.empty()) {
								entry.instance = ScriptEngine::GetInstance()->CreateScript(entry.scriptPath);
								if (entry.instance) {
									char logBuf[1024];
									sprintf_s(logBuf, "[EditorUI] Inspector: Created instance for %s, Restoring params: %s\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
									OutputDebugStringA(logBuf);
									
									// パラメータが空でなければデシリアライズ
									if (!entry.parameterData.empty()) {
										entry.instance->DeserializeParameters(entry.parameterData);
									}
								} else if (entry.instance) {
									// パラメータが空の場合は初期状態をセット
									entry.parameterData = entry.instance->SerializeParameters();
									char logBuf[1024];
									sprintf_s(logBuf, "[EditorUI] Inspector: Creating instance for %s, Param was empty, setting default: %s\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
									OutputDebugStringA(logBuf);
								}
							}

							// 実体があれば、スクリプト自身の編集欄を描く
							if (entry.instance) {
								ImGui::PushID("ScriptEditor");
								
								// 描く前と後のパラメータ
								std::string oldParams = entry.parameterData;
								entry.instance->OnEditorUI();
								std::string newParams = entry.instance->SerializeParameters();

								// 比較を少し賢くする
								// プレイ中以外で、かつ実際に中身が意味のある変更をされた場合のみ更新
								if (!scene->GetIsPlaying()) {
									bool shouldUpdate = false;
									if (oldParams != newParams) {
										// "{}" から具体的な値への変化、または値同士の変化
										if (oldParams == "{}" || oldParams.empty()) {
											// 初期状態からの設定
											shouldUpdate = true;
										} else if (newParams != "{}" && !newParams.empty()) {
											// 値が変更された
											shouldUpdate = true;
										}
									}

									if (shouldUpdate) {
										entry.parameterData = newParams;
										char logBuf[1024];
										sprintf_s(logBuf, "[EditorUI] Parameter Changed: %s -> %s\n", oldParams.c_str(), newParams.c_str());
										OutputDebugStringA(logBuf);
									}
								}
								ImGui::PopID();
							}

							// このスクリプトを外す (予約する)
							if (StructureButton("Remove Script")) {
								pendingScriptEdit = ScriptEdit{ScriptEditKind::Remove, selected,
									static_cast<size_t>(i), entry.scriptPath, entry.instance, {}};
								// 削除後のループの番号ずれを避けるため、ここで抜ける
								ImGui::TreePop();
								ImGui::PopID();
								break; 
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
						ImGui::Separator();
					}

					// スクリプトの欄を増やす / コンポーネントごと外す (予約する)
					if (StructureButton("Add Script")) {
						pendingScriptEdit = ScriptEdit{ScriptEditKind::Add, selected, 0, {}, {}, {}};
					}
					ImGui::SameLine();
					if (StructureButton("Remove Component##SC"))
						pendingScriptEdit = ScriptEdit{ScriptEditKind::RemoveComponent, selected, 0, {}, {}, {}};
				}
			}
			// 平行光源
			if (auto* dl = registry.try_get<DirectionalLightComponent>(entity)) {
				ImGuiIdScope componentId("DirectionalLightComponent");
				if (ImGui::CollapsingHeader("DirectionalLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・色・強さ
					ImGui::Checkbox("Enabled##DL", &dl->enabled);
					ImGui::ColorEdit3("Light Color", &dl->color.x);
					ImGui::DragFloat("Intensity", &dl->intensity, 0.1f, 0, 100);
					if (StructureButton("Remove##DL")) registry.remove<DirectionalLightComponent>(entity);
				}
			}
			// 点光源
			if (auto* pl = registry.try_get<PointLightComponent>(entity)) {
				ImGuiIdScope componentId("PointLightComponent");
				if (ImGui::CollapsingHeader("PointLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・色・強さ・届く範囲
					ImGui::Checkbox("Enabled##PL", &pl->enabled);
					ImGui::ColorEdit3("Light Color", &pl->color.x);
					ImGui::DragFloat("Intensity", &pl->intensity, 0.1f, 0, 100);
					ImGui::DragFloat("Range", &pl->range, 0.1f, 0, 1000);
					if (StructureButton("Remove##PL")) registry.remove<PointLightComponent>(entity);
				}
			}
			// パーティクルエミッター: シェーダーの種類 (煙を選ぶと煙用の設定を入れる)、設定ファイルへの保存
			if (auto* pe = registry.try_get<ParticleEmitterComponent>(entity)) {
				ImGuiIdScope componentId("ParticleEmitterComponent");
				if (ImGui::CollapsingHeader("ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・設定ファイル
					ImGui::Checkbox("Enabled##PE", &pe->enabled);
					AssetField("Asset", pe->assetPath, {".json"});

					// シェーダーの種類の選択
					// シェーダーの選択肢 (表示名・シェーダー名・加算合成か)
					struct ShaderOption { const char* label; const char* shaderName; bool isAdditive; };
					static const ShaderOption shaderOptions[] = {
						{ "(Default) Particle",              "",                        false },
						{ "Particle (Alpha Blend)",          "Particle",               false },
						{ "Particle (Additive)",             "ParticleAdditive",       true  },
						{ "Procedural Smoke",                "ProceduralSmoke",        false },
						{ "Procedural Smoke (Additive)",     "ProceduralSmokeAdditive",true  },
					};
					static const int shaderOptionCount = IM_ARRAYSIZE(shaderOptions);

					// 今のシェーダーの番号
					int currentIdx = 0;
					for (int si = 0; si < shaderOptionCount; ++si) {
						if (pe->emitter.params.shaderName == shaderOptions[si].shaderName) {
							currentIdx = si;
							break;
						}
					}

					// 選んだらシェーダー名と加算合成を設定する
					if (ImGui::Combo("Shader Type##PE", &currentIdx, [](void* data, int idx, const char** out) -> bool {
						*out = ((const ShaderOption*)data)[idx].label;
						return true;
					}, (void*)shaderOptions, shaderOptionCount)) {
						pe->emitter.params.shaderName = shaderOptions[currentIdx].shaderName;
						pe->emitter.params.isAdditive = shaderOptions[currentIdx].isAdditive;

						// 煙のシェーダーを選んだら、煙らしい設定 (上昇・広がり・色) を入れる
						std::string sn = shaderOptions[currentIdx].shaderName;
						if (sn == "ProceduralSmoke" || sn == "ProceduralSmokeAdditive") {
							pe->emitter.params.startVelocity = {0, 1.5f, 0};
							pe->emitter.params.velocityVariance = {0.3f, 0.3f, 0.3f};
							pe->emitter.params.acceleration = {0, 0.5f, 0}; // 上昇気流
							pe->emitter.params.damping = 1.0f;
							pe->emitter.params.lifeTime = 3.0f;
							pe->emitter.params.lifeTimeVariance = 0.5f;
							pe->emitter.params.startSize = {1.5f, 1.5f, 1.5f};
							pe->emitter.params.endSize = {4.0f, 4.0f, 4.0f};
							pe->emitter.params.startColor = {0.85f, 0.9f, 0.95f, 0.7f};  // 水蒸気らしい薄青白いグレー
							pe->emitter.params.endColor = {0.6f, 0.65f, 0.7f, 0.0f}; // 滑らかに空気色にフェード
							pe->emitter.params.emitRate = 8.0f;
						}
					}

					// 設定ファイルへ保存する (パスが空ならエラー)
					if (ImGui::Button("Save Asset##PE")) {
						if (!pe->assetPath.empty()) {
							if (pe->emitter.SaveToJson(pe->assetPath)) {
								Log("Saved Particle Asset: " + pe->assetPath);
							} else {
								LogError("Failed to save Particle Asset: " + pe->assetPath);
							}
						} else {
							LogError("Particle Asset Path is empty.");
						}
					}
					ImGui::SameLine();
					if (StructureButton("Remove##PE")) registry.remove<ParticleEmitterComponent>(entity);
				}
			}
			// 流体エミッター
			if (auto* fec = registry.try_get<FluidEmitterComponent>(entity)) {
				ImGuiIdScope componentId("FluidEmitterComponent");
				if (ImGui::CollapsingHeader("FluidEmitter", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・1 フレームの数 (1 秒あたりの数も表示)
					ImGui::Checkbox("Enabled##FEC", &fec->enabled);
					ImGui::DragInt("Emit Rate", &fec->emitCountPerFrame, 1, 0, 1000);
					ImGui::Text("%d particles / second", fec->emitCountPerFrame * 60);
					// 水を残すか・合計の上限 (0 は無制限)
					ImGui::Checkbox("Retain water", &fec->retainWater);
					ImGui::DragInt("Particle limit (0 = continuous)", &fec->particleLimit, 100, 0, 25000);
					// 色・初速・流体の種類
					ImGui::ColorEdit4("Color", &fec->color.x);
					ImGui::DragFloat3("Velocity", &fec->velocity.x, 0.1f);
					ImGui::DragFloat("Fluid Type", &fec->fluidType, 0.1f, 0.0f, 1.0f);
					if (StructureButton("Remove##FEC")) registry.remove<FluidEmitterComponent>(entity);
				}
			}
			// 旧アクションゲーム用: 入力・移動
			if (auto* pi = registry.try_get<PlayerInputComponent>(entity)) {
				ImGuiIdScope componentId("PlayerInputComponent");
				if (ImGui::CollapsingHeader("PlayerInput", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・入力の向き (表示だけ)
					ImGui::Checkbox("Enabled##PI", &pi->enabled);
					ImGui::Text("MoveDir: (%.2f, %.2f)", pi->moveDir.x, pi->moveDir.y);
					if (StructureButton("Remove##PI")) registry.remove<PlayerInputComponent>(entity);
				}
			}
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				ImGuiIdScope componentId("CharacterMovementComponent");
				if (ImGui::CollapsingHeader("CharacterMovement", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・速さ・ジャンプ力・重力
					ImGui::Checkbox("Enabled##CM", &cm->enabled);
					ImGui::DragFloat("Speed", &cm->speed, 0.1f);
					ImGui::DragFloat("Jump Power", &cm->jumpPower, 0.1f);
					ImGui::DragFloat("Gravity", &cm->gravity, 0.1f);
					if (StructureButton("Remove##CM")) registry.remove<CharacterMovementComponent>(entity);
				}
			}
			// スポットライト
			if (auto* sl = registry.try_get<SpotLightComponent>(entity)) {
				ImGuiIdScope componentId("SpotLightComponent");
				if (ImGui::CollapsingHeader("SpotLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・色・強さ・届く範囲
					ImGui::Checkbox("Enabled##SL", &sl->enabled);
					ImGui::ColorEdit3("Color", &sl->color.x);
					ImGui::DragFloat("Intensity", &sl->intensity, 0.1f);
					ImGui::DragFloat("Range", &sl->range, 0.1f);
					if (StructureButton("Remove##SL")) registry.remove<SpotLightComponent>(entity);
				}
			}
			// UI: 配置・画像・文字・ボタン
			if (auto* rt = registry.try_get<RectTransformComponent>(entity)) {
				ImGuiIdScope componentId("RectTransformComponent");
				if (ImGui::CollapsingHeader("RectTransform", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・位置・大きさ・基準点
					ImGui::Checkbox("Enabled##RT", &rt->enabled);
					ImGui::DragFloat2("Pos", &rt->pos.x);
					ImGui::DragFloat2("Size", &rt->size.x);
					ImGui::DragFloat2("Anchor", &rt->anchor.x, 0.01f, 0, 1);
					if (StructureButton("Remove##RT")) registry.remove<RectTransformComponent>(entity);
				}
			}
			if (auto* img = registry.try_get<UIImageComponent>(entity)) {
				ImGuiIdScope componentId("UIImageComponent");
				if (ImGui::CollapsingHeader("UIImage", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・テクスチャ (変えたら読み込み直す)
					ImGui::Checkbox("Enabled##IMG", &img->enabled);
					if (AssetField("Texture", img->texturePath, {".png", ".jpg"})) {
						img->textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(img->texturePath, false);
					}
					// 色・重なり順
					ImGui::ColorEdit4("Color", &img->color.x);
					ImGui::DragInt("Layer", &img->layer, 1, -100, 100);
					// 9 分割の画像なら、上下左右の境界を設定する
					ImGui::Checkbox("9-Slice", &img->is9Slice);
					if (img->is9Slice) {
						ImGui::DragFloat("Border Top", &img->borderTop, 1.0f, 0, 500);
						ImGui::DragFloat("Border Bottom", &img->borderBottom, 1.0f, 0, 500);
						ImGui::DragFloat("Border Left", &img->borderLeft, 1.0f, 0, 500);
						ImGui::DragFloat("Border Right", &img->borderRight, 1.0f, 0, 500);
					}
					if (StructureButton("Remove##IMG")) registry.remove<UIImageComponent>(entity);
				}
			}
			if (auto* txt = registry.try_get<UITextComponent>(entity)) {
				ImGuiIdScope componentId("UITextComponent");
				if (ImGui::CollapsingHeader("UIText", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・文字 (複数行)
					ImGui::Checkbox("Enabled##TXT", &txt->enabled);
					char tbuf[1024]; CopyToBuffer(tbuf, txt->text);
					if (ImGui::InputTextMultiline("Text", tbuf, sizeof(tbuf), ImVec2(0, ImGui::GetTextLineHeight() * 3.5f))) txt->text = tbuf;
					// 大きさ・色
					ImGui::DragFloat("Font Size", &txt->fontSize, 1.0f, 1, 100);
					ImGui::ColorEdit4("Color##TXT", &txt->color.x);

					// フォント選択ドロップダウン
					std::vector<std::string> fonts = { "C:\\Windows\\Fonts\\msgothic.ttc" };
					// Resources/Fonts の中のフォントも一覧に入れる
					std::string fontDir = GetUnifiedProjectPath("Resources/Fonts");
					std::error_code fontError;
					for (auto& p : fs::directory_iterator(Engine::PathUtils::FromUTF8(fontDir), fontError)) {
						std::string ext = ToLower(Engine::PathUtils::ToUTF8(p.path().extension().wstring()));
						if (ext == ".ttf" || ext == ".ttc" || ext == ".otf") {
							fonts.push_back(GetUnifiedProjectPath("Resources/Fonts/" + Engine::PathUtils::ToUTF8(p.path().filename().wstring())));
						}
					}

					// フォントの選択
					if (ImGui::BeginCombo("Font Path", txt->fontPath.c_str())) {
						for (const auto& f : fonts) {
							bool isSelected = (txt->fontPath == f);
							if (ImGui::Selectable(f.c_str(), isSelected)) txt->fontPath = f;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					// 一覧に無いフォントを指定する
					AssetField("Custom Font", txt->fontPath, { ".ttf", ".ttc", ".otf" });

					// RectTransform が無ければ、付けるためのボタンを出す (橙)
					if (!registry.all_of<RectTransformComponent>(entity)) {
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
						if (ImGui::Button("Add RectTransform (Show UI Gizmo)")) {
							registry.emplace<RectTransformComponent>(entity);
						}
						ImGui::PopStyleColor();
					}

					if (StructureButton("Remove##TXT")) registry.remove<UITextComponent>(entity);
				}
			}
			if (auto* btn = registry.try_get<UIButtonComponent>(entity)) {
				ImGuiIdScope componentId("UIButtonComponent");
				if (ImGui::CollapsingHeader("UIButton", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・マウスが乗っているか (表示だけ)
					ImGui::Checkbox("Enabled##BTN", &btn->enabled);
					ImGui::Text("Hovered: %s", btn->isHovered ? "Yes" : "No");
					
					// クリック判定の範囲を個別に調整する
					ImGui::DragFloat2("Hitbox Offset", &btn->hitboxOffset.x, 1.0f);
					ImGui::DragFloat2("Hitbox Scale", &btn->hitboxScale.x, 0.01f, 0.0f, 10.0f);
					
					if (StructureButton("Remove##BTN")) registry.remove<UIButtonComponent>(entity);
				}
			}
			// 川
			if (auto* riv = registry.try_get<RiverComponent>(entity)) {
				ImGuiIdScope componentId("RiverComponent");
				if (ImGui::CollapsingHeader("River", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・幅・流れの速さ・テクスチャ
					ImGui::Checkbox("Enabled##RIV", &riv->enabled);
					ImGui::DragFloat("Width", &riv->width, 0.1f);
					ImGui::DragFloat("Speed", &riv->flowSpeed, 0.1f);
					AssetField("Texture", riv->texturePath, {".png", ".jpg"});
					if (StructureButton("Remove##RIV")) registry.remove<RiverComponent>(entity);
				}
			}
			// アニメーター: モデルに含まれるクリップから選ぶ
			if (auto* anim = registry.try_get<AnimatorComponent>(entity)) {
				ImGuiIdScope componentId("AnimatorComponent");
				if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##ANIM", &anim->enabled);
					// メッシュのモデルにアニメーションがあれば、その一覧から選ぶ
					if (auto* mr = registry.try_get<MeshRendererComponent>(entity)) {
						if (mr->modelHandle != 0) {
							auto* m = Engine::Renderer::GetInstance()->GetModel(mr->modelHandle);
							if (m && !m->GetData().animations.empty()) {
								if (ImGui::BeginCombo("Clips", anim->currentAnimation.c_str())) {
									for (const auto& a : m->GetData().animations) {
										if (ImGui::Selectable(a.name.c_str(), anim->currentAnimation == a.name)) {
											anim->currentAnimation = a.name;
											anim->time = 0.0f;
										}
									}
									ImGui::EndCombo();
								}
							} else {
								ImGui::Text("No animations in model.");
							}
						} else {
							ImGui::Text("No Model attached.");
						}
					} else {
						ImGui::Text("Require MeshRenderer.");
					}

					// 速さ・再生中か・ループ・骨の表示
					ImGui::DragFloat("Speed", &anim->speed, 0.1f);
					ImGui::Checkbox("Playing", &anim->isPlaying);
					ImGui::SameLine();
					ImGui::Checkbox("Loop", &anim->loop);
					ImGui::Checkbox("Draw Skeleton (Debug)", &anim->drawSkeleton);

					if (StructureButton("Remove##ANIM")) registry.remove<AnimatorComponent>(entity);
				}
			}
			// 旧アクションゲーム用: カメラ追従・攻撃判定・食らい判定・頭上の HP バー
			if (auto* ct = registry.try_get<CameraTargetComponent>(entity)) {
				ImGuiIdScope componentId("CameraTargetComponent");
				if (ImGui::CollapsingHeader("CameraTarget", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・距離・高さ・なめらかさ
					ImGui::Checkbox("Enabled##CT", &ct->enabled);
					ImGui::DragFloat("Dist", &ct->distance, 0.1f);
					ImGui::DragFloat("Height", &ct->height, 0.1f);
					ImGui::DragFloat("Smooth", &ct->smoothSpeed, 0.1f);
					if (StructureButton("Remove##CT")) registry.remove<CameraTargetComponent>(entity);
				}
			}
			if (auto* hb = registry.try_get<HitboxComponent>(entity)) {
				ImGuiIdScope componentId("HitboxComponent");
				if (ImGui::CollapsingHeader("Hitbox", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・中心・大きさ・ダメージ
					ImGui::Checkbox("Enabled##HB", &hb->enabled);
					ImGui::DragFloat3("Ofs##HB", &hb->center.x, 0.1f);
					ImGui::DragFloat3("Size##HB", &hb->size.x, 0.1f);
					ImGui::DragFloat("DMG##HB", &hb->damage, 1.0f);
					
					// タグの選択
					const char* currentTagName = TagToString(hb->tag);
					if (ImGui::BeginCombo("Tag##HBTagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::VFX; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (hb->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) hb->tag = t;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					// 有効か (攻撃中か)
					ImGui::Checkbox("Active##HB", &hb->isActive);
					if (StructureButton("Remove##HB")) registry.remove<HitboxComponent>(entity);
				}
			}
			if (auto* hb = registry.try_get<HurtboxComponent>(entity)) {
				ImGuiIdScope componentId("HurtboxComponent");
				if (ImGui::CollapsingHeader("Hurtbox", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・中心・大きさ・ダメージ倍率
					ImGui::Checkbox("Enabled##HurtB", &hb->enabled);
					ImGui::DragFloat3("Ofs##HurtB", &hb->center.x, 0.1f);
					ImGui::DragFloat3("Size##HurtB", &hb->size.x, 0.1f);
					ImGui::DragFloat("Mult##HurtB", &hb->damageMultiplier, 0.1f);

					// タグの選択
					const char* currentTagName = TagToString(hb->tag);
					if (ImGui::BeginCombo("Tag##HurtBTagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::VFX; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (hb->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) hb->tag = t;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					if (StructureButton("Remove##HurtB")) registry.remove<HurtboxComponent>(entity);
				}
			}
			if (auto* ws = registry.try_get<WorldSpaceUIComponent>(entity)) {
				ImGuiIdScope componentId("WorldSpaceUIComponent");
				if (ImGui::CollapsingHeader("WorldSpaceUI", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か・HP バーを出すか・頭上のずれ
					ImGui::Checkbox("Enabled##WS", &ws->enabled);
					ImGui::Checkbox("HP Bar##WS", &ws->showHealthBar);
					ImGui::DragFloat3("Ofs##WS", &ws->offset.x, 0.1f);
					if (StructureButton("Remove##WS")) registry.remove<WorldSpaceUIComponent>(entity);
				}
			}
			// 音を聞く位置
			if (auto* al = registry.try_get<AudioListenerComponent>(entity)) {
				ImGuiIdScope componentId("AudioListenerComponent");
				if (ImGui::CollapsingHeader("AudioListener", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##AL", &al->enabled);
					ImGui::Text("Listening...");
					if (StructureButton("Remove##AL")) registry.remove<AudioListenerComponent>(entity);
				}
			}
			// 汎用の変数 (数値)
			if (auto* var = registry.try_get<VariableComponent>(entity)) {
				ImGuiIdScope componentId("VariableComponent");
				if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 有効か
					ImGui::Checkbox("Enabled##VAR", &var->enabled);
					// 数値の変数を順に編集する
					for (auto& [k, v] : var->values) {
						ImGui::PushID(k.c_str());
						ImGui::DragFloat(k.c_str(), &v, 0.1f);
						ImGui::PopID();
					}
					if (StructureButton("Remove##VAR")) registry.remove<VariableComponent>(entity);
				}
			}
		}
		
		// コンポーネントの追加ボタン
		ImGui::Separator();
		DrawAddComponentButton(registry, entity, scene->GetIsPlaying());
		// スクリプトの変更の予約があれば、描画の後でここで反映する
		if (pendingScriptEdit) ApplyScriptEdit(registry, *pendingScriptEdit, scene);
		ImGui::PopID();
	} else {
		ImGui::Text("No Selection");
	}
	// ウィンドウの End は呼び出し側で行う
}

// Project ウィンドウ: Resources 以下をアイコンで表示する
void EditorUI::ShowProject([[maybe_unused]] Engine::Renderer* renderer, [[maybe_unused]] GameScene* scene) {
	// ウィンドウを開く
	ImGui::Begin(kProjectWindow, &s_windows.project);
	// currentDir はプロジェクト相対の UTF-8 パス ("Resources/Models")。
	// 以前は作業ディレクトリ基準で列挙しており、exe の場所によってはフォルダが無く例外で落ちていた
	static std::string currentDir = "Resources";
	static char searchBuf[128] = "";
	static std::map<std::string, uint32_t> s_thumbnails; // 読み込んだサムネイルのテクスチャ (パス → ハンドル)

	// 上部のバー (戻る・現在のフォルダ・検索)
	ImGui::BeginDisabled(currentDir == "Resources");
	// Back: 1 つ上のフォルダーへ (Resources より上へは行かない)
	if (ImGui::Button("Back")) {
		size_t slash = currentDir.find_last_of('/');
		currentDir = slash == std::string::npos ? "Resources" : currentDir.substr(0, slash);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	// 今のフォルダー
	ImGui::TextUnformatted(currentDir.c_str());
	// 右端に検索欄と Clear ボタンを置く
	const float searchWidth = 180.0f;
	const float clearWidth = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - searchWidth - clearWidth - ImGui::GetStyle().ItemSpacing.x));
	ImGui::SetNextItemWidth(searchWidth);
	ImGui::InputTextWithHint("##Search", "Search Assets...", searchBuf, sizeof(searchBuf));
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter files in current directory");
	ImGui::SameLine();
	if (ImGui::Button("Clear##ProjectSearch")) searchBuf[0] = '\0';

	ImGui::Separator();

	// アイコンを並べる格子の設定
	// セルの大きさと、1 行に並べる数
	float padding = 16.0f;
	float thumbnailSize = 64.0f;
	float cellSize = thumbnailSize + padding;
	float panelWidth = ImGui::GetContentRegionAvail().x;
	int columnCount = (int)(panelWidth / cellSize);
	if (columnCount < 1) columnCount = 1;

	// フォルダーの中身 (名前とフォルダーか)
	struct Entry { std::string name; bool directory; };
	std::vector<Entry> entries;
	std::error_code ec;
	// 今のフォルダーの中を列挙する (例外を出さない版)
	const std::wstring absDir = Engine::PathUtils::FromUTF8(GetUnifiedProjectPath(currentDir));
	for (fs::directory_iterator it(absDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
		std::error_code typeError;
		entries.push_back({Engine::PathUtils::ToUTF8(it->path().filename().wstring()), it->is_directory(typeError)});
	}
	// 開けなければ、その旨と Resources へ戻るボタンを出す
	if (ec && entries.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Cannot open %s", currentDir.c_str());
		if (ImGui::Button("Return to Resources")) currentDir = "Resources";
	}
	// フォルダを先に、名前順に並べる
	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		if (a.directory != b.directory) return a.directory;
		return ToLower(a.name) < ToLower(b.name);
	});

	// スクロールする領域に列で並べる
	ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::Columns(columnCount, 0, false);

	// 検索文字 (小文字) と、次に開くフォルダー
	const std::string searchStr = ToLower(searchBuf);
	std::string nextDir;
	int i = 0; // 同じ名前のファイルがあっても ID が衝突しないよう、番号で PushID する
	for (const auto& entry : entries) {
		// 検索に合わないものは飛ばす
		if (!searchStr.empty() && ToLower(entry.name).find(searchStr) == std::string::npos)
			continue;

		ImGui::PushID(i++);
		// プロジェクト相対のパスと拡張子
		const std::string relPath = currentDir + "/" + entry.name;
		const std::string ext = entry.directory ? std::string() : ToLower(Engine::PathUtils::ToUTF8(fs::path(Engine::PathUtils::FromUTF8(entry.name)).extension().wstring()));

		// アイコンを種類で選ぶ (フォルダー・モデル・プレハブ・音・スクリプト)
		uint32_t icon = s_icons.file;
		if (entry.directory) {
			icon = s_icons.folder;
		} else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") icon = s_icons.model;
		else if (ext == ".prefab") icon = s_icons.prefab;
		else if (ext == ".wav" || ext == ".mp3") icon = s_icons.audio;
		else if (ext == ".h" || ext == ".cpp" || ext == ".json") icon = s_icons.script;
		else if (ext == ".png" || ext == ".jpg") {
			// 画像はサムネイルを表示する
			auto found = s_thumbnails.find(relPath);
			if (found == s_thumbnails.end()) found = s_thumbnails.emplace(relPath, Engine::Renderer::GetInstance()->LoadTexture2D(relPath, false)).first;
			icon = found->second;
		}

		// 背景の無いアイコンのボタン
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		const bool clicked = ImGui::ImageButton("##asset", (ImTextureID)renderer->GetTextureSrvGpu(icon).ptr, ImVec2(thumbnailSize, thumbnailSize));
		ImGui::PopStyleColor();
		// フォルダーをクリックしたら、描き終わってから開く
		if (clicked && entry.directory) nextDir = relPath;
		// パスと操作の案内をツールチップで出す
		if (ImGui::IsItemHovered()) {
			const char* hint = (ext == ".json" || ext == ".prefab") ? "\n(double-click to open / instantiate)" : "";
			ImGui::SetTooltip("%s%s", relPath.c_str(), hint);
		}
		// ダブルクリック: フォルダを開く / シーンを読み込む / プレハブを配置する
		if (!entry.directory && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			if (ext == ".json" && currentDir.rfind("Resources/Scenes", 0) == 0 && scene && !scene->GetIsPlaying()) {
				LoadScene(scene, relPath);
			} else if (ext == ".prefab" && scene && !scene->GetIsPlaying()) {
				auto created = LoadPrefab(scene, relPath);
				if (!created.empty()) scene->SelectEntity(created.front());
			}
		}

		// ドラッグ元: プロジェクト相対のパスを運ぶ
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("PROJECT_ASSET", relPath.c_str(), relPath.size() + 1);
			ImGui::Text("%s", entry.name.c_str());
			ImGui::EndDragDropSource();
		}

		// 名前
		ImGui::TextWrapped("%s", entry.name.c_str());
		ImGui::NextColumn();
		ImGui::PopID();
	}

	ImGui::Columns(1);
	ImGui::EndChild();
	// 次のフォルダーへ移る
	if (!nextDir.empty()) currentDir = nextDir;
	ImGui::End();
}

// Lighting ウィンドウ: ポストエフェクトの種類とパラメータ、背景のキューブマップ
void EditorUI::ShowSceneSettings(Engine::Renderer* renderer) {
	// ウィンドウの Begin は呼び出し側で行う
	// 今のパラメータと、変更されたか
	auto pp = renderer->GetPostProcessParams();
	bool ch = false;

	// ポストエフェクトの種類 (常に表示)
	static const char* ppTypes[] = { "None", "Default(CRT)", "Rich", "Anime", "Grayscale", "Smoothing", "GaussianFilter", "OutlinePost", "RadialBlur", "Random", "PaperFrame" };
	static int currentPP = 1; // デフォルトはCRT
	// シーン読み込みで無効化された場合などに表示が食い違わないよう、実際の状態へ合わせる
	if (!renderer->GetPostProcessEnabled()) currentPP = 0;
	else if (currentPP == 0) currentPP = 1;
	// 種類の選択 (None は無効、Default は CRT、それ以外は名前で設定する)
	if (ImGui::BeginCombo("PostEffect", ppTypes[currentPP])) {
		for (int n = 0; n < IM_ARRAYSIZE(ppTypes); n++) {
			bool isSelected = (currentPP == n);
			if (ImGui::Selectable(ppTypes[n], isSelected)) {
				currentPP = n;
				if (currentPP == 0) {
					renderer->SetPostProcessEnabled(false);
				} else if (currentPP == 1) {
					renderer->SetPostEffect("");
					renderer->SetPostProcessEnabled(true);
				} else {
					renderer->SetPostEffect(ppTypes[currentPP]);
				}
			}
			if (isSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// パラメータ（ポストエフェクト有効時のみ表示）
	if (currentPP > 0) {
		ImGui::Separator();
		// 周辺減光・ノイズ・歪み・色ずれ・走査線・SAN 値
		ch |= ImGui::DragFloat("Vignette", &pp.vignette, 0.01f);
		ch |= ImGui::DragFloat("Noise", &pp.noiseStrength, 0.01f);
		ch |= ImGui::DragFloat("Distortion", &pp.distortion, 0.001f);
		ch |= ImGui::DragFloat("ChromaShift", &pp.chromaShift, 0.0001f);
		ch |= ImGui::DragFloat("Scanline", &pp.scanline, 0.01f);
		ch |= ImGui::DragFloat("SAN", &pp.san, 0.01f, 0.0f, 1.0f);
	}
	// 変わったら反映する
	if (ch) renderer->SetPostProcessParams(pp);

	// 背景にキューブマップを使うか
	bool useCubemapBg = renderer->GetUseCubemapBackground();
	if (ImGui::Checkbox("Use Cubemap Background", &useCubemapBg)) {
		renderer->SetUseCubemapBackground(useCubemapBg);
	}
}

// Console ウィンドウ
void EditorUI::ShowConsole() {
	// ウィンドウを開く
	ImGui::Begin(kConsoleWindow, &s_windows.console);
	// 表示する種類・自動スクロール・まとめ表示と、絞り込みの文字
	static bool showInfo = true, showWarning = true, showError = true, autoScroll = true, collapse = false;
	static char filter[128] = "";
	// Clear: ログを消す / Copy: すべてのログをクリップボードへ
	if (ImGui::Button("Clear##Console")) consoleLog.clear();
	ImGui::SameLine();
	if (ImGui::Button("Copy")) {
		std::string all;
		for (const auto& log : consoleLog) all += log.message + "\n";
		ImGui::SetClipboardText(all.c_str());
	}
	ImGui::SameLine();
	ImGui::Checkbox("Collapse", &collapse);
	ImGui::SameLine();
	// 種類ごとの表示の切り替え (件数付き)
	ImGui::Checkbox(("Info " + std::to_string(CountLogs(LogLevel::Info))).c_str(), &showInfo);
	ImGui::SameLine();
	ImGui::Checkbox(("Warn " + std::to_string(CountLogs(LogLevel::Warning))).c_str(), &showWarning);
	ImGui::SameLine();
	ImGui::Checkbox(("Error " + std::to_string(CountLogs(LogLevel::Error))).c_str(), &showError);
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &autoScroll);
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##ConsoleFilter", "Filter...", filter, sizeof(filter));
	ImGui::Separator();

	// 絞り込みの文字 (小文字)
	const std::string needle = ToLower(filter);
	ImGui::BeginChild("ConsoleLines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	// Collapse: 同じ内容のログを1行にまとめて件数を表示する (Unity と同じ)
	std::map<std::string, int> repeats;
	if (collapse) for (const auto& log : consoleLog) ++repeats[log.message];
	std::map<std::string, bool> shown;
	// ログを順に表示する
	for (const auto& log : consoleLog) {
		// 表示しない種類と、絞り込みに合わないものは飛ばす
		if ((log.level == LogLevel::Info && !showInfo) || (log.level == LogLevel::Warning && !showWarning) || (log.level == LogLevel::Error && !showError)) continue;
		if (!needle.empty() && ToLower(log.message).find(needle) == std::string::npos) continue;
		// まとめ表示なら、同じ内容は最初の 1 回だけ出す
		if (collapse) {
			if (shown[log.message]) continue;
			shown[log.message] = true;
		}
		// エラーは赤、警告は黄
		ImVec4 color = log.level == LogLevel::Error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) :
			log.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.35f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
		// 時刻と内容
		ImGui::TextDisabled("%7.2f", log.time);
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(log.message.c_str());
		ImGui::PopStyleColor();
		if (collapse && repeats[log.message] > 1) {
			ImGui::SameLine();
			ImGui::TextDisabled("(%d)", repeats[log.message]);
		}
	}
	// 一番下にいれば、新しいログに合わせてスクロールする
	if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
	ImGui::EndChild();
	ImGui::End();
}

// シーン内のアニメーション (スキニング Animator / キーフレーム Motion) を一覧し、まとめて再生・停止・選択する
void EditorUI::ShowAnimationWindow(Engine::Renderer* renderer, GameScene* scene) {
	// シーンが無ければ何もしない
	if (!scene) return;
	// このシーンの ECS と、表示した数
	auto& reg = scene->GetComponents();
	int count = 0;
	// すべて再生 / すべて停止
	if (ImGui::Button("Play all")) {
		for (auto e : reg.view<AnimatorComponent>()) reg.get<AnimatorComponent>(e).isPlaying = true;
		for (auto e : reg.view<MotionComponent>()) reg.get<MotionComponent>(e).isPlaying = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop all")) {
		for (auto e : reg.view<AnimatorComponent>()) reg.get<AnimatorComponent>(e).isPlaying = false;
		for (auto e : reg.view<MotionComponent>()) reg.get<MotionComponent>(e).isPlaying = false;
	}
	ImGui::Separator();
	// 表の列: 物・クリップ・再生・速さ
	if (ImGui::BeginTable("AnimationList", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
		ImGui::TableSetupColumn("Object");
		ImGui::TableSetupColumn("Clip");
		ImGui::TableSetupColumn("Play", ImGuiTableColumnFlags_WidthFixed, 40.0f);
		ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableHeadersRow();
		// 名前の列 (クリックで選ぶ)
		auto drawName = [&](entt::entity e) {
			ImGui::TableNextColumn();
			const char* name = reg.all_of<NameComponent>(e) ? reg.get<NameComponent>(e).name.c_str() : "Object";
			if (ImGui::Selectable(name, scene->GetSelectedEntity() == e, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) scene->SelectEntity(e);
		};
		// アニメーター: モデルのクリップから選ぶ
		for (auto e : reg.view<AnimatorComponent>()) {
			auto& anim = reg.get<AnimatorComponent>(e);
			ImGui::PushID(static_cast<int>(entt::to_integral(e)));
			ImGui::TableNextRow();
			drawName(e);
			ImGui::TableNextColumn();
			// メッシュのモデル
			const Engine::Model* model = nullptr;
			if (auto* mr = reg.try_get<MeshRendererComponent>(e); mr && mr->modelHandle != 0 && renderer) model = renderer->GetModel(mr->modelHandle);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (model && !model->GetData().animations.empty()) {
				if (ImGui::BeginCombo("##Clip", anim.currentAnimation.c_str())) {
					for (const auto& a : model->GetData().animations) {
						if (ImGui::Selectable(a.name.c_str(), anim.currentAnimation == a.name)) {
							anim.currentAnimation = a.name;
							anim.time = 0.0f;
						}
					}
					ImGui::EndCombo();
				}
			} else {
				ImGui::TextDisabled("%s", anim.currentAnimation.empty() ? "(no clips)" : anim.currentAnimation.c_str());
			}
			// 再生中か・速さ
			ImGui::TableNextColumn();
			ImGui::Checkbox("##Play", &anim.isPlaying);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::DragFloat("##Speed", &anim.speed, 0.05f, 0.0f, 10.0f, "%.2fx");
			ImGui::PopID();
			++count;
		}
		// モーション: クリップから選ぶ (ID がアニメーターと重ならないようにずらす)
		for (auto e : reg.view<MotionComponent>()) {
			auto& motion = reg.get<MotionComponent>(e);
			ImGui::PushID(static_cast<int>(entt::to_integral(e)) + 0x40000000);
			ImGui::TableNextRow();
			drawName(e);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("##MotionClip", motion.activeClip.c_str())) {
				for (auto& [clipName, clip] : motion.clips) {
					if (ImGui::Selectable(clipName.c_str(), motion.activeClip == clipName)) {
						motion.activeClip = clipName;
						motion.currentTime = 0.0f;
						motion.selectedKeyframe = -1;
					}
				}
				ImGui::EndCombo();
			}
			// 再生中か
			ImGui::TableNextColumn();
			ImGui::Checkbox("##Play", &motion.isPlaying);
			ImGui::TableNextColumn();
			ImGui::TextDisabled("motion");
			ImGui::PopID();
			++count;
		}
		ImGui::EndTable();
	}
	if (count == 0) ImGui::TextDisabled("No Animator or Motion components in the scene.");
}
// 画面上の点からワールド空間のレイ (原点と向き) を求める
void EditorUI::ScreenToWorldRay(float screenX, float screenY, float imageW, float imageH, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, DirectX::XMVECTOR& outOrig, DirectX::XMVECTOR& outDir) {
	// ビュー行列と射影行列の逆行列
	DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
	DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);

	// 画面の位置を -1〜1 の正規化デバイス座標にする
	float vx = (2.0f * screenX / imageW - 1.0f);
	float vy = (1.0f - 2.0f * screenY / imageH);

	// 奥の面の点をビュー空間、ワールド空間へ戻す
	DirectX::XMVECTOR rayEndNDC = DirectX::XMVectorSet(vx, vy, 1.0f, 1.0f);
	DirectX::XMVECTOR rayEndView = DirectX::XMVector3TransformCoord(rayEndNDC, invProj);
	DirectX::XMVECTOR rayEndWorld = DirectX::XMVector3TransformCoord(rayEndView, invView);

	// 原点はカメラの位置、向きはそこから奥の点へ
	outOrig = invView.r[3];
	outDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(rayEndWorld, outOrig));
}

} // namespace Game

