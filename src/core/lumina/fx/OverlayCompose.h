/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * OverlayCompose: the pixel core of the optional app_server overlay
 * compositor (Level 3, patches/haiku/). Self-contained on purpose — no
 * Lumina or Haiku headers — so the very same file is copied into app_server
 * by patches/haiku/apply.sh and unit-tested here on any host.
 *
 * Model
 *   back   app_server's back buffer: what every ordinary window drew. With
 *          the patch, overlay windows do not clip the windows below them,
 *          so `back` holds the real content *under* each overlay.
 *   layers overlay windows, back to front: frame, straight-alpha BGRA
 *          content, opacity, backdrop blur radius, tint, rounded corners
 *          and a soft drop shadow.
 *   out    the composed image that is sent to the front buffer.
 *
 * Compose() produces out = back + layers for one rectangle. Composing any
 * sub-rectangle yields exactly the pixels a full-screen composition would
 * (blur samples a margin around the area), so app_server can compose only
 * the rectangles it is copying to the screen anyway — the cost scales with
 * damage, not with screen size, and idle cost is zero.
 */
#ifndef LUMINA_OVERLAY_COMPOSE_H
#define LUMINA_OVERLAY_COMPOSE_H

#include <cstdint>
#include <vector>

namespace overlay_compose {

// 32-bit B,G,R,A pixels (B_RGBA32/B_RGB32 byte order).
struct Surface {
	uint8_t*	bits = nullptr;
	int			width = 0;
	int			height = 0;
	int			stride = 0;		// bytes per row
};

// Inclusive integer rectangle (app_server clipping_rect convention).
struct Rect {
	int		left = 0;
	int		top = 0;
	int		right = -1;
	int		bottom = -1;

	bool IsValid() const { return right >= left && bottom >= top; }
	int Width() const { return right - left + 1; }
	int Height() const { return bottom - top + 1; }
};

Rect Intersect(const Rect& a, const Rect& b);
Rect Inset(const Rect& r, int by);	// negative grows

struct Layer {
	Rect			frame;					// screen coordinates
	const uint8_t*	bits = nullptr;			// frame-sized content, straight
	int				stride = 0;				// alpha; nullptr = no content
	float			opacity = 1.0f;			// content opacity 0..1
	int				blurRadius = 0;			// backdrop blur (box radius, in
											// screen pixels)
	int				downsample = 1;			// blur at 1/n resolution (1, 2, 4);
											// blocks align to a global grid,
											// so partial = full still holds
	uint8_t			tintB = 0, tintG = 0, tintR = 0;
	float			tintAmount = 0.0f;		// backdrop tint 0..1
	float			cornerRadius = 0.0f;	// antialiased rounded corners
	int				shadowRadius = 0;		// soft shadow extent (pixels)
	float			shadowOpacity = 0.0f;	// 0..1 at the window edge
	int				shadowOffsetY = 0;
};

// How far outside its frame a layer changes pixels (shadow).
int Margin(const Layer& layer);
// The screen rectangle a layer affects.
Rect Footprint(const Layer& layer);

// Composes `area` of out from back and the layers. back and out must have
// the same size and may not alias. `scratch` is reused between calls
// (keep one per thread).
void Compose(const Surface& back, const Surface& out, Rect area,
	const Layer* layers, int count, std::vector<uint8_t>& scratch);

// Number of box-blur passes used for the backdrop (Gaussian approximation).
constexpr int kBlurPasses = 3;

}	// namespace overlay_compose

#endif
