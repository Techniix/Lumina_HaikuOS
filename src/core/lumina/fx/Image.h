/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * CPU bitmap effects on 32-bit BGRA buffers, the memory layout of Haiku's
 * B_RGB32 / B_RGBA32 (little endian: B, G, R, A).
 *
 * All operations work on an ImageView (non-owning: pointer, size, stride),
 * so they apply directly to BBitmap::Bits() without copies. Image is a
 * small owning buffer for scratch space and tests.
 *
 * Performance notes: blurs are separable sliding-window box blurs (O(1) per
 * pixel regardless of radius); three passes approximate a Gaussian. For
 * large radii, Downsample2x first and blur at quarter area — the result is
 * indistinguishable for frosted glass and 4-16x cheaper.
 */
#ifndef LUMINA_CORE_FX_IMAGE_H
#define LUMINA_CORE_FX_IMAGE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lumina {
namespace fx {

struct Color {
	uint8_t b = 0, g = 0, r = 0, a = 255;
	static Color RGB(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
		{ Color c; c.r = r; c.g = g; c.b = b; c.a = a; return c; }
};

struct ImageView {
	uint8_t*	bits = nullptr;
	int			width = 0;
	int			height = 0;
	int			stride = 0;		// bytes per row

	uint8_t* Row(int y) const { return bits + size_t(y) * size_t(stride); }
	uint8_t* Pixel(int x, int y) const { return Row(y) + x * 4; }
	bool IsValid() const
		{ return bits != nullptr && width > 0 && height > 0
			&& stride >= width * 4; }
	ImageView Sub(int x, int y, int w, int h) const;
};

class Image {
public:
	Image() = default;
	Image(int width, int height);
	void Resize(int width, int height);
	ImageView View();
	int Width() const { return fWidth; }
	int Height() const { return fHeight; }
	void Fill(Color c);

private:
	std::vector<uint8_t>	fData;
	int						fWidth = 0;
	int						fHeight = 0;
};

// Copies src into dst (sizes must match; copies the overlapping area).
void Copy(const ImageView& src, const ImageView& dst);

// Halves resolution with a 2x2 box filter. dst must be (w+1)/2 x (h+1)/2.
void Downsample2x(const ImageView& src, const ImageView& dst);

// Bilinear scale of src into dst (any sizes).
void ScaleBilinear(const ImageView& src, const ImageView& dst);

// In-place box blur, horizontal then vertical, `passes` times. `scratch`
// must be at least max(width, height) * 4 bytes (allocated if empty).
void BoxBlur(const ImageView& image, int radius, int passes,
	std::vector<uint8_t>& scratch);

// Box radii for `passes` box blurs approximating a Gaussian of sigma.
std::vector<int> GaussianBoxRadii(double sigma, int passes = 3);
void GaussianBlur(const ImageView& image, double sigma,
	std::vector<uint8_t>& scratch);

// Blend every pixel toward `tint` by amount (0..1). Alpha is preserved.
void Tint(const ImageView& image, Color tint, double amount);

struct Grade {
	double	brightness = 0;		// -1..1 added
	double	contrast = 1;		// multiplier around 0.5
	double	saturation = 1;		// 0 = gray
	double	gamma = 1;
};
void ApplyGrade(const ImageView& image, const Grade& grade);

// Ordered-dither noise (±amplitude) to hide banding in smooth gradients.
void Dither(const ImageView& image, int amplitude);

// Porter-Duff source-over of a premultiplied-or-straight src onto opaque
// dst. `opacity` scales src alpha. Straight alpha is assumed (Haiku's
// B_RGBA32 bitmaps are not premultiplied).
void BlendOver(const ImageView& src, const ImageView& dst, double opacity = 1);

// Cross-fade: dst = a*(1-t) + b*t. All three must have the same size.
void CrossFade(const ImageView& a, const ImageView& b, const ImageView& dst,
	double t);

// Rounded-rectangle alpha mask (antialiased), applied to image alpha.
void RoundCorners(const ImageView& image, float radius);

// Fills `panel` by mirroring the band of rows adjacent to it, as seen in
// `band` (the screen strip directly above a bottom panel, below a top
// panel, etc.). Used by the "edge glass" strategy: pixels under an opaque
// window do not exist anywhere in app_server (no backing store), so the
// best physically-plausible source for a frosted panel is its neighbour.
enum class Edge { Top, Bottom, Left, Right };
void SynthesizeFromEdgeBand(const ImageView& band, Edge panelEdge,
	const ImageView& panel);

// Mean absolute difference between two same-sized images, sampled every
// `step` pixels. Cheap change detection for capture refresh.
double SampledDifference(const ImageView& a, const ImageView& b, int step = 7);

}	// namespace fx
}	// namespace lumina

#endif
