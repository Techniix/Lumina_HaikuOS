/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lumina {
namespace fx {

namespace {
inline uint8_t
Clamp8(double v)
{
	return v <= 0 ? 0 : v >= 255 ? 255 : uint8_t(v + 0.5);
}

inline uint8_t
Clamp8i(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : uint8_t(v);
}
}	// namespace


ImageView
ImageView::Sub(int x, int y, int w, int h) const
{
	ImageView sub;
	x = std::clamp(x, 0, width);
	y = std::clamp(y, 0, height);
	w = std::clamp(w, 0, width - x);
	h = std::clamp(h, 0, height - y);
	sub.bits = bits != nullptr ? Pixel(x, y) : nullptr;
	sub.width = w;
	sub.height = h;
	sub.stride = stride;
	return sub;
}


Image::Image(int width, int height)
{
	Resize(width, height);
}


void
Image::Resize(int width, int height)
{
	fWidth = std::max(width, 0);
	fHeight = std::max(height, 0);
	fData.assign(size_t(fWidth) * size_t(fHeight) * 4, 0);
}


ImageView
Image::View()
{
	ImageView view;
	view.bits = fData.empty() ? nullptr : fData.data();
	view.width = fWidth;
	view.height = fHeight;
	view.stride = fWidth * 4;
	return view;
}


void
Image::Fill(Color c)
{
	for (size_t i = 0; i + 3 < fData.size(); i += 4) {
		fData[i] = c.b;
		fData[i + 1] = c.g;
		fData[i + 2] = c.r;
		fData[i + 3] = c.a;
	}
}


void
Copy(const ImageView& src, const ImageView& dst)
{
	int w = std::min(src.width, dst.width);
	int h = std::min(src.height, dst.height);
	for (int y = 0; y < h; y++)
		std::memcpy(dst.Row(y), src.Row(y), size_t(w) * 4);
}


void
Downsample2x(const ImageView& src, const ImageView& dst)
{
	for (int y = 0; y < dst.height; y++) {
		int sy0 = std::min(y * 2, src.height - 1);
		int sy1 = std::min(y * 2 + 1, src.height - 1);
		const uint8_t* r0 = src.Row(sy0);
		const uint8_t* r1 = src.Row(sy1);
		uint8_t* out = dst.Row(y);
		for (int x = 0; x < dst.width; x++) {
			int sx0 = std::min(x * 2, src.width - 1) * 4;
			int sx1 = std::min(x * 2 + 1, src.width - 1) * 4;
			for (int c = 0; c < 4; c++) {
				out[x * 4 + c] = uint8_t((r0[sx0 + c] + r0[sx1 + c]
					+ r1[sx0 + c] + r1[sx1 + c] + 2) / 4);
			}
		}
	}
}


void
ScaleBilinear(const ImageView& src, const ImageView& dst)
{
	if (!src.IsValid() || !dst.IsValid())
		return;
	// Column sample positions and weights are the same for every row:
	// precompute them once (fixed point, 8-bit weights).
	std::vector<int> x0(size_t(dst.width)), x1(size_t(dst.width)),
		wx(size_t(dst.width));
	double sx = double(src.width) / dst.width;
	for (int x = 0; x < dst.width; x++) {
		double fx = (x + 0.5) * sx - 0.5;
		int base = int(std::floor(fx));
		int weight = int((fx - std::floor(fx)) * 256);
		if (fx < 0) {
			base = 0;
			weight = 0;
		}
		base = std::clamp(base, 0, src.width - 1);
		x0[size_t(x)] = base * 4;
		x1[size_t(x)] = std::min(base + 1, src.width - 1) * 4;
		wx[size_t(x)] = weight;
	}
	double sy = double(src.height) / dst.height;
	for (int y = 0; y < dst.height; y++) {
		double fy = (y + 0.5) * sy - 0.5;
		int y0 = std::clamp(int(std::floor(fy)), 0, src.height - 1);
		int y1 = std::min(y0 + 1, src.height - 1);
		int wy = fy < 0 ? 0 : int((fy - std::floor(fy)) * 256);
		const uint8_t* r0 = src.Row(y0);
		const uint8_t* r1 = src.Row(y1);
		uint8_t* out = dst.Row(y);
		for (int x = 0; x < dst.width; x++) {
			int a = x0[size_t(x)], b = x1[size_t(x)], w = wx[size_t(x)];
			for (int c = 0; c < 4; c++) {
				int top = r0[a + c] * (256 - w) + r0[b + c] * w;
				int bottom = r1[a + c] * (256 - w) + r1[b + c] * w;
				out[x * 4 + c] = uint8_t((top * (256 - wy) + bottom * wy
					+ (1 << 15)) >> 16);
			}
		}
	}
}


static void
BlurLine(uint8_t* line, int count, int pixelStep, int radius, uint8_t* scratch)
{
	// Sliding window over `count` pixels spaced `pixelStep` bytes apart,
	// with edge clamping. Result written back through scratch.
	if (radius <= 0 || count <= 1)
		return;
	const int window = radius * 2 + 1;
	int sum[4] = {0, 0, 0, 0};
	auto at = [&](int i) -> const uint8_t* {
		i = std::clamp(i, 0, count - 1);
		return line + size_t(i) * size_t(pixelStep);
	};
	for (int i = -radius; i <= radius; i++) {
		const uint8_t* p = at(i);
		for (int c = 0; c < 4; c++)
			sum[c] += p[c];
	}
	for (int i = 0; i < count; i++) {
		for (int c = 0; c < 4; c++)
			scratch[i * 4 + c] = uint8_t((sum[c] + window / 2) / window);
		const uint8_t* add = at(i + radius + 1);
		const uint8_t* remove = at(i - radius);
		for (int c = 0; c < 4; c++)
			sum[c] += add[c] - remove[c];
	}
	for (int i = 0; i < count; i++)
		std::memcpy(line + size_t(i) * size_t(pixelStep), scratch + i * 4, 4);
}


void
BoxBlur(const ImageView& image, int radius, int passes,
	std::vector<uint8_t>& scratch)
{
	if (!image.IsValid() || radius <= 0)
		return;
	size_t needed = size_t(std::max(image.width, image.height)) * 4;
	if (scratch.size() < needed)
		scratch.resize(needed);
	int rx = std::min(radius, image.width);
	int ry = std::min(radius, image.height);
	for (int pass = 0; pass < passes; pass++) {
		for (int y = 0; y < image.height; y++)
			BlurLine(image.Row(y), image.width, 4, rx, scratch.data());
		for (int x = 0; x < image.width; x++)
			BlurLine(image.bits + x * 4, image.height, image.stride, ry,
				scratch.data());
	}
}


std::vector<int>
GaussianBoxRadii(double sigma, int passes)
{
	// Kovesi: ideal width for n box filters approximating a Gaussian.
	std::vector<int> radii;
	if (sigma <= 0 || passes <= 0)
		return radii;
	double ideal = std::sqrt(12 * sigma * sigma / passes + 1);
	int lower = int(std::floor(ideal));
	if (lower % 2 == 0)
		lower--;
	int upper = lower + 2;
	double mIdeal = (12 * sigma * sigma - passes * lower * lower
		- 4 * passes * lower - 3 * passes) / (-4.0 * lower - 4);
	int m = int(std::round(mIdeal));
	for (int i = 0; i < passes; i++)
		radii.push_back(((i < m ? lower : upper) - 1) / 2);
	return radii;
}


void
GaussianBlur(const ImageView& image, double sigma, std::vector<uint8_t>& scratch)
{
	for (int radius : GaussianBoxRadii(sigma, 3))
		BoxBlur(image, radius, 1, scratch);
}


void
Tint(const ImageView& image, Color tint, double amount)
{
	amount = std::clamp(amount, 0.0, 1.0);
	int a = int(amount * 256);
	int inv = 256 - a;
	int tb = tint.b * a, tg = tint.g * a, tr = tint.r * a;
	for (int y = 0; y < image.height; y++) {
		uint8_t* p = image.Row(y);
		for (int x = 0; x < image.width; x++, p += 4) {
			p[0] = uint8_t((p[0] * inv + tb) >> 8);
			p[1] = uint8_t((p[1] * inv + tg) >> 8);
			p[2] = uint8_t((p[2] * inv + tr) >> 8);
		}
	}
}


void
ApplyGrade(const ImageView& image, const Grade& grade)
{
	// Precompute a per-channel tone curve; saturation is applied per pixel.
	uint8_t lut[256];
	double invGamma = grade.gamma > 0 ? 1.0 / grade.gamma : 1.0;
	for (int i = 0; i < 256; i++) {
		double v = i / 255.0;
		v = std::pow(v, invGamma);
		v = (v - 0.5) * grade.contrast + 0.5 + grade.brightness;
		lut[i] = Clamp8(v * 255);
	}
	bool saturate = grade.saturation != 1;
	int sat = int(grade.saturation * 256);
	for (int y = 0; y < image.height; y++) {
		uint8_t* p = image.Row(y);
		for (int x = 0; x < image.width; x++, p += 4) {
			int b = lut[p[0]], g = lut[p[1]], r = lut[p[2]];
			if (saturate) {
				// Rec. 601 luma in fixed point
				int luma = (r * 77 + g * 150 + b * 29) >> 8;
				r = luma + (((r - luma) * sat) >> 8);
				g = luma + (((g - luma) * sat) >> 8);
				b = luma + (((b - luma) * sat) >> 8);
			}
			p[0] = Clamp8i(b);
			p[1] = Clamp8i(g);
			p[2] = Clamp8i(r);
		}
	}
}


void
Dither(const ImageView& image, int amplitude)
{
	static const int kBayer4[4][4] = {
		{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	if (amplitude <= 0)
		return;
	for (int y = 0; y < image.height; y++) {
		uint8_t* p = image.Row(y);
		for (int x = 0; x < image.width; x++, p += 4) {
			int n = (kBayer4[y & 3][x & 3] - 8) * amplitude / 8;
			p[0] = Clamp8i(p[0] + n);
			p[1] = Clamp8i(p[1] + n);
			p[2] = Clamp8i(p[2] + n);
		}
	}
}


void
BlendOver(const ImageView& src, const ImageView& dst, double opacity)
{
	int w = std::min(src.width, dst.width);
	int h = std::min(src.height, dst.height);
	int o = int(std::clamp(opacity, 0.0, 1.0) * 256);
	for (int y = 0; y < h; y++) {
		const uint8_t* s = src.Row(y);
		uint8_t* d = dst.Row(y);
		for (int x = 0; x < w; x++, s += 4, d += 4) {
			int a = (s[3] * o) >> 8;
			if (a == 0)
				continue;
			int inv = 255 - a;
			d[0] = uint8_t((s[0] * a + d[0] * inv + 127) / 255);
			d[1] = uint8_t((s[1] * a + d[1] * inv + 127) / 255);
			d[2] = uint8_t((s[2] * a + d[2] * inv + 127) / 255);
			d[3] = uint8_t(a + (d[3] * inv + 127) / 255);
		}
	}
}


void
CrossFade(const ImageView& a, const ImageView& b, const ImageView& dst,
	double t)
{
	int w = std::min({a.width, b.width, dst.width});
	int h = std::min({a.height, b.height, dst.height});
	int tb = int(std::clamp(t, 0.0, 1.0) * 256);
	int ta = 256 - tb;
	for (int y = 0; y < h; y++) {
		const uint8_t* pa = a.Row(y);
		const uint8_t* pb = b.Row(y);
		uint8_t* d = dst.Row(y);
		for (int i = 0; i < w * 4; i++)
			d[i] = uint8_t((pa[i] * ta + pb[i] * tb) >> 8);
	}
}


void
RoundCorners(const ImageView& image, float radius)
{
	if (radius <= 0)
		return;
	float r = std::min({radius, image.width / 2.0f, image.height / 2.0f});
	for (int y = 0; y < image.height; y++) {
		for (int x = 0; x < image.width; x++) {
			float cx = -1, cy = -1;
			if (x < r && y < r) { cx = r; cy = r; }
			else if (x >= image.width - r && y < r)
				{ cx = image.width - r; cy = r; }
			else if (x < r && y >= image.height - r)
				{ cx = r; cy = image.height - r; }
			else if (x >= image.width - r && y >= image.height - r)
				{ cx = image.width - r; cy = image.height - r; }
			else
				continue;
			float dx = (x + 0.5f) - cx, dy = (y + 0.5f) - cy;
			float distance = std::sqrt(dx * dx + dy * dy);
			float coverage = std::clamp(r - distance + 0.5f, 0.0f, 1.0f);
			uint8_t* p = image.Pixel(x, y);
			p[3] = uint8_t(p[3] * coverage);
		}
	}
}


void
SynthesizeFromEdgeBand(const ImageView& band, Edge panelEdge,
	const ImageView& panel)
{
	if (!band.IsValid() || !panel.IsValid())
		return;
	switch (panelEdge) {
		case Edge::Bottom:
		case Edge::Top:
		{
			// band is a horizontal strip of the same width as the panel.
			// Panel row y mirrors band row (distance from the shared edge).
			for (int y = 0; y < panel.height; y++) {
				int distance = panelEdge == Edge::Bottom ? y
					: panel.height - 1 - y;
				int k = distance % (2 * band.height);
				if (k >= band.height)
					k = 2 * band.height - 1 - k;
				int bandRow = panelEdge == Edge::Bottom ? band.height - 1 - k : k;
				const uint8_t* source = band.Row(bandRow);
				int w = std::min(band.width, panel.width);
				std::memcpy(panel.Row(y), source, size_t(w) * 4);
				for (int x = w; x < panel.width; x++)
					std::memcpy(panel.Pixel(x, y), band.Pixel(band.width - 1,
						bandRow), 4);
			}
			break;
		}
		case Edge::Left:
		case Edge::Right:
		{
			for (int y = 0; y < panel.height; y++) {
				int by = std::min(y, band.height - 1);
				for (int x = 0; x < panel.width; x++) {
					int distance = panelEdge == Edge::Right ? x
						: panel.width - 1 - x;
					int k = distance % (2 * band.width);
					if (k >= band.width)
						k = 2 * band.width - 1 - k;
					int bandCol = panelEdge == Edge::Right ? band.width - 1 - k
						: k;
					std::memcpy(panel.Pixel(x, y), band.Pixel(bandCol, by), 4);
				}
			}
			break;
		}
	}
}


double
SampledDifference(const ImageView& a, const ImageView& b, int step)
{
	int w = std::min(a.width, b.width);
	int h = std::min(a.height, b.height);
	if (w <= 0 || h <= 0)
		return 0;
	step = std::max(step, 1);
	uint64_t total = 0, samples = 0;
	for (int y = 0; y < h; y += step) {
		const uint8_t* pa = a.Row(y);
		const uint8_t* pb = b.Row(y);
		for (int x = (y / step) % step; x < w; x += step) {
			for (int c = 0; c < 3; c++)
				total += uint64_t(std::abs(pa[x * 4 + c] - pb[x * 4 + c]));
			samples += 3;
		}
	}
	return samples ? double(total) / samples : 0;
}

}	// namespace fx
}	// namespace lumina
