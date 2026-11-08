/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Window lifecycle transitions and cursor themes — the Level 2 ("enhanced
 * integration") half of the Window Engine. Research and source trace:
 * docs/07-advanced-integration-research.md.
 *
 * Lifecycle capture, in one paragraph
 * -----------------------------------
 * Every way a window leaves the screen — BWindow::Hide(), Quit() and
 * close, Minimize() from the title tab, Deskbar, Twitcher, a shortcut or
 * the application itself — ends in Desktop::HideWindow(). That method
 * holds the all-window write lock, rebuilds clipping, marks the uncovered
 * area dirty (which only *posts* redraw requests to the windows below) and
 * then calls NotifyWindowHidden() — still under the lock. Nobody can paint
 * before we return, so the window's pixels are intact in the drawing
 * buffer. We copy them into the bitmap of a proxy window that lumina_server
 * registered earlier, move/resize/show that proxy in the same locked
 * transaction, and let the windows below repaint *around* it. The proxy
 * is then animated by the existing timeline (resizing a view bitmap with
 * B_FOLLOW_ALL scales it inside app_server — no client round-trip per
 * frame) or crossfaded into the remembered background, and hidden when it
 * lands. The application's own hide/close/minimize proceeds untouched.
 *
 * Restores run in reverse: Desktop::MinimizeWindow(false) shows the window
 * and notifies us under the lock before the window has painted; we park it
 * off-screen, fly the cached snapshot out of the panel button and unpark
 * the window beneath the proxy when it lands.
 *
 * Every step degrades to "no animation": no proxy registered, too much of
 * the window covered, a busy clock, an unexpected buffer format, or the ABI
 * guard → the window simply disappears/appears as on stock Haiku.
 */
#include "LuminaEngine.h"

#include <string.h>
#include <syslog.h>

#include <Entry.h>

#include "CursorManager.h"
#include "Desktop.h"
#include "DesktopSettings.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "ServerFont.h"
#include "ServerWindow.h"
#include "View.h"
#include "Window.h"
#include "decorator/Decorator.h"
#include "drawing/DrawingEngine.h"
#include "drawing/HWInterface.h"
#include "RenderingBuffer.h"

#include <lumina/Json.h>
#include <lumina/haiku/Protocol.h>

using namespace lumina::haiku;

namespace lumina {
namespace engine {

namespace {

const int kRing = 3;				// background validity border (pixels)
const double kRestoreSettle = 0.06;	// keep covering while the app repaints
const double kShowWindowSeconds = 0.25;	// added/hidden → first activation
const size_t kMaxProxies = 4;

#define LIFECYCLE_LOG(priority, ...) \
	syslog(priority, "lumina-engine: " __VA_ARGS__)

RectF
ToRect(BRect r)
{
	return RectF{r.left, r.top, r.right, r.bottom};
}

BRect
ToBRect(const RectF& r)
{
	return BRect(r.left, r.top, r.right, r.bottom);
}

bool
Is32Bit(color_space space)
{
	return space == B_RGB32 || space == B_RGBA32;
}

fx::ImageView
ViewOf(ServerBitmap* bitmap, int width, int height)
{
	fx::ImageView view;
	view.bits = (uint8_t*)bitmap->Bits();
	view.width = width;
	view.height = height;
	view.stride = bitmap->BytesPerRow();
	return view;
}

}	// namespace


// #pragma mark - registration


void
LuminaEngine::_RegisterProxies(const BMessage& command)
{
	// Replaces the pool. A proxy must be one of lumina_server's own hidden,
	// borderless, floating-all windows whose first child view carries a
	// 32-bit view bitmap (the pixel store we write into).
	for (Proxy& proxy : fProxies) {
		if (proxy.source >= 0)
			_ReleaseProxy(proxy, "re-registered");
	}
	fProxies.clear();
	int32 token;
	for (int32 i = 0; fProxies.size() < kMaxProxies
			&& command.FindInt32(LUMINA_ENGINE_FIELD_TOKEN, i, &token) == B_OK;
			i++) {
		Window* window = _FindWindow(token);
		const char* problem = NULL;
		if (window == NULL)
			problem = "unknown window";
		else if (!_IsLuminaClient(window))
			problem = "not a Lumina window";
		else if (window->Look() != B_NO_BORDER_WINDOW_LOOK
			|| window->Feel() != B_FLOATING_ALL_WINDOW_FEEL)
			problem = "must be borderless and floating-all";
		else if (!window->IsHidden())
			problem = "must be hidden";
		else if (_ProxyBitmap(window, NULL) == NULL)
			problem = "no 32-bit view bitmap";
		if (problem != NULL) {
			LIFECYCLE_LOG(LOG_WARNING, "proxy %" B_PRId32 " rejected: %s",
				token, problem);
			continue;
		}
		Proxy proxy;
		proxy.token = token;
		fProxies.push_back(proxy);
	}
	LIFECYCLE_LOG(LOG_INFO, "%zu lifecycle prox%s registered",
		fProxies.size(), fProxies.size() == 1 ? "y" : "ies");
}


void
LuminaEngine::_SetAnchors(const BMessage& command)
{
	// Panel buttons per window: where minimized windows fly to and restores
	// come from. "lumina:replace" = the sender's full layout (the panel);
	// otherwise entries are added/updated.
	if (command.GetBool("lumina:replace", false))
		fAnchors.clear();
	int32 token;
	BRect frame;
	for (int32 i = 0; command.FindInt32(LUMINA_ENGINE_FIELD_TOKEN, i, &token)
				== B_OK
			&& command.FindRect(LUMINA_ENGINE_FIELD_FRAME, i, &frame) == B_OK;
			i++) {
		if (frame.IsValid())
			fAnchors[token] = frame;
	}
}


LuminaEngine::Proxy*
LuminaEngine::_FreeProxy()
{
	for (Proxy& proxy : fProxies) {
		if (proxy.source < 0 && _FindWindow(proxy.token) != NULL)
			return &proxy;
	}
	return NULL;
}


LuminaEngine::Proxy*
LuminaEngine::_ProxyByToken(int32 token)
{
	for (Proxy& proxy : fProxies) {
		if (proxy.token == token)
			return &proxy;
	}
	return NULL;
}


bool
LuminaEngine::_IsProxy(int32 token)
{
	return _ProxyByToken(token) != NULL;
}


ServerBitmap*
LuminaEngine::_ProxyBitmap(Window* proxyWindow, View** _view)
{
	View* top = proxyWindow->TopView();
	View* view = top != NULL ? top->FirstChild() : NULL;
	ServerBitmap* bitmap = view != NULL ? view->ViewBitmap() : NULL;
	if (bitmap == NULL || !Is32Bit(bitmap->ColorSpace()))
		return NULL;
	if (_view != NULL)
		*_view = view;
	return bitmap;
}


// #pragma mark - capture


BRect
LuminaEngine::_ScreenClip(BRect rect) const
{
	return rect & fDesktop->VirtualScreen().Frame();
}


bool
LuminaEngine::_ReadScreen(BRect area, const fx::ImageView& into)
{
	// Reads straight from app_server's drawing buffer: the RGBA32 back
	// buffer on the accelerant interface (always double-buffered, see
	// AccelerantHWInterface::SetMode), which also never contains the
	// software cursor. Lock order matches ServerWindow drawing: window lock
	// (held by our caller) before the drawing engine.
	area = _ScreenClip(area);
	if (!area.IsValid() || !into.IsValid()
		|| into.width < area.IntegerWidth() + 1
		|| into.height < area.IntegerHeight() + 1)
		return false;
	DrawingEngine* engine = fDesktop->GetDrawingEngine();
	::HWInterface* hw = fDesktop->HWInterface();
	if (engine == NULL || hw == NULL || !engine->LockExclusiveAccess())
		return false;
	bool ok = false;
	RenderingBuffer* buffer = hw->DrawingBuffer();
	if (buffer != NULL && Is32Bit(buffer->ColorSpace())) {
		fx::ImageView source;
		source.bits = (uint8_t*)buffer->Bits();
		source.width = int(buffer->Width());
		source.height = int(buffer->Height());
		source.stride = int(buffer->BytesPerRow());
		int w = area.IntegerWidth() + 1;
		int h = area.IntegerHeight() + 1;
		fx::Copy(source.Sub(int(area.left), int(area.top), w, h),
			into.Sub(0, 0, w, h));
		ok = true;
	}
	engine->UnlockExclusiveAccess();
	return ok;
}


fx::WindowTraits
LuminaEngine::_Traits(Window* window, BRect outer)
{
	fx::WindowTraits traits;
	window_feel feel = window->Feel();
	window_look look = window->Look();
	traits.normalFeel = feel == B_NORMAL_WINDOW_FEEL;
	traits.bordered = look == B_TITLED_WINDOW_LOOK
		|| look == B_DOCUMENT_WINDOW_LOOK || look == B_FLOATING_WINDOW_LOOK
		|| look == B_MODAL_WINDOW_LOOK || look == B_BORDERED_WINDOW_LOOK;
	traits.inStack = _InStack(window);
	traits.onCurrentWorkspace
		= window->InWorkspace(fDesktop->CurrentWorkspace());
	if (window->ServerWindow() != NULL
		&& window->ServerWindow()->App() != NULL) {
		BString signature(window->ServerWindow()->App()->Signature());
		signature.ToLower();
		traits.desktopShell = signature.StartsWith("application/x-vnd.lumina-")
			|| signature == "application/x-vnd.be-trak"
			|| signature == "application/x-vnd.be-tskb";
	}
	if (_IsProxy(_TokenOf(window)))
		traits.desktopShell = true;
	traits.width = outer.IntegerWidth() + 1;
	traits.height = outer.IntegerHeight() + 1;

	// How much of the outer frame is not covered by windows above it. The
	// departing window is still in the workspace list, so "above" is
	// simply every later window on the current workspace.
	BRegion covered;
	int32 workspace = fDesktop->CurrentWorkspace();
	for (Window* above = window->NextWindow(workspace); above != NULL;
			above = above->NextWindow(workspace)) {
		if (!above->IsHidden())
			covered.Include(&above->VisibleRegion());
	}
	BRegion own(outer);
	double total = 0;
	for (int32 i = 0; i < own.CountRects(); i++) {
		BRect r = own.RectAt(i);
		total += (r.Width() + 1) * (r.Height() + 1);
	}
	own.Exclude(&covered);
	double visible = 0;
	for (int32 i = 0; i < own.CountRects(); i++) {
		BRect r = own.RectAt(i);
		visible += (r.Width() + 1) * (r.Height() + 1);
	}
	traits.visibleFraction = total > 0 ? float(visible / total) : 0;
	return traits;
}


bool
LuminaEngine::_PlaceProxy(Proxy& proxy, Window* proxyWindow, BRect frame,
	const fx::ImageView& pixels)
{
	// Puts `pixels` (frame-sized; already inside the proxy bitmap when
	// pixels.bits points there) on screen at `frame`, in one transaction.
	View* view = NULL;
	ServerBitmap* bitmap = _ProxyBitmap(proxyWindow, &view);
	if (bitmap == NULL || view == NULL)
		return false;
	int w = frame.IntegerWidth() + 1;
	int h = frame.IntegerHeight() + 1;
	if (w > bitmap->Width() || h > bitmap->Height() || w <= 0 || h <= 0)
		return false;
	fx::ImageView target = ViewOf(bitmap, w, h);
	if (pixels.bits != target.bits)
		fx::Copy(pixels, target);

	// Size first (the view follows with B_FOLLOW_ALL), then describe the
	// bitmap for the current view bounds: from here on app_server rescales
	// it whenever the proxy is resized (View::ResizeBy → resize_frame).
	BRect current = proxyWindow->Frame();
	float dw = frame.Width() - current.Width();
	float dh = frame.Height() - current.Height();
	if (dw != 0 || dh != 0)
		fDesktop->ResizeWindowBy(proxyWindow, dw, dh);
	current = proxyWindow->Frame();
	if (current.LeftTop() != frame.LeftTop()) {
		fDesktop->MoveWindowBy(proxyWindow, frame.left - current.left,
			frame.top - current.top);
	}
	// View::SetViewBitmap adopts a reference; keep the count balanced.
	bitmap->AcquireReference();
	view->SetViewBitmap(bitmap, IntRect(0, 0, w - 1, h - 1),
		IntRect(0, 0, w - 1, h - 1),
		B_FOLLOW_LEFT | B_FOLLOW_RIGHT | B_FOLLOW_TOP | B_FOLLOW_BOTTOM, 0);
	if (proxyWindow->IsHidden())
		fDesktop->ShowWindow(proxyWindow);
	else {
		BRegion dirty(proxyWindow->VisibleRegion());
		proxyWindow->ProcessDirtyRegion(dirty);
	}
	return true;
}


void
LuminaEngine::_PushLifecycle(const Proxy& proxy, BRect frame,
	const char* decision)
{
	if (fSubscribers.empty())
		return;
	BMessage message(LUMINA_ENGINE_EVENT);
	message.AddString(LUMINA_ENGINE_FIELD_OP, LUMINA_EVENT_LIFECYCLE);
	message.AddInt32(LUMINA_EV_TOKEN, proxy.source);
	message.AddInt32(LUMINA_EV_PROXY, proxy.token);
	message.AddString(LUMINA_EV_KIND, fx::LifecycleKindName(proxy.kind));
	message.AddString(LUMINA_EV_DECISION, decision);
	message.AddRect(LUMINA_EV_FRAME, frame);
	message.AddInt64(LUMINA_EV_TIME, system_time());
	_FlushCoalesced(true);
	_Send(message);
}


// #pragma mark - lifecycle hooks (write lock held)


void
LuminaEngine::_LifecycleShown(Window* window)
{
	if (fPendingShow.empty())
		return;
	// First activation right after a window was added or re-shown: it has
	// not painted yet (redraws are posted, and we hold the lock), so what
	// is on screen under it is the background. Remember it (plus a ring
	// around it) for a later true crossfade.
	int32 token = _TokenOf(window);
	auto pending = fPendingShow.find(token);
	if (pending == fPendingShow.end())
		return;
	bool fresh = _Now() - pending->second < kShowWindowSeconds;
	fPendingShow.erase(pending);
	if (!fresh || !fActive || !fLifecycle.enabled || !fLifecycle.fade
		|| fParked.count(token) != 0)
		return;
	BRect outer = _OuterFrame(window);
	fx::WindowTraits traits = _Traits(window, outer);
	if (fx::Decide(fLifecycle, fx::LifecycleKind::Close, traits, true)
			!= fx::Decision::Animate)
		return;
	BRect area = outer.InsetByCopy(-kRing, -kRing);
	if (_ScreenClip(area) != area)
		return;		// ring must be fully on screen
	fx::Image image(area.IntegerWidth() + 1, area.IntegerHeight() + 1);
	if (!_ReadScreen(area, image.View()))
		return;
	if (fBackgrounds.Put(token, image.View())) {
		fBackgroundFrames[token] = outer;
		fStats.backgroundsRemembered++;
	}
}


void
LuminaEngine::_LifecycleHidden(Window* window, bool fromMinimize)
{
	int32 token = _TokenOf(window);
	if (_IsProxy(token))
		return;
	fPendingShow[token] = _Now();	// a later Show() is a fresh appearance
	if (!fActive || !fLifecycle.enabled)
		return;
	fx::LifecycleKind kind = fromMinimize
		? fx::LifecycleKind::Minimize : fx::LifecycleKind::Hide;
	BRect outer = _ScreenClip(_OuterFrame(window));
	if (!outer.IsValid())
		return;
	Proxy* proxy = _FreeProxy();
	fx::Decision decision = fx::Decide(fLifecycle, kind,
		_Traits(window, outer), proxy != NULL);
	if (decision != fx::Decision::Animate) {
		if (decision != fx::Decision::Disabled
			&& decision != fx::Decision::KindDisabled)
			fStats.lifecycleSkipped++;
		if (fromMinimize)
			fSnapshots.Erase(token);	// nothing current to restore from
		return;
	}
	Window* proxyWindow = _FindWindow(proxy->token);
	ServerBitmap* bitmap = _ProxyBitmap(proxyWindow, NULL);
	int w = outer.IntegerWidth() + 1;
	int h = outer.IntegerHeight() + 1;
	if (bitmap == NULL || w > bitmap->Width() || h > bitmap->Height())
		return;

	bigtime_t start = system_time();
	fx::ImageView pixels = ViewOf(bitmap, w, h);
	if (!_ReadScreen(outer, pixels)) {
		fStats.lifecycleSkipped++;
		return;
	}
	if (fromMinimize)
		fSnapshots.Put(token, pixels);

	// True crossfade for hide/close when the remembered background is
	// still what the windows below will repaint (ring check, see
	// fx::BackgroundStillValid); otherwise shrink.
	bool fade = false;
	if (!fromMinimize && fLifecycle.fade) {
		fx::Image* remembered = fBackgrounds.Get(token);
		auto frame = fBackgroundFrames.find(token);
		if (remembered != NULL && frame != fBackgroundFrames.end()
			&& frame->second == _OuterFrame(window)) {
			BRect area = outer.InsetByCopy(-kRing, -kRing);
			fx::Image live(remembered->Width(), remembered->Height());
			if (_ScreenClip(area) == area && live.Width() == w + 2 * kRing
				&& _ReadScreen(area, live.View())
				&& fx::BackgroundStillValid(remembered->View(), live.View(),
					kRing)) {
				proxy->from.Resize(w, h);
				fx::Copy(pixels, proxy->from.View());
				proxy->to.Resize(w, h);
				fx::Copy(remembered->View().Sub(kRing, kRing, w, h),
					proxy->to.View());
				fade = true;
			}
		}
	}

	if (!_PlaceProxy(*proxy, proxyWindow, outer, pixels)) {
		fStats.lifecycleSkipped++;
		return;
	}
	bigtime_t cost = system_time() - start;
	if (cost > fStats.maxCaptureUsecs)
		fStats.maxCaptureUsecs = cost;

	double now = _Now();
	proxy->source = token;
	proxy->kind = kind;
	proxy->since = now;
	proxy->duration = fLifecycle.durationSeconds;
	proxy->releaseAt = 0;
	proxy->fading = fade;
	proxy->flying = !fade;
	fStats.lifecycleAnimated++;

	if (fade) {
		fStats.lifecycleFaded++;
		// The fade is driven from _LifecycleTick; ask the clock to run.
		BMessage started(LUMINA_ENGINE_EVENT);
		started.AddString(LUMINA_ENGINE_FIELD_OP, "animation-started");
		started.AddInt32(LUMINA_EV_TOKEN, proxy->token);
		started.AddInt32("lumina:fps", fSettings.maxFps);
		_Send(started);
		if (fLastTick <= 0 || now - fLastTick > 0.25)
			fLastTick = now;
		_PushLifecycle(*proxy, outer, "fade");
		return;
	}

	RectF target;
	if (fromMinimize) {
		auto anchor = fAnchors.find(token);
		RectF anchorRect{1, 1, 0, 0};
		if (anchor != fAnchors.end())
			anchorRect = ToRect(anchor->second);
		target = fx::MinimizeTarget(ToRect(outer), anchorRect,
			ToRect(fDesktop->VirtualScreen().Frame()));
	} else
		target = fx::CloseTarget(ToRect(outer), fLifecycle.closeScale);
	_StartAnimation(proxyWindow, ToBRect(target), proxy->duration,
		fromMinimize ? "ease-in-cubic" : "ease-out-cubic", "live", now);
	_PushLifecycle(*proxy, outer, fx::DecisionName(decision));
}


void
LuminaEngine::_LifecycleRemoved(int32 token)
{
	// Desktop::RemoveWindow() hides first (we already took over) and then
	// removes: the same transition, now known to be a close.
	for (Proxy& proxy : fProxies) {
		if (proxy.source == token && proxy.kind == fx::LifecycleKind::Hide)
			proxy.kind = fx::LifecycleKind::Close;
	}
	if (_IsProxy(token)) {
		// lumina_server closed a proxy (quit or re-registration).
		for (auto it = fProxies.begin(); it != fProxies.end(); ++it) {
			if (it->token == token) {
				fProxies.erase(it);
				break;
			}
		}
	}
	fSnapshots.Erase(token);
	fBackgrounds.Erase(token);
	fBackgroundFrames.erase(token);
	fPendingShow.erase(token);
	fAnchors.erase(token);
}


void
LuminaEngine::_LifecycleRestored(Window* window)
{
	int32 token = _TokenOf(window);
	fx::Image* snapshot = fSnapshots.Get(token);
	if (!fActive || !fLifecycle.enabled || snapshot == NULL)
		return;
	BRect outer = _ScreenClip(_OuterFrame(window));
	Proxy* proxy = _FreeProxy();
	fx::WindowTraits traits = _Traits(window, outer);
	if (fx::Decide(fLifecycle, fx::LifecycleKind::Restore, traits,
			proxy != NULL) != fx::Decision::Animate
		|| snapshot->Width() != outer.IntegerWidth() + 1
		|| snapshot->Height() != outer.IntegerHeight() + 1) {
		fSnapshots.Erase(token);
		return;
	}
	Window* proxyWindow = _FindWindow(proxy->token);
	auto anchor = fAnchors.find(token);
	RectF anchorRect{1, 1, 0, 0};
	if (anchor != fAnchors.end())
		anchorRect = ToRect(anchor->second);
	BRect start = ToBRect(fx::MinimizeTarget(ToRect(outer), anchorRect,
		ToRect(fDesktop->VirtualScreen().Frame())));

	// The proxy bitmap must hold the full-size snapshot; the view bitmap
	// then shows it scaled to the (small) start frame.
	View* view = NULL;
	ServerBitmap* bitmap = _ProxyBitmap(proxyWindow, &view);
	int w = snapshot->Width();
	int h = snapshot->Height();
	if (bitmap == NULL || w > bitmap->Width() || h > bitmap->Height())
		return;
	fx::Copy(snapshot->View(), ViewOf(bitmap, w, h));
	BRect current = proxyWindow->Frame();
	fDesktop->ResizeWindowBy(proxyWindow, start.Width() - current.Width(),
		start.Height() - current.Height());
	current = proxyWindow->Frame();
	fDesktop->MoveWindowBy(proxyWindow, start.left - current.left,
		start.top - current.top);
	bitmap->AcquireReference();
	view->SetViewBitmap(bitmap, IntRect(0, 0, w - 1, h - 1),
		IntRect(0, 0, start.IntegerWidth(), start.IntegerHeight()),
		B_FOLLOW_LEFT | B_FOLLOW_RIGHT | B_FOLLOW_TOP | B_FOLLOW_BOTTOM, 0);
	fDesktop->ShowWindow(proxyWindow);

	// Park the real window (it has not painted yet) until the proxy lands.
	// The park deadline counts from the expected landing.
	double now = _Now();
	BPoint intended = window->Frame().LeftTop();
	BPoint offset = _ParkOffset(window);
	fDesktop->MoveWindowBy(window, offset.x, offset.y);
	fParked[token] = Parked{intended, window->Frame().LeftTop(),
		now + fLifecycle.durationSeconds};

	proxy->source = token;
	proxy->kind = fx::LifecycleKind::Restore;
	proxy->since = now;
	proxy->duration = fLifecycle.durationSeconds;
	proxy->releaseAt = 0;
	proxy->fading = false;
	proxy->flying = true;
	fStats.lifecycleAnimated++;
	_StartAnimation(proxyWindow, outer, proxy->duration, "ease-out-cubic",
		"live", now);
	_PushLifecycle(*proxy, outer, "animate");
}


void
LuminaEngine::_LifecycleTick(double now)
{
	for (Proxy& proxy : fProxies) {
		if (proxy.source < 0)
			continue;
		Window* proxyWindow = _FindWindow(proxy.token);
		if (proxyWindow == NULL) {
			proxy.source = -1;
			continue;
		}
		if (proxy.releaseAt > 0) {
			if (now >= proxy.releaseAt)
				_ReleaseProxy(proxy, "landed");
			continue;
		}
		if (now - proxy.since > proxy.duration + fLifecycle.proxyTimeout) {
			fStats.lifecycleTimeouts++;
			_ReleaseProxy(proxy, "timeout");
			continue;
		}
		if (proxy.fading) {
			double t = proxy.duration > 0
				? (now - proxy.since) / proxy.duration : 1;
			ServerBitmap* bitmap = _ProxyBitmap(proxyWindow, NULL);
			int w = proxy.from.Width();
			int h = proxy.from.Height();
			if (t >= 1 || bitmap == NULL) {
				_ReleaseProxy(proxy, "faded");
				continue;
			}
			fx::ImageView from = proxy.from.View();
			fx::ImageView to = proxy.to.View();
			fx::ImageView dst = ViewOf(bitmap, w, h);
			// Ease-out so most of the change happens early.
			double eased = 1 - (1 - t) * (1 - t);
			fx::CrossFadeRows(from.bits, from.stride, to.bits, to.stride,
				dst.bits, dst.stride, w, h, eased);
			BRegion dirty(proxyWindow->VisibleRegion());
			proxyWindow->ProcessDirtyRegion(dirty);
			fLastTick = now;
		}
	}
}


void
LuminaEngine::_ReleaseProxy(Proxy& proxy, const char* why)
{
	Window* proxyWindow = _FindWindow(proxy.token);
	if (proxy.kind == fx::LifecycleKind::Restore && proxy.source >= 0
		&& fParked.count(proxy.source) != 0)
		_Unpark(proxy.source, false);
	fTimeline.Cancel(proxy.token);
	fSteppers.erase(proxy.token);
	if (proxyWindow != NULL && !proxyWindow->IsHidden())
		fDesktop->HideWindow(proxyWindow);
	if (proxyWindow != NULL && proxy.fading) {
		BMessage done(LUMINA_ENGINE_EVENT);
		done.AddString(LUMINA_ENGINE_FIELD_OP, LUMINA_EVENT_ANIM_DONE);
		done.AddInt32(LUMINA_EV_TOKEN, proxy.token);
		_Send(done);
	}
	proxy.source = -1;
	proxy.fading = false;
	proxy.flying = false;
	proxy.releaseAt = 0;
	proxy.from = fx::Image();
	proxy.to = fx::Image();
}


void
LuminaEngine::_ProxyLanded(int32 token)
{
	// The geometry animation of a proxy finished (or was completed early
	// because the clock stopped).
	Proxy* proxy = _ProxyByToken(token);
	if (proxy == NULL || proxy->source < 0)
		return;
	proxy->flying = false;
	if (proxy->kind == fx::LifecycleKind::Restore) {
		// Bring the real window back beneath the proxy and keep covering it
		// for a moment while it repaints.
		if (fParked.count(proxy->source) != 0)
			_Unpark(proxy->source, false);
		proxy->releaseAt = _Now() + kRestoreSettle;
	} else
		_ReleaseProxy(*proxy, "landed");
}


bool
LuminaEngine::_ProxiesBusy() const
{
	for (const Proxy& proxy : fProxies) {
		if (proxy.source >= 0)
			return true;
	}
	return false;
}


void
LuminaEngine::_LifecycleReset()
{
	for (Proxy& proxy : fProxies) {
		if (proxy.source >= 0)
			_ReleaseProxy(proxy, "reset");
	}
	fProxies.clear();
	fSnapshots.Clear();
	fBackgrounds.Clear();
	fBackgroundFrames.clear();
	fPendingShow.clear();
	fAnchors.clear();
}


// #pragma mark - cursor themes


void
LuminaEngine::_ApplyCursorTheme(const char* path)
{
	// app_server already contains a complete cursor-set loader,
	// CursorManager::SetCursorSet(), which replaces the system cursor of
	// every BCursorID found in a CursorSet file — but nothing in Haiku calls
	// it. Applications keep asking for B_CURSOR_ID_I_BEAM, _RESIZE_*, …
	// by ID, so the theme reaches every native application while custom
	// application cursors are left alone. Resetting re-runs the stock
	// initialisation at the current UI scale.
	CursorManager& manager = fDesktop->GetCursorManager();
	if (path != NULL && path[0] != '\0') {
		BEntry entry(path);
		if (!entry.Exists()) {
			LIFECYCLE_LOG(LOG_WARNING, "cursor theme %s not found", path);
			return;
		}
		manager.SetCursorSet(path);
		fCursorTheme = path;
		fStats.cursorThemesApplied++;
	} else {
		// Same scale as Desktop::Init(): bold font size / 12.
		DesktopSettings settings(fDesktop);
		ServerFont bold;
		settings.GetDefaultBoldFont(bold);
		manager.InitializeCursors(bold.Size() / 12.0f);
		fCursorTheme = "";
	}
	// The cursor on screen is still the old object until something sets a
	// cursor again; show the new default now (applications re-apply their
	// own cursor on the next mouse movement over their views).
	fDesktop->SetCursor(NULL);
	LIFECYCLE_LOG(LOG_INFO, "cursor theme: %s",
		fCursorTheme.IsEmpty() ? "stock" : fCursorTheme.String());
}

}	// namespace engine
}	// namespace lumina
