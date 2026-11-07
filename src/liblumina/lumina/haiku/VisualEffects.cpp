/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "VisualEffects.h"

#include <algorithm>
#include <cmath>

#include <Autolock.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <OS.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include <lumina/Log.h>

namespace lumina {
namespace haiku {
namespace VisualEffects {

namespace {

double
NowMs()
{
	return system_time() / 1000.0;
}


BRect
Lerp(BRect a, BRect b, double t)
{
	return BRect(std::round(a.left + (b.left - a.left) * t),
		std::round(a.top + (b.top - a.top) * t),
		std::round(a.right + (b.right - a.right) * t),
		std::round(a.bottom + (b.bottom - a.bottom) * t));
}


class ProxyView : public BView {
public:
	ProxyView(BRect frame, BBitmap* image, BBitmap* icon)
		:
		BView(frame, "proxy", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS),
		fImage(image),
		fIcon(icon),
		fNext(NULL),
		fSlide(-1),
		fForward(true)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	~ProxyView()
	{
		delete fImage;
		delete fIcon;
		delete fNext;
	}

	void Draw(BRect update)
	{
		BRect bounds = Bounds();
		if (fSlide >= 0) {
			_DrawSlide(bounds);
			return;
		}
		if (fImage != NULL) {
			DrawBitmapAsync(fImage, fImage->Bounds(), bounds,
				B_FILTER_BITMAP_BILINEAR);
			return;
		}
		// Placeholder in the Haiku window idiom: yellow tab, border, panel.
		float tab = std::min(bounds.Height() * 0.12f, 22.0f);
		BRect tabRect(bounds.left, bounds.top,
			bounds.left + std::max(bounds.Width() * 0.45f, 8.0f),
			bounds.top + tab);
		BRect body(bounds.left, bounds.top + tab, bounds.right, bounds.bottom);
		SetHighUIColor(B_WINDOW_TAB_COLOR);
		FillRect(tabRect);
		SetHighUIColor(B_PANEL_BACKGROUND_COLOR);
		FillRect(body);
		SetHighUIColor(B_WINDOW_BORDER_COLOR);
		StrokeRect(body);
		if (fIcon != NULL && body.Width() > 40 && body.Height() > 40) {
			float size = std::min({body.Width(), body.Height()} ) * 0.25f;
			size = std::min(size, 64.0f);
			BRect target(0, 0, size, size);
			target.OffsetTo(body.left + (body.Width() - size) / 2,
				body.top + (body.Height() - size) / 2);
			SetDrawingMode(B_OP_ALPHA);
			DrawBitmapAsync(fIcon, fIcon->Bounds(), target,
				B_FILTER_BITMAP_BILINEAR);
			SetDrawingMode(B_OP_COPY);
		}
	}

	void FrameResized(float, float)
	{
		Invalidate();
	}

	void _DrawSlide(BRect bounds)
	{
		float offset = std::floor((bounds.Width() + 1) * fSlide);
		float direction = fForward ? 1 : -1;
		if (fImage != NULL) {
			DrawBitmapAsync(fImage, fImage->Bounds(),
				bounds.OffsetByCopy(-offset * direction, 0));
		}
		BRect incoming = bounds.OffsetByCopy(
			(bounds.Width() + 1 - offset) * direction, 0);
		if (fNext != NULL)
			DrawBitmapAsync(fNext, fNext->Bounds(), incoming);
		else {
			SetHighColor(BScreen(Window()).DesktopColor());
			FillRect(incoming & bounds);
		}
	}

	BBitmap*	fImage;
	BBitmap*	fIcon;
	BBitmap*	fNext;
	float		fSlide;
	bool		fForward;
};


class ProxyWindow : public BWindow {
public:
	ProxyWindow(BRect frame, BBitmap* image, BBitmap* icon, bool all)
		:
		BWindow(frame, "Lumina transition", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_AVOID_FOCUS | B_NOT_MOVABLE
				| B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE,
			all ? B_ALL_WORKSPACES : B_CURRENT_WORKSPACE)
	{
		fView = new ProxyView(Bounds(), image, icon);
		AddChild(fView);
	}

	void SetFrame(BRect frame)
	{
		BAutolock _(this);
		MoveTo(frame.LeftTop());
		ResizeTo(frame.Width(), frame.Height());
		fView->Invalidate();
		fView->Sync();
	}

	void SetSlide(float progress)
	{
		BAutolock _(this);
		fView->fSlide = progress;
		fView->Invalidate();
		fView->Sync();
	}

	void ShowAndSync()
	{
		Show();
		BAutolock _(this);
		fView->Sync();
	}

	void Close()
	{
		if (Lock())
			Quit();
	}

	ProxyView*	fView;
};


struct FrameLoop {
	static void Run(double durationMs, int fps,
		const std::function<void(double)>& step,
		const std::function<void(double)>& onFrame)
	{
		bigtime_t interval = 1000000 / std::max(fps, 1);
		bigtime_t start = system_time();
		bigtime_t next = start;
		while (true) {
			double elapsed = (system_time() - start) / 1000.0;
			double t = durationMs > 0 ? std::min(elapsed / durationMs, 1.0) : 1.0;
			double t0 = NowMs();
			step(t);
			if (onFrame)
				onFrame(NowMs() - t0);
			if (t >= 1.0)
				break;
			next += interval;
			if (next < system_time())
				next = system_time();	// late: never burst
			snooze_until(next, B_SYSTEM_TIMEBASE);
		}
	}
};

}	// namespace


BBitmap*
CaptureScreen(BRect rect, double* ms)
{
	BScreen screen;
	rect = rect & screen.Frame();
	if (!rect.IsValid())
		return NULL;
	BBitmap* bitmap = new BBitmap(BRect(0, 0, rect.IntegerWidth(),
		rect.IntegerHeight()), B_RGB32);
	double start = NowMs();
	if (bitmap->InitCheck() != B_OK
		|| screen.ReadBitmap(bitmap, false, &rect) != B_OK) {
		delete bitmap;
		return NULL;
	}
	if (ms)
		*ms = NowMs() - start;
	return bitmap;
}


fx::Capabilities
MeasureCapabilities(bool engineActive)
{
	fx::Capabilities caps;
	caps.engineLoaded = engineActive;
	caps.engineAnimation = engineActive;
	system_info info;
	if (get_system_info(&info) == B_OK) {
		caps.cpuCount = int(info.cpu_count);
		caps.lowMemory = uint64(info.max_pages) * B_PAGE_SIZE
			< 1024ULL * 1024 * 1024;
	}
	// 512x256 sample (0.13 MP), best of 3 to ignore one-off stalls.
	BRect sample(0, 0, 511, 255);
	double best = -1;
	for (int i = 0; i < 3; i++) {
		double ms = 0;
		BBitmap* bitmap = CaptureScreen(sample, &ms);
		if (bitmap == NULL)
			break;
		delete bitmap;
		if (best < 0 || ms < best)
			best = ms;
	}
	caps.screenReadback = best >= 0;
	caps.readbackMsPerMegapixel = best >= 0 ? best / (512 * 256 / 1e6) : -1;
	LUMINA_LOG_INFO("fx", "capabilities: readback %s (%.1f ms/MP), cpus %d, "
		"engine %s", caps.screenReadback ? "yes" : "no",
		caps.readbackMsPerMegapixel, caps.cpuCount,
		engineActive ? "active" : "absent");
	return caps;
}


status_t
RunProxyTransition(ProxySpec spec)
{
	ProxySpec* heap = new ProxySpec(std::move(spec));
	thread_id thread = spawn_thread([](void* data) -> status_t {
		ProxySpec* s = static_cast<ProxySpec*>(data);
		ProxyWindow* proxy = new ProxyWindow(s->from, s->image, s->icon,
			s->allWorkspaces);
		s->image = NULL;	// adopted by the view
		s->icon = NULL;
		proxy->ShowAndSync();
		if (s->onCovered)
			s->onCovered();
		FrameLoop::Run(s->durationMs, s->fps, [&](double t) {
			double p = s->curve.Progress(t * s->durationMs / 1000.0,
				s->durationMs / 1000.0);
			proxy->SetFrame(Lerp(s->from, s->to, p));
		}, s->onFrame);
		if (s->onArrived)
			s->onArrived();
		snooze(bigtime_t(s->settleMs * 1000));
		proxy->Close();
		delete s;
		return B_OK;
	}, "lumina proxy", B_DISPLAY_PRIORITY, heap);
	if (thread < 0) {
		delete heap->image;
		delete heap->icon;
		delete heap;
		return thread;
	}
	return resume_thread(thread);
}


status_t
RunWorkspaceSlide(SlideSpec spec)
{
	SlideSpec* heap = new SlideSpec(std::move(spec));
	thread_id thread = spawn_thread([](void* data) -> status_t {
		SlideSpec* s = static_cast<SlideSpec*>(data);
		BRect frame = BScreen().Frame();
		ProxyWindow* proxy = new ProxyWindow(frame, s->current, NULL, true);
		proxy->fView->fNext = s->next;
		proxy->fView->fForward = s->forward;
		proxy->fView->fSlide = 0;
		s->current = s->next = NULL;
		proxy->ShowAndSync();
		if (s->onCovered)
			s->onCovered();
		FrameLoop::Run(s->durationMs, 60, [&](double t) {
			proxy->SetSlide(float(s->curve.Progress(t * s->durationMs / 1000.0,
				s->durationMs / 1000.0)));
		}, s->onFrame);
		snooze(30000);	// let the new workspace finish painting beneath
		proxy->Close();
		delete s;
		return B_OK;
	}, "lumina workspace slide", B_DISPLAY_PRIORITY, heap);
	if (thread < 0) {
		delete heap->current;
		delete heap->next;
		delete heap;
		return thread;
	}
	return resume_thread(thread);
}

}	// namespace VisualEffects
}	// namespace haiku
}	// namespace lumina
