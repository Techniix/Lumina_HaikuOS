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

	// Level 0 = copy of the source, so the caller's buffer is untouched.
	fLevels[0].Resize(source.width, source.height);
	Copy(source, fLevels[0].View());
	double mark = NowMs();
	for (int i = 1; i <= levels; i++) {
		ImageView previous = fLevels[size_t(i) - 1].View();
		fLevels[size_t(i)].Resize((previous.width + 1) / 2,
			(previous.height + 1) / 2);
		Downsample2x(previous, fLevels[size_t(i)].View());
	}
	t.downsampleMs = NowMs() - mark;

	mark = NowMs();
	ImageView work = fLevels[size_t(levels)].View();
	double sigma = params.blurRadius / double(1 << levels) / 2.0;
	if (sigma > 0.3)
		GaussianBlur(work, sigma, fScratch);
	t.blurMs = NowMs() - mark;

	mark = NowMs();
	if (levels == 0)
		Copy(work, out);
	else
		ScaleBilinear(work, out);
	t.upscaleMs = NowMs() - mark;

	mark = NowMs();
	Grade grade;
	grade.saturation = params.saturation;
	grade.brightness = params.brightness;
	if (grade.saturation != 1 || grade.brightness != 0)
		ApplyGrade(out, grade);
	Tint(out, params.tint, params.tintAmount);
	if (params.topHighlight && out.height > 2) {
		ImageView line = out.Sub(0, 0, out.width, 1);
		Tint(line, Color::RGB(255, 255, 255), 0.45);
	}
	if (params.dither)
		Dither(out, 2);
	// Output is opaque: the panel window covers the screen.
	for (int y = 0; y < out.height; y++) {
		uint8_t* p = out.Row(y);
		for (int x = 0; x < out.width; x++)
			p[x * 4 + 3] = 255;
	}
	t.gradeMs = NowMs() - mark;
	t.totalMs = NowMs() - start;
	if (timing)
		*timing = t;
}

}	// namespace fx
}	// namespace lumina
