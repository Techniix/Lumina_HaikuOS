/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * LifecycleProxies: the windows the Window Engine uses to stand in for a
 * departing or returning window (close, hide, minimize, restore).
 *
 * Each proxy is a hidden, borderless, floating-all, focus-avoiding window
 * whose single child view displays a screen-sized B_RGB32 view bitmap. The
 * engine (inside app_server) writes captured pixels straight into that
 * bitmap, positions and shows the window and animates it — this process
 * never draws into it and never shows or hides it. Creating them here
 * (rather than inside app_server) keeps every window owned by a normal
 * client team: if lumina_server dies, app_server removes the proxies with
 * it and nothing can be left on screen.
 *
 * Memory: two proxies at 1920x1080 cost 2 x 8 MB of shared bitmap memory,
 * allocated only while fx.lifecycle.enabled is true.
 */
#ifndef LUMINA_SERVER_LIFECYCLE_PROXIES_H
#define LUMINA_SERVER_LIFECYCLE_PROXIES_H

#include <Rect.h>
#include <SupportDefs.h>

#include <vector>

class BWindow;

namespace lumina {
namespace haiku {
class EngineLink;
}

class LifecycleProxies {
public:
	LifecycleProxies();
	~LifecycleProxies();

	// Creates the windows (if needed, or if the screen grew) and sends
	// their server tokens to the engine. Safe to call repeatedly.
	status_t Register(haiku::EngineLink* engine, int32 count = 2);
	// Unregisters (empty list to the engine) and destroys the windows.
	void Release(haiku::EngineLink* engine);

	bool IsActive() const { return !fWindows.empty(); }
	const std::vector<int32>& Tokens() const { return fTokens; }

private:
	status_t _Create(int32 count);
	void _Destroy();
	static int32 _FindToken(const char* title);

	std::vector<BWindow*>	fWindows;
	std::vector<int32>		fTokens;
	BRect					fScreen;
};

}	// namespace lumina

#endif
