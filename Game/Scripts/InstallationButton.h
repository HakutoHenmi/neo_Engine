#pragma once
#include "IScript.h"

namespace Game {

class InstallationButton : public IScript {
public:
	InstallationButton();

	enum ButtonTypes {
		Cannon = 0,
		Pipe,
		Tank, 
		ButtonTypesNum,
	};

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

	static bool IsButtonPressed(ButtonTypes type);


	

private:

	ButtonTypes buttonTypes_ = Cannon;
	
	static bool isButtonPressed_[ButtonTypesNum];

};

} // namespace Game