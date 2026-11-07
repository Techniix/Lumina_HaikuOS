/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Visual Effects Engine — Haiku backends.
 *
 * Policy (what to do, at which quality) lives in lumina_core
 * (fx::SelectPlan, fx::Governor). This file holds the *mechanisms* that
 * talk to Haiku, each chosen by the plan and each with a cheaper fallback:
 *
 *   ScreenCapture      BScreen::ReadBitmap with cost measurement
 *   MeasureCapabilities  fills fx::Capabilities for the running system
 *   ProxyTransition    bitmap stand-in that animates geometry (minimize,
 *                      restore, open placeholder, close) — the uncovered
 *                      desktop is always live, no frozen background needed
 *   WorkspaceSlide     full-screen proxy sliding between workspace images
 *
 * Everything here is honest simulation on top of a non-compositing
 * app_server: proxies are opaque windows that move and scale; nothing
 * blends with other windows (see docs/vfx.md for the evidence).
 */
#ifndef LUMINA_HAIKU_VISUAL_EFFECTS_H
#define LUMINA_HAIKU_VISUAL_EFFECTS_H

#include <Rect.h>

#include <functional>

#include <lumina/anim/Easing.h>
#include <lumina/fx/Strategy.h>

class BBitmap;

namespace lumina {
namespace haiku {
namespace VisualEffects {

// Captures a screen rectangle into a new B_RGB32 bitmap (caller owns), or
// NULL. `ms` receives the elapsed time.
BBitmap* CaptureScreen(BRect rect, double* ms = NULL);

// Measures readback speed (small, ~10 ms test) and fills the capabilities
// that don't depend on the Window Engine.
fx::Capabilities MeasureCapabilities(bool engineActive);

struct ProxySpec {
	BRect		from;
	BRect		to;
	double		durationMs = 220;
	anim::Curve	curve = anim::Curve::Easing(anim::EaseFamily::Cubic,
					anim::EaseMode::Out);
	int			fps = 60;
	BBitmap*	image = NULL;		// adopted; NULL → window placeholder
	BBitmap*	icon = NULL;		// adopted; drawn on placeholders
	bool		allWorkspaces = false;
	// Called on the proxy thread once the proxy covers `from` (e.g. now
	// minimize the real window underneath it).
	std::function<void()> onCovered;
	// Called after the last frame, before the proxy disappears (e.g. raise
	// or unpark the real window beneath). `settleMs` later the proxy goes.
	std::function<void()> onArrived;
	double		settleMs = 40;
	// Per-frame cost report (feeds fx::Governor).
	std::function<void(double frameMs)> onFrame;
};

// Runs asynchronously on its own thread; returns immediately.
status_t RunProxyTransition(ProxySpec spec);

struct SlideSpec {
	BBitmap*	current = NULL;		// adopted, full screen
	BBitmap*	next = NULL;		// adopted, may be NULL (desktop colour)
	bool		forward = true;		// next enters from the right
	double		durationMs = 260;
	anim::Curve	curve = anim::Curve::Easing(anim::EaseFamily::Cubic,
					anim::EaseMode::Out);
	std::function<void()> onCovered;	// switch workspaces here
	std::function<void(double frameMs)> onFrame;
};

status_t RunWorkspaceSlide(SlideSpec spec);

// Waits (bounded) for running transitions to finish. Call before
// destroying objects their callbacks reference (e.g. at quit).
void WaitForTransitions(bigtime_t timeout = 2000000);

}	// namespace VisualEffects
}	// namespace haiku
}	// namespace lumina

#endif
