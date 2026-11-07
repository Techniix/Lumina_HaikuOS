/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Panel background ("skin") with the strategies of the Visual Effects
 * Engine (see docs/vfx.md):
 *
 *   solid      Deskbar look: BControlLook menu-bar background. No cost.
 *   wallpaper  Frosted rendering of the *desktop background image* region
 *              under the panel, read from Tracker's be:bgndimginfo
 *              attribute. Honest about what it is: it does not show
 *              windows passing behind the panel.
 *   edge       Frosted rendering synthesized from the live screen band
 *              adjacent to the panel (BScreen::ReadBitmap). Follows windows
 *              moving near the panel; refreshed at fx.panelGlass.refreshHz
 *              and only re-rendered when the band actually changed.
 *
 * Applets don't paint their own background; they call DrawBackground(),
 * which copies the matching region of the shared skin bitmap, so the whole
 * panel reads as one continuous surface.
 */
#ifndef LUMINA_PANEL_SKIN_H
#define LUMINA_PANEL_SKIN_H

#include <Rect.h>
#include <String.h>

#include <memory>

#include <lumina/fx/Glass.h>
#include <lumina/fx/Image.h>
#include <lumina/fx/Strategy.h>

class BBitmap;
class BView;

namespace lumina {
namespace panel {

class PanelSkin {
public:
	PanelSkin();
	~PanelSkin();

	void Configure(fx::GlassStrategy strategy, const fx::GlassParams& params,
		const BString& edge);
	fx::GlassStrategy Strategy() const { return fStrategy; }

	// Rebuilds the background for a panel occupying `screenFrame`. Returns
	// true if the bitmap changed (the panel should redraw).
	bool Update(BRect screenFrame, bool force);

	// "capture" strategy: renders glass from the TRUE pixels at
	// `screenFrame`. Only valid while the panel does not cover that area
	// (before Show(), or while hidden during a workspace switch).
	bool CaptureUnderPanel(BRect screenFrame);
	bool UsesTrueCapture() const { return fTrueCapture; }

	// Paints the skin region under `view`'s `update` rect.
	void DrawBackground(BView* view, BRect update) const;

	double LastRenderMs() const { return fLastRenderMs; }
	double LastCaptureMs() const { return fLastCaptureMs; }

private:
	bool _UpdateWallpaper(BRect screenFrame);
	bool _UpdateEdge(BRect screenFrame, bool force);
	void _Render(const fx::ImageView& source);
	BBitmap* _LoadWallpaperFor(BRect screenFrame);

	fx::GlassStrategy	fStrategy;
	bool				fTrueCapture;
	fx::GlassParams		fParams;
	BString				fEdge;
	BBitmap*			fSkin;		// panel-sized B_RGB32
	BBitmap*			fBand;		// captured band
	fx::Image			fPreviousBand;
	fx::Image			fSource;
	fx::GlassRenderer	fRenderer;
	BRect				fFrame;
	BString				fWallpaperKey;
	double				fLastRenderMs;
	double				fLastCaptureMs;
};

}	// namespace panel
}	// namespace lumina

#endif
