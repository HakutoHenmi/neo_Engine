#pragma once
// ==================================================
// LoadingScreen
//   Scene initialization runs synchronously on the main thread. While a load is
//   active, a dedicated thread draws the loading screen at a steady ~60 fps with
//   the renderer's private UI command list, so long loads never stall it. The main
//   thread keeps calling Pump() between loads to service window messages; taps and
//   clicks are queued from those messages and the gamepad is polled on the
//   loading thread. The visuals come from the game through SetDrawer().
// ==================================================
#include <functional>
#include <string>

namespace Engine {

class LoadingScreen {
public:
	struct State {
		std::string scene;
		float progress = 0;  // 0..1, monotonic estimate
		float elapsed = 0;   // seconds since Begin
		float dt = 0;        // seconds since the previous loading frame
		bool startup = false;
		bool finished = false;
		// First frame, drawn on the main thread before the loading thread starts:
		// the drawer should touch every texture and glyph it may use.
		bool warmup = false;
		// Input since the previous loading frame.
		int step = 0;            // -1 previous / +1 next (arrow keys, A / D, D-pad, LB / RB, stick)
		bool clicked = false;    // left click at (clickX, clickY) in WindowDX::kW x kH pixels
		float clickX = 0, clickY = 0;
		float mouseX = -1, mouseY = -1;
		bool padMode = false;    // the gamepad is the active device
	};
	using Drawer = std::function<void(const State&)>;

	static void SetDrawer(Drawer drawer);
	// Called on the main thread. Nested Begin / End pairs join the outer load.
	static void Begin(const std::string& scene, bool startup = false);
	// Called by loaders on the main thread; cheap, never renders.
	static void Pump();
	static void End();
	static bool Active();
	// Final state of the most recent load (used to fade the loading UI out).
	static const State& Last();
};

} // namespace Engine
