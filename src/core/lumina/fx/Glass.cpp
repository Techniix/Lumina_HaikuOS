/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Glass.h"

#include <algorithm>
#include <chrono>

namespace lumina {
namespace fx {

namespace {
double
NowMs()
{
	return std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}
}


void
GlassRenderer::Render(const ImageView& source, const ImageView& out,
	const GlassParams& params, GlassTiming* timing)
{
	GlassTiming t;
	double start = NowMs();
	if (!source.IsValid() || !out.IsValid())
		return;

	int levels = std::clamp(params.downsample, 0, 3);
	// Don't downsample below a few pixels.
	while (levels > 0 && ((source.width >> levels) < 4
			|| (source.height >> levels) < 2))
		levels--;
	if (int(fLevels.size()) < levels + 1)
		fLevels.resize(size_t(levels) + 1);

	// Build the pyramid straight from the source (no full-res copy when
	// downsampling; level 0 is only materialized for levels == 0 so the
	// caller's buffer is never modified).
	double mark = NowMs();
	ImageView work;
	if (levels == 0) {
		fLevels[0].Resize(source.width, source.height);
		Copy(source, fLevels[0].View());
		work = fLevels[0].View();
	} else {
		ImageView previous = source;
		for (int i = 1; i <= levels; i++) {
			fLevels[size_t(i)].Resize((previous.width + 1) / 2,
				(previous.height + 1) / 2);
			Downsample2x(previous, fLevels[size_t(i)].View());
			previous = fLevels[size_t(i)].View();
		}
		work = previous;
	}
	t.downsampleMs = NowMs() - mark;

	mark = NowMs();
	double sigma = params.blurRadius / double(1 << levels) / 2.0;
	if (sigma > 0.3)
		GaussianBlur(work, sigma, fScratch);
	t.blurMs = NowMs() - mark;

	// Saturation, brightness and tint are linear in the pixel values, so
	// they commute with bilinear upscaling: apply them at low resolution
	// (1/4^levels of the work).
	mark = NowMs();
	Grade grade;
	grade.saturation = params.saturation;
	grade.brightness = params.brightness;
	if (grade.saturation != 1 || grade.brightness != 0)
		ApplyGrade(work, grade);
	Tint(work, params.tint, params.tintAmount);
	t.gradeMs = NowMs() - mark;

	mark = NowMs();
	if (levels == 0)
		Copy(work, out);
	else
		ScaleBilinear(work, out);
	t.upscaleMs = NowMs() - mark;

	mark = NowMs();
	if (params.topHighlight && out.height > 2) {
		ImageView line = out.Sub(0, 0, out.width, 1);
		Tint(line, Color::RGB(255, 255, 255), 0.45);
	}
	// Dither and force opaque alpha in one pass (the panel covers the
	// screen; alpha is meaningless there).
	static const int kBayer4[4][4] = {
		{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	for (int y = 0; y < out.height; y++) {
		uint8_t* p = out.Row(y);
		for (int x = 0; x < out.width; x++, p += 4) {
			if (params.dither) {
				int n = (kBayer4[y & 3][x & 3] - 8) / 4;
				for (int c = 0; c < 3; c++) {
					int v = p[c] + n;
					p[c] = uint8_t(v < 0 ? 0 : v > 255 ? 255 : v);
				}
			}
			p[3] = 255;
		}
	}
	t.gradeMs += NowMs() - mark;
	t.totalMs = NowMs() - start;
	if (timing)
		*timing = t;
}

}	// namespace fx
}	// namespace lumina
