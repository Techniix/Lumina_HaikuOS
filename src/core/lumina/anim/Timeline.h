/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Timeline: keyed, retargetable rectangle animations.
 *
 * Starting an animation for a key that is already animating retargets it:
 * the new animation begins at the current sampled position and, for spring
 * curves, inherits the current velocity, so interrupted motion never jumps
 * or stops dead. This is what makes snapping a window while it is still
 * flying look physical.
 *
 * GeometryStepper decides what to actually apply to a real window each
 * frame. Moving a window in app_server is a cheap blit; resizing makes the
 * client re-layout and redraw, so resizes can be throttled or deferred.
 */
#ifndef LUMINA_CORE_ANIM_TIMELINE_H
#define LUMINA_CORE_ANIM_TIMELINE_H

#include <cstdint>
#include <map>
#include <vector>

#include "../Rules.h"	// RectF
#include "Easing.h"

namespace lumina {
namespace anim {

class RectAnimation {
public:
	RectAnimation() = default;
	RectAnimation(const RectF& from, const RectF& to, const Curve& curve,
		double duration, double startTime, const RectF& initialVelocity = RectF());

	RectF Sample(double now) const;
	RectF Velocity(double now) const;	// units per second, per edge
	bool IsFinished(double now) const { return now >= fStart + fTotal; }
	double EndTime() const { return fStart + fTotal; }
	const RectF& Target() const { return fTo; }
	const RectF& Origin() const { return fFrom; }

private:
	double _Component(int index, double now) const;
	double _ComponentVelocity(int index, double now) const;

	RectF	fFrom;
	RectF	fTo;
	Curve	fCurve;
	double	fDuration = 0;
	double	fStart = 0;
	double	fTotal = 0;		// running time (settle time for springs)
	double	fV0[4] = {0, 0, 0, 0};	// normalized initial velocities
};

struct Frame {
	int64_t	key;
	RectF	rect;
	bool	finished;
};

class Timeline {
public:
	// Starts or retargets. Returns true if this was a retarget.
	bool Animate(int64_t key, const RectF& from, const RectF& to,
		const Curve& curve, double duration, double now);
	void Cancel(int64_t key);
	void CancelAll();
	bool IsAnimating(int64_t key) const;
	bool HasActive() const { return !fAnimations.empty(); }
	size_t ActiveCount() const { return fAnimations.size(); }

	// Samples all animations; finished ones emit their exact target and are
	// removed.
	std::vector<Frame> Tick(double now);

	bool Current(int64_t key, double now, RectF& out) const;

private:
	std::map<int64_t, RectAnimation> fAnimations;
};

enum class ResizeMode { Live, Throttled, Final };

bool ParseResizeMode(const std::string& name, ResizeMode& mode);

struct GeometryStep {
	float	dx = 0, dy = 0;		// move by
	float	dw = 0, dh = 0;		// resize by
	bool	Empty() const { return dx == 0 && dy == 0 && dw == 0 && dh == 0; }
};

class GeometryStepper {
public:
	GeometryStepper(ResizeMode mode = ResizeMode::Throttled,
		double resizeInterval = 1.0 / 30)
		: fMode(mode), fInterval(resizeInterval) {}

	// `applied` is the window frame as last applied; `desired` the sampled
	// animation frame. Integer pixel deltas are produced; size changes are
	// rate limited according to the mode. `final` forces the exact target.
	GeometryStep Step(const RectF& applied, const RectF& desired, double now,
		bool final);

	void Reset() { fLastResize = -1e9; }

private:
	ResizeMode	fMode;
	double		fInterval;
	double		fLastResize = -1e9;
};

}	// namespace anim
}	// namespace lumina

#endif
