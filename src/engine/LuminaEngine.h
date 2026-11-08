/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * LuminaEngine: the DesktopListener at the heart of the Window Engine.
 *
 * Execution model: thread-free and client-clocked
 * ------------------------------------------------
 * The engine owns NO threads. Every line of engine code runs inside an
 * app_server hook, on an app_server thread, with the desktop window lock
 * held in write mode:
 *
 *   - SetDecoratorSettings()  (AS_SET_DECORATOR_SETTINGS takes the write
 *     lock in Desktop::SetWindowDecoratorSettings) — commands, including
 *     the animation "tick" that lumina_server sends once per frame while
 *     animations run;
 *   - MouseDown/Up/Moved()    (MouseFilter::Filter holds LockAllWindows);
 *   - Window*()               (notified from inside locked Desktop methods).
 *
 * Why: app_server unregisters listeners from Desktop::ReloadDecor() while
 * holding the write lock and calls unload_add_on() right after. A worker
 * thread would either deadlock (joining a thread that waits for the lock)
 * or execute code from an unloaded image. With no threads, unregistering
 * is trivially safe.
 *
 * Consequences
 *   - Window* pointers are only used within one hook; tokens are resolved
 *     again every time.
 *   - Desktop::MoveWindowBy/ResizeWindowBy re-acquire the write lock; the
 *     lock is recursive for its holder (rw_lock owner_count), which is
 *     exactly how Stack & Tile moves windows from its own hooks.
 *   - Listener notifications are suppressed while our hook runs
 *     (DesktopObservable::InvokeGuard), so animation frames don't flood
 *     subscribers with "moved" events.
 *   - If the clock stops (lumina_server gone), the next mouse event
 *     completes stale animations instantly: windows never get stuck
 *     half-way.
 *   - Events are pushed to subscribers with zero-timeout SendMessage();
 *     a busy client loses events (counted) instead of stalling app_server.
 *
 * Safety: an ABI guard keeps window-changing features disabled when the
 * running Haiku revision differs from the one the add-on was built for.
 */
#ifndef LUMINA_ENGINE_H
#define LUMINA_ENGINE_H

#include <Messenger.h>
#include <OS.h>
#include <String.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include <InterfaceDefs.h>
#include <Window.h>

#include "DesktopListener.h"

#include <lumina/Rules.h>
#include <lumina/anim/Timeline.h>
#include <lumina/fx/Lifecycle.h>

struct lumina_overlay_api;
class Desktop;
class ServerBitmap;
class View;
class Window;

namespace lumina {
namespace engine {

struct EngineSettings {
	bool		snapEnabled = false;
	float		snapThreshold = 2;
	std::string	snapLayout = "halves+quarters";
	bool		animate = false;
	float		durationSeconds = 0.18f;
	std::string	curve = "ease-out-cubic";
	std::string	resizeMode = "throttled";
	int32		maxFps = 60;
	bool		openTransition = false;	// park new windows for a proxy
	float		parkTimeout = 0.8f;		// seconds before auto-unpark
	struct Reserved {
		int32		screen;
		std::string	edge;
		float		thickness;
	};
	std::vector<Reserved> reserved;
};

struct EngineStats {
	uint64		commands = 0;
	uint64		ticks = 0;
	uint64		framesApplied = 0;
	uint64		eventsSent = 0;
	uint64		eventsDropped = 0;
	uint64		snaps = 0;
	uint64		animationsStarted = 0;
	uint64		animationsRetargeted = 0;
	uint64		staleCompletions = 0;
	uint64		parked = 0;
	uint64		autoUnparked = 0;
	bigtime_t	maxTickUsecs = 0;
	bigtime_t	totalTickUsecs = 0;
	// lifecycle transitions
	uint64		lifecycleAnimated = 0;
	uint64		lifecycleFaded = 0;
	uint64		lifecycleSkipped = 0;
	uint64		lifecycleTimeouts = 0;
	uint64		backgroundsRemembered = 0;
	bigtime_t	maxCaptureUsecs = 0;
	uint64		cursorThemesApplied = 0;
};

class LuminaEngine : public DesktopListener {
public:
	LuminaEngine();
	virtual ~LuminaEngine();

	// DesktopListener
	virtual int32 Identifier();
	virtual void ListenerRegistered(Desktop* desktop);
	virtual void ListenerUnregistered();
	virtual bool HandleMessage(Window* sender, BPrivate::LinkReceiver& link,
		BPrivate::LinkSender& reply);
	virtual void WindowAdded(Window* window);
	virtual void WindowRemoved(Window* window);
	virtual bool KeyPressed(uint32 what, int32 key, int32 modifiers);
	virtual void MouseEvent(BMessage* message);
	virtual void MouseDown(Window* window, BMessage* message,
		const BPoint& where);
	virtual void MouseUp(Window* window, BMessage* message,
		const BPoint& where);
	virtual void MouseMoved(Window* window, BMessage* message,
		const BPoint& where);
	virtual void WindowMoved(Window* window);
	virtual void WindowResized(Window* window);
	virtual void WindowActivated(Window* window);
	virtual void WindowSentBehind(Window* window, Window* behindOf);
	virtual void WindowWorkspacesChanged(Window* window, uint32 workspaces);
	virtual void WindowHidden(Window* window, bool fromMinimize);
	virtual void WindowMinimized(Window* window, bool minimize);
	virtual void WindowTabLocationChanged(Window* window, float location,
		bool isShifting);
	virtual void SizeLimitsChanged(Window* window, int32 minWidth,
		int32 maxWidth, int32 minHeight, int32 maxHeight);
	virtual void WindowLookChanged(Window* window, window_look look);
	virtual void WindowFeelChanged(Window* window, window_feel feel);
	virtual bool SetDecoratorSettings(Window* window, const BMessage& settings);
	virtual void GetDecoratorSettings(Window* window, BMessage& settings);

	// Pure helper (unit-testable): content frame that places the window's
	// outer frame (tab + borders) at outerTarget.
	static BRect ContentFrameForOuter(BRect outerTarget, BRect contentFrame,
		BRect outerFrame);

private:
	void _HandleCommand(Window* sender, const BMessage& command);
	void _ApplyConfiguration(const char* json);
	void _Tick(double now);
	void _FinishStaleAnimations(double now);
	void _StartAnimation(Window* window, BRect target, float duration,
		const char* curveName, const char* resizeMode, double now);
	void _MoveResize(Window* window, BRect frame);
	bool _ShouldPark(Window* window);
	BPoint _ParkOffset(Window* window);
	bool _InStack(Window* window);
	void _Unpark(int32 token, bool automatic);
	void _UnparkStale(double now);

	void _Push(const char* op, Window* window, bool flag = false,
		const char* zone = NULL);
	void _Send(BMessage& message);
	void _FlushCoalesced(bool force);

	Window* _FindWindow(int32 token);
	BRect _OuterFrame(Window* window);
	BRect _UsableArea(BRect screenFrame, int32 screenIndex) const;
	bool _ScreenFor(BPoint where, BRect& frame, int32& index) const;
	static int32 _TokenOf(Window* window);
	static double _Now();

	void _CheckAbi();
	BString _StatsJson() const;
	bool _IsLuminaClient(Window* window) const;

	// Lifecycle transitions and cursor themes (LuminaEngineLifecycle.cpp).
	// See docs/07-advanced-integration-research.md §C, §D, §E, §G.
	struct Proxy {
		int32				token = -1;		// the proxy window
		int32				source = -1;	// window it stands in for; -1 free
		fx::LifecycleKind	kind = fx::LifecycleKind::Hide;
		double				since = 0;
		double				duration = 0;
		double				releaseAt = 0;	// >0: hide then (restore settle)
		bool				fading = false;
		bool				flying = false;	// geometry animation running
		fx::Image			from;			// crossfade endpoints
		fx::Image			to;
	};
	void _RegisterProxies(const BMessage& command);
	void _SetAnchors(const BMessage& command);
	Proxy* _FreeProxy();
	Proxy* _ProxyByToken(int32 token);
	bool _IsProxy(int32 token);
	ServerBitmap* _ProxyBitmap(Window* proxyWindow, View** _view);
	fx::WindowTraits _Traits(Window* window, BRect outer);
	BRect _ScreenClip(BRect rect) const;
	bool _ReadScreen(BRect area, const fx::ImageView& into);
	bool _PlaceProxy(Proxy& proxy, Window* proxyWindow, BRect frame,
		const fx::ImageView& pixels);
	void _LifecycleHidden(Window* window, bool fromMinimize);
	void _LifecycleRemoved(int32 token);
	void _LifecycleRestored(Window* window);
	void _LifecycleShown(Window* window);
	void _LifecycleTick(double now);
	void _ReleaseProxy(Proxy& proxy, const char* why);
	void _ProxyLanded(int32 token);
	bool _ProxiesBusy() const;
	void _LifecycleReset();
	void _PushLifecycle(const Proxy& proxy, BRect frame, const char* decision);
	void _ApplyCursorTheme(const char* path);

	// Level 3: app_server overlay compositor, if patched in
	// (LuminaEngineOverlay.cpp).
	void _ResolveCompositor();
	void _HandleOverlayCommand(const BString& op, const BMessage& command);
	void _ResetOverlays();
	const lumina_overlay_api*	fOverlayApi = NULL;
	std::set<int32>				fOverlays;

	fx::LifecycleSettings	fLifecycle;
	std::vector<Proxy>		fProxies;
	fx::SnapshotCache		fSnapshots;		// last image of minimized windows
	fx::SnapshotCache		fBackgrounds;	// what was under a window (+ring)
	std::map<int32, BRect>	fBackgroundFrames;
	std::map<int32, double>	fPendingShow;	// added/hidden, not yet shown
	std::map<int32, BRect>	fAnchors;		// panel buttons per window
	BString					fCursorTheme;

	Desktop*			fDesktop;
	bool				fActive;
	BString				fInactiveReason;
	BString				fRunningAbi;

	EngineSettings		fSettings;
	EngineStats			fStats;
	std::vector<BMessenger> fSubscribers;

	anim::Timeline		fTimeline;
	std::map<int32, anim::GeometryStepper> fSteppers;
	double				fLastTick;

	struct Parked {
		BPoint	intended;	// where the window wants to be (LeftTop)
		BPoint	parkedAt;	// where we put it
		double	since;
	};
	std::map<int32, Parked> fParked;

	std::map<int32, BMessage> fCoalesced;	// latest moved/resized per token
	bigtime_t			fLastFlush;

	int32				fDragToken;
	bool				fDragMoved;
	bool				fDragResized;
};

}	// namespace engine
}	// namespace lumina

#endif
