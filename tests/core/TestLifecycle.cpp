#include "../LuminaTest.h"

#include <chrono>
#include <cstdio>

#include <lumina/CursorTheme.h>
#include <lumina/Json.h>
#include <lumina/fx/Lifecycle.h>

using namespace lumina;
using namespace lumina::fx;

namespace {
WindowTraits
Typical()
{
	WindowTraits t;
	t.normalFeel = true;
	t.bordered = true;
	t.onCurrentWorkspace = true;
	t.visibleFraction = 1;
	t.width = 640;
	t.height = 480;
	return t;
}

void
FillSolid(Image& image, uint8_t value)
{
	image.Fill(Color::RGB(value, value, value));
}

Value
Parse(const char* text)
{
	Value v;
	JsonError error;
	ParseJson(text, v, &error);
	return v;
}
}	// namespace


TEST_CASE("lifecycle: opt-in, per-kind switches and eligibility")
{
	LifecycleSettings s;
	CHECK(Decide(s, LifecycleKind::Close, Typical(), true)
		== Decision::Disabled);
	s.Apply(Parse("{\"enabled\":true,\"minimize\":false}"));
	CHECK(Decide(s, LifecycleKind::Close, Typical(), true)
		== Decision::Animate);
	CHECK(Decide(s, LifecycleKind::Minimize, Typical(), true)
		== Decision::KindDisabled);
	CHECK(Decide(s, LifecycleKind::Close, Typical(), false)
		== Decision::NoProxy);

	WindowTraits shell = Typical();
	shell.desktopShell = true;
	CHECK(Decide(s, LifecycleKind::Close, shell, true)
		== Decision::NotEligible);
	WindowTraits stacked = Typical();
	stacked.inStack = true;
	CHECK(Decide(s, LifecycleKind::Close, stacked, true)
		== Decision::NotEligible);

	WindowTraits hidden = Typical();
	hidden.visibleFraction = 0.4f;
	CHECK(Decide(s, LifecycleKind::Close, hidden, true)
		== Decision::Obscured);
	// Restores draw from the cache: visibility is irrelevant.
	CHECK(Decide(s, LifecycleKind::Restore, hidden, true)
		== Decision::Animate);

	WindowTraits huge = Typical();
	huge.width = 10000;
	huge.height = 10000;
	CHECK(Decide(s, LifecycleKind::Close, huge, true)
		== Decision::TooLarge);
}

TEST_CASE("lifecycle: settings are clamped")
{
	LifecycleSettings s;
	s.Apply(Parse("{\"durationMs\":99999,\"closeScale\":0.01,"
		"\"minVisible\":7,\"cacheBudgetMB\":-5,\"proxyTimeoutMs\":1}"));
	CHECK_NEAR(s.durationSeconds, 2.0, 1e-6);
	CHECK_NEAR(s.closeScale, 0.3, 1e-6);
	CHECK_NEAR(s.minVisible, 1.0, 1e-6);
	CHECK_EQ(s.cacheBudgetMB, 0);
	CHECK_NEAR(s.proxyTimeout, 0.2, 1e-6);
}

TEST_CASE("lifecycle: transition targets")
{
	RectF outer{100, 100, 299, 199};
	RectF close = CloseTarget(outer, 0.9f);
	CHECK_NEAR(close.left + close.right, outer.left + outer.right, 1.0);
	CHECK_NEAR(close.Width(), outer.Width() * 0.9, 1.0);

	RectF anchor{500, 760, 540, 780};
	RectF screen{0, 0, 1023, 767};
	CHECK(MinimizeTarget(outer, anchor, screen) == anchor);
	RectF invalid{1, 1, 0, 0};
	RectF fallback = MinimizeTarget(outer, invalid, screen);
	CHECK(IsValid(fallback));
	CHECK_EQ(fallback.bottom, screen.bottom);
	CHECK(fallback.left > 400 && fallback.right < 624);
}

TEST_CASE("lifecycle: ring check accepts unchanged backgrounds only")
{
	const int ring = 2;
	Image remembered(40, 30), live(40, 30);
	FillSolid(remembered, 80);
	FillSolid(live, 80);
	// The interior differs (the window itself is there now): irrelevant.
	ImageView inside = live.View().Sub(ring, ring, 40 - 2 * ring,
		30 - 2 * ring);
	for (int y = 0; y < inside.height; y++)
		for (int x = 0; x < inside.width; x++)
			inside.Pixel(x, y)[0] = 250;
	CHECK(BackgroundStillValid(remembered.View(), live.View(), ring));

	// Something changed around the window: reject.
	Image changed(40, 30);
	FillSolid(changed, 160);
	CHECK(!BackgroundStillValid(remembered.View(), changed.View(), ring));
	// Size mismatch: reject.
	Image other(41, 30);
	FillSolid(other, 80);
	CHECK(!BackgroundStillValid(remembered.View(), other.View(), ring));
}

TEST_CASE("lifecycle: crossfade endpoints and midpoint")
{
	Image a(8, 4), b(8, 4), out(8, 4);
	FillSolid(a, 0);
	FillSolid(b, 200);
	ImageView va = a.View(), vb = b.View(), vo = out.View();
	CrossFadeRows(va.bits, va.stride, vb.bits, vb.stride, vo.bits, vo.stride,
		8, 4, 0.0);
	CHECK_EQ(int(vo.Pixel(3, 2)[1]), 0);
	CrossFadeRows(va.bits, va.stride, vb.bits, vb.stride, vo.bits, vo.stride,
		8, 4, 1.0);
	CHECK_EQ(int(vo.Pixel(3, 2)[1]), 200);
	CrossFadeRows(va.bits, va.stride, vb.bits, vb.stride, vo.bits, vo.stride,
		8, 4, 0.5);
	CHECK_EQ(int(vo.Pixel(7, 3)[2]), 100);
	CHECK_EQ(int(vo.Pixel(0, 0)[3]), 255);
}

TEST_CASE("lifecycle: crossfade cost for an 800x600 window")
{
	Image a(800, 600), b(800, 600), out(800, 600);
	FillSolid(a, 10);
	FillSolid(b, 240);
	ImageView va = a.View(), vb = b.View(), vo = out.View();
	auto start = std::chrono::steady_clock::now();
	const int frames = 12;
	for (int i = 0; i < frames; i++) {
		CrossFadeRows(va.bits, va.stride, vb.bits, vb.stride, vo.bits,
			vo.stride, 800, 600, i / double(frames));
	}
	double ms = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count() / frames;
	std::printf("    [bench] crossfade 800x600: %.2f ms/frame\n", ms);
	CHECK(ms < 16.0);
}

TEST_CASE("lifecycle: snapshot cache honours its byte budget (LRU)")
{
	SnapshotCache cache(3 * 100 * 100 * 4);
	Image img(100, 100);
	FillSolid(img, 7);
	CHECK(cache.Put(1, img.View()));
	CHECK(cache.Put(2, img.View()));
	CHECK(cache.Put(3, img.View()));
	CHECK_EQ(cache.Count(), size_t(3));
	CHECK(cache.Get(1) != nullptr);		// 1 is now most recent
	CHECK(cache.Put(4, img.View()));	// evicts 2 (least recent)
	CHECK(cache.Get(2) == nullptr);
	CHECK(cache.Get(1) != nullptr);
	CHECK(cache.Bytes() <= cache.Budget());
	Image big(400, 400);
	CHECK(!cache.Put(9, big.View()));	// larger than the whole budget
	cache.SetBudget(100 * 100 * 4);
	CHECK_EQ(cache.Count(), size_t(1));
	cache.Clear();
	CHECK_EQ(cache.Bytes(), size_t(0));
}

TEST_CASE("cursor theme: manifest parsing, roles and sizes")
{
	cursor::Theme theme;
	std::vector<std::string> problems;
	CHECK(cursor::ParseTheme(Parse(
		"{\"name\":\"Soft\",\"sizes\":[44,22,22,3],\"cursors\":{"
		"\"default\":{\"file\":\"default.png\",\"hotspot\":[1,1]},"
		"\"text\":\"text.png\","
		"\"bogus\":{\"file\":\"x.png\"},"
		"\"create-link\":{\"file\":\"link.png\"},"
		"\"help\":{\"file\":\"../evil.png\"},"
		"\"progress\":{\"file\":\"busy.png\",\"hotspot\":[30,2]}}}"),
		theme, problems));
	CHECK_EQ(theme.name, std::string("Soft"));
	CHECK_EQ(theme.sizes.size(), size_t(2));
	CHECK_EQ(theme.sizes.front(), 22);
	CHECK(theme.Find("default") != nullptr);
	CHECK(theme.Find("text") != nullptr);
	CHECK(theme.Find("bogus") == nullptr);
	CHECK(theme.Find("create-link") == nullptr);	// not loadable upstream
	CHECK(theme.Find("help") == nullptr);			// unsafe path
	const cursor::Entry* busy = theme.Find("progress");
	REQUIRE(busy != nullptr);
	CHECK(busy->hotX <= 21);						// clamped
	CHECK(problems.size() >= 4);

	CHECK_EQ(cursor::PickSize(theme, 1.0f), 22);
	CHECK_EQ(cursor::PickSize(theme, 1.5f), 44);
	CHECK_EQ(cursor::PickSize(theme, 4.0f), 44);
	float x, y;
	cursor::ScaledHotspot(theme, *theme.Find("default"), 44, x, y);
	CHECK_NEAR(x, 2, 1e-6);
	CHECK_EQ(cursor::RelativePath(*theme.Find("text"), 44),
		std::string("44/text.png"));

	const cursor::Role* role = cursor::FindRole("resize-nwse");
	REQUIRE(role != nullptr);
	CHECK_EQ(role->id, 26);
	CHECK_EQ(cursor::Roles().size(), size_t(29));

	uint8_t px[8] = {200, 100, 50, 128, 10, 20, 30, 255};
	cursor::Premultiply(px, 2, 1, 8);
	CHECK_EQ(int(px[0]), 100);
	CHECK_EQ(int(px[3]), 128);
	CHECK_EQ(int(px[4]), 10);

	cursor::Theme empty;
	problems.clear();
	CHECK(!cursor::ParseTheme(Parse("{\"cursors\":{}}"), empty, problems));
}
