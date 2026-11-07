/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "LuminaEngine.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <syslog.h>

#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>

#include "Desktop.h"
#include "ServerApp.h"
#include "ServerWindow.h"
#include "Window.h"
#include "decorator/Decorator.h"

#include <lumina/Json.h>
#include <lumina/anim/Easing.h>
#include <lumina/haiku/Protocol.h>

#ifndef LUMINA_ENGINE_ABI
#	define LUMINA_ENGINE_ABI "unknown"
#endif

using namespace lumina::haiku;

namespace lumina {
namespace engine {

namespace {
const bigtime_t kCoalesceInterval = 33333;	// moved/resized ≤ ~30 Hz
const double kStaleTickSeconds = 0.25;		// clock considered stopped

#define ENGINE_LOG(priority, ...) \
	syslog(priority, "lumina-engine: " __VA_ARGS__)

RectF
ToRectF(BRect r)
{
	return RectF{r.left, r.top, r.right, r.bottom};
}

BRect
ToBRect(const RectF& r)
{
	return BRect(r.left, r.top, r.right, r.bottom);
}
}	// namespace


LuminaEngine::LuminaEngine()
	:
	fDesktop(NULL),
	fActive(false),
	fLastTick(0),
	fLastFlush(0),
	fDragToken(-1),
	fDragMoved(false),
	fDragResized(false)
{
}


LuminaEngine::~LuminaEngine()
{
}


int32
LuminaEngine::Identifier()
{
	return LUMINA_ENGINE_LISTENER_ID;
}


// #pragma mark - lifecycle


void
LuminaEngine::_CheckAbi()
{
	struct utsname name;
	fRunningAbi = "";
	if (uname(&name) == 0) {
		fRunningAbi = name.version;
		int32 space = fRunningAbi.FindFirst(' ');
		if (space > 0)
			fRunningAbi.Truncate(space);
	}
	BString built(LUMINA_ENGINE_ABI);

	bool overrideFile = false;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("Lumina/engine-allow-abi-mismatch");
		overrideFile = BEntry(path.Path()).Exists();
	}

	if (built == "unknown") {
		fActive = true;
		fInactiveReason = "unpinned development build";
	} else if (built == fRunningAbi) {
		fActive = true;
		fInactiveReason = "";
	} else if (overrideFile) {
		fActive = true;
		fInactiveReason.SetToFormat("ABI mismatch overridden (built %s, "
			"running %s)", built.String(), fRunningAbi.String());
	} else {
		fActive = false;
		fInactiveReason.SetToFormat("ABI mismatch: built for %s, running %s; "
			"rebuild the engine", built.String(), fRunningAbi.String());
	}
	ENGINE_LOG(fActive ? LOG_INFO : LOG_WARNING, "%s (built %s, running %s)",
		fActive ? "engine active" : "engine INACTIVE", built.String(),
		fRunningAbi.String());
}


void
LuminaEngine::ListenerRegistered(Desktop* desktop)
{
	fDesktop = desktop;
	_CheckAbi();
}


void
LuminaEngine::ListenerUnregistered()
{
	// Called with the desktop write lock held, right before app_server
	// unloads this image. We own no threads, so there is nothing to join.
	// Land in-flight animations on their targets so no window is left
	// half-way when the decorator is switched.
	if (fDesktop != NULL && fTimeline.HasActive())
		_Tick(1e12);
	while (fDesktop != NULL && !fParked.empty())
		_Unpark(fParked.begin()->first, true);
	fTimeline.CancelAll();
	fSteppers.clear();
	fCoalesced.clear();
	fSubscribers.clear();
	fDesktop = NULL;
}


bool
LuminaEngine::HandleMessage(Window* sender, BPrivate::LinkReceiver& link,
	BPrivate::LinkSender& reply)
{
	// AS_TALK_TO_DESKTOP_LISTENER needs BWindow's private link; we use the
	// public Set/GetDecoratorSettings path instead.
	return false;
}


// #pragma mark - helpers


int32
LuminaEngine::_TokenOf(Window* window)
{
	if (window == NULL || window->ServerWindow() == NULL)
		return -1;
	return window->ServerWindow()->ServerToken();
}


double
LuminaEngine::_Now()
{
	return system_time() / 1000000.0;
}


Window*
LuminaEngine::_FindWindow(int32 token)
{
	if (fDesktop == NULL || token < 0)
		return NULL;
	for (Window* window = fDesktop->AllWindows().FirstWindow(); window != NULL;
			window = window->NextWindow(kAllWindowList)) {
		if (_TokenOf(window) == token)
			return window;
	}
	return NULL;
}


BRect
LuminaEngine::_OuterFrame(Window* window)
{
	::Decorator* decorator = window->Decorator();
	if (decorator == NULL)
		return window->Frame();
	BRect outer = decorator->GetFootprint().Frame();
	return outer.IsValid() ? outer : window->Frame();
}


BRect
LuminaEngine::ContentFrameForOuter(BRect outerTarget, BRect contentFrame,
	BRect outerFrame)
{
	BRect result;
	result.left = outerTarget.left + (contentFrame.left - outerFrame.left);
	result.top = outerTarget.top + (contentFrame.top - outerFrame.top);
	result.right = outerTarget.right - (outerFrame.right - contentFrame.right);
	result.bottom = outerTarget.bottom
		- (outerFrame.bottom - contentFrame.bottom);
	return result;
}


bool
LuminaEngine::_ScreenFor(BPoint where, BRect& frame, int32& index) const
{
	const ::VirtualScreen& screens = fDesktop->VirtualScreen();
	for (int32 i = 0; i < screens.CountScreens(); i++) {
		BRect screenFrame = screens.ScreenFrameAt(i);
		if (screenFrame.Contains(where)) {
			frame = screenFrame;
			index = i;
			return true;
		}
	}
	if (screens.CountScreens() > 0) {
		frame = screens.ScreenFrameAt(0);
		index = 0;
		return true;
	}
	return false;
}


BRect
LuminaEngine::_UsableArea(BRect screen, int32 screenIndex) const
{
	for (const EngineSettings::Reserved& r : fSettings.reserved) {
		if (r.screen != screenIndex)
			continue;
		if (r.edge == "top")
			screen.top += r.thickness;
		else if (r.edge == "bottom")
			screen.bottom -= r.thickness;
		else if (r.edge == "left")
			screen.left += r.thickness;
		else if (r.edge == "right")
			screen.right -= r.thickness;
	}
	return screen;
}


// #pragma mark - events


void
LuminaEngine::_Send(BMessage& message)
{
	for (size_t i = 0; i < fSubscribers.size();) {
		// Zero timeout: never stall app_server on a busy client.
		status_t status = fSubscribers[i].SendMessage(&message,
			(BHandler*)NULL, 0);
		if (status == B_BAD_PORT_ID || status == B_BAD_VALUE) {
			fSubscribers.erase(fSubscribers.begin() + long(i));
			continue;
		}
		if (status == B_OK)
			fStats.eventsSent++;
		else
			fStats.eventsDropped++;
		i++;
	}
}


void
LuminaEngine::_FlushCoalesced(bool force)
{
	if (fCoalesced.empty())
		return;
	bigtime_t now = system_time();
	if (!force && now - fLastFlush < kCoalesceInterval)
		return;
	fLastFlush = now;
	std::map<int32, BMessage> pending;
	pending.swap(fCoalesced);
	for (auto& entry : pending)
		_Send(entry.second);
}


void
LuminaEngine::_Push(const char* op, Window* window, bool flag, const char* zone)
{
	if (window == NULL || fSubscribers.empty())
		return;
	BMessage message(LUMINA_ENGINE_EVENT);
	message.AddString(LUMINA_ENGINE_FIELD_OP, op);
	int32 token = _TokenOf(window);
	message.AddInt32(LUMINA_EV_TOKEN, token);
	message.AddRect(LUMINA_EV_FRAME, window->Frame());
	message.AddString(LUMINA_EV_TITLE, window->Title());
	message.AddInt32(LUMINA_EV_WORKSPACES, int32(window->Workspaces()));
	message.AddBool(LUMINA_EV_FLAG, flag);
	message.AddInt32(LUMINA_EV_FEEL, int32(window->Feel()));
	message.AddInt32(LUMINA_EV_LOOK, int32(window->Look()));
	message.AddInt64(LUMINA_EV_TIME, system_time());
	if (zone != NULL)
		message.AddString(LUMINA_EV_ZONE, zone);
	if (window->ServerWindow() != NULL) {
		message.AddInt32(LUMINA_EV_TEAM, window->ServerWindow()->ClientTeam());
		if (window->ServerWindow()->App() != NULL) {
			message.AddString(LUMINA_EV_SIG,
				window->ServerWindow()->App()->Signature());
		}
	}

	if (strcmp(op, LUMINA_EVENT_MOVED) == 0
		|| strcmp(op, LUMINA_EVENT_RESIZED) == 0) {
		fCoalesced[token] = message;
		_FlushCoalesced(false);
		return;
	}
	_FlushCoalesced(true);	// keep ordering: pending moves before this event
	_Send(message);
}


// #pragma mark - window operations (write lock held)


void
LuminaEngine::_MoveResize(Window* window, BRect frame)
{
	BRect current = window->Frame();
	float dw = frame.Width() - current.Width();
	float dh = frame.Height() - current.Height();
	if (dw != 0 || dh != 0)
		fDesktop->ResizeWindowBy(window, dw, dh);
	current = window->Frame();
	float dx = frame.left - current.left;
	float dy = frame.top - current.top;
	if (dx != 0 || dy != 0)
		fDesktop->MoveWindowBy(window, dx, dy);
}


void
LuminaEngine::_StartAnimation(Window* window, BRect target, float duration,
	const char* curveName, const char* resizeMode, double now)
{
	anim::Curve curve;
	if (curveName == NULL || curveName[0] == '\0'
		|| !anim::ParseCurve(curveName, curve))
		curve = anim::Curve::Easing(anim::EaseFamily::Cubic, anim::EaseMode::Out);
	int32 token = _TokenOf(window);
	bool retarget = fTimeline.Animate(token, ToRectF(window->Frame()),
		ToRectF(target), curve, duration, now);
	if (retarget)
		fStats.animationsRetargeted++;
	else {
		anim::ResizeMode mode = anim::ResizeMode::Throttled;
		if (resizeMode != NULL)
			anim::ParseResizeMode(resizeMode, mode);
		fSteppers[token] = anim::GeometryStepper(mode);
		fStats.animationsStarted++;
	}
	if (fLastTick <= 0 || now - fLastTick > kStaleTickSeconds)
		fLastTick = now;	// a fresh clock period starts now
	// Ask the clock (lumina_server) to start ticking.
	BMessage started(LUMINA_ENGINE_EVENT);
	started.AddString(LUMINA_ENGINE_FIELD_OP, "animation-started");
	started.AddInt32(LUMINA_EV_TOKEN, token);
	started.AddInt32("lumina:fps", fSettings.maxFps);
	_Send(started);
}


void
LuminaEngine::_Tick(double now)
{
	if (fDesktop == NULL || !fTimeline.HasActive())
		return;
	bigtime_t start = system_time();
	fLastTick = now;
	std::vector<anim::Frame> frames = fTimeline.Tick(now);
	for (const anim::Frame& frame : frames) {
		int32 token = int32(frame.key);
		Window* window = _FindWindow(token);
		if (window == NULL) {
			fSteppers.erase(token);
			continue;
		}
		anim::GeometryStep step = fSteppers[token].Step(
			ToRectF(window->Frame()), frame.rect, now, frame.finished);
		if (step.dw != 0 || step.dh != 0)
			fDesktop->ResizeWindowBy(window, step.dw, step.dh);
		if (step.dx != 0 || step.dy != 0)
			fDesktop->MoveWindowBy(window, step.dx, step.dy);
		if (!step.Empty())
			fStats.framesApplied++;
		if (frame.finished) {
			fSteppers.erase(token);
			_Push(LUMINA_EVENT_ANIM_DONE, window);
		}
	}
	bigtime_t cost = system_time() - start;
	fStats.ticks++;
	fStats.totalTickUsecs += cost;
	if (cost > fStats.maxTickUsecs)
		fStats.maxTickUsecs = cost;
}


void
LuminaEngine::_FinishStaleAnimations(double now)
{
	if (!fTimeline.HasActive() || now - fLastTick < kStaleTickSeconds)
		return;
	// The clock stopped: land everything on its target right now.
	fStats.staleCompletions++;
	_Tick(1e12);
}


// #pragma mark - open transitions (parking)


bool
LuminaEngine::_ShouldPark(Window* window)
{
	if (!fActive || !fSettings.openTransition || fSubscribers.empty())
		return false;
	if (window->Feel() != B_NORMAL_WINDOW_FEEL)
		return false;
	if (window->Look() != B_TITLED_WINDOW_LOOK
		&& window->Look() != B_DOCUMENT_WINDOW_LOOK)
		return false;
	if (window->ServerWindow() == NULL || window->ServerWindow()->App() == NULL)
		return false;
	BString signature(window->ServerWindow()->App()->Signature());
	signature.ToLower();
	// Never park the desktop shell itself.
	return !signature.StartsWith("application/x-vnd.lumina-")
		&& signature != "application/x-vnd.be-trak"
		&& signature != "application/x-vnd.be-tskb";
}


void
LuminaEngine::_Unpark(int32 token, bool automatic)
{
	auto it = fParked.find(token);
	if (it == fParked.end())
		return;
	BPoint offset = it->second.offset;
	fParked.erase(it);
	Window* window = _FindWindow(token);
	if (window == NULL)
		return;
	fDesktop->MoveWindowBy(window, -offset.x, -offset.y);
	if (automatic)
		fStats.autoUnparked++;
	_Push("unparked", window, automatic);
}


void
LuminaEngine::_UnparkStale(double now)
{
	if (fParked.empty())
		return;
	std::vector<int32> stale;
	for (const auto& entry : fParked) {
		if (now - entry.second.since > fSettings.parkTimeout)
			stale.push_back(entry.first);
	}
	for (int32 token : stale)
		_Unpark(token, true);
}


// #pragma mark - commands


void
LuminaEngine::_ApplyConfiguration(const char* json)
{
	Value doc;
	JsonError error;
	if (json == NULL || !ParseJson(json, doc, &error)) {
		ENGINE_LOG(LOG_WARNING, "bad configuration: %s",
			error.ToString().c_str());
		return;
	}
	if (const Value* snap = doc.Find("snap")) {
		if (const Value* v = snap->Find("enabled"))
			fSettings.snapEnabled = v->AsBool();
		if (const Value* v = snap->Find("threshold"))
			fSettings.snapThreshold = float(v->AsNumber(2));
		if (const Value* v = snap->Find("layout"))
			fSettings.snapLayout = v->AsString("halves+quarters");
	}
	if (const Value* animation = doc.Find("animation")) {
		if (const Value* v = animation->Find("enabled"))
			fSettings.animate = v->AsBool();
		if (const Value* v = animation->Find("durationMs"))
			fSettings.durationSeconds = float(v->AsNumber(180) / 1000.0);
		if (const Value* v = animation->Find("curve"))
			fSettings.curve = v->AsString("ease-out-cubic");
		if (const Value* v = animation->Find("resizeMode"))
			fSettings.resizeMode = v->AsString("throttled");
		if (const Value* v = animation->Find("maxFps"))
			fSettings.maxFps = int32(v->AsInt(60));
		if (const Value* v = animation->Find("openTransition"))
			fSettings.openTransition = v->AsBool();
		if (const Value* v = animation->Find("parkTimeout"))
			fSettings.parkTimeout = float(v->AsNumber(0.8));
	}
	if (const Value* reserved = doc.Find("reserved")) {
		fSettings.reserved.clear();
		for (const Value& item : reserved->Items()) {
			EngineSettings::Reserved r;
			r.screen = int32(item.Find("screen")
				? item.Find("screen")->AsInt() : 0);
			r.edge = item.Find("edge") ? item.Find("edge")->AsString() : "";
			r.thickness = float(item.Find("thickness")
				? item.Find("thickness")->AsNumber() : 0);
			if (r.thickness > 0)
				fSettings.reserved.push_back(r);
		}
	}
}


void
LuminaEngine::_HandleCommand(Window* sender, const BMessage& command)
{
	fStats.commands++;
	BString op = command.GetString(LUMINA_ENGINE_FIELD_OP, "");
	double now = _Now();

	if (op == LUMINA_OP_SUBSCRIBE || op == LUMINA_OP_UNSUBSCRIBE) {
		BMessenger target;
		if (command.FindMessenger(LUMINA_ENGINE_FIELD_TARGET, &target) != B_OK)
			return;
		for (auto it = fSubscribers.begin(); it != fSubscribers.end(); ++it) {
			if (*it == target) {
				fSubscribers.erase(it);
				break;
			}
		}
		if (op == LUMINA_OP_SUBSCRIBE && target.IsValid())
			fSubscribers.push_back(target);
		return;
	}
	if (op == LUMINA_OP_CONFIGURE) {
		_ApplyConfiguration(command.GetString(LUMINA_ENGINE_FIELD_JSON, NULL));
		return;
	}
	if (op == LUMINA_OP_RESERVE) {
		BString wrapped("{\"reserved\":");
		wrapped << command.GetString(LUMINA_ENGINE_FIELD_JSON, "[]") << "}";
		_ApplyConfiguration(wrapped.String());
		return;
	}

	// Below here windows change: only with a passing ABI guard.
	if (!fActive || fDesktop == NULL)
		return;

	if (op == "tick") {
		_Tick(now);
		_UnparkStale(now);
		_FlushCoalesced(true);
		return;
	}
	if (op == "unpark") {
		_Unpark(command.GetInt32(LUMINA_ENGINE_FIELD_TOKEN, 0), false);
		return;
	}
	if (op == LUMINA_OP_LIST) {
		for (Window* window = fDesktop->AllWindows().FirstWindow();
				window != NULL; window = window->NextWindow(kAllWindowList))
			_Push(LUMINA_EVENT_ADDED, window);
		return;
	}
	int32 token = command.GetInt32(LUMINA_ENGINE_FIELD_TOKEN, 0);
	if (op == LUMINA_OP_CANCEL) {
		if (token == 0) {
			fTimeline.CancelAll();
			fSteppers.clear();
		} else {
			fTimeline.Cancel(token);
			fSteppers.erase(token);
		}
		return;
	}

	Window* window = _FindWindow(token);
	if (window == NULL)
		return;
	BRect frame;
	bool hasFrame = command.FindRect(LUMINA_ENGINE_FIELD_FRAME, &frame) == B_OK
		&& frame.IsValid();

	if (op == LUMINA_OP_ANIMATE && hasFrame) {
		_StartAnimation(window, frame,
			command.GetFloat(LUMINA_ENGINE_FIELD_DURATION,
				fSettings.durationSeconds),
			command.GetString(LUMINA_ENGINE_FIELD_CURVE,
				fSettings.curve.c_str()),
			command.GetString(LUMINA_ENGINE_FIELD_RESIZE,
				fSettings.resizeMode.c_str()), now);
	} else if (op == LUMINA_OP_MOVE_RESIZE && hasFrame) {
		fTimeline.Cancel(token);
		fSteppers.erase(token);
		_MoveResize(window, frame);
	} else if (op == LUMINA_OP_MINIMIZE) {
		fDesktop->MinimizeWindow(window, command.GetBool("lumina:value", true));
	} else if (op == LUMINA_OP_ACTIVATE) {
		fDesktop->ActivateWindow(window);
	} else if (op == LUMINA_OP_WORKSPACE) {
		int32 workspace = command.GetInt32(LUMINA_ENGINE_FIELD_WORKSPACE, -1);
		if (workspace >= 0 && workspace < 32)
			fDesktop->SetWindowWorkspaces(window, 1UL << workspace);
	}
}


// #pragma mark - DesktopListener hooks


void
LuminaEngine::WindowAdded(Window* window)
{
	_UnparkStale(_Now());
	if (_ShouldPark(window)) {
		// The window is still hidden (BWindow shows itself later), so moving
		// it off-screen now can't flash. Its intended frame is reported so a
		// user-space proxy can animate into it; "unpark" (or the timeout)
		// moves it back.
		BRect frame = window->Frame();
		const ::VirtualScreen& screens = fDesktop->VirtualScreen();
		BPoint offset(screens.Frame().Width() * 2 + frame.Width() + 64, 0);
		_Push("parked", window);	// carries the intended frame
		fDesktop->MoveWindowBy(window, offset.x, offset.y);
		fParked[_TokenOf(window)] = Parked{offset, _Now()};
		fStats.parked++;
		return;
	}
	_Push(LUMINA_EVENT_ADDED, window);
}


void
LuminaEngine::WindowRemoved(Window* window)
{
	int32 token = _TokenOf(window);
	fTimeline.Cancel(token);
	fSteppers.erase(token);
	fCoalesced.erase(token);
	fParked.erase(token);
	if (fDragToken == token)
		fDragToken = -1;
	_Push(LUMINA_EVENT_REMOVED, window);
}


bool
LuminaEngine::KeyPressed(uint32 what, int32 key, int32 modifiers)
{
	// Keyboard tiling bindings arrive in a later milestone; never consume.
	return false;
}


void
LuminaEngine::MouseEvent(BMessage* message)
{
}


void
LuminaEngine::MouseDown(Window* window, BMessage* message, const BPoint& where)
{
	double now = _Now();
	_FinishStaleAnimations(now);
	_UnparkStale(now);
	int32 token = _TokenOf(window);
	fDragToken = token;
	fDragMoved = false;
	fDragResized = false;
	// The user grabbed an animating window: hand control back immediately.
	if (fTimeline.IsAnimating(token)) {
		fTimeline.Cancel(token);
		fSteppers.erase(token);
	}
}


void
LuminaEngine::MouseUp(Window* window, BMessage* message, const BPoint& where)
{
	int32 token = _TokenOf(window);
	bool wasDrag = token >= 0 && token == fDragToken && fDragMoved
		&& !fDragResized;
	fDragToken = -1;
	_FlushCoalesced(true);
	if (!wasDrag || window == NULL || !fActive || !fSettings.snapEnabled)
		return;
	// Only normal, movable windows snap.
	if (!window->IsNormal() || (window->Flags() & B_NOT_MOVABLE) != 0)
		return;

	BRect screen;
	int32 screenIndex;
	if (!_ScreenFor(where, screen, screenIndex))
		return;
	std::string zone = SnapZoneAt(where.x, where.y, ToRectF(screen),
		fSettings.snapThreshold, fSettings.snapLayout);
	if (zone.empty())
		return;
	RectF target;
	if (!PlacementRect(zone, ToRectF(_UsableArea(screen, screenIndex)), target))
		return;
	BRect content = ContentFrameForOuter(ToBRect(target), window->Frame(),
		_OuterFrame(window));
	if (!content.IsValid())
		return;

	fStats.snaps++;
	if (fSettings.animate) {
		_StartAnimation(window, content, fSettings.durationSeconds,
			fSettings.curve.c_str(), fSettings.resizeMode.c_str(), _Now());
	} else
		_MoveResize(window, content);
	_Push(LUMINA_EVENT_SNAPPED, window, false, zone.c_str());
}


void
LuminaEngine::MouseMoved(Window* window, BMessage* message, const BPoint& where)
{
	// Mouse movement is the most frequent hook: use it as a watchdog so a
	// parked window or a stalled animation can never get stuck.
	if (!fParked.empty() || fTimeline.HasActive()) {
		double now = _Now();
		_UnparkStale(now);
		_FinishStaleAnimations(now);
	}
	_FlushCoalesced(false);
}


void
LuminaEngine::WindowMoved(Window* window)
{
	if (_TokenOf(window) == fDragToken)
		fDragMoved = true;
	_Push(LUMINA_EVENT_MOVED, window);
}


void
LuminaEngine::WindowResized(Window* window)
{
	if (_TokenOf(window) == fDragToken)
		fDragResized = true;
	_Push(LUMINA_EVENT_RESIZED, window);
}


void
LuminaEngine::WindowActivated(Window* window)
{
	_Push(LUMINA_EVENT_ACTIVATED, window);
}


void
LuminaEngine::WindowSentBehind(Window* window, Window* behindOf)
{
}


void
LuminaEngine::WindowWorkspacesChanged(Window* window, uint32 workspaces)
{
	_Push(LUMINA_EVENT_WORKSPACES, window);
}


void
LuminaEngine::WindowHidden(Window* window, bool fromMinimize)
{
	_Push(LUMINA_EVENT_HIDDEN, window, fromMinimize);
}


void
LuminaEngine::WindowMinimized(Window* window, bool minimize)
{
	_Push(LUMINA_EVENT_MINIMIZED, window, minimize);
}


void
LuminaEngine::WindowTabLocationChanged(Window* window, float location,
	bool isShifting)
{
}


void
LuminaEngine::SizeLimitsChanged(Window* window, int32 minWidth, int32 maxWidth,
	int32 minHeight, int32 maxHeight)
{
}


void
LuminaEngine::WindowLookChanged(Window* window, window_look look)
{
}


void
LuminaEngine::WindowFeelChanged(Window* window, window_feel feel)
{
}


bool
LuminaEngine::SetDecoratorSettings(Window* window, const BMessage& settings)
{
	if (settings.what != LUMINA_ENGINE_COMMAND)
		return false;
	_HandleCommand(window, settings);
	return false;	// nothing for app_server to redraw
}


void
LuminaEngine::GetDecoratorSettings(Window* window, BMessage& settings)
{
	settings.AddBool(LUMINA_ENGINE_STATUS_PRESENT, true);
	settings.AddInt32(LUMINA_ENGINE_STATUS_VERSION, kEngineProtocolVersion);
	settings.AddString(LUMINA_ENGINE_STATUS_ABI, LUMINA_ENGINE_ABI);
	settings.AddString(LUMINA_ENGINE_STATUS_RUNNING_ABI, fRunningAbi);
	settings.AddBool(LUMINA_ENGINE_STATUS_ACTIVE, fActive);
	settings.AddString(LUMINA_ENGINE_STATUS_REASON, fInactiveReason);
	settings.AddInt32(LUMINA_ENGINE_STATUS_ANIMATING,
		int32(fTimeline.ActiveCount()));
	settings.AddString(LUMINA_ENGINE_STATUS_STATS, _StatsJson());
}


BString
LuminaEngine::_StatsJson() const
{
	BString json;
	double avg = fStats.ticks > 0
		? double(fStats.totalTickUsecs) / double(fStats.ticks) : 0;
	json.SetToFormat("{\"commands\":%" B_PRIu64 ",\"ticks\":%" B_PRIu64
		",\"framesApplied\":%" B_PRIu64 ",\"eventsSent\":%" B_PRIu64
		",\"eventsDropped\":%" B_PRIu64 ",\"snaps\":%" B_PRIu64
		",\"animationsStarted\":%" B_PRIu64 ",\"animationsRetargeted\":%"
		B_PRIu64 ",\"staleCompletions\":%" B_PRIu64
		",\"avgTickUsecs\":%.1f,\"maxTickUsecs\":%" B_PRId64
		",\"parked\":%" B_PRIu64 ",\"autoUnparked\":%" B_PRIu64
		",\"subscribers\":%d,\"snapEnabled\":%s,\"animate\":%s}",
		fStats.commands, fStats.ticks, fStats.framesApplied, fStats.eventsSent,
		fStats.eventsDropped, fStats.snaps, fStats.animationsStarted,
		fStats.animationsRetargeted, fStats.staleCompletions, avg,
		fStats.maxTickUsecs, fStats.parked, fStats.autoUnparked,
		int(fSubscribers.size()),
		fSettings.snapEnabled ? "true" : "false",
		fSettings.animate ? "true" : "false");
	return json;
}

}	// namespace engine
}	// namespace lumina
