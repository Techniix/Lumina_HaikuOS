/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "LifecycleProxies.h"

#include <string.h>
#include <unistd.h>

#include <Bitmap.h>
#include <Message.h>
#include <OS.h>
#include <Screen.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/HaikuLog.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>

namespace lumina {

using namespace haiku;

namespace {

const char* kLog = "lifecycle";
const char* kTitlePrefix = "lumina-lifecycle-proxy-";

class ProxyWindow : public BWindow {
public:
	ProxyWindow(const char* title, BRect screen)
		:
		BWindow(BRect(-64, -64, -33, -33), title, B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL,
			B_AVOID_FOCUS | B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
				| B_NOT_MINIMIZABLE | B_NO_WORKSPACE_ACTIVATION
				| B_ASYNCHRONOUS_CONTROLS,
			B_ALL_WORKSPACES),
		fBitmap(new BBitmap(screen.OffsetToCopy(0, 0), B_RGB32))
	{
		// The engine resizes the window freely (down to a panel button).
		SetSizeLimits(1, 32767, 1, 32767);
		BView* pixels = new BView(Bounds(), "pixels", B_FOLLOW_ALL, 0);
		// No background fill: the view bitmap covers the whole view, so
		// app_server never paints a colour under it (no flash).
		pixels->SetViewColor(B_TRANSPARENT_COLOR);
		AddChild(pixels);
		if (fBitmap->InitCheck() == B_OK) {
			pixels->SetViewBitmap(fBitmap, fBitmap->Bounds(), pixels->Bounds(),
				B_FOLLOW_ALL, 0);
		}
	}

	BBitmap* Bitmap() const { return fBitmap; }

	// Quit() deletes the window and its views (the server drops the view
	// bitmap reference with them) before the client bitmap is freed.
	static void Destroy(ProxyWindow* window)
	{
		BBitmap* bitmap = window->fBitmap;
		window->Lock();
		window->Quit();
		delete bitmap;
	}

private:
	BBitmap*	fBitmap;
};

}	// namespace


LifecycleProxies::LifecycleProxies()
{
}


LifecycleProxies::~LifecycleProxies()
{
	_Destroy();
}


int32
LifecycleProxies::_FindToken(const char* title)
{
	std::vector<WindowSnapshot> windows;
	if (PrivateApi::ListWindows(windows, getpid()) != B_OK)
		return -1;
	for (const WindowSnapshot& window : windows) {
		if (window.title == title)
			return window.token;
	}
	return -1;
}


status_t
LifecycleProxies::_Create(int32 count)
{
	BRect screen = BScreen().Frame();
	for (int32 i = 0; i < count; i++) {
		BString title(kTitlePrefix);
		title << i;
		ProxyWindow* window = new ProxyWindow(title.String(), screen);
		if (window->Bitmap()->InitCheck() != B_OK) {
			ProxyWindow::Destroy(window);
			return B_NO_MEMORY;
		}
		// Start the looper without ever showing the window: Hide() raises
		// the show level, Show() lowers it back to hidden and runs the
		// looper (the same pattern EngineLink uses for its command window).
		window->Hide();
		window->Show();
		window->Lock();
		window->Sync();
		window->Unlock();
		int32 token = _FindToken(title.String());
		if (token < 0) {
			ProxyWindow::Destroy(window);
			return B_ERROR;
		}
		fWindows.push_back(window);
		fTokens.push_back(token);
	}
	fScreen = screen;
	return B_OK;
}


void
LifecycleProxies::_Destroy()
{
	for (BWindow* window : fWindows)
		ProxyWindow::Destroy(static_cast<ProxyWindow*>(window));
	fWindows.clear();
	fTokens.clear();
}


status_t
LifecycleProxies::Register(EngineLink* engine, int32 count)
{
	if (engine == NULL)
		return B_BAD_VALUE;
	BRect screen = BScreen().Frame();
	if (!fWindows.empty() && (screen.Width() > fScreen.Width()
			|| screen.Height() > fScreen.Height()
			|| int32(fWindows.size()) != count)) {
		// Bitmaps must cover the whole screen: rebuild after a mode change.
		Release(engine);
	}
	if (fWindows.empty()) {
		status_t status = _Create(count);
		if (status != B_OK) {
			LUMINA_LOG_WARN(kLog, "cannot create proxies: %s", strerror(status));
			_Destroy();
			return status;
		}
	}
	BMessage command;
	command.AddString(LUMINA_ENGINE_FIELD_OP, LUMINA_OP_PROXIES);
	for (int32 token : fTokens)
		command.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
	status_t status = engine->Send(command);
	LUMINA_LOG_INFO(kLog, "registered %d proxies with the engine: %s",
		int(fTokens.size()), strerror(status));
	return status;
}


void
LifecycleProxies::Release(EngineLink* engine)
{
	if (fWindows.empty())
		return;
	if (engine != NULL) {
		BMessage command;
		command.AddString(LUMINA_ENGINE_FIELD_OP, LUMINA_OP_PROXIES);
		engine->Send(command);	// empty list: engine forgets the pool
	}
	_Destroy();
}

}	// namespace lumina
