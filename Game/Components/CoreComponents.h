// CoreComponents.h
// どのエンティティにも使う基本のコンポーネント: 名前・親子・位置・色・エディタの状態・タグ・変数・モーション・スクリプト・自動削除・体力。
// ObjectTypes.h がすべての Components/*.h をまとめて読み込む。
#pragma once
#include "ComponentBase.h"

namespace Game {

// エンティティの表示名 (Hierarchy に表示され、保存時の名前にもなる)
struct NameComponent {
	std::string name = "Object";
	NameComponent() = default;
	NameComponent(const std::string& n) : name(n) {}
};

// 親子関係。parentId が有効なら親の行列を掛けてワールド行列を求める
struct HierarchyComponent {
	entt::entity parentId = entt::null;
};

// 位置・回転 (ラジアン)・拡大率。ワールド行列の元になる
struct TransformComponent : public Component {
	DirectX::XMFLOAT3 translate = {0, 0, 0};
	DirectX::XMFLOAT3 rotate = {0, 0, 0};
	DirectX::XMFLOAT3 scale = {1, 1, 1};
	
	TransformComponent() { type = ComponentType::RectTransform; }
	TransformComponent(const DirectX::XMFLOAT3& t, const DirectX::XMFLOAT3& r, const DirectX::XMFLOAT3& s)
		: translate(t), rotate(r), scale(s) { type = ComponentType::RectTransform; }

	// エンジン側の Transform 型へ変換する
	Engine::Transform GetTransform() const {
		Engine::Transform t;
		t.translate = {translate.x, translate.y, translate.z};
		t.rotate = {rotate.x, rotate.y, rotate.z};
		t.scale = {scale.x, scale.y, scale.z};
		return t;
	}
	// 拡大→回転→平行移動の順でローカル行列を作る
	Engine::Matrix4x4 ToMatrix() const {
		using namespace DirectX;
		XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
		XMMATRIX R = XMMatrixRotationRollPitchYaw(rotate.x, rotate.y, rotate.z);
		XMMATRIX T = XMMatrixTranslation(translate.x, translate.y, translate.z);
		Engine::Matrix4x4 out;
		XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&out), S * R * T);
		return out;
	}
};

// 描画色だけを上書きしたいときに付ける色
struct ColorComponent {
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
};

// エディタ専用の状態。locked はビューポートでクリック選択させない設定
struct EditorStateComponent {
	bool locked = false;
	bool isPendingDestroy = false;
};

// エンティティの役割を表すタグ。検索 (GameScene::GetEntitiesByTag) や当たり判定の相手判定に使う
enum class TagType : uint32_t {
	Untagged = 0,
	Player,
	Enemy,
	Bullet,
	Projectile,
	Wall,
	Default,
	VFX,
	PauseUI // ポーズメニューの UI
};

// タグ → 文字列 (保存・表示用)
inline const char* TagToString(TagType tag) {
	switch (tag) {
	case TagType::Player: return "Player";
	case TagType::Enemy: return "Enemy";
	case TagType::Bullet: return "Bullet";
	case TagType::Projectile: return "Projectile";
	case TagType::Wall: return "Wall";
	case TagType::VFX: return "VFX";
	case TagType::Default: return "Default";
	case TagType::PauseUI: return "PauseUI";
	default: return "Untagged";
	}
}

// 文字列 → タグ (読み込み用)。知らない文字列は Untagged
inline TagType StringToTag(const std::string& s) {
	if (s == "Player") return TagType::Player;
	if (s == "Enemy") return TagType::Enemy;
	if (s == "Bullet") return TagType::Bullet;
	if (s == "Projectile") return TagType::Projectile;
	if (s == "Wall") return TagType::Wall;
	if (s == "VFX") return TagType::VFX;
	if (s == "Default") return TagType::Default;
	if (s == "PauseUI") return TagType::PauseUI;
	return TagType::Untagged;
}

// タグを持たせるコンポーネント
struct TagComponent : public Component {
	TagType tag = TagType::Untagged;
	TagComponent() { type = ComponentType::Tag; }
	TagComponent(TagType t) : tag(t) { type = ComponentType::Tag; }
};

// HP・スタミナと被弾演出の状態。HP は直接書き換えず関数経由で操作する
class HealthComponent : public Component {
public:
    // 現在値の取得
    float Hp() const { return hp_; }
    float MaxHp() const { return maxHp_; }
    float Stamina() const { return stamina_; }
    float MaxStamina() const { return maxStamina_; }
    bool IsDead() const { return isDead_; }

    // 初期化・エディタでの変更・保存データの復元用に値を直接設定する
    // 既存のシーンファイルやサンドバッグの値を保つため、ここでは範囲に収めない
    void SetHp(float value) { hp_ = value; }
    void SetMaxHp(float value) { maxHp_ = value; }
    void SetDead(bool dead) { isDead_ = dead; }
    // ダメージ・回復 (0〜最大値に収める)
    void ApplyDamage(float damage) { hp_ = (std::max)(0.0f, hp_ - damage); }
    void RecoverHp(float amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }
    void RecoverStamina(float amount) { stamina_ = (std::min)(maxStamina_, stamina_ + amount); }
    // HP を消費して技を使う (足りなければ false)
    bool TryConsumeHp(float cost) {
        if (hp_ >= cost) {
            hp_ -= cost;
            return true;
        }
        return false;
    }
    // HP が 0 以下なら最大値まで戻す (リスタート用)
    void RestoreHpIfDepleted() { if (hp_ <= 0) hp_ = maxHp_; }

	float recoverableFluid = 0.0f;   // 被弾で散った、回収可能な体液量
	int damageTakenCount = 0;        // 被ダメージ回数（リザルト表示用）
	float invincibleTime = 0.0f;     // 残り無敵時間（ゼロ以上なら無敵）

	// ヒット演出用 (白く光る時間・一時停止の時間・元の色)
	float hitFlashTimer = 0.0f;      // ヒットフラッシュ（白く光る）
	float hitStopTimer = 0.0f;       // ヒットストップ（一時停止）
	DirectX::XMFLOAT4 baseColor = { 1, 1, 1, 1 }; // 元の色保存用
	bool baseColorSaved = false;

	HealthComponent() { type = ComponentType::Health; }
// HP・スタミナの実体 (外から直接書き換えさせない)
private:
    float hp_ = 100.0f;
    float maxHp_ = 100.0f;
    float stamina_ = 100.0f;
    float maxStamina_ = 100.0f;
    bool isDead_ = false;
};

// スクリプト1つ分の情報 (クラス名・保存パラメータ・実体)
struct ScriptEntry {
	std::string scriptPath = "";      // スクリプトのクラス名 (例: "PlayerScript")
	std::string parameterData = "";   // 初期化用パラメータ(JSON)
	std::shared_ptr<IScript> instance = nullptr; // C++スクリプトのインスタンス
	bool isStarted = false; // PlayモードでStart()が呼ばれたか
};

// スクリプトコンポーネント (1つのエンティティに複数のスクリプトを付けられる)
struct ScriptComponent : public Component {
	std::vector<ScriptEntry> scripts;
	ScriptComponent() { type = ComponentType::Script; }
};

// 指定時間が経つと自動で削除される
struct AutoDestroyComponent : public Component {
	float timer = 1.0f;
	AutoDestroyComponent() { type = static_cast<ComponentType>(999); }
};

// 汎用の変数置き場 (スクリプト間で数値・文字列を受け渡す)
struct VariableComponent : public Component {
	std::map<std::string, float> values;
	std::map<std::string, std::string> strings; // 文字列の値
	VariableComponent() { type = ComponentType::Variable; }

	float GetValue(const std::string& key, float defaultVal = 0.0f) const {
		auto it = values.find(key);
		return (it != values.end()) ? it->second : defaultVal;
	}
	void SetValue(const std::string& key, float val) {
		values[key] = val;
	}

	std::string GetString(const std::string& key, const std::string& defaultVal = "") const {
		auto it = strings.find(key);
		return (it != strings.end()) ? it->second : defaultVal;
	}
	void SetString(const std::string& key, const std::string& val) {
		strings[key] = val;
	}
};

// モーションエディタで作るキーフレームアニメーション
struct MotionComponent : public Component {
	// キーフレーム (時刻と、その時点の位置・回転・拡大)
	struct Keyframe {
		float time = 0.0f;
		DirectX::XMFLOAT3 translate = {0, 0, 0};
		DirectX::XMFLOAT3 rotate = {0, 0, 0};
		DirectX::XMFLOAT3 scale = {1, 1, 1};
	};

	// 1つのモーション (キーフレーム列と長さ、ループ)
	struct MotionClip {
		std::string name;
		std::vector<Keyframe> keyframes;
		float totalDuration = 1.0f;
		bool loop = false;
	};

	// 名前で引くモーション一覧と、再生中の状態
	std::map<std::string, MotionClip> clips;
	std::string activeClip = "Default";
	
	float currentTime = 0.0f;
	bool isPlaying = false;
	int selectedKeyframe = -1; // エディタで選択中のキーフレーム番号

	MotionComponent() { 
		type = ComponentType::Motion; 
		// 最初から1つ「Default」クリップを用意しておく
		MotionClip defaultClip;
		defaultClip.name = "Default";
		defaultClip.keyframes.push_back({0.0f, {0,0,0}, {0,0,0}, {1,1,1}});
		defaultClip.keyframes.push_back({1.0f, {5,0,0}, {0,0,0}, {1,1,1}});
		clips["Default"] = defaultClip;
	}

	// 指定した名前のモーションを最初から再生する
	void PlayAnimation(const std::string& clipName) {
		if (clips.find(clipName) != clips.end()) {
			activeClip = clipName;
			currentTime = 0.0f;
			isPlaying = true;
		}
	}
};

} // namespace Game
