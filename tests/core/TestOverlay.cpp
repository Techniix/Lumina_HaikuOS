#include "../LuminaTest.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include <lumina/fx/OverlayCompose.h>

using namespace overlay_compose;

namespace {
struct Buffer {
	std::vector<uint8_t> data;
	Surface surface;
	Buffer(int w, int h)
		: data(size_t(w) * h * 4, 0)
	{
		surface.bits = data.data();
		surface.width = w;
		surface.height = h;
		surface.stride = w * 4;
	}
	uint8_t* At(int x, int y) { return &data[(size_t(y) * surface.width + x) * 4]; }
};

void
Checker(Buffer& b, int cell)
{
	for (int y = 0; y < b.surface.height; y++)
		for (int x = 0; x < b.surface.width; x++) {
			uint8_t v = ((x / cell + y / cell) & 1) ? 230 : 20;
			uint8_t* p = b.At(x, y);
			p[0] = v; p[1] = uint8_t(v / 2); p[2] = uint8_t(255 - v); p[3] = 255;
		}
}

Rect
R(int l, int t, int r, int b)
{
	Rect rect;
	rect.left = l; rect.top = t; rect.right = r; rect.bottom = b;
	return rect;
}
}	// namespace


TEST_CASE("overlay: no layers is a plain copy")
{
	Buffer back(64, 48), out(64, 48);
	Checker(back, 5);
	std::vector<uint8_t> scratch;
	Compose(back.surface, out.surface, R(0, 0, 63, 47), nullptr, 0, scratch);
	CHECK(back.data == out.data);
}

TEST_CASE("overlay: content opacity blends over the real background")
{
	Buffer back(32, 32), out(32, 32);
	for (size_t i = 0; i < back.data.size(); i += 4) {
		back.data[i] = 0; back.data[i + 1] = 0; back.data[i + 2] = 200;
		back.data[i + 3] = 255;
	}
	std::vector<uint8_t> content(16 * 16 * 4);
	for (size_t i = 0; i < content.size(); i += 4) {
		content[i] = 200; content[i + 1] = 0; content[i + 2] = 0;
		content[i + 3] = 255;
	}
	Layer layer;
	layer.frame = R(8, 8, 23, 23);
	layer.bits = content.data();
	layer.stride = 16 * 4;
	layer.opacity = 0.5f;
	std::vector<uint8_t> scratch;
	Compose(back.surface, out.surface, R(0, 0, 31, 31), &layer, 1, scratch);
	CHECK_NEAR(out.At(15, 15)[0], 100, 2);		// half blue content
	CHECK_NEAR(out.At(15, 15)[2], 100, 2);		// half red background
	CHECK_EQ(int(out.At(2, 2)[2]), 200);		// outside: untouched
	layer.opacity = 1.0f;
	Compose(back.surface, out.surface, R(0, 0, 31, 31), &layer, 1, scratch);
	CHECK_EQ(int(out.At(15, 15)[0]), 199);		// (200*256)>>8 rounding
	CHECK_EQ(int(out.At(15, 15)[2]), 0);
}

TEST_CASE("overlay: backdrop blur smooths only what is under the layer")
{
	Buffer back(96, 64), out(96, 64);
	Checker(back, 2);
	Layer glass;
	glass.frame = R(20, 10, 75, 40);
	glass.blurRadius = 4;
	std::vector<uint8_t> scratch;
	Compose(back.surface, out.surface, R(0, 0, 95, 63), &glass, 1, scratch);
	// Inside: the 2-px checkerboard (20 vs 230) is flattened to ~125.
	int v = out.At(48, 25)[0];
	CHECK(v > 100 && v < 150);
	// Outside: identical to back.
	CHECK(memcmp(out.At(5, 5), back.At(5, 5), 4) == 0);
	CHECK(memcmp(out.At(90, 60), back.At(90, 60), 4) == 0);
}

TEST_CASE("overlay: composing sub-rectangles matches a full composition")
{
	// This property lets app_server compose only damaged rectangles.
	Buffer back(160, 90), full(160, 90), part(160, 90);
	Checker(back, 3);
	std::vector<uint8_t> content(60 * 30 * 4);
	for (size_t i = 0; i < content.size(); i += 4) {
		content[i] = uint8_t(i % 251); content[i + 1] = 90;
		content[i + 2] = 30; content[i + 3] = uint8_t((i / 4) % 256);
	}
	Layer layers[2];
	layers[0].frame = R(10, 50, 159, 89);
	layers[0].blurRadius = 6;
	layers[0].tintAmount = 0.4f;
	layers[0].tintR = layers[0].tintG = layers[0].tintB = 220;
	layers[0].downsample = 2;
	layers[1].frame = R(40, 20, 99, 49);
	layers[1].bits = content.data();
	layers[1].stride = 60 * 4;
	layers[1].opacity = 0.8f;
	layers[1].blurRadius = 3;
	layers[1].cornerRadius = 6;
	layers[1].shadowRadius = 8;
	layers[1].shadowOpacity = 0.4f;
	layers[1].shadowOffsetY = 2;
	std::vector<uint8_t> scratch;
	Compose(back.surface, full.surface, R(0, 0, 159, 89), layers, 2, scratch);
	// Tile the screen with odd-sized rectangles.
	for (int y = 0; y < 90; y += 17)
		for (int x = 0; x < 160; x += 23)
			Compose(back.surface, part.surface, R(x, y, x + 22, y + 16),
				layers, 2, scratch);
	CHECK(full.data == part.data);

	// Same with 4x downsampling on the glass layer.
	layers[0].downsample = 4;
	layers[0].blurRadius = 12;
	Compose(back.surface, full.surface, R(0, 0, 159, 89), layers, 2, scratch);
	for (int y = 0; y < 90; y += 13)
		for (int x = 0; x < 160; x += 29)
			Compose(back.surface, part.surface, R(x, y, x + 28, y + 12),
				layers, 2, scratch);
	CHECK(full.data == part.data);
}

TEST_CASE("overlay: rounded corners and shadow stay local")
{
	Buffer back(80, 80), out(80, 80);
	for (size_t i = 0; i < back.data.size(); i++)
		back.data[i] = (i % 4 == 3) ? 255 : 180;
	std::vector<uint8_t> content(40 * 40 * 4, 255);
	for (size_t i = 0; i < content.size(); i += 4) {
		content[i] = 10; content[i + 1] = 10; content[i + 2] = 10;
	}
	Layer card;
	card.frame = R(20, 20, 59, 59);
	card.bits = content.data();
	card.stride = 40 * 4;
	card.cornerRadius = 8;
	card.shadowRadius = 10;
	card.shadowOpacity = 0.5f;
	std::vector<uint8_t> scratch;
	Compose(back.surface, out.surface, R(0, 0, 79, 79), &card, 1, scratch);
	CHECK_EQ(int(out.At(40, 40)[0]), 10);		// inside: content
	CHECK(out.At(20, 20)[0] > 60);				// corner pixel: mostly not card
	CHECK(out.At(40, 62)[0] < 180);				// just below: shadowed
	CHECK_EQ(int(out.At(40, 75)[0]), 180);		// beyond the shadow: untouched
	CHECK_EQ(int(out.At(2, 2)[0]), 180);
	CHECK_EQ(Margin(card), 10);
	CHECK_EQ(Footprint(card).left, 10);
}

TEST_CASE("overlay: compose cost for a blurred 1920x40 panel")
{
	Buffer back(1920, 200), out(1920, 200);
	Checker(back, 7);
	Layer panel;
	panel.frame = R(0, 160, 1919, 199);
	panel.blurRadius = 6;
	panel.tintAmount = 0.5f;
	std::vector<uint8_t> scratch;
	auto start = std::chrono::steady_clock::now();
	const int frames = 10;
	for (int i = 0; i < frames; i++)
		Compose(back.surface, out.surface, panel.frame, &panel, 1, scratch);
	double ms = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count() / frames;
	std::printf("    [bench] overlay compose 1920x40 blur r6x3: %.2f ms\n", ms);
	panel.downsample = 4;
	panel.blurRadius = 12;
	start = std::chrono::steady_clock::now();
	for (int i = 0; i < frames; i++)
		Compose(back.surface, out.surface, panel.frame, &panel, 1, scratch);
	double low = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count() / frames;
	std::printf("    [bench] overlay compose 1920x40 blur r12 at 1/4: %.2f ms\n",
		low);
	CHECK(low < ms);
	// Small damage (a 64x40 clock update) must be cheap.
	start = std::chrono::steady_clock::now();
	for (int i = 0; i < frames; i++)
		Compose(back.surface, out.surface, R(1800, 160, 1863, 199), &panel, 1,
			scratch);
	double small = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count() / frames;
	std::printf("    [bench] overlay compose 64x40 damage: %.3f ms\n", small);
	CHECK(small < ms);
}
