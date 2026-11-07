/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Frosted-glass pipeline: source pixels → downsample → blur → upscale →
 * grade → tint → highlight → dither. Pure CPU, deterministic, timed.
 *
 * The *source* is the hard part on Haiku and is chosen by strategy:
 *   wallpaper: the desktop background image (static, always available)
 *   edge:      the live screen band next to the panel, mirrored
 *   capture:   live pixels under the panel (requires hiding the panel for
 *              the capture — see docs/vfx.md for why and what it costs)
 */
#ifndef LUMINA_CORE_FX_GLASS_H
#define LUMINA_CORE_FX_GLASS_H

#include <vector>

#include "Image.h"

namespace lumina {
namespace fx {

struct GlassParams {
	int		blurRadius = 12;	// in full-resolution pixels
	int		downsample = 2;		// 0..3 halvings before blurring
	Color	tint = Color::RGB(220, 220, 220);
	double	tintAmount = 0.55;
	double	saturation = 1.2;
	double	brightness = 0.0;
	bool	topHighlight = true;	// 1px light line, Haiku bevel style
	bool	dither = true;
};

struct GlassTiming {
	double	downsampleMs = 0;
	double	blurMs = 0;
	double	upscaleMs = 0;
	double	gradeMs = 0;
	double	totalMs = 0;
};

class GlassRenderer {
public:
	// Renders `source` (same size as `out`) into `out`.
	void Render(const ImageView& source, const ImageView& out,
		const GlassParams& params, GlassTiming* timing = nullptr);

private:
	std::vector<Image>		fLevels;
	std::vector<uint8_t>	fScratch;
};

}	// namespace fx
}	// namespace lumina

#endif
