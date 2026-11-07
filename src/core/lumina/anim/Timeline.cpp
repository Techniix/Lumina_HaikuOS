/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Timeline.h"

#include <cmath>

namespace lumina {
namespace anim {

namespace {
float
Get(const RectF& r, int i)
{
	switch (i) {
		case 0: return r.left;
		case 1: return r.top;
		case 2: return r.right;
		default: return r.bottom;
	}
}

void
Set(RectF& r, int i, float v)
{
	switch (i) {
		case 0: r.left = v; break;
		case 1: r.top = v; break;
		case 2: r.right = v; break;
		default: r.bottom = v; break;
	}
}
}	// namespace


RectAnimation::RectAnimation(const RectF& from, const RectF& to,
	const Curve& curve, double duration, double startTime,
	const RectF& initialVelocity)
	:
	fFrom(from),
	fTo(to),
	fCurve(curve),
	fDuration(duration),
	fStart(startTime)
{
	double longest = 0;
	for (int i = 0; i < 4; i++) {
		double distance = Get(to, i) - Get(from, i);
		fV0[i] = std::fabs(distance) > 0.5
			? Get(initialVelocity, i) / distance : 0;
		// Clamp absurd normalized velocities (tiny distance, big velocity).
		if (fV0[i] > 60)
			fV0[i] = 60;
		if (fV0[i] < -60)
			fV0[i] = -60;
		double total = curve.Duration(duration, fV0[i]);
		if (total > longest)
			longest = total;
	}
	fTotal = longest;
}


double
RectAnimation::_Component(int index, double now) const
{
	double from = Get(fFrom, index);
	double to = Get(fTo, index);
	if (from == to)
		return to;
	double elapsed = now - fStart;
	if (elapsed >= fTotal)
		return to;
	double p = fCurve.Progress(elapsed, fDuration, fV0[index]);
	return from + (to - from) * p;
}


double
RectAnimation::_ComponentVelocity(int index, double now) const
{
	double from = Get(fFrom, index);
	double to = Get(fTo, index);
	double elapsed = now - fStart;
	if (from == to || elapsed >= fTotal)
		return 0;
	return (to - from) * fCurve.ProgressVelocity(elapsed, fDuration, fV0[index]);
}


RectF
RectAnimation::Sample(double now) const
{
	RectF r;
	for (int i = 0; i < 4; i++)
		Set(r, i, float(_Component(i, now)));
	return r;
}


RectF
RectAnimation::Velocity(double now) const
{
	RectF v;
	for (int i = 0; i < 4; i++)
		Set(v, i, float(_ComponentVelocity(i, now)));
	return v;
}


bool
Timeline::Animate(int64_t key, const RectF& from, const RectF& to,
	const Curve& curve, double duration, double now)
{
	auto it = fAnimations.find(key);
	if (it != fAnimations.end() && !it->second.IsFinished(now)) {
		RectF current = it->second.Sample(now);
		RectF velocity = it->second.Velocity(now);
		it->second = RectAnimation(current, to, curve, duration, now,
			velocity);
		return true;
	}
	fAnimations[key] = RectAnimation(from, to, curve, duration, now);
	return false;
}


void
Timeline::Cancel(int64_t key)
{
	fAnimations.erase(key);
}


void
Timeline::CancelAll()
{
	fAnimations.clear();
}


bool
Timeline::IsAnimating(int64_t key) const
{
	return fAnimations.find(key) != fAnimations.end();
}


bool
Timeline::Current(int64_t key, double now, RectF& out) const
{
	auto it = fAnimations.find(key);
	if (it == fAnimations.end())
		return false;
	out = it->second.Sample(now);
	return true;
}


std::vector<Frame>
Timeline::Tick(double now)
{
	std::vector<Frame> frames;
	for (auto it = fAnimations.begin(); it != fAnimations.end();) {
		bool finished = it->second.IsFinished(now);
		frames.push_back({it->first,
			finished ? it->second.Target() : it->second.Sample(now), finished});
		if (finished)
			it = fAnimations.erase(it);
		else
			++it;
	}
	return frames;
}


bool
ParseResizeMode(const std::string& name, ResizeMode& mode)
{
	if (name == "live")
		mode = ResizeMode::Live;
	else if (name == "throttled")
		mode = ResizeMode::Throttled;
	else if (name == "final")
		mode = ResizeMode::Final;
	else
		return false;
	return true;
}


GeometryStep
GeometryStepper::Step(const RectF& applied, const RectF& desired, double now,
	bool final)
{
	GeometryStep step;
	float wantLeft = std::round(desired.left);
	float wantTop = std::round(desired.top);
	float wantWidth = std::round(desired.Width());
	float wantHeight = std::round(desired.Height());
	float haveWidth = std::round(applied.Width());
	float haveHeight = std::round(applied.Height());

	step.dx = wantLeft - std::round(applied.left);
	step.dy = wantTop - std::round(applied.top);

	bool sizeChanges = wantWidth != haveWidth || wantHeight != haveHeight;
	bool allowResize = final;
	if (!allowResize && sizeChanges) {
		switch (fMode) {
			case ResizeMode::Live:
				allowResize = true;
				break;
			case ResizeMode::Throttled:
				allowResize = now - fLastResize >= fInterval;
				break;
			case ResizeMode::Final:
				allowResize = false;
				break;
		}
	}
	if (allowResize && sizeChanges) {
		step.dw = wantWidth - haveWidth;
		step.dh = wantHeight - haveHeight;
		fLastResize = now;
	}
	return step;
}

}	// namespace anim
}	// namespace lumina
