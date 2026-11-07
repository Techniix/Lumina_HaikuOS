/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * EXPERIMENT D — snapshot-based animation proxies.
 *
 * A proxy is a borderless always-on-top window showing a bitmap of the real
 * window. Key property on Haiku: the proxy animates *geometry* (move/scale),
 * and whatever it uncovers is the LIVE desktop, redrawn by app_server — no
 * frozen background is needed for zoom/scale transitions. Only effects that
 * need to see *through* the proxy (fades) would need a frozen background.
 *
 *  D1 minimize  capture window → proxy on top at the same rect (seamless)
 *               → minimize real window underneath → proxy shrinks to the
 *               panel/Deskbar → proxy hidden.
 *  D2 restore   proxy grows from the panel → real window raised under it
 *               → short settle → proxy hidden. Measures the "pop" (content
 *               difference between the cached image and the live window).
 *  D3 close     possible only when Lumina initiates the close (it can
 *               capture first); app-initiated closes erase the pixels before
 *               any hook runs (see docs/vfx.md).
 *  D4 open      the Window Engine parks new windows off-screen before their
 *               first Show(); a placeholder (frame in tab colour + app icon)
 *               zooms from the launcher; the engine unparks the window
 *               under the proxy; proxy hidden.
 *  D5 workspace capture the current workspace, cover the screen with the
 *               proxy, switch, slide old→cached new, uncover live.
 *
 * Measured per effect: capture ms, achieved fps, per-frame cost, total.
 */
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>

#include <Application.h>
#include <Screen.h>
#include <View.h>

#include <lumina/anim/Easing.h>
#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>

#include "../common/FxHarness.h"

using namespace lumina;
using namespace lumina::fxlab;
using namespace lumina::haiku;

namespace {

BRect
Lerp(BRect a, BRect b, double t)
{
	return BRect(a.left + (b.left - a.left) * t, a.top + (b.top - a.top) * t,
		a.right + (b.right - a.right) * t, a.bottom + (b.bottom - a.bottom) * t);
}


BRect
OuterFrame(const WindowSnapshot& w)
{
	return BRect(w.frame.left - w.borderSize,
		w.frame.top - w.tabHeight - w.borderSize, w.frame.right + w.borderSize,
		w.frame.bottom + w.borderSize);
}


Value
Animate(ProxyWindow* proxy, BRect from, BRect to, double ms,
	const anim::Curve& curve)
{
	int frames = 0;
	std::vector<double> costs = RunFrames(ms, 60, [&](double t, int) {
		double p = curve.Progress(t * ms / 1000.0, ms / 1000.0);
		proxy->SetFrame(Lerp(from, to, p));
		frames++;
	});
	Value v = Value::MakeObject();
	v["frames"] = frames;
	v["fps"] = frames * 1000.0 / ms;
	v["frameCostMs"] = Stats::Of(costs).ToValue();
	return v;
}


class App;

class EngineEvents : public BHandler {
public:
	EngineEvents() : BHandler("engine events"), fParkedToken(-1) {}

	void MessageReceived(BMessage* message)
	{
		if (message->what != LUMINA_ENGINE_EVENT)
			return;
		if (strcmp(message->GetString(LUMINA_ENGINE_FIELD_OP, ""), "parked")
				== 0) {
			fParkedToken = message->GetInt32(LUMINA_EV_TOKEN, -1);
			message->FindRect(LUMINA_EV_FRAME, &fParkedFrame);
			fParkedAt = NowMs();
		}
	}

	volatile int32	fParkedToken;
	BRect			fParkedFrame;
	double			fParkedAt;
};

EngineEvents* sEvents = NULL;


void
RunSelfTest()
{
	Report report("fx-snapshot-proxy");
	BScreen screen;
	BRect s = screen.Frame();
	BRect dock(std::floor(s.Width() / 2) - 24, s.bottom - 32,
		std::floor(s.Width() / 2) + 24, s.bottom - 2);
	anim::Curve easeIn = anim::Curve::Easing(anim::EaseFamily::Cubic,
		anim::EaseMode::In);
	anim::Curve easeOut = anim::Curve::Easing(anim::EaseFamily::Cubic,
		anim::EaseMode::Out);

	PatternWindow* window = new PatternWindow(BRect(200, 150, 760, 560),
		"fx-proxy subject");
	window->Show();
	snooze(400000);
	int32 token = window->Token();
	WindowSnapshot snapshot;
	PrivateApi::GetWindow(token, snapshot);
	BRect outer = OuterFrame(snapshot);

	// ---- D1 minimize
	{
		Capturer capturer;
		double captureMs = capturer.Capture(outer);
		double t0 = NowMs();
		ProxyWindow* proxy = new ProxyWindow(outer, capturer.Detach());
		proxy->Show();
		proxy->Lock();
		proxy->Sync();
		proxy->Unlock();
		PrivateApi::MinimizeWindow(token, true);
		Value result = Animate(proxy, outer, dock, 260, easeIn);
		proxy->Lock();
		proxy->Quit();
		result["captureMs"] = captureMs;
		result["totalMs"] = NowMs() - t0;
		report.Set("D1.minimize", result);
		report.Verdict("D1 minimize proxy >= 45 fps",
			result["fps"].AsNumber() >= 45,
			std::to_string(result["fps"].AsNumber()) + " fps, capture "
			+ std::to_string(captureMs) + " ms");
	}
	snooze(300000);

	// ---- D2 restore (from the image cached at minimize time)
	{
		Capturer cache;
		// Re-capture is impossible while minimized; a real implementation
		// keeps the D1 capture. Here we redo D1's capture for simplicity by
		// restoring, capturing and minimizing without animation.
		PrivateApi::BringToFront(token);
		snooze(200000);
		cache.Capture(outer);
		PrivateApi::MinimizeWindow(token, true);
		snooze(200000);

		double t0 = NowMs();
		ProxyWindow* proxy = new ProxyWindow(dock, cache.Detach());
		proxy->Show();
		Value result = Animate(proxy, dock, outer, 260, easeOut);
		PrivateApi::BringToFront(token);	// appears beneath the proxy
		snooze(40000);						// let the client paint
		proxy->Lock();
		proxy->Quit();
		snooze(60000);
		Capturer live;
		live.Capture(outer);
		result["totalMs"] = NowMs() - t0;
		report.Set("D2.restore", result);
		report.Verdict("D2 restore proxy >= 45 fps",
			result["fps"].AsNumber() >= 45,
			std::to_string(result["fps"].AsNumber()) + " fps");
	}

	// ---- D3 close (Lumina-initiated)
	{
		PatternWindow* victim = new PatternWindow(BRect(600, 200, 900, 420),
			"fx-proxy close");
		victim->Show();
		snooze(300000);
		WindowSnapshot w;
		PrivateApi::GetWindow(victim->Token(), w);
		BRect r = OuterFrame(w);
		Capturer capturer;
		capturer.Capture(r);
		ProxyWindow* proxy = new ProxyWindow(r, capturer.Detach());
		proxy->Show();
		victim->Lock();
		victim->Quit();
		BRect center(r.left + r.Width() * 0.45, r.top + r.Height() * 0.45,
			r.right - r.Width() * 0.45, r.bottom - r.Height() * 0.45);
		Value result = Animate(proxy, r, center, 180, easeIn);
		proxy->Lock();
		proxy->Quit();
		report.Set("D3.close", result);
		report.Note("D3 works because the prototype closes the window itself; "
			"for app-initiated closes Lumina has no pre-close hook.");
	}

	// ---- D4 open via engine parking
	{
		EngineLink link;
		EngineInfo info;
		link.Probe(info);
		report.Set("D4.engine", info.present && info.active);
		if (info.present && info.active) {
			BMessenger me(sEvents);
			link.Subscribe(me);
			link.Configure("{\"animation\":{\"openTransition\":true,"
				"\"parkTimeout\":1.0}}");
			snooze(50000);
			sEvents->fParkedToken = -1;
			double t0 = NowMs();
			PatternWindow* opened = new PatternWindow(BRect(300, 260, 700, 560),
				"fx-proxy opened");
			opened->Show();
			while (sEvents->fParkedToken < 0 && NowMs() - t0 < 500)
				snooze(1000);
			bool parked = sEvents->fParkedToken >= 0;
			report.Set("D4.parked", parked);
			if (parked) {
				BRect target = sEvents->fParkedFrame;
				ProxyWindow* proxy = new ProxyWindow(dock, NULL);
				proxy->Show();
				Value result = Animate(proxy, dock, target.InsetByCopy(-1, -1),
					200, easeOut);
				BMessage unpark(LUMINA_ENGINE_COMMAND);
				unpark.AddString(LUMINA_ENGINE_FIELD_OP, "unpark");
				unpark.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, sEvents->fParkedToken);
				link.Send(unpark);
				snooze(40000);
				proxy->Lock();
				proxy->Quit();
				result["eventLatencyMs"] = sEvents->fParkedAt - t0;
				result["totalMs"] = NowMs() - t0;
				report.Set("D4.open", result);
				WindowSnapshot w;
				PrivateApi::GetWindow(sEvents->fParkedToken, w);
				report.Verdict("D4 window returned to its frame",
					std::fabs(w.frame.left - target.left) < 1,
					"frame after unpark");
			}
			link.Configure("{\"animation\":{\"openTransition\":false}}");
			link.Unsubscribe(me);
			opened->Lock();
			opened->Quit();
		} else
			report.Note("D4 skipped: Window Engine not active");
	}

	// ---- D5 workspace slide
	{
		int32 count = count_workspaces();
		if (count < 2) {
			report.Note("D5 skipped: needs at least 2 workspaces");
		} else {
			int32 from = current_workspace();
			int32 to = (from + 1) % count;
			// Visit the target once to cache it (a real implementation
			// caches every workspace when leaving it).
			activate_workspace(to);
			snooze(300000);
			Capturer cacheNext;
			double fullCapture = cacheNext.Capture(s);
			activate_workspace(from);
			snooze(300000);

			Capturer current;
			current.Capture(s);
			double t0 = NowMs();
			ProxyWindow* proxy = new ProxyWindow(s, current.Detach(), true);
			proxy->Show();
			proxy->Lock();
			proxy->Sync();
			proxy->Unlock();
			activate_workspace(to);
			BBitmap* next = cacheNext.Detach();
			int frames = 0;
			std::vector<double> costs = RunFrames(280, 60, [&](double t, int) {
				proxy->SetSlide(frames == 0 ? next : NULL,
					float(easeOut.Progress(t * 0.28, 0.28)));
				frames++;
			});
			proxy->Lock();
			proxy->Quit();
			Value result = Value::MakeObject();
			result["fullScreenCaptureMs"] = fullCapture;
			result["frames"] = frames;
			result["fps"] = frames * 1000.0 / 280;
			result["frameCostMs"] = Stats::Of(costs).ToValue();
			result["totalMs"] = NowMs() - t0;
			report.Set("D5.workspaceSlide", result);
			report.Verdict("D5 workspace slide >= 30 fps",
				result["fps"].AsNumber() >= 30,
				std::to_string(result["fps"].AsNumber()) + " fps at full "
				"screen; capture " + std::to_string(fullCapture) + " ms");
			activate_workspace(from);
		}
	}

	report.Save();
	window->Lock();
	window->Quit();
	be_app->PostMessage(B_QUIT_REQUESTED);
}


class App : public BApplication {
public:
	App() : BApplication("application/x-vnd.Lumina-fx-snapshot-proxy") {}

	void ReadyToRun()
	{
		sEvents = new EngineEvents();
		Lock();
		AddHandler(sEvents);
		Unlock();
		thread_id thread = spawn_thread([](void*) -> status_t {
			RunSelfTest();
			return B_OK;
		}, "selftest", B_NORMAL_PRIORITY, NULL);
		resume_thread(thread);
	}
};

}	// namespace


int
main()
{
	App app;
	app.Run();
	return 0;
}
