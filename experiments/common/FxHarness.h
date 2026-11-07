/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Shared harness for the visual-effects research prototypes:
 * screen capture with timing, pixel comparison, a controllable test-pattern
 * window, a bitmap proxy window, and JSON reports written to
 * ~/config/var/log/Lumina/fx-reports/.
 *
 * EXPERIMENTAL: not part of the stable desktop.
 */
#ifndef LUMINA_FXLAB_HARNESS_H
#define LUMINA_FXLAB_HARNESS_H

#include <Bitmap.h>
#include <Rect.h>
#include <View.h>
#include <Window.h>

#include <functional>
#include <string>
#include <vector>

#include <lumina/Value.h>
#include <lumina/fx/Image.h>

namespace lumina {
namespace fxlab {

double NowMs();
fx::ImageView ViewOf(BBitmap* bitmap);

// Mean of per-channel absolute differences (0..255).
double Difference(BBitmap* a, BBitmap* b);
double MeanLuma(BBitmap* bitmap);

struct Stats {
	double	min = 0, max = 0, mean = 0;
	int		count = 0;
	static Stats Of(const std::vector<double>& samples);
	Value ToValue() const;
};

// Screen capture into a reusable bitmap.
class Capturer {
public:
	Capturer();
	~Capturer();
	// Captures `screenRect`; returns the elapsed milliseconds (or -1).
	double Capture(BRect screenRect);
	BBitmap* Bitmap() const { return fBitmap; }
	BBitmap* Detach();	// caller owns the returned bitmap

private:
	BBitmap*	fBitmap;
};

class Report {
public:
	explicit Report(const std::string& name);
	Value& Root() { return fRoot; }
	void Set(const std::string& path, const Value& value);
	void Note(const std::string& text);
	void Verdict(const std::string& key, bool pass, const std::string& detail);
	std::string Save();	// returns the file path

private:
	std::string	fName;
	Value		fRoot;
};

// A normal titled window painting a high-contrast pattern whose colours
// change with SetPhase(); counts Draw() and FrameResized() calls so
// prototypes can measure the client-side cost of their effects.
class PatternWindow : public BWindow {
public:
	PatternWindow(BRect frame, const char* title);
	void SetPhase(int phase);
	int32 DrawCount() const;
	int32 ResizeCount() const;
	void ResetCounters();
	int32 Token();	// app_server token (via _get_object_token)

private:
	class PatternView;
	PatternView*	fView;
};

// Borderless always-on-top window showing a bitmap scaled to its bounds.
// The building block of snapshot-based transitions.
class ProxyWindow : public BWindow {
public:
	ProxyWindow(BRect frame, BBitmap* bitmap /* adopted */,
		bool allWorkspaces = false);
	virtual ~ProxyWindow();
	void SetBitmap(BBitmap* bitmap /* adopted */);
	void SetFrame(BRect frame);		// MoveTo + ResizeTo, locked
	void SetSlide(BBitmap* next /* adopted */, float progress);
	BView* ContentView() const;

private:
	class ProxyView;
	ProxyView*	fView;
};

// Runs a frame loop calling `step(progress, frameIndex)` for `durationMs`,
// pacing at `fps`; returns per-frame CPU milliseconds of `step`.
std::vector<double> RunFrames(double durationMs, int fps,
	const std::function<void(double, int)>& step);

}	// namespace fxlab
}	// namespace lumina

#endif
