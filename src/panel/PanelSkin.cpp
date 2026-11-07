/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "PanelSkin.h"

#include <algorithm>
#include <chrono>

#include <fs_attr.h>

#include <Bitmap.h>
#include <ControlLook.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Node.h>
#include <Path.h>
#include <Screen.h>
#include <TranslationUtils.h>
#include <View.h>

#include <lumina/Log.h>

namespace lumina {
namespace panel {

namespace {
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

// Tracker's desktop background attribute (see Tracker's BackgroundImage).
const char* kBackgroundImageInfo = "be:bgndimginfo";
const char* kBackgroundImageInfoPath = "be:bgndimginfopath";
const char* kBackgroundImageInfoWorkspaces = "be:bgndimginfoworkspaces";
const char* kBackgroundImageInfoMode = "be:bgndimginfomode";
const char* kBackgroundImageInfoOffset = "be:bgndimginfooffset";
enum { kAtOffset = 0, kCentered = 1, kScaledToFit = 2, kTiled = 3 };
}	// namespace


PanelSkin::PanelSkin()
	:
	fStrategy(fx::GlassStrategy::Solid),
	fSkin(NULL),
	fBand(NULL),
	fLastRenderMs(0),
	fLastCaptureMs(0)
{
}


PanelSkin::~PanelSkin()
{
	delete fSkin;
	delete fBand;
}


void
PanelSkin::Configure(fx::GlassStrategy strategy, const fx::GlassParams& params,
	const BString& edge)
{
	// "capture" needs the panel to disappear while sampling; that lives in
	// experiments/ until it proves flicker-free (docs/vfx.md). Panels use
	// the nearest flicker-free strategy instead.
	if (strategy == fx::GlassStrategy::Capture)
		strategy = fx::GlassStrategy::Edge;
	fStrategy = strategy;
	fParams = params;
	fEdge = edge;
	fWallpaperKey = "";
	fPreviousBand.Resize(0, 0);
}


bool
PanelSkin::Update(BRect screenFrame, bool force)
{
	bool frameChanged = screenFrame != fFrame;
	fFrame = screenFrame;
	switch (fStrategy) {
		case fx::GlassStrategy::Solid:
		{
			bool had = fSkin != NULL;
			delete fSkin;
			fSkin = NULL;
			return had || frameChanged;
		}
		case fx::GlassStrategy::Wallpaper:
			return _UpdateWallpaper(screenFrame) || frameChanged;
		case fx::GlassStrategy::Edge:
		case fx::GlassStrategy::Capture:
			return _UpdateEdge(screenFrame, force || frameChanged);
	}
	return false;
}


void
PanelSkin::_Render(const fx::ImageView& source)
{
	BRect bounds(0, 0, fFrame.IntegerWidth(), fFrame.IntegerHeight());
	if (fSkin == NULL || fSkin->Bounds() != bounds) {
		delete fSkin;
		fSkin = new BBitmap(bounds, B_RGB32);
	}
	double start = NowMs();
	fx::GlassTiming timing;
	fRenderer.Render(source, ViewOf(fSkin), fParams, &timing);
	fLastRenderMs = NowMs() - start;
}


BBitmap*
PanelSkin::_LoadWallpaperFor(BRect screenFrame)
{
	BPath desktop;
	if (find_directory(B_DESKTOP_DIRECTORY, &desktop) != B_OK)
		return NULL;
	BNode node(desktop.Path());
	BMessage info;
	attr_info attrInfo;
	if (node.GetAttrInfo(kBackgroundImageInfo, &attrInfo) != B_OK)
		return NULL;
	std::vector<char> buffer(size_t(attrInfo.size));
	if (node.ReadAttr(kBackgroundImageInfo, B_MESSAGE_TYPE, 0, buffer.data(),
			buffer.size()) != ssize_t(buffer.size())
		|| info.Unflatten(buffer.data()) != B_OK)
		return NULL;

	uint32 workspaceMask = 1UL << current_workspace();
	const char* path = NULL;
	int32 mode = kScaledToFit;
	BPoint offset(0, 0);
	for (int32 i = 0; ; i++) {
		int32 workspaces = 0;
		if (info.FindInt32(kBackgroundImageInfoWorkspaces, i, &workspaces)
				!= B_OK)
			break;
		if ((uint32(workspaces) & workspaceMask) == 0)
			continue;
		info.FindString(kBackgroundImageInfoPath, i, &path);
		info.FindInt32(kBackgroundImageInfoMode, i, &mode);
		info.FindPoint(kBackgroundImageInfoOffset, i, &offset);
		break;
	}

	BScreen screen;
	BRect screenBounds = screen.Frame();
	BString key;
	key << (path != NULL ? path : "<color>") << ":" << mode << ":"
		<< current_workspace() << ":" << screenFrame.left << ","
		<< screenFrame.top << "," << screenFrame.right << ","
		<< screenFrame.bottom;
	if (key == fWallpaperKey && fSkin != NULL)
		return NULL;
	fWallpaperKey = key;

	BBitmap* canvas = new BBitmap(screenBounds, B_RGB32, true);
	BView* view = new BView(screenBounds, "wallpaper", 0, 0);
	canvas->AddChild(view);
	canvas->Lock();
	view->SetHighColor(screen.DesktopColor());
	view->FillRect(screenBounds);
	BBitmap* image = path != NULL ? BTranslationUtils::GetBitmap(path) : NULL;
	if (image != NULL) {
		BRect imageBounds = image->Bounds();
		switch (mode) {
			case kScaledToFit:
				view->DrawBitmap(image, imageBounds, screenBounds,
					B_FILTER_BITMAP_BILINEAR);
				break;
			case kCentered:
				view->DrawBitmap(image, BPoint(
					(screenBounds.Width() - imageBounds.Width()) / 2,
					(screenBounds.Height() - imageBounds.Height()) / 2));
				break;
			case kTiled:
				for (float y = 0; y <= screenBounds.bottom;
						y += imageBounds.Height() + 1) {
					for (float x = 0; x <= screenBounds.right;
							x += imageBounds.Width() + 1)
						view->DrawBitmap(image, BPoint(x, y));
				}
				break;
			default:
				view->DrawBitmap(image, offset);
		}
		delete image;
	}
	view->Sync();
	canvas->Unlock();
	return canvas;
}


bool
PanelSkin::_UpdateWallpaper(BRect screenFrame)
{
	BBitmap* canvas = _LoadWallpaperFor(screenFrame);
	if (canvas == NULL)
		return false;
	fx::ImageView whole = ViewOf(canvas);
	fx::ImageView region = whole.Sub(int(screenFrame.left),
		int(screenFrame.top), screenFrame.IntegerWidth() + 1,
		screenFrame.IntegerHeight() + 1);
	fSource.Resize(region.width, region.height);
	fx::Copy(region, fSource.View());
	delete canvas;
	_Render(fSource.View());
	return true;
}


bool
PanelSkin::_UpdateEdge(BRect screenFrame, bool force)
{
	BScreen screen;
	BRect screenBounds = screen.Frame();
	float bandDepth = std::min<float>(24, std::max<float>(screenFrame.Height(),
		screenFrame.Width()));
	fx::Edge edge = fx::Edge::Bottom;
	BRect band;
	if (fEdge == "top") {
		edge = fx::Edge::Top;
		band = BRect(screenFrame.left, screenFrame.bottom + 1,
			screenFrame.right, screenFrame.bottom + bandDepth);
	} else if (fEdge == "left") {
		edge = fx::Edge::Left;
		band = BRect(screenFrame.right + 1, screenFrame.top,
			screenFrame.right + bandDepth, screenFrame.bottom);
	} else if (fEdge == "right") {
		edge = fx::Edge::Right;
		band = BRect(screenFrame.left - bandDepth, screenFrame.top,
			screenFrame.left - 1, screenFrame.bottom);
	} else {
		band = BRect(screenFrame.left, screenFrame.top - bandDepth,
			screenFrame.right, screenFrame.top - 1);
	}
	band = band & screenBounds;
	if (!band.IsValid())
		return false;

	BRect bandBounds(0, 0, band.IntegerWidth(), band.IntegerHeight());
	if (fBand == NULL || fBand->Bounds() != bandBounds) {
		delete fBand;
		fBand = new BBitmap(bandBounds, B_RGB32);
		force = true;
	}
	double start = NowMs();
	if (screen.ReadBitmap(fBand, false, &band) != B_OK)
		return false;
	fLastCaptureMs = NowMs() - start;

	fx::ImageView current = ViewOf(fBand);
	if (!force && fPreviousBand.Width() == current.width
		&& fPreviousBand.Height() == current.height
		&& fx::SampledDifference(fPreviousBand.View(), current) < 0.75)
		return false;	// nothing visible changed: no re-render, no redraw
	fPreviousBand.Resize(current.width, current.height);
	fx::Copy(current, fPreviousBand.View());

	fSource.Resize(screenFrame.IntegerWidth() + 1,
		screenFrame.IntegerHeight() + 1);
	fx::SynthesizeFromEdgeBand(current, edge, fSource.View());
	_Render(fSource.View());
	return true;
}


void
PanelSkin::DrawBackground(BView* view, BRect update) const
{
	BRect panelInView = view->ConvertFromScreen(fFrame);
	if (fSkin == NULL) {
		rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
		be_control_look->DrawMenuBarBackground(view, panelInView, update, base);
		return;
	}
	BRect source = update.OffsetByCopy(-panelInView.left, -panelInView.top);
	view->DrawBitmap(fSkin, source, update);
}

}	// namespace panel
}	// namespace lumina
