/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Client channel to the Lumina Window Engine (the app_server add-on).
 *
 * Transport: BWindow::SetDecoratorSettings() / GetDecoratorSettings() on a
 * private, never-shown titled window. Both are public BWindow API; app_server
 * forwards the message to every DesktopListener of the active decorator
 * add-on. If the Lumina decorator isn't selected, commands are silently
 * ignored by app_server and Probe() reports the engine as absent — the
 * fallback path is always "do nothing", never a crash.
 */
#ifndef LUMINA_HAIKU_ENGINE_LINK_H
#define LUMINA_HAIKU_ENGINE_LINK_H

#include <Message.h>
#include <Messenger.h>
#include <Rect.h>
#include <String.h>

class BWindow;

namespace lumina {
namespace haiku {

struct EngineInfo {
	bool	present = false;	// listener answered
	bool	active = false;		// ABI guard passed, features enabled
	int32	version = 0;
	BString	builtAbi;
	BString	runningAbi;
	BString	reason;
	int32	animating = 0;
	BString	statsJson;
	bool	compositor = false;	// overlay compositor (patched app_server)
};

class EngineLink {
public:
	EngineLink();
	~EngineLink();

	status_t InitCheck() const { return fWindow != NULL ? B_OK : B_NO_INIT; }

	status_t Probe(EngineInfo& info);

	status_t Subscribe(const BMessenger& target);
	status_t Unsubscribe(const BMessenger& target);
	status_t MoveResize(int32 token, BRect frame);
	status_t Animate(int32 token, BRect frame, float durationSeconds,
		const char* curve, const char* resizeMode = "throttled",
		int32 maxFps = 60);
	status_t Cancel(int32 token = 0);
	// Lands every running animation and parked window immediately.
	status_t Finish();
	status_t Minimize(int32 token, bool minimize);
	status_t Activate(int32 token);
	status_t SetWorkspace(int32 token, int32 workspace);
	status_t Configure(const BString& json);
	status_t Reserve(const BString& json);
	status_t RequestList(const BMessenger& target);

	status_t Send(BMessage& command);

	// Locating and selecting the decorator add-on.
	static bool FindEngineAddOn(BString& path);
	static bool IsEngineSelected();

private:
	BWindow*	fWindow;
};

}	// namespace haiku
}	// namespace lumina

#endif
