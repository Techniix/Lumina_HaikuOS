#include "../LuminaTest.h"

#include <lumina/anim/Easing.h>
#include <lumina/anim/Timeline.h>

using namespace lumina;
using namespace lumina::anim;

TEST_CASE("easing: endpoints and monotonicity")
{
	for (const std::string& name : KnownCurveNames()) {
		Curve c;
		REQUIRE(ParseCurve(name, c));
		if (c.GetKind() == Curve::Kind::Spring)
			continue;
		CHECK_NEAR(c.Progress(0, 1), 0, 1e-6);
		CHECK_NEAR(c.Progress(1, 1), 1, 1e-6);
		CHECK_EQ(c.ToString().empty(), false);
	}
	Curve c = Curve::Easing(EaseFamily::Cubic, EaseMode::Out);
	double last = -1;
	for (int i = 0; i <= 100; i++) {
		double p = c.Progress(i / 100.0, 1);
		CHECK(p >= last);
		last = p;
	}
	// ease-out is ahead of linear at the midpoint
	CHECK(c.Progress(0.5, 1) > 0.5);
	CHECK(Curve::Easing(EaseFamily::Cubic, EaseMode::In).Progress(0.5, 1) < 0.5);
	CHECK_NEAR(Curve::Easing(EaseFamily::Quad, EaseMode::InOut).Progress(0.5, 1),
		0.5, 1e-9);
}

TEST_CASE("easing: cubic-bezier matches CSS reference points")
{
	Curve ease;
	REQUIRE(ParseCurve("cubic-bezier(0.25, 0.1, 0.25, 1)", ease));
	// CSS "ease" at t=0.5 ≈ 0.8024 (reference implementations)
	CHECK_NEAR(ease.Progress(0.5, 1), 0.8024, 0.002);
	Curve linearBezier;
	REQUIRE(ParseCurve("cubic-bezier(0, 0, 1, 1)", linearBezier));
	CHECK_NEAR(linearBezier.Progress(0.3, 1), 0.3, 1e-4);
	Curve bad;
	std::string error;
	CHECK(!ParseCurve("cubic-bezier(2, 0, 0, 1)", bad, &error));
	CHECK(!ParseCurve("cubic-bezier(1,2,3)", bad, &error));
	CHECK(!ParseCurve("wobble", bad, &error));
	CHECK(!ParseCurve("spring(0, 1)", bad, &error));
	CHECK(ParseCurve("spring(300, 0.5)", bad, &error));
}

TEST_CASE("spring: analytic solution properties")
{
	// Critically damped: no overshoot, converges.
	Spring critical(400, 1.0);
	double maxPos = 0;
	for (double t = 0; t < 2; t += 0.001)
		maxPos = std::max(maxPos, critical.Position(t));
	CHECK(maxPos <= 1.0 + 1e-9);
	CHECK_NEAR(critical.Position(2), 1, 1e-4);

	// Underdamped overshoots.
	Spring bouncy(400, 0.3);
	maxPos = 0;
	for (double t = 0; t < 2; t += 0.001)
		maxPos = std::max(maxPos, bouncy.Position(t));
	CHECK(maxPos > 1.2);

	// Overdamped is slower than critical at the same stiffness.
	Spring sluggish(400, 2.0);
	CHECK(sluggish.Position(0.1) < critical.Position(0.1));

	// Initial velocity carries motion forward.
	Spring pushed(400, 1.0, 20);
	CHECK(pushed.Position(0.02) > critical.Position(0.02));
	CHECK_NEAR(pushed.Velocity(0), 20, 0.5);

	// Settle times are finite and ordered.
	CHECK(critical.SettleTime() < 1.0);
	CHECK(bouncy.SettleTime() > critical.SettleTime());
	CHECK(Spring(1600, 1.0).SettleTime() < critical.SettleTime());
}

TEST_CASE("timeline: animates, finishes exactly on target")
{
	Timeline timeline;
	RectF from{0, 0, 100, 100}, to{200, 50, 400, 250};
	Curve curve = Curve::Easing(EaseFamily::Cubic, EaseMode::Out);
	CHECK(!timeline.Animate(7, from, to, curve, 0.2, 10.0));
	auto frames = timeline.Tick(10.1);
	REQUIRE(frames.size() == 1);
	CHECK(!frames[0].finished);
	CHECK(frames[0].rect.left > 0 && frames[0].rect.left < 200);
	frames = timeline.Tick(10.3);
	REQUIRE(frames.size() == 1);
	CHECK(frames[0].finished);
	CHECK(frames[0].rect == to);
	CHECK(!timeline.HasActive());
}

TEST_CASE("timeline: spring retarget preserves position and velocity")
{
	Timeline timeline;
	Curve spring = Curve::MakeSpring(300, 0.9);
	RectF a{0, 0, 100, 100}, b{1000, 0, 1100, 100}, c{0, 500, 100, 600};
	timeline.Animate(1, a, b, spring, 0, 0.0);
	RectF mid;
	REQUIRE(timeline.Current(1, 0.08, mid));
	CHECK(mid.left > 50 && mid.left < 1000);
	CHECK(timeline.Animate(1, a, c, spring, 0, 0.08));	// retarget
	RectF after;
	REQUIRE(timeline.Current(1, 0.08, after));
	CHECK_NEAR(after.left, mid.left, 0.01);	// continuous position
	RectF later;
	REQUIRE(timeline.Current(1, 0.09, later));
	// Still moving right for a moment thanks to inherited velocity.
	CHECK(later.left > after.left);
	auto frames = timeline.Tick(20);
	REQUIRE(frames.size() == 1);
	CHECK(frames[0].rect == c);
}

TEST_CASE("geometry stepper: resize policies")
{
	RectF applied{0, 0, 100, 100};
	RectF desired{10, 5, 150.4f, 130};
	GeometryStepper live(ResizeMode::Live);
	GeometryStep s = live.Step(applied, desired, 0, false);
	CHECK_EQ(s.dx, 10.0f);
	CHECK_EQ(s.dy, 5.0f);
	CHECK_EQ(s.dw, 40.0f);
	CHECK_EQ(s.dh, 25.0f);

	GeometryStepper final(ResizeMode::Final);
	s = final.Step(applied, desired, 0, false);
	CHECK_EQ(s.dw, 0.0f);
	s = final.Step(applied, desired, 0, true);
	CHECK_EQ(s.dw, 40.0f);

	GeometryStepper throttled(ResizeMode::Throttled, 0.05);
	s = throttled.Step(applied, desired, 1.0, false);
	CHECK_EQ(s.dw, 40.0f);
	s = throttled.Step(applied, desired, 1.01, false);
	CHECK_EQ(s.dw, 0.0f);	// within interval: move only
	CHECK_EQ(s.dx, 10.0f);
	s = throttled.Step(applied, desired, 1.06, false);
	CHECK_EQ(s.dw, 40.0f);

	ResizeMode mode;
	CHECK(ParseResizeMode("final", mode) && mode == ResizeMode::Final);
	CHECK(!ParseResizeMode("wiggly", mode));
}
