/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Easing curves and an analytic damped spring.
 *
 * A Curve maps normalized time to normalized progress. Duration-based curves
 * (easings, cubic-bezier) take t in [0,1]. Springs are physical: their
 * duration emerges from stiffness and damping; Curve::Duration() reports the
 * settle time so springs and easings can be scheduled the same way.
 *
 * Curve specs accepted by ParseCurve():
 *   "linear", "ease-in-quad", "ease-out-cubic", "ease-in-out-quint",
 *   "ease-out-expo", "ease-out-back", "ease"  (CSS), "ease-in-out" (CSS)
 *   "cubic-bezier(0.2, 0.8, 0.2, 1)"
 *   "spring"  or  "spring(stiffness, dampingRatio)"
 */
#ifndef LUMINA_CORE_ANIM_EASING_H
#define LUMINA_CORE_ANIM_EASING_H

#include <string>
#include <vector>

namespace lumina {
namespace anim {

enum class EaseFamily { Linear, Quad, Cubic, Quart, Quint, Sine, Expo, Back };
enum class EaseMode { In, Out, InOut };

double Ease(EaseFamily family, EaseMode mode, double t);

// CSS-style cubic bezier with endpoints (0,0) and (1,1).
class CubicBezier {
public:
	CubicBezier(double x1 = 0.25, double y1 = 0.1, double x2 = 0.25,
		double y2 = 1.0);
	double Evaluate(double t) const;	// t = time fraction → progress

private:
	double _SampleX(double s) const;
	double _SampleY(double s) const;
	double _SampleDX(double s) const;
	double _SolveS(double x) const;

	double fCX, fBX, fAX, fCY, fBY, fAY;
};

// Normalized damped harmonic oscillator moving 0 → 1.
// stiffness: k (mass = 1), dampingRatio: ζ (1 = critical).
// initialVelocity: normalized units per second (velocity / distance).
class Spring {
public:
	Spring(double stiffness = 420, double dampingRatio = 0.86,
		double initialVelocity = 0);

	double Position(double seconds) const;		// progress, may overshoot
	double Velocity(double seconds) const;		// progress per second
	// Time after which |1 - position| < epsilon and |velocity| < epsilon
	// stay true (approx.). Capped at 10 s.
	double SettleTime(double epsilon = 0.001) const;

	double Stiffness() const { return fStiffness; }
	double DampingRatio() const { return fDamping; }

private:
	double _Displacement(double t) const;

	double fStiffness;
	double fDamping;
	double fV0;
	double fOmega0;
};

class Curve {
public:
	enum class Kind { Easing, Bezier, Spring };

	static Curve Linear();
	static Curve Easing(EaseFamily family, EaseMode mode);
	static Curve Bezier(double x1, double y1, double x2, double y2);
	static Curve MakeSpring(double stiffness, double dampingRatio);

	Kind GetKind() const { return fKind; }

	// Progress at `elapsed` seconds for an animation of nominal `duration`
	// seconds (ignored for springs). initialVelocity is used by springs only.
	double Progress(double elapsed, double duration,
		double initialVelocity = 0) const;
	// d(progress)/dt in 1/s, numerically.
	double ProgressVelocity(double elapsed, double duration,
		double initialVelocity = 0) const;
	// Total running time in seconds: duration, or the spring settle time.
	double Duration(double nominalDuration, double initialVelocity = 0) const;

	std::string ToString() const;

	double SpringStiffness() const { return fStiffness; }
	double SpringDamping() const { return fDamping; }

private:
	Kind		fKind = Kind::Easing;
	EaseFamily	fFamily = EaseFamily::Linear;
	EaseMode	fMode = EaseMode::Out;
	CubicBezier	fBezier;
	double		fStiffness = 420;
	double		fDamping = 0.86;
	double		fBezierParams[4] = {0.25, 0.1, 0.25, 1.0};
};

bool ParseCurve(const std::string& spec, Curve& out, std::string* error = nullptr);
std::vector<std::string> KnownCurveNames();

}	// namespace anim
}	// namespace lumina

#endif
