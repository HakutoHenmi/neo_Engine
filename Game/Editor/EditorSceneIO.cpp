// EditorSceneIO.cpp
// エディタのシーンの読み書き: JSON からの復元 (RestoreSceneFromJson)、JSON への書き出し (SerializeEntity)、
// シーンファイルの保存・読み込み・追加、メモリ上の保存と復元 (PLAY の巻き戻し)、プレハブの読み込み。
#include "EditorInternal.h"

namespace Game {
// エディタの内部で共有する状態と関数 (EditorInternal.h)
using namespace EditorInternal;

// 現在開いているシーンファイル (保存先の既定値)
// 空のときは本編のシーンファイル (game.json の mainStage) に保存する
std::string EditorUI::currentScenePath;

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
std::vector<entt::entity> EditorInternal::RestoreSceneFromJson(GameScene* scene, const json& j, bool append) {
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
std::string EditorInternal::SerializeEntity(SceneComponents& registry, entt::entity entity) {
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

} // namespace Game
