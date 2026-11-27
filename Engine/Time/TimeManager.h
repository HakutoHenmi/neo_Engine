#pragma once

namespace Engine {

class TimeManager {
public:
	static TimeManager& GetInstance() {
		static TimeManager instance;
		return instance;
	}

	void Update(float dt) {
		unscaledDt_ = dt;
		if (unscaledDt_ > 0.1f)
			unscaledDt_ = 0.1f; // 大きな飛びを避けるため dt に上限を設ける

		// ヒットストップの処理
		if (hitStopTimer_ > 0.0f) {
			hitStopTimer_ -= unscaledDt_;
			scaledDt_ = 0.0f; // 時間を止める
		} else {
			scaledDt_ = unscaledDt_ * timeScale_;
		}
	}

	float GetDeltaTime() const {
		return scaledDt_;
	}
	float GetUnscaledDeltaTime() const {
		return unscaledDt_;
	}

	void SetTimeScale(float scale) {
		timeScale_ = scale;
	}
	float GetTimeScale() const {
		return timeScale_;
	}

	// ヒットストップ: duration 秒だけ止める
	void SetHitstop(float duration) {
		hitStopTimer_ = duration;
	}

private:
	TimeManager() = default;
	float unscaledDt_ = 0.0f;
	float scaledDt_ = 0.0f;
	float timeScale_ = 1.0f;
	float hitStopTimer_ = 0.0f;
};

} // namespace Engine
