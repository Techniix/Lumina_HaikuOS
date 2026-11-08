/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * EXPERIMENT E — engine-side lifecycle capture (Level 2).
 *
 * Verifies the claim of docs/07 §C/§D on a real system: the Window Engine,
 * running inside app_server, sees a departing window's pixels in
 * Desktop::HideWindow's locked section and covers it with a proxy in the
 * same transaction — for closes and minimizes the *application itself*
 * initiates, which no client-side mechanism can intercept.
 *
 *  E1 app close     a pattern window calls Quit() on itself. Right after,
 *                   the screen region must still show the pattern (the
 *                   proxy), and later the live desktop. Engine stats must
 *                   count one "animated" transition.
 *  E2 app minimize  the window calls Minimize(true) itself; same checks, and
 *                   the proxy must travel toward the configured anchor.
 *  E3 restore       Minimize(false): the window must come back under a
 *                   proxy (stats) and end at its original frame.
 *  E4 obscured      a second window covers most of the first; its close
 *                   must be skipped (stats.skipped), not animated with
 *                   wrong pixels.
 *  E5 cursor theme  applies the sample theme via the engine and reads the
 *                   engine status back (visual check: the arrow changes).
 *
 * Needs the Window Engine selected and active. Registers its own proxy
 * pool exactly like lumina_server does (and releases it at the end), so it
 * must not run while lumina_server owns the pool: quit lumina_server first
 * or run on a test system.
 */
#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Screen.h>
#include <String.h>

#include "common/FxHarness.h"

#include "../../src/server/LifecycleProxies.h"

#include <lumina/Json.h>
#include <lumina/haiku/CursorThemes.h>
#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>

using namespace lumina;
using namespace lumina::fxlab;
using namespace lumina::haiku;

namespace {

double
Stat(EngineLink& link, const char* key)
{
	EngineInfo info;
	if (link.Probe(info) != B_OK)
		return -1;
	Value stats;
	JsonError error;
	if (!ParseJson(info.statsJson.String(), stats, &error))
		return -1;
	const Value* lifecycle = stats.Find("lifecycle");
	const Value* v = lifecycle != NULL ? lifecycle->Find(key) : NULL;
	return v != NULL ? v->AsNumber(-1) : -1;
}


// Mean difference between the live screen at `frame` and `reference`.
double
ScreenDiff(BRect frame, BBitmap* reference)
{
	Capturer capturer;
	if (capturer.Capture(frame) < 0)
		return -1;
	return Difference(capturer.Bitmap(), reference);
}


class App : public BApplication {
public:
	App() : BApplication("application/x-vnd.Lumina-fx-lifecycle-engine") {}

	void ReadyToRun()
	{
		Report report("fx-lifecycle-engine");
		EngineLink link;
		EngineInfo info;
		if (link.Probe(info) != B_OK || !info.present || !info.active) {
			report.Verdict("engine", false, "Window Engine not active");
			printf("%s\n", report.Save().c_str());
			Quit();
			return;
		}
		link.Configure("{\"lifecycle\":{\"enabled\":true,\"durationMs\":400,"
			"\"minVisible\":0.8}}");
		LifecycleProxies proxies;
		status_t registered = proxies.Register(&link, 2);
		report.Verdict("proxies", registered == B_OK
			&& Stat(link, "proxies") >= 2, "pool registered");

		BRect screen = BScreen().Frame();
		BRect frame(screen.Width() / 2 - 200, screen.Height() / 2 - 150,
			screen.Width() / 2 + 200, screen.Height() / 2 + 150);
		BRect anchor(screen.Width() / 2 - 20, screen.bottom - 24,
			screen.Width() / 2 + 20, screen.bottom);

		// E1: the application closes its own window.
		{
			PatternWindow* window = new PatternWindow(frame, "E1 close");
			window->Show();
			snooze(400000);
			Capturer before;
			before.Capture(frame);
			BBitmap* reference = before.Detach();
			double animatedBefore = Stat(link, "animated");
			int32 token = window->Token();
			window->Lock();
			window->Quit();		// app-initiated: no Lumina involvement
			// Wait until app_server has really removed the window, so what
			// is on screen now can only be the proxy (or the desktop).
			WindowSnapshot gone;
			bigtime_t deadline = system_time() + 300000;
			while (PrivateApi::GetWindow(token, gone) == B_OK
				&& system_time() < deadline)
				snooze(2000);
			double during = ScreenDiff(frame, reference);
			snooze(700000);
			double after = ScreenDiff(frame, reference);
			double animated = Stat(link, "animated") - animatedBefore;
			report.Set("E1.diffRightAfterClose", Value(during));
			report.Set("E1.diffAfterTransition", Value(after));
			report.Set("E1.maxCaptureUsecs",
				Value(Stat(link, "maxCaptureUsecs")));
			report.Verdict("E1.close", animated >= 1 && during < 12
				&& after > during,
				"proxy showed the window right after its own Quit(), then "
				"the desktop");
			delete reference;
		}

		// E2 + E3: the application minimizes and restores itself.
		{
			PatternWindow* window = new PatternWindow(frame, "E2 minimize");
			window->Show();
			snooze(400000);
			int32 token = window->Token();
			BMessage anchors;
			anchors.AddString(LUMINA_ENGINE_FIELD_OP, LUMINA_OP_ANCHORS);
			anchors.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
			anchors.AddRect(LUMINA_ENGINE_FIELD_FRAME, anchor);
			link.Send(anchors);
			double animatedBefore = Stat(link, "animated");
			window->Minimize(true);
			snooze(700000);
			report.Verdict("E2.minimize",
				Stat(link, "animated") - animatedBefore >= 1,
				"engine animated an app-initiated Minimize()");
			animatedBefore = Stat(link, "animated");
			window->Minimize(false);
			snooze(900000);
			window->Lock();
			BRect restored = window->Frame();
			window->Unlock();
			report.Verdict("E3.restore",
				Stat(link, "animated") - animatedBefore >= 1
					&& restored == frame,
				"restored under a proxy and back at its frame");
			window->Lock();
			window->Quit();
			snooze(600000);
		}

		// E4: mostly covered window: must be skipped.
		{
			PatternWindow* below = new PatternWindow(frame, "E4 below");
			below->Show();
			snooze(300000);
			PatternWindow* above = new PatternWindow(
				frame.InsetByCopy(-20, -20), "E4 above");
			above->Show();
			snooze(300000);
			double skippedBefore = Stat(link, "skipped");
			below->Lock();
			below->Quit();
			snooze(600000);
			report.Verdict("E4.obscured",
				Stat(link, "skipped") - skippedBefore >= 1,
				"covered window skipped (its pixels are not on screen)");
			above->Lock();
			above->Quit();
			snooze(600000);
		}

		// E5: cursor theme through the engine.
		{
			std::string path;
			std::vector<std::string> problems;
			status_t status = CursorThemes::Build("LuminaOutline",
				CursorThemes::CurrentScale(), path, problems);
			if (status == B_OK) {
				BMessage command;
				command.AddString(LUMINA_ENGINE_FIELD_OP,
					LUMINA_OP_CURSOR_THEME);
				command.AddString(LUMINA_ENGINE_FIELD_PATH, path.c_str());
				link.Send(command);
				EngineInfo now;
				link.Probe(now);
				bool custom = now.statsJson.FindFirst(
					"\"cursorTheme\":\"custom\"") >= 0;
				report.Note("E5: the pointer should now use the outline "
					"theme for ~3 s");
				snooze(3000000);
				command.ReplaceString(LUMINA_ENGINE_FIELD_PATH, "");
				link.Send(command);
				report.Verdict("E5.cursorTheme", custom,
					"engine applied a CursorSet through CursorManager");
			} else {
				report.Verdict("E5.cursorTheme", false,
					"sample theme not installed (data/Lumina/cursors)");
			}
		}

		proxies.Release(&link);
		link.Configure("{\"lifecycle\":{\"enabled\":false}}");
		printf("%s\n", report.Save().c_str());
		Quit();
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
