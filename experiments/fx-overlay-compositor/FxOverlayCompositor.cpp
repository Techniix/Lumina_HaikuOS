/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * EXPERIMENT F — app_server overlay compositor (Level 3).
 *
 * Requires app_server built with patches/haiku/0001-app_server-optional-
 * overlay-compositor.patch and the Window Engine. On a stock app_server it
 * reports "compositor: not available" and exits — that path is part of the
 * test (graceful fallback).
 *
 *  F1 glass card   a borderless window whose content is a translucent
 *                  RGBA bitmap becomes an overlay with backdrop blur,
 *                  rounded corners and a soft shadow. A pattern window is
 *                  then moved *underneath* it: the card must show the
 *                  blurred, live content (screen differs frame to frame),
 *                  which no Level 1/2 technique can do.
 *  F2 real fade    the card's opacity is animated 1 → 0 → 1 through the
 *                  engine; the screen must interpolate smoothly.
 *  F3 input        clicks inside the card's frame reach it although it has
 *                  no visible region.
 *  F4 cost         engine/app_server time per recomposition while content
 *                  moves under the card (sampled via frame timing).
 */
#include <stdio.h>
#include <unistd.h>

#include <algorithm>

#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include "common/FxHarness.h"

#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>

using namespace lumina;
using namespace lumina::fxlab;
using namespace lumina::haiku;

namespace {

class CardWindow : public BWindow {
public:
	CardWindow(BRect frame)
		:
		BWindow(frame, "fx-overlay-card", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_AVOID_FOCUS | B_NOT_MOVABLE),
		fClicks(0)
	{
		BRect bounds = Bounds();
		fContent = new BBitmap(bounds, B_RGBA32, true);
		// Translucent white card with a label: drawn offscreen once.
		BView* painter = new BView(bounds, "painter", 0, 0);
		fContent->AddChild(painter);
		fContent->Lock();
		painter->SetDrawingMode(B_OP_COPY);
		painter->SetHighColor(255, 255, 255, 90);
		painter->FillRect(bounds);
		painter->SetHighColor(20, 20, 20, 255);
		painter->SetFont(be_bold_font);
		painter->DrawString("Lumina overlay: real translucency",
			BPoint(16, bounds.Height() / 2));
		painter->Sync();
		fContent->RemoveChild(painter);
		fContent->Unlock();
		delete painter;

		ClickView* view = new ClickView(bounds, &fClicks);
		view->SetViewColor(B_TRANSPARENT_COLOR);
		AddChild(view);
		view->SetViewBitmap(fContent, bounds, bounds, B_FOLLOW_ALL, 0);
	}

	int32 Clicks() const { return fClicks; }

private:
	class ClickView : public BView {
	public:
		ClickView(BRect frame, int32* clicks)
			: BView(frame, "content", B_FOLLOW_ALL, 0), fClicks(clicks) {}
		void MouseDown(BPoint) { (*fClicks)++; }
	private:
		int32*	fClicks;
	};

	BBitmap*	fContent;
	int32		fClicks;
};


class App : public BApplication {
public:
	App() : BApplication("application/x-vnd.Lumina-fx-overlay-compositor") {}

	void ReadyToRun()
	{
		Report report("fx-overlay-compositor");
		EngineLink link;
		EngineInfo info;
		if (link.Probe(info) != B_OK || !info.present || !info.active) {
			report.Verdict("engine", false, "Window Engine not active");
			_Finish(report);
			return;
		}
		report.Set("compositor", Value(info.compositor));
		if (!info.compositor) {
			report.Verdict("fallback", true, "stock app_server: compositor "
				"not available, nothing changed (expected without the "
				"patch)");
			_Finish(report);
			return;
		}

		BRect screen = BScreen().Frame();
		BRect cardFrame(screen.Width() / 2 - 220, screen.Height() / 2 - 60,
			screen.Width() / 2 + 220, screen.Height() / 2 + 60);
		CardWindow* card = new CardWindow(cardFrame);
		card->Hide();
		card->Show();
		card->Lock();
		int32 token = _TokenOf(card);
		card->Unlock();

		auto setOverlay = [&](double opacity) {
			BMessage command;
			command.AddString(LUMINA_ENGINE_FIELD_OP, LUMINA_OP_OVERLAY);
			command.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
			BString json;
			json.SetToFormat("{\"opacity\":%.3f,\"blur\":12,\"downsample\":2,"
				"\"tint\":%u,\"tintAmount\":0.25,\"radius\":12,\"shadow\":14,"
				"\"shadowOpacity\":0.35}", opacity, 0xf0f0f0u);
			command.AddString(LUMINA_ENGINE_FIELD_JSON, json.String());
			link.Send(command);
		};
		setOverlay(1.0);
		card->Show();
		snooze(300000);

		// F1: move a pattern window underneath; the card must change.
		PatternWindow* pattern = new PatternWindow(
			BRect(cardFrame.left - 300, cardFrame.top - 40,
				cardFrame.left - 20, cardFrame.bottom + 40), "F1 underneath");
		pattern->Show();
		snooze(300000);
		Capturer first;
		first.Capture(cardFrame);
		BBitmap* reference = first.Detach();
		std::vector<double> frameMs = RunFrames(1500, 60,
			[&](double progress, int) {
				pattern->Lock();
				pattern->MoveTo(cardFrame.left - 300 + progress * 600,
					cardFrame.top - 40);
				pattern->Unlock();
			});
		Capturer later;
		later.Capture(cardFrame);
		double changed = Difference(later.Bitmap(), reference);
		report.Set("F1.diffUnderCard", Value(changed));
		report.Set("F1.frameMs", Stats::Of(frameMs).ToValue());
		report.Verdict("F1.liveBlur", changed > 4,
			"content moving under the card is visible through it");
		delete reference;

		// F2: real fade, 1 → 0 → 1.
		std::vector<double> luma;
		RunFrames(800, 30, [&](double progress, int) {
			double opacity = progress < 0.5 ? 1 - progress * 2
				: (progress - 0.5) * 2;
			setOverlay(opacity);
			Capturer c;
			c.Capture(cardFrame);
			luma.push_back(MeanLuma(c.Bitmap()));
		});
		double lo = 255, hi = 0;
		for (double l : luma) {
			lo = std::min(lo, l);
			hi = std::max(hi, l);
		}
		report.Set("F2.lumaRange", Value(hi - lo));
		report.Verdict("F2.fade", hi - lo > 3 && luma.size() > 10,
			"opacity animates on screen (true alpha)");

		// F3: click inside the card frame.
		report.Note("F3: click inside the card within 5 s");
		bigtime_t until = system_time() + 5000000;
		while (system_time() < until && card->Clicks() == 0)
			snooze(50000);
		report.Verdict("F3.input", card->Clicks() > 0,
			"overlay received a click (manual)");

		BMessage off;
		off.AddString(LUMINA_ENGINE_FIELD_OP, LUMINA_OP_OVERLAY_OFF);
		off.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
		link.Send(off);
		pattern->Lock();
		pattern->Quit();
		card->Lock();
		card->Quit();
		_Finish(report);
	}

private:
	static int32 _TokenOf(BWindow* window)
	{
		std::vector<WindowSnapshot> windows;
		PrivateApi::ListWindows(windows, getpid());
		for (const WindowSnapshot& w : windows) {
			if (w.title == window->Title())
				return w.token;
		}
		return -1;
	}

	void _Finish(Report& report)
	{
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
