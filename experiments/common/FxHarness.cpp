/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "FxHarness.h"

#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>

#include <Autolock.h>
#include <Screen.h>

#include <lumina/Json.h>
#include <lumina/Profile.h>
#include <lumina/haiku/Paths.h>
#include <lumina/haiku/PrivateApi.h>

#include <unistd.h>


namespace lumina {
namespace fxlab {

double
NowMs()
{
	return std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}


fx::ImageView
ViewOf(BBitmap* bitmap)
{
	fx::ImageView view;
	if (bitmap == NULL)
		return view;
	view.bits = static_cast<uint8_t*>(bitmap->Bits());
	view.width = bitmap->Bounds().IntegerWidth() + 1;
	view.height = bitmap->Bounds().IntegerHeight() + 1;
	view.stride = int(bitmap->BytesPerRow());
	return view;
}


double
Difference(BBitmap* a, BBitmap* b)
{
	if (a == NULL || b == NULL)
		return -1;
	return fx::SampledDifference(ViewOf(a), ViewOf(b), 3);
}


double
MeanLuma(BBitmap* bitmap)
{
	fx::ImageView v = ViewOf(bitmap);
	if (!v.IsValid())
		return -1;
	double total = 0;
	int64 count = 0;
	for (int y = 0; y < v.height; y += 2) {
		uint8_t* p = v.Row(y);
		for (int x = 0; x < v.width; x += 2) {
			total += (p[x * 4 + 2] * 77 + p[x * 4 + 1] * 150
				+ p[x * 4] * 29) / 256.0;
			count++;
		}
	}
	return count ? total / count : -1;
}


Stats
Stats::Of(const std::vector<double>& samples)
{
	Stats s;
	s.count = int(samples.size());
	if (samples.empty())
		return s;
	s.min = *std::min_element(samples.begin(), samples.end());
	s.max = *std::max_element(samples.begin(), samples.end());
	double total = 0;
	for (double v : samples)
		total += v;
	s.mean = total / samples.size();
	return s;
}


Value
Stats::ToValue() const
{
	Value v = Value::MakeObject();
	v["min"] = min;
	v["mean"] = mean;
	v["max"] = max;
	v["n"] = count;
	return v;
}


// #pragma mark - Capturer


Capturer::Capturer()
	:
	fBitmap(NULL)
{
}


Capturer::~Capturer()
{
	delete fBitmap;
}


double
Capturer::Capture(BRect rect)
{
	BRect bounds(0, 0, rect.IntegerWidth(), rect.IntegerHeight());
	if (fBitmap == NULL || fBitmap->Bounds() != bounds) {
		delete fBitmap;
		fBitmap = new BBitmap(bounds, B_RGB32);
	}
	BScreen screen;
	double start = NowMs();
	if (screen.ReadBitmap(fBitmap, false, &rect) != B_OK)
		return -1;
	return NowMs() - start;
}


BBitmap*
Capturer::Detach()
{
	BBitmap* bitmap = fBitmap;
	fBitmap = NULL;
	return bitmap;
}


// #pragma mark - Report


Report::Report(const std::string& name)
	:
	fName(name),
	fRoot(Value::MakeObject())
{
	fRoot["prototype"] = name;
	time_t now = time(NULL);
	char stamp[64];
	strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
	fRoot["when"] = stamp;
	BScreen screen;
	BRect frame = screen.Frame();
	char mode[64];
	snprintf(mode, sizeof(mode), "%dx%d", frame.IntegerWidth() + 1,
		frame.IntegerHeight() + 1);
	fRoot["screen"] = mode;
	accelerant_device_info info;
	if (screen.GetDeviceInfo(&info) == B_OK)
		fRoot["graphics"] = std::string(info.name) + " / " + info.chipset;
	fRoot["notes"] = Value::MakeArray();
	fRoot["verdicts"] = Value::MakeObject();
}


void
Report::Set(const std::string& path, const Value& value)
{
	fRoot.SetPath(path, value);
}


void
Report::Note(const std::string& text)
{
	fRoot["notes"].Push(text);
	printf("  note: %s\n", text.c_str());
}


void
Report::Verdict(const std::string& key, bool pass, const std::string& detail)
{
	Value v = Value::MakeObject();
	v["pass"] = pass;
	v["detail"] = detail;
	fRoot["verdicts"][key] = v;
	printf("  [%s] %s: %s\n", pass ? "PASS" : "FAIL", key.c_str(),
		detail.c_str());
}


std::string
Report::Save()
{
	std::string dir = haiku::Paths::LogDir() + "/fx-reports";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	std::string stamp = fRoot["when"].AsString();
	for (char& c : stamp) {
		if (c == ':')
			c = '-';
	}
	std::string path = dir + "/" + fName + "-" + stamp + ".json";
	WriteFileAtomic(path, WriteJson(fRoot));
	printf("report: %s\n", path.c_str());
	return path;
}


// #pragma mark - PatternWindow


class PatternWindow::PatternView : public BView {
public:
	PatternView(BRect frame)
		:
		BView(frame, "pattern", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void Draw(BRect update)
	{
		atomic_add(&fDraws, 1);
		static const rgb_color kPalettes[3][2] = {
			{{230, 60, 40, 255}, {30, 30, 200, 255}},
			{{40, 200, 60, 255}, {250, 230, 30, 255}},
			{{250, 250, 250, 255}, {10, 10, 10, 255}},
		};
		const rgb_color* palette = kPalettes[fPhase % 3];
		BRect bounds = Bounds();
		const float cell = 24;
		for (float y = 0; y <= bounds.bottom; y += cell) {
			for (float x = 0; x <= bounds.right; x += cell) {
				bool odd = (int(x / cell) + int(y / cell)) & 1;
				SetHighColor(palette[odd ? 1 : 0]);
				FillRect(BRect(x, y, x + cell - 1, y + cell - 1));
			}
		}
	}

	void FrameResized(float, float)
	{
		atomic_add(&fResizes, 1);
		Invalidate();
	}

	int32	fPhase = 0;
	int32	fDraws = 0;
	int32	fResizes = 0;
};


PatternWindow::PatternWindow(BRect frame, const char* title)
	:
	BWindow(frame, title, B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS)
{
	fView = new PatternView(Bounds());
	AddChild(fView);
}


void
PatternWindow::SetPhase(int phase)
{
	BAutolock _(this);
	fView->fPhase = phase;
	fView->Invalidate();
	fView->Sync();
}


int32 PatternWindow::DrawCount() const { return fView->fDraws; }
int32 PatternWindow::ResizeCount() const { return fView->fResizes; }


void
PatternWindow::ResetCounters()
{
	fView->fDraws = 0;
	fView->fResizes = 0;
}


int32
PatternWindow::Token()
{
	// Match by (unique) title among our own team's windows.
	std::vector<haiku::WindowSnapshot> windows;
	haiku::PrivateApi::ListWindows(windows, getpid());
	for (const haiku::WindowSnapshot& w : windows) {
		if (w.title == Title())
			return w.token;
	}
	return -1;
}


// #pragma mark - ProxyWindow


class ProxyWindow::ProxyView : public BView {
public:
	ProxyView(BRect frame, BBitmap* bitmap)
		:
		BView(frame, "proxy", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS),
		fBitmap(bitmap),
		fNext(NULL),
		fSlide(-1)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	~ProxyView()
	{
		delete fBitmap;
		delete fNext;
	}

	void Draw(BRect update)
	{
		BRect bounds = Bounds();
		if (fSlide >= 0 && fBitmap != NULL) {
			// Workspace slide: current leaves left, next enters from right.
			float offset = std::floor(bounds.Width() * fSlide);
			DrawBitmapAsync(fBitmap, fBitmap->Bounds(),
				bounds.OffsetByCopy(-offset, 0));
			if (fNext != NULL) {
				DrawBitmapAsync(fNext, fNext->Bounds(),
					bounds.OffsetByCopy(bounds.Width() + 1 - offset, 0));
			} else {
				SetHighColor(BScreen(Window()).DesktopColor());
				FillRect(BRect(bounds.right + 1 - offset, 0, bounds.right,
					bounds.bottom));
			}
			return;
		}
		if (fBitmap == NULL) {
			SetHighUIColor(B_PANEL_BACKGROUND_COLOR);
			FillRect(update);
			return;
		}
		DrawBitmapAsync(fBitmap, fBitmap->Bounds(), bounds,
			B_FILTER_BITMAP_BILINEAR);
	}

	void FrameResized(float, float)
	{
		Invalidate();
	}

	BBitmap*	fBitmap;
	BBitmap*	fNext;
	float		fSlide;
};


ProxyWindow::ProxyWindow(BRect frame, BBitmap* bitmap, bool allWorkspaces)
	:
	BWindow(frame, "Lumina transition", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_ALL_WINDOW_FEEL, B_AVOID_FOCUS | B_NOT_MOVABLE
			| B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE,
		allWorkspaces ? B_ALL_WORKSPACES : B_CURRENT_WORKSPACE)
{
	fView = new ProxyView(Bounds(), bitmap);
	AddChild(fView);
}


ProxyWindow::~ProxyWindow()
{
}


void
ProxyWindow::SetBitmap(BBitmap* bitmap)
{
	BAutolock _(this);
	delete fView->fBitmap;
	fView->fBitmap = bitmap;
	fView->fSlide = -1;
	fView->Invalidate();
}


void
ProxyWindow::SetFrame(BRect frame)
{
	BAutolock _(this);
	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());
	fView->Invalidate();
	fView->Sync();
}


void
ProxyWindow::SetSlide(BBitmap* next, float progress)
{
	BAutolock _(this);
	if (next != NULL && next != fView->fNext) {
		delete fView->fNext;
		fView->fNext = next;
	}
	fView->fSlide = progress;
	fView->Invalidate();
	fView->Sync();
}


BView*
ProxyWindow::ContentView() const
{
	return fView;
}


std::vector<double>
RunFrames(double durationMs, int fps,
	const std::function<void(double, int)>& step)
{
	std::vector<double> costs;
	bigtime_t interval = 1000000 / std::max(fps, 1);
	bigtime_t start = system_time();
	bigtime_t next = start;
	int frame = 0;
	while (true) {
		double elapsed = (system_time() - start) / 1000.0;
		double progress = std::min(elapsed / durationMs, 1.0);
		double t0 = NowMs();
		step(progress, frame++);
		costs.push_back(NowMs() - t0);
		if (progress >= 1.0)
			break;
		next += interval;
		if (next < system_time())
			next = system_time();
		snooze_until(next, B_SYSTEM_TIMEBASE);
	}
	return costs;
}

}	// namespace fxlab
}	// namespace lumina
