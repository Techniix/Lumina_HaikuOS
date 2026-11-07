/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * EXPERIMENT B — screen-capture-assisted panel glass.
 *
 * Pixels under an opaque window do not exist in app_server (no backing
 * store), so "blur what is behind the panel" needs the panel to not cover
 * that area at capture time. This prototype measures every way to get
 * those pixels:
 *
 *  B1 readback cost   BScreen::ReadBitmap() for panel bands and full screen
 *                     (front buffer, i.e. video memory: often slow).
 *  B2 glass cost      CPU cost of the frosted-glass pipeline per quality.
 *  B3 edge glass      live band *next to* the panel → mirrored → blurred.
 *                     Never needs to hide the panel. Cost per refresh.
 *  B4 hide-capture    Hide panel → wait until the exposed region has been
 *                     redrawn by app_server and the clients → capture →
 *                     Show. The hidden time IS the flicker; measured.
 *  B5 move-capture    Same, but moving the panel off-screen instead of
 *                     hiding it (sometimes cheaper in app_server).
 *  B6 reveal capture  Show the panel with transparent views, capture its
 *                     own region (= true underlying pixels, see experiment
 *                     A/H1), paint glass. Measures the time to first paint.
 *
 * Targets (written to the report and checked automatically):
 *   edge refresh  <= 2.0 ms per update for a 1920x32 panel (8 Hz = 1.6 %
 *                    of one core)
 *   hide-capture  flicker <= 16.7 ms (one frame) to be acceptable at all
 *   reveal        first glass paint <= 33 ms after Show()
 *   readback      < 40 ms per megapixel for a "capture" strategy to be
 *                    chosen automatically by the effects engine
 *
 * Run: fx-capture-glass [--demo edge|reveal|hide]
 */
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include <lumina/fx/Glass.h>

#include "../common/FxHarness.h"

using namespace lumina;
using namespace lumina::fxlab;

namespace {

class GlassView : public BView {
public:
	GlassView(BRect frame)
		:
		BView(frame, "glass", B_FOLLOW_ALL, B_WILL_DRAW),
		fGlass(NULL),
		fTransparent(true)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	~GlassView() { delete fGlass; }

	void Draw(BRect update)
	{
		if (fGlass != NULL)
			DrawBitmap(fGlass, update, update);
		else if (!fTransparent) {
			SetHighUIColor(B_MENU_BACKGROUND_COLOR);
			FillRect(update);
		}
	}

	BBitmap*	fGlass;
	bool		fTransparent;
};


class GlassPanel : public BWindow {
public:
	GlassPanel(BRect frame)
		:
		BWindow(frame, "fx-capture-glass", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_AVOID_FOCUS | B_NOT_MOVABLE)
	{
		fView = new GlassView(Bounds());
		AddChild(fView);
	}

	void SetGlass(BBitmap* glass)
	{
		Lock();
		delete fView->fGlass;
		fView->fGlass = glass;
		fView->Invalidate();
		fView->Sync();
		Unlock();
	}

	void SetOpaque()
	{
		Lock();
		fView->fTransparent = false;
		fView->Invalidate();
		fView->Sync();
		Unlock();
	}

	GlassView*	fView;
};


BBitmap*
RenderGlass(BBitmap* source, fx::GlassRenderer& renderer, double* ms)
{
	BBitmap* out = new BBitmap(source->Bounds(), B_RGB32);
	fx::GlassParams params;
	fx::GlassTiming timing;
	renderer.Render(ViewOf(source), ViewOf(out), params, &timing);
	if (ms)
		*ms = timing.totalMs;
	return out;
}


// Captures until two consecutive captures agree and differ from the panel
// colour: the moment the exposed area is fully redrawn.
double
WaitForSettledCapture(BRect rect, Capturer& capturer, double timeoutMs,
	BBitmap* panelPixels)
{
	double start = NowMs();
	Capturer previous;
	previous.Capture(rect);
	while (NowMs() - start < timeoutMs) {
		snooze(1000);
		capturer.Capture(rect);
		bool stable = Difference(previous.Bitmap(), capturer.Bitmap()) < 0.5;
		bool revealed = panelPixels == NULL
			|| Difference(panelPixels, capturer.Bitmap()) > 2.0;
		if (stable && revealed)
			return NowMs() - start;
		previous.Capture(rect);
	}
	return -1;
}


void
RunSelfTest(const char* demo)
{
	Report report("fx-capture-glass");
	BScreen screen;
	BRect s = screen.Frame();

	// ---- B1 readback
	struct Size { const char* name; BRect rect; };
	std::vector<Size> sizes = {
		{"1920x32", BRect(0, s.bottom - 31, std::min(1919.0f, s.right), s.bottom)},
		{"1920x64", BRect(0, s.bottom - 63, std::min(1919.0f, s.right), s.bottom)},
		{"1920x256", BRect(0, s.bottom - 255, std::min(1919.0f, s.right), s.bottom)},
		{"fullscreen", s},
	};
	double msPerMegapixel = 0;
	for (const Size& size : sizes) {
		Capturer capturer;
		std::vector<double> samples;
		for (int i = 0; i < 20; i++)
			samples.push_back(capturer.Capture(size.rect));
		Stats stats = Stats::Of(samples);
		report.Set(std::string("B1.readback.") + size.name, stats.ToValue());
		double megapixels = (size.rect.Width() + 1) * (size.rect.Height() + 1)
			/ 1e6;
		if (strcmp(size.name, "fullscreen") == 0)
			msPerMegapixel = stats.mean / megapixels;
		printf("readback %-10s mean %.2f ms\n", size.name, stats.mean);
	}
	report.Set("B1.msPerMegapixel", msPerMegapixel);
	report.Verdict("B1 readback fast enough for capture strategy",
		msPerMegapixel < 40, std::to_string(msPerMegapixel) + " ms/MP");

	// ---- B2 glass pipeline
	fx::GlassRenderer renderer;
	{
		fx::Image source(1920, 32), out(1920, 32);
		for (int downsample = 0; downsample <= 3; downsample++) {
			fx::GlassParams params;
			params.downsample = downsample;
			std::vector<double> samples;
			for (int i = 0; i < 30; i++) {
				fx::GlassTiming t;
				renderer.Render(source.View(), out.View(), params, &t);
				samples.push_back(t.totalMs);
			}
			report.Set("B2.glass1920x32.downsample" + std::to_string(downsample),
				Stats::Of(samples).ToValue());
		}
	}

	// A busy pattern under the test panel area.
	BRect panelRect(s.left, s.bottom - 31, s.right, s.bottom);
	PatternWindow* pattern = new PatternWindow(BRect(s.left + 80,
		s.bottom - 300, s.left + 900, s.bottom - 10), "fx-glass pattern");
	pattern->Show();
	snooze(400000);

	// ---- B3 edge glass (panel stays visible)
	GlassPanel* panel = new GlassPanel(panelRect);
	panel->SetOpaque();
	panel->Show();
	snooze(150000);
	{
		BRect band(panelRect.left, panelRect.top - 24, panelRect.right,
			panelRect.top - 1);
		std::vector<double> total, capture;
		fx::Image source(panelRect.IntegerWidth() + 1,
			panelRect.IntegerHeight() + 1);
		Capturer capturer;
		for (int i = 0; i < 30; i++) {
			double t0 = NowMs();
			capture.push_back(capturer.Capture(band));
			fx::SynthesizeFromEdgeBand(ViewOf(capturer.Bitmap()),
				fx::Edge::Bottom, source.View());
			BBitmap* glass = new BBitmap(panelRect.OffsetToCopy(0, 0), B_RGB32);
			fx::GlassParams params;
			renderer.Render(source.View(), ViewOf(glass), params);
			panel->SetGlass(glass);
			total.push_back(NowMs() - t0);
			if (i % 10 == 5)
				pattern->SetPhase(i / 10);
		}
		Stats stats = Stats::Of(total);
		report.Set("B3.edge.totalMs", stats.ToValue());
		report.Set("B3.edge.captureMs", Stats::Of(capture).ToValue());
		report.Verdict("B3 edge refresh within 2 ms", stats.mean <= 2.0,
			std::to_string(stats.mean) + " ms per refresh (incl. capture)");
	}

	// ---- B4 hide-capture-show
	{
		std::vector<double> hidden, settle;
		for (int i = 0; i < 8; i++) {
			pattern->SetPhase(i);
			Capturer capturer, shown;
			shown.Capture(panelRect);
			double t0 = NowMs();
			panel->Lock();
			panel->Hide();
			panel->Sync();
			panel->Unlock();
			double s0 = WaitForSettledCapture(panelRect, capturer, 200,
				shown.Bitmap());
			double rendered;
			BBitmap* glass = RenderGlass(capturer.Bitmap(), renderer, &rendered);
			panel->SetGlass(glass);
			panel->Lock();
			panel->Show();
			panel->Sync();
			panel->Unlock();
			hidden.push_back(NowMs() - t0);
			settle.push_back(s0);
			snooze(100000);
		}
		Stats stats = Stats::Of(hidden);
		report.Set("B4.hideCapture.hiddenMs", stats.ToValue());
		report.Set("B4.hideCapture.settleMs", Stats::Of(settle).ToValue());
		report.Verdict("B4 hide-capture flicker <= 1 frame", stats.max <= 16.7,
			"panel invisible for " + std::to_string(stats.mean) + " ms avg, "
			+ std::to_string(stats.max) + " ms worst");
	}

	// ---- B5 move off-screen instead of hiding
	{
		std::vector<double> hidden;
		for (int i = 0; i < 8; i++) {
			pattern->SetPhase(i + 1);
			Capturer capturer, shown;
			shown.Capture(panelRect);
			double t0 = NowMs();
			panel->Lock();
			panel->MoveTo(panelRect.left, s.bottom + 100);
			panel->Sync();
			panel->Unlock();
			WaitForSettledCapture(panelRect, capturer, 200, shown.Bitmap());
			BBitmap* glass = RenderGlass(capturer.Bitmap(), renderer, NULL);
			panel->SetGlass(glass);
			panel->Lock();
			panel->MoveTo(panelRect.LeftTop());
			panel->Sync();
			panel->Unlock();
			hidden.push_back(NowMs() - t0);
			snooze(100000);
		}
		Stats stats = Stats::Of(hidden);
		report.Set("B5.moveCapture.hiddenMs", stats.ToValue());
		report.Verdict("B5 move-capture flicker <= 1 frame", stats.max <= 16.7,
			std::to_string(stats.mean) + " ms avg");
	}

	// ---- B6 reveal capture
	{
		panel->Lock();
		panel->Quit();
		std::vector<double> firstPaint;
		for (int i = 0; i < 5; i++) {
			pattern->SetPhase(i);
			snooze(150000);
			GlassPanel* reveal = new GlassPanel(panelRect);
			double t0 = NowMs();
			reveal->Show();
			reveal->Lock();
			reveal->Sync();
			reveal->Unlock();
			Capturer capturer;
			capturer.Capture(panelRect);
			BBitmap* glass = RenderGlass(capturer.Bitmap(), renderer, NULL);
			reveal->SetGlass(glass);
			firstPaint.push_back(NowMs() - t0);
			snooze(200000);
			if (i < 4 || demo == NULL) {
				reveal->Lock();
				reveal->Quit();
			}
		}
		Stats stats = Stats::Of(firstPaint);
		report.Set("B6.reveal.firstPaintMs", stats.ToValue());
		report.Verdict("B6 reveal capture paints within 2 frames",
			stats.max <= 33, std::to_string(stats.mean) + " ms avg; no "
			"flicker by construction (the unpainted window shows the true "
			"background)");
	}

	report.Note("Recommended strategy order (automatic): reveal-capture on "
		"show + edge refresh while visible; hide/move-capture only if B4/B5 "
		"pass on this hardware.");
	report.Save();
	if (demo == NULL) {
		pattern->Lock();
		pattern->Quit();
		be_app->PostMessage(B_QUIT_REQUESTED);
	}
}


class App : public BApplication {
public:
	App(const char* demo)
		: BApplication("application/x-vnd.Lumina-fx-capture-glass"),
		  fDemo(demo) {}

	void ReadyToRun()
	{
		thread_id thread = spawn_thread([](void* data) -> status_t {
			RunSelfTest(static_cast<App*>(data)->fDemo);
			return B_OK;
		}, "selftest", B_NORMAL_PRIORITY, this);
		resume_thread(thread);
	}

	const char* fDemo;
};

}	// namespace


int
main(int argc, char** argv)
{
	App app(argc > 2 && strcmp(argv[1], "--demo") == 0 ? argv[2] : NULL);
	app.Run();
	return 0;
}
