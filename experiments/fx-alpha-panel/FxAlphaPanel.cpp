/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * EXPERIMENT A — native alpha blending in a top-level panel window.
 *
 * Question: can B_OP_ALPHA / B_ALPHA_OVERLAY / B_ALPHA_COMPOSITE /
 * B_TRANSPARENT_COLOR give a panel real translucency over *other* windows?
 *
 * Hypotheses under test (each measured, not assumed):
 *  H1 "reveal capture": a window whose views use B_TRANSPARENT_COLOR and
 *     draw nothing leaves the framebuffer untouched, so immediately after
 *     Show() its region still holds the exact pixels underneath.
 *  H2 "no compositing": when the window underneath repaints or moves, the
 *     panel region does NOT follow (app_server keeps no per-window
 *     buffers), i.e. transparency between windows is impossible.
 *  H3 "accumulation": drawing a translucent fill with B_OP_ALPHA into a
 *     transparent view blends with the panel's *previous* output, so
 *     repeated redraws darken monotonically.
 *  H4 "snapshot composite": drawing a captured background bitmap first and
 *     then the translucent content gives a stable, correct-looking result
 *     (frozen, but no accumulation).
 *  H5 offscreen: B_ALPHA_COMPOSITE (not B_ALPHA_OVERLAY) produces a usable
 *     alpha mask when drawing into a transparent B_RGBA32 bitmap.
 *
 * Run: fx-alpha-panel            (self-test, writes a JSON report)
 *      fx-alpha-panel --show     (leave the snapshot-composite panel up)
 */
#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include <lumina/fx/Image.h>

#include "../common/FxHarness.h"

using namespace lumina;
using namespace lumina::fxlab;

namespace {

enum PanelMode {
	kModeNothing,		// transparent, draws nothing
	kModeAlphaFill,		// transparent + B_OP_ALPHA fill each Draw()
	kModeSnapshot,		// draw captured background, then alpha fill
};

class AlphaView : public BView {
public:
	AlphaView(BRect frame)
		:
		BView(frame, "alpha", B_FOLLOW_ALL, B_WILL_DRAW),
		fMode(kModeNothing),
		fBackground(NULL),
		fDraws(0)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	~AlphaView() { delete fBackground; }

	void Draw(BRect update)
	{
		fDraws++;
		if (fMode == kModeNothing)
			return;
		if (fMode == kModeSnapshot && fBackground != NULL)
			DrawBitmap(fBackground, BPoint(0, 0));
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		SetHighColor(0, 0, 0, 64);	// 25 % black
		FillRect(Bounds());
		SetHighColor(255, 255, 255, 96);
		StrokeLine(Bounds().LeftTop(), Bounds().RightTop());
		SetDrawingMode(B_OP_COPY);
	}

	PanelMode	fMode;
	BBitmap*	fBackground;
	int32		fDraws;
};


class AlphaPanel : public BWindow {
public:
	AlphaPanel(BRect frame)
		:
		BWindow(frame, "fx-alpha-panel", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_AVOID_FOCUS | B_NOT_MOVABLE)
	{
		fView = new AlphaView(Bounds());
		AddChild(fView);
	}

	void SetMode(PanelMode mode, BBitmap* background = NULL)
	{
		Lock();
		fView->fMode = mode;
		if (background != NULL) {
			delete fView->fBackground;
			fView->fBackground = background;
		}
		fView->Invalidate();
		fView->Sync();
		Unlock();
	}

	void Redraw(int times)
	{
		for (int i = 0; i < times; i++) {
			Lock();
			fView->Invalidate();
			fView->Sync();
			Unlock();
			snooze(16000);
		}
	}

	AlphaView*	fView;
};


// H5: draw a half-transparent circle into a fully transparent RGBA bitmap
// with each blending function and inspect the resulting alpha.
void
TestOffscreenBlending(Report& report)
{
	for (int composite = 0; composite <= 1; composite++) {
		BRect bounds(0, 0, 63, 63);
		BBitmap bitmap(bounds, B_RGBA32, true);
		memset(bitmap.Bits(), 0, bitmap.BitsLength());
		BView* view = new BView(bounds, "o", 0, 0);
		bitmap.AddChild(view);
		bitmap.Lock();
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA,
			composite ? B_ALPHA_COMPOSITE : B_ALPHA_OVERLAY);
		view->SetHighColor(255, 0, 0, 128);
		view->FillEllipse(bounds.InsetByCopy(8, 8));
		view->Sync();
		bitmap.Unlock();
		uint8* center = static_cast<uint8*>(bitmap.Bits())
			+ 32 * bitmap.BytesPerRow() + 32 * 4;
		uint8* corner = static_cast<uint8*>(bitmap.Bits());
		std::string key = composite ? "offscreen.composite" : "offscreen.overlay";
		report.Set(key + ".centerAlpha", int(center[3]));
		report.Set(key + ".centerRed", int(center[2]));
		report.Set(key + ".cornerAlpha", int(corner[3]));
	}
	int compositeAlpha = int(report.Root().FindPath(
		"offscreen.composite.centerAlpha")->AsInt());
	int overlayAlpha = int(report.Root().FindPath(
		"offscreen.overlay.centerAlpha")->AsInt());
	report.Verdict("H5 offscreen alpha mask", compositeAlpha > 100
		&& compositeAlpha < 160,
		"B_ALPHA_COMPOSITE alpha=" + std::to_string(compositeAlpha)
		+ ", B_ALPHA_OVERLAY alpha=" + std::to_string(overlayAlpha)
		+ " (want ~128 for composite)");

	// Cost of app_server alpha blending vs the CPU path (fx::BlendOver).
	BRect panel(0, 0, 1919, 31);
	BBitmap target(panel, B_RGB32, true);
	BView* view = new BView(panel, "t", 0, 0);
	target.AddChild(view);
	BBitmap overlay(panel, B_RGBA32);
	memset(overlay.Bits(), 0x80, overlay.BitsLength());
	target.Lock();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	double start = NowMs();
	for (int i = 0; i < 50; i++)
		view->DrawBitmap(&overlay, BPoint(0, 0));
	view->Sync();
	double serverMs = (NowMs() - start) / 50;
	target.Unlock();
	start = NowMs();
	for (int i = 0; i < 50; i++)
		fx::BlendOver(ViewOf(&overlay), ViewOf(&target));
	double cpuMs = (NowMs() - start) / 50;
	report.Set("blend1920x32.appServerMs", serverMs);
	report.Set("blend1920x32.cpuMs", cpuMs);
}


void
RunSelfTest(bool keepShowing)
{
	Report report("fx-alpha-panel");
	BScreen screen;
	BRect s = screen.Frame();
	BRect panelRect(s.left + 100, s.bottom - 160, s.right - 100, s.bottom - 120);
	BRect beside = panelRect.OffsetByCopy(0, -60);	// a control region

	// A pattern window under the panel area.
	PatternWindow* pattern = new PatternWindow(
		BRect(panelRect.left + 50, panelRect.top - 200, panelRect.left + 650,
			panelRect.bottom + 60), "fx-alpha pattern");
	pattern->Show();
	snooze(400000);

	Capturer before, after;
	before.Capture(panelRect);

	// H1
	AlphaPanel* panel = new AlphaPanel(panelRect);
	panel->Show();
	snooze(150000);
	after.Capture(panelRect);
	double revealDiff = Difference(before.Bitmap(), after.Bitmap());
	report.Set("H1.revealDiff", revealDiff);
	report.Verdict("H1 reveal capture", revealDiff < 2.0,
		"mean pixel difference before/after showing a transparent window: "
		+ std::to_string(revealDiff));
	BBitmap* revealed = after.Detach();

	// H2: repaint and move the window underneath.
	Capturer control0, control1, panel1;
	control0.Capture(beside);
	pattern->SetPhase(1);
	snooze(200000);
	control1.Capture(beside);
	panel1.Capture(panelRect);
	double outside = Difference(control0.Bitmap(), control1.Bitmap());
	double inside = Difference(revealed, panel1.Bitmap());
	report.Set("H2.changedOutsidePanel", outside);
	report.Set("H2.changedInsidePanel", inside);
	pattern->Lock();
	pattern->MoveBy(120, 0);
	pattern->Unlock();
	snooze(200000);
	Capturer panel2;
	panel2.Capture(panelRect);
	double afterMove = Difference(revealed, panel2.Bitmap());
	report.Set("H2.changedInsidePanelAfterMove", afterMove);
	report.Verdict("H2 no inter-window compositing",
		outside > 20 && inside < 2.0 && afterMove < 2.0,
		"underlying window changed by " + std::to_string(outside)
		+ " outside the panel but only " + std::to_string(inside) + "/"
		+ std::to_string(afterMove) + " inside it (frozen)");

	// H3: alpha fill accumulation.
	panel->SetMode(kModeAlphaFill);
	snooze(50000);
	Capturer a1, a10;
	a1.Capture(panelRect);
	panel->Redraw(9);
	a10.Capture(panelRect);
	double luma1 = MeanLuma(a1.Bitmap()), luma10 = MeanLuma(a10.Bitmap());
	report.Set("H3.lumaAfter1", luma1);
	report.Set("H3.lumaAfter10", luma10);
	report.Verdict("H3 accumulation", luma10 < luma1 - 5,
		"mean luma " + std::to_string(luma1) + " -> " + std::to_string(luma10)
		+ " after 10 redraws (darkening = blending with own output)");

	// H4: snapshot composite.
	panel->SetMode(kModeSnapshot, revealed);
	snooze(50000);
	Capturer s1, s10;
	s1.Capture(panelRect);
	panel->Redraw(9);
	s10.Capture(panelRect);
	double stable = Difference(s1.Bitmap(), s10.Bitmap());
	report.Set("H4.driftAfter10", stable);
	report.Verdict("H4 snapshot composite is stable", stable < 1.0,
		"difference between 1st and 10th redraw: " + std::to_string(stable));

	TestOffscreenBlending(report);

	report.Note("Conclusion template: if H1/H2/H4 pass, real translucency "
		"between windows is impossible without app_server changes, but "
		"'reveal capture' + snapshot compositing gives correct-looking glass "
		"that is frozen while visible.");
	report.Save();

	if (!keepShowing) {
		panel->Lock();
		panel->Quit();
		pattern->Lock();
		pattern->Quit();
		be_app->PostMessage(B_QUIT_REQUESTED);
	}
}


class App : public BApplication {
public:
	App(bool show)
		: BApplication("application/x-vnd.Lumina-fx-alpha-panel"),
		  fShow(show) {}

	void ReadyToRun()
	{
		thread_id thread = spawn_thread([](void* data) -> status_t {
			RunSelfTest(*static_cast<bool*>(data));
			return B_OK;
		}, "selftest", B_NORMAL_PRIORITY, &fShow);
		resume_thread(thread);
	}

	bool fShow;
};

}	// namespace


int
main(int argc, char** argv)
{
	App app(argc > 1 && strcmp(argv[1], "--show") == 0);
	app.Run();
	return 0;
}
