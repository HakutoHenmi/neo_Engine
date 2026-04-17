#pragma once
#include "IScript.h"

namespace Game {

class PhaseTransition : public IScript {
public:
 enum class FadeState {
		Idle,
		FadeOut,
		FadeIn
	};

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

    static void RequestFade(float duration = -1.0f);
	static bool ConsumeSwitchPoint();
	static bool IsAvailable();


private:
  inline static bool isAvailable_ = false;
	inline static bool switchPoint_ = false;
	inline static bool requestFade_ = false;
	inline static FadeState fadeState_ = FadeState::Idle;
	inline static float alpha_ = 0.0f;
	inline static float fadeDuration_ = 0.35f;
};

} // namespace Game