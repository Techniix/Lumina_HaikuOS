#include "../LuminaTest.h"

#include <chrono>
#include <cstdio>

#include <lumina/ConfigStore.h>
#include <lumina/fx/Glass.h>
#include <lumina/fx/Image.h>
#include <lumina/fx/Strategy.h>

using namespace lumina;
using namespace lumina::fx;

namespace {
void
Checker(Image& image, int cell)
{
	ImageView v = image.View();
	for (int y = 0; y < v.height; y++) {
		for (int x = 0; x < v.width; x++) {
			uint8_t c = ((x / cell + y / cell) & 1) ? 255 : 0;
			uint8_t* p = v.Pixel(x, y);
			p[0] = p[1] = p[2] = c;
			p[3] = 255;
		}
	}
}
}	// namespace

TEST_CASE("fx: box blur preserves mean and flattens contrast")
{
	Image image(64, 32);
	Checker(image, 4);
	std::vector<uint8_t> scratch;
	BoxBlur(image.View(), 6, 3, scratch);
	ImageView v = image.View();
	long sum = 0;
	int lo = 255, hi = 0;
	for (int y = 4; y < v.height - 4; y++) {
		for (int x = 4; x < v.width - 4; x++) {
			int c = v.Pixel(x, y)[0];
			sum += c;
			lo = std::min(lo, c);
			hi = std::max(hi, c);
		}
	}
	double mean = double(sum) / ((v.height - 8) * (v.width - 8));
	CHECK_NEAR(mean, 127.5, 6);
	CHECK(hi - lo < 40);
	CHECK_EQ(int(v.Pixel(10, 10)[3]), 255);
}

TEST_CASE("fx: gaussian box radii approximate sigma")
{
	auto radii = GaussianBoxRadii(5.0, 3);
	REQUIRE(radii.size() == 3);
	// variance of n boxes of width w: sum((w^2-1)/12) ≈ sigma^2
	double variance = 0;
	for (int r : radii) {
		int w = 2 * r + 1;
		variance += (w * w - 1) / 12.0;
	}
	CHECK_NEAR(variance, 25.0, 4.0);
	CHECK(GaussianBoxRadii(0, 3).empty());
}

TEST_CASE("fx: downsample, scale, tint, grade, crossfade, blend")
{
	Image src(8, 8);
	src.Fill(Color::RGB(200, 100, 50));
	Image half(4, 4);
	Downsample2x(src.View(), half.View());
	CHECK_EQ(int(half.View().Pixel(1, 1)[2]), 200);
	Image big(13, 7);
	ScaleBilinear(half.View(), big.View());
	CHECK_EQ(int(big.View().Pixel(12, 6)[1]), 100);

	Image t(2, 2);
	t.Fill(Color::RGB(0, 0, 0));
	Tint(t.View(), Color::RGB(255, 255, 255), 0.5);
	CHECK_NEAR(t.View().Pixel(0, 0)[0], 127, 2);

	Image g(1, 1);
	g.Fill(Color::RGB(200, 100, 50));
	Grade gray;
	gray.saturation = 0;
	ApplyGrade(g.View(), gray);
	uint8_t* p = g.View().Pixel(0, 0);
	CHECK(p[0] == p[1] && p[1] == p[2]);

	Image a(2, 1), b(2, 1), out(2, 1);
	a.Fill(Color::RGB(0, 0, 0));
	b.Fill(Color::RGB(200, 200, 200));
	CrossFade(a.View(), b.View(), out.View(), 0.25);
	CHECK_NEAR(out.View().Pixel(0, 0)[0], 50, 1);

	Image dst(1, 1), overlay(1, 1);
	dst.Fill(Color::RGB(0, 0, 0));
	overlay.Fill(Color::RGB(255, 255, 255, 128));
	BlendOver(overlay.View(), dst.View());
	CHECK_NEAR(dst.View().Pixel(0, 0)[0], 128, 2);
}

TEST_CASE("fx: rounded corners clear corner alpha")
{
	Image image(20, 20);
	image.Fill(Color::RGB(1, 2, 3));
	RoundCorners(image.View(), 6);
	CHECK_EQ(int(image.View().Pixel(0, 0)[3]), 0);
	CHECK_EQ(int(image.View().Pixel(10, 10)[3]), 255);
	CHECK_EQ(int(image.View().Pixel(10, 0)[3]), 255);
}

TEST_CASE("fx: edge band synthesis mirrors from the shared edge")
{
	// band rows 0..3 have values 10,20,30,40; row 3 touches a bottom panel.
	Image band(5, 4);
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 5; x++)
			band.View().Pixel(x, y)[0] = uint8_t(10 * (y + 1));
	}
	Image panel(5, 6);
	SynthesizeFromEdgeBand(band.View(), Edge::Bottom, panel.View());
	CHECK_EQ(int(panel.View().Pixel(0, 0)[0]), 40);	// adjacent row
	CHECK_EQ(int(panel.View().Pixel(0, 1)[0]), 30);
	CHECK_EQ(int(panel.View().Pixel(0, 3)[0]), 10);
	CHECK_EQ(int(panel.View().Pixel(0, 4)[0]), 10);	// mirrored back
	CHECK_EQ(int(panel.View().Pixel(0, 5)[0]), 20);

	Image topPanel(5, 2);
	SynthesizeFromEdgeBand(band.View(), Edge::Top, topPanel.View());
	CHECK_EQ(int(topPanel.View().Pixel(0, 1)[0]), 10);	// adjacent row 0
}

TEST_CASE("fx: sampled difference")
{
	Image a(32, 32), b(32, 32);
	a.Fill(Color::RGB(10, 10, 10));
	b.Fill(Color::RGB(10, 10, 10));
	CHECK_NEAR(SampledDifference(a.View(), b.View()), 0, 1e-9);
	b.Fill(Color::RGB(20, 20, 20));
	CHECK_NEAR(SampledDifference(a.View(), b.View()), 10, 1e-9);
}

TEST_CASE("fx: glass renderer output is opaque and timed")
{
	Image source(1920, 48), out(1920, 48);
	Checker(source, 8);
	GlassRenderer renderer;
	GlassParams params;
	GlassTiming timing;
	renderer.Render(source.View(), out.View(), params, &timing);
	CHECK_EQ(int(out.View().Pixel(100, 20)[3]), 255);
	CHECK(timing.totalMs >= 0);
	// Blurred checkerboard + tint: low contrast across the panel body.
	int lo = 255, hi = 0;
	for (int x = 50; x < 1870; x += 13) {
		int c = out.View().Pixel(x, 24)[1];
		lo = std::min(lo, c);
		hi = std::max(hi, c);
	}
	CHECK(hi - lo < 30);
	std::printf("    glass 1920x48 downsample=%d: %.2f ms (blur %.2f)\n",
		params.downsample, timing.totalMs, timing.blurMs);
}

TEST_CASE("fx strategy: everything off by default")
{
	ConfigStore config;
	EffectProfile profile = EffectProfile::FromConfig(config);
	Capabilities caps;
	EffectPlan plan = SelectPlan(profile, caps, Quality::High);
	CHECK(plan.quality == Quality::Off);
	CHECK(plan.snapGeometry == GeometryStrategy::Instant);
	CHECK(plan.glass == GlassStrategy::Solid);
}

TEST_CASE("fx strategy: degradation chain")
{
	EffectProfile profile;
	profile.quality = Quality::High;
	profile.animateWindows = true;
	profile.glass = GlassStrategy::Capture;
	profile.proxies = true;

	Capabilities none;
	EffectPlan plan = SelectPlan(profile, none, Quality::High);
	CHECK(plan.snapGeometry == GeometryStrategy::Instant);
	CHECK(plan.glass == GlassStrategy::Wallpaper);
	CHECK(plan.minimizeGeometry == GeometryStrategy::Instant);

	Capabilities full;
	full.engineLoaded = full.engineAnimation = full.screenReadback = true;
	full.readbackMsPerMegapixel = 10;
	plan = SelectPlan(profile, full, Quality::High);
	CHECK(plan.snapGeometry == GeometryStrategy::EngineAnimated);
	CHECK(plan.glass == GlassStrategy::Capture);
	CHECK(plan.minimizeGeometry == GeometryStrategy::Proxy);

	// Slow readback: capture degrades to edge.
	full.readbackMsPerMegapixel = 120;
	plan = SelectPlan(profile, full, Quality::High);
	CHECK(plan.glass == GlassStrategy::Edge);

	// Governor at Low: wallpaper glass, final-only resizes, 30 fps cap.
	plan = SelectPlan(profile, full, Quality::Low);
	CHECK(plan.glass == GlassStrategy::Wallpaper);
	CHECK_EQ(plan.resizeMode, std::string("final"));
	CHECK(plan.maxFps <= 30);
	CHECK(plan.minimizeGeometry == GeometryStrategy::Instant);
}

TEST_CASE("fx governor: downgrades under load, recovers when cheap")
{
	Governor governor(4.0, Quality::High);
	double now = 0;
	for (int i = 0; i < 20; i++) {
		governor.Record(9.0, now);
		now += 1.0 / 60;
	}
	CHECK(int(governor.Current()) < int(Quality::High));
	Quality degraded = governor.Current();
	for (int i = 0; i < 60 * 12; i++) {
		governor.Record(0.5, now);
		now += 1.0 / 60;
	}
	CHECK(int(governor.Current()) > int(degraded));
	CHECK(governor.Downgrades() >= 1);
	governor.SetCeiling(Quality::Low);
	CHECK(governor.Current() == Quality::Low);
}
