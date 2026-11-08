/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "OverlayCompose.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace overlay_compose {

namespace {

inline uint8_t*
PixelAt(const Surface& s, int x, int y)
{
	return s.bits + size_t(y) * size_t(s.stride) + size_t(x) * 4;
}


// Coverage (0..1) of pixel (x, y) by a rounded rectangle, antialiased over
// one pixel. Only the corner squares can be partial.
float
RoundedCoverage(const Rect& r, float radius, int x, int y)
{
	if (x < r.left || x > r.right || y < r.top || y > r.bottom)
		return 0.0f;
	if (radius <= 0)
		return 1.0f;
	float cx, cy;
	float fx = x + 0.5f, fy = y + 0.5f;
	float left = r.left + radius, right = r.right + 1 - radius;
	float top = r.top + radius, bottom = r.bottom + 1 - radius;
	if (fx < left)
		cx = left;
	else if (fx > right)
		cx = right;
	else
		return 1.0f;
	if (fy < top)
		cy = top;
	else if (fy > bottom)
		cy = bottom;
	else
		return 1.0f;
	float d = std::sqrt((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy));
	return std::min(std::max(radius - d + 0.5f, 0.0f), 1.0f);
}


// Distance from (x, y) to a rounded rectangle (0 inside).
float
RoundedDistance(const Rect& r, float radius, int x, int y)
{
	float fx = x + 0.5f, fy = y + 0.5f;
	float left = r.left + radius, right = r.right + 1 - radius;
	float top = r.top + radius, bottom = r.bottom + 1 - radius;
	float dx = std::max(std::max(left - fx, fx - right), 0.0f);
	float dy = std::max(std::max(top - fy, fy - bottom), 0.0f);
	return std::max(std::sqrt(dx * dx + dy * dy) - radius, 0.0f);
}


// One horizontal + vertical box blur pass over a w x h BGRA buffer with
// edge clamping. `line` is scratch of max(w, h) * 4 ints.
void
BoxBlurPass(uint8_t* bits, int w, int h, int radius, std::vector<int>& line)
{
	if (radius <= 0)
		return;
	int window = 2 * radius + 1;
	line.resize(size_t(std::max(w, h)) * 3);
	// horizontal
	for (int y = 0; y < h; y++) {
		uint8_t* row = bits + size_t(y) * w * 4;
		int sum[3] = {0, 0, 0};
		for (int i = -radius; i <= radius; i++) {
			const uint8_t* p = row + std::min(std::max(i, 0), w - 1) * 4;
			sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2];
		}
		for (int x = 0; x < w; x++) {
			line[x * 3] = sum[0];
			line[x * 3 + 1] = sum[1];
			line[x * 3 + 2] = sum[2];
			const uint8_t* add = row + std::min(x + radius + 1, w - 1) * 4;
			const uint8_t* sub = row + std::max(x - radius, 0) * 4;
			sum[0] += add[0] - sub[0];
			sum[1] += add[1] - sub[1];
			sum[2] += add[2] - sub[2];
		}
		for (int x = 0; x < w; x++) {
			row[x * 4] = uint8_t(line[x * 3] / window);
			row[x * 4 + 1] = uint8_t(line[x * 3 + 1] / window);
			row[x * 4 + 2] = uint8_t(line[x * 3 + 2] / window);
		}
	}
	// vertical
	for (int x = 0; x < w; x++) {
		uint8_t* col = bits + size_t(x) * 4;
		size_t stride = size_t(w) * 4;
		int sum[3] = {0, 0, 0};
		for (int i = -radius; i <= radius; i++) {
			const uint8_t* p = col + std::min(std::max(i, 0), h - 1) * stride;
			sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2];
		}
		for (int y = 0; y < h; y++) {
			line[y * 3] = sum[0];
			line[y * 3 + 1] = sum[1];
			line[y * 3 + 2] = sum[2];
			const uint8_t* add = col + std::min(y + radius + 1, h - 1) * stride;
			const uint8_t* sub = col + std::max(y - radius, 0) * stride;
			sum[0] += add[0] - sub[0];
			sum[1] += add[1] - sub[1];
			sum[2] += add[2] - sub[2];
		}
		for (int y = 0; y < h; y++) {
			col[y * stride] = uint8_t(line[y * 3] / window);
			col[y * stride + 1] = uint8_t(line[y * 3 + 1] / window);
			col[y * stride + 2] = uint8_t(line[y * 3 + 2] / window);
		}
	}
}

}	// namespace


Rect
Intersect(const Rect& a, const Rect& b)
{
	Rect r;
	r.left = std::max(a.left, b.left);
	r.top = std::max(a.top, b.top);
	r.right = std::min(a.right, b.right);
	r.bottom = std::min(a.bottom, b.bottom);
	return r;
}


Rect
Inset(const Rect& r, int by)
{
	Rect out = r;
	out.left += by;
	out.top += by;
	out.right -= by;
	out.bottom -= by;
	return out;
}


int
Margin(const Layer& layer)
{
	if (layer.shadowRadius <= 0 || layer.shadowOpacity <= 0)
		return 0;
	return layer.shadowRadius + std::abs(layer.shadowOffsetY);
}


Rect
Footprint(const Layer& layer)
{
	return Inset(layer.frame, -Margin(layer));
}


void
Compose(const Surface& back, const Surface& out, Rect area,
	const Layer* layers, int count, std::vector<uint8_t>& scratch)
{
	Rect screen{0, 0, back.width - 1, back.height - 1};
	area = Intersect(area, screen);
	if (!area.IsValid())
		return;

	// 1. Start from what the ordinary windows drew.
	for (int y = area.top; y <= area.bottom; y++) {
		memcpy(PixelAt(out, area.left, y), PixelAt(back, area.left, y),
			size_t(area.Width()) * 4);
	}

	std::vector<int> line;
	for (int i = 0; i < count; i++) {
		const Layer& layer = layers[i];
		if (!layer.frame.IsValid()
			|| !Intersect(Footprint(layer), area).IsValid())
			continue;

		// 2. Shadow: darken around the (rounded) frame, falling off with
		// distance. Inside the frame the layer covers it anyway.
		if (Margin(layer) > 0) {
			Rect shadowFrame = layer.frame;
			shadowFrame.top += layer.shadowOffsetY;
			shadowFrame.bottom += layer.shadowOffsetY;
			Rect s = Intersect(Footprint(layer), area);
			for (int y = s.top; y <= s.bottom; y++) {
				uint8_t* p = PixelAt(out, s.left, y);
				for (int x = s.left; x <= s.right; x++, p += 4) {
					if (RoundedCoverage(layer.frame, layer.cornerRadius, x, y)
							>= 1.0f)
						continue;
					float d = RoundedDistance(shadowFrame, layer.cornerRadius,
						x, y);
					if (d >= layer.shadowRadius)
						continue;
					float t = 1.0f - d / layer.shadowRadius;
					float darken = layer.shadowOpacity * t * t;
					// Under a partially covered corner pixel only the
					// uncovered part shows the shadow.
					darken *= 1.0f - RoundedCoverage(layer.frame,
						layer.cornerRadius, x, y);
					int keep = int((1.0f - darken) * 256);
					p[0] = uint8_t((p[0] * keep) >> 8);
					p[1] = uint8_t((p[1] * keep) >> 8);
					p[2] = uint8_t((p[2] * keep) >> 8);
				}
			}
		}

		Rect body = Intersect(layer.frame, area);
		if (!body.IsValid())
			continue;

		// 3. Backdrop: blur (and tint) what is under the layer, optionally
		// at 1/f resolution on a screen-aligned grid. A margin of
		// passes*radius (+1 for bilinear) is sampled around the area so a
		// partial composition matches the full one exactly: edge clamping
		// travels `radius` per pass and never reaches the needed pixels.
		int f = layer.downsample == 2 || layer.downsample == 4
			? layer.downsample : 1;
		bool hasBackdrop = layer.blurRadius > 0 || layer.tintAmount > 0;
		Rect low;			// low-res sample rect (low-res coordinates)
		int lowW = 0;
		if (hasBackdrop) {
			int radius = layer.blurRadius > 0
				? std::max(1, layer.blurRadius / f) : 0;
			int reach = radius * kBlurPasses + (f > 1 ? 1 : 0);
			Rect lowScreen{0, 0, (back.width - 1) / f, (back.height - 1) / f};
			Rect lowBody{body.left / f, body.top / f, body.right / f,
				body.bottom / f};
			low = Intersect(Inset(lowBody, -reach), lowScreen);
			lowW = low.Width();
			int lowH = low.Height();
			scratch.resize(size_t(lowW) * lowH * 4);
			for (int ly = 0; ly < lowH; ly++) {
				for (int lx = 0; lx < lowW; lx++) {
					int x0 = (low.left + lx) * f, y0 = (low.top + ly) * f;
					int x1 = std::min(x0 + f, back.width);
					int y1 = std::min(y0 + f, back.height);
					int sum[3] = {0, 0, 0}, n = 0;
					for (int y = y0; y < y1; y++) {
						const uint8_t* q = PixelAt(back, x0, y);
						for (int x = x0; x < x1; x++, q += 4) {
							sum[0] += q[0]; sum[1] += q[1]; sum[2] += q[2];
							n++;
						}
					}
					uint8_t* d = &scratch[(size_t(ly) * lowW + lx) * 4];
					d[0] = uint8_t(sum[0] / n);
					d[1] = uint8_t(sum[1] / n);
					d[2] = uint8_t(sum[2] / n);
					d[3] = 255;
				}
			}
			for (int pass = 0; pass < kBlurPasses; pass++)
				BoxBlurPass(scratch.data(), lowW, lowH, radius, line);
			if (layer.tintAmount > 0) {
				int t8 = int(std::min(std::max(layer.tintAmount, 0.0f), 1.0f)
					* 256);
				for (size_t p = 0; p < scratch.size(); p += 4) {
					scratch[p] = uint8_t((scratch[p] * (256 - t8)
						+ layer.tintB * t8) >> 8);
					scratch[p + 1] = uint8_t((scratch[p + 1] * (256 - t8)
						+ layer.tintG * t8) >> 8);
					scratch[p + 2] = uint8_t((scratch[p + 2] * (256 - t8)
						+ layer.tintR * t8) >> 8);
				}
			}
		}
		// Backdrop colour at screen pixel (x, y): direct at f = 1, bilinear
		// in global low-res coordinates otherwise (clamped to the screen's
		// low-res bounds only, never to the sample rect).
		auto backdropAt = [&](int x, int y, int& b, int& g, int& r) {
			if (f == 1) {
				const uint8_t* q = &scratch[(size_t(y - low.top) * lowW
					+ (x - low.left)) * 4];
				b = q[0]; g = q[1]; r = q[2];
				return;
			}
			int maxX = (back.width - 1) / f, maxY = (back.height - 1) / f;
			// 8-bit fixed point: u = (x + 0.5) / f - 0.5
			int u = ((2 * x + 1) * 256) / (2 * f) - 128;
			int v = ((2 * y + 1) * 256) / (2 * f) - 128;
			int i0 = u >> 8, j0 = v >> 8;
			int fu = u & 255, fv = v & 255;
			if (u < 0) { i0 = 0; fu = 0; }
			if (v < 0) { j0 = 0; fv = 0; }
			int i1 = std::min(i0 + 1, maxX), j1 = std::min(j0 + 1, maxY);
			i0 = std::min(i0, maxX);
			j0 = std::min(j0, maxY);
			auto px = [&](int i, int j) {
				return &scratch[(size_t(j - low.top) * lowW + (i - low.left))
					* 4];
			};
			const uint8_t* a = px(i0, j0);
			const uint8_t* bb = px(i1, j0);
			const uint8_t* c = px(i0, j1);
			const uint8_t* d = px(i1, j1);
			int out[3];
			for (int k = 0; k < 3; k++) {
				int top = a[k] * (256 - fu) + bb[k] * fu;
				int bottom = c[k] * (256 - fu) + d[k] * fu;
				out[k] = (top * (256 - fv) + bottom * fv) >> 16;
			}
			b = out[0]; g = out[1]; r = out[2];
		};

		// 4. Body: backdrop, then content over it, through the rounded mask.
		int opacity8 = int(std::min(std::max(layer.opacity, 0.0f), 1.0f) * 256);
		for (int y = body.top; y <= body.bottom; y++) {
			uint8_t* p = PixelAt(out, body.left, y);
			for (int x = body.left; x <= body.right; x++, p += 4) {
				float coverage = RoundedCoverage(layer.frame,
					layer.cornerRadius, x, y);
				if (coverage <= 0)
					continue;
				int r = p[2], g = p[1], b = p[0];
				if (hasBackdrop)
					backdropAt(x, y, b, g, r);
				if (layer.bits != nullptr) {
					const uint8_t* c = layer.bits
						+ size_t(y - layer.frame.top) * layer.stride
						+ size_t(x - layer.frame.left) * 4;
					int a = (c[3] * opacity8) >> 8;
					b = (b * (256 - a) + c[0] * a) >> 8;
					g = (g * (256 - a) + c[1] * a) >> 8;
					r = (r * (256 - a) + c[2] * a) >> 8;
				}
				int k = int(coverage * 256);
				p[0] = uint8_t((p[0] * (256 - k) + b * k) >> 8);
				p[1] = uint8_t((p[1] * (256 - k) + g * k) >> 8);
				p[2] = uint8_t((p[2] * (256 - k) + r * k) >> 8);
				p[3] = 255;
			}
		}
	}
}

}	// namespace overlay_compose
