/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Easing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lumina {
namespace anim {

namespace {
const double kPi = 3.14159265358979323846;

double
EaseIn(EaseFamily family, double t)
{
	switch (family) {
		case EaseFamily::Linear: return t;
		case EaseFamily::Quad: return t * t;
		case EaseFamily::Cubic: return t * t * t;
		case EaseFamily::Quart: return t * t * t * t;
		case EaseFamily::Quint: return t * t * t * t * t;
		case EaseFamily::Sine: return 1 - std::cos(t * kPi / 2);
		case EaseFamily::Expo: return t <= 0 ? 0 : std::pow(2, 10 * t - 10);
		case EaseFamily::Back:
		{
			const double c1 = 1.70158, c3 = c1 + 1;
			return c3 * t * t * t - c1 * t * t;
		}
	}
	return t;
}
}	// namespace


double
Ease(EaseFamily family, EaseMode mode, double t)
{
	t = std::clamp(t, 0.0, 1.0);
	switch (mode) {
		case EaseMode::In:
			return EaseIn(family, t);
		case EaseMode::Out:
			return 1 - EaseIn(family, 1 - t);
		case EaseMode::InOut:
			return t < 0.5 ? EaseIn(family, 2 * t) / 2
				: 1 - EaseIn(family, 2 - 2 * t) / 2;
	}
	return t;
}


// #pragma mark - CubicBezier


CubicBezier::CubicBezier(double x1, double y1, double x2, double y2)
{
	fCX = 3 * x1;
	fBX = 3 * (x2 - x1) - fCX;
	fAX = 1 - fCX - fBX;
	fCY = 3 * y1;
	fBY = 3 * (y2 - y1) - fCY;
	fAY = 1 - fCY - fBY;
}


double CubicBezier::_SampleX(double s) const
	{ return ((fAX * s + fBX) * s + fCX) * s; }
double CubicBezier::_SampleY(double s) const
	{ return ((fAY * s + fBY) * s + fCY) * s; }
double CubicBezier::_SampleDX(double s) const
	{ return (3 * fAX * s + 2 * fBX) * s + fCX; }


double
CubicBezier::_SolveS(double x) const
{
	// Newton-Raphson, falling back to bisection.
	double s = x;
	for (int i = 0; i < 8; i++) {
		double error = _SampleX(s) - x;
		if (std::fabs(error) < 1e-7)
			return s;
		double d = _SampleDX(s);
		if (std::fabs(d) < 1e-6)
			break;
		s -= error / d;
	}
	double lo = 0, hi = 1;
	s = x;
	for (int i = 0; i < 60; i++) {
		double value = _SampleX(s);
		if (std::fabs(value - x) < 1e-7)
			break;
		if (value < x)
			lo = s;
		else
			hi = s;
		s = (lo + hi) / 2;
	}
	return s;
}


double
CubicBezier::Evaluate(double t) const
{
	if (t <= 0)
		return 0;
	if (t >= 1)
		return 1;
	return _SampleY(_SolveS(t));
}


// #pragma mark - Spring


Spring::Spring(double stiffness, double dampingRatio, double initialVelocity)
	:
	fStiffness(std::max(stiffness, 1e-3)),
	fDamping(std::max(dampingRatio, 1e-3)),
	fV0(initialVelocity),
	fOmega0(std::sqrt(std::max(stiffness, 1e-3)))
{
}


double
Spring::_Displacement(double t) const
{
	// d(0) = 1, d'(0) = -v0. Position = 1 - d.
	const double w0 = fOmega0, z = fDamping, v0 = fV0;
	if (z < 1) {
		double wd = w0 * std::sqrt(1 - z * z);
		return std::exp(-z * w0 * t)
			* (std::cos(wd * t) + ((z * w0 - v0) / wd) * std::sin(wd * t));
	}
	if (z == 1)
		return std::exp(-w0 * t) * (1 + (w0 - v0) * t);
	double root = std::sqrt(z * z - 1);
	double r1 = -w0 * (z - root);
	double r2 = -w0 * (z + root);
	double a = (-v0 - r2) / (r1 - r2);
	double b = 1 - a;
	return a * std::exp(r1 * t) + b * std::exp(r2 * t);
}


double
Spring::Position(double seconds) const
{
	if (seconds <= 0)
		return 0;
	return 1 - _Displacement(seconds);
}


double
Spring::Velocity(double seconds) const
{
	const double h = 1e-4;
	double t = std::max(seconds, 0.0);
	if (t < h)
		return (Position(t + h) - Position(t)) / h + (t <= 0 ? 0 : 0);
	return (Position(t + h) - Position(t - h)) / (2 * h);
}


double
Spring::SettleTime(double epsilon) const
{
	const double step = 1.0 / 240;
	double settledSince = -1;
	for (double t = 0; t < 10.0; t += step) {
		double d = _Displacement(t);
		double v = Velocity(t);
		bool settled = std::fabs(d) < epsilon && std::fabs(v) < epsilon * 10;
		if (settled) {
			if (settledSince < 0)
				settledSince = t;
			if (t - settledSince > 0.05)
				return settledSince;
		} else
			settledSince = -1;
	}
	return 10.0;
}


// #pragma mark - Curve


Curve
Curve::Linear()
{
	return Easing(EaseFamily::Linear, EaseMode::In);
}


Curve
Curve::Easing(EaseFamily family, EaseMode mode)
{
	Curve c;
	c.fKind = Kind::Easing;
	c.fFamily = family;
	c.fMode = mode;
	return c;
}


Curve
Curve::Bezier(double x1, double y1, double x2, double y2)
{
	Curve c;
	c.fKind = Kind::Bezier;
	c.fBezier = CubicBezier(x1, y1, x2, y2);
	c.fBezierParams[0] = x1;
	c.fBezierParams[1] = y1;
	c.fBezierParams[2] = x2;
	c.fBezierParams[3] = y2;
	return c;
}


Curve
Curve::MakeSpring(double stiffness, double dampingRatio)
{
	Curve c;
	c.fKind = Kind::Spring;
	c.fStiffness = stiffness;
	c.fDamping = dampingRatio;
	return c;
}


double
Curve::Progress(double elapsed, double duration, double initialVelocity) const
{
	switch (fKind) {
		case Kind::Spring:
			return Spring(fStiffness, fDamping, initialVelocity)
				.Position(elapsed);
		case Kind::Bezier:
			return duration <= 0 ? 1 : fBezier.Evaluate(elapsed / duration);
		case Kind::Easing:
			return duration <= 0 ? 1 : Ease(fFamily, fMode, elapsed / duration);
	}
	return 1;
}


double
Curve::ProgressVelocity(double elapsed, double duration,
	double initialVelocity) const
{
	if (fKind == Kind::Spring)
		return Spring(fStiffness, fDamping, initialVelocity).Velocity(elapsed);
	if (duration <= 0 || elapsed >= duration)
		return 0;
	const double h = std::min(1e-4, duration / 100);
	double a = std::max(elapsed - h, 0.0);
	double b = std::min(elapsed + h, duration);
	if (b <= a)
		return 0;
	return (Progress(b, duration) - Progress(a, duration)) / (b - a);
}


double
Curve::Duration(double nominalDuration, double initialVelocity) const
{
	if (fKind == Kind::Spring)
		return Spring(fStiffness, fDamping, initialVelocity).SettleTime();
	return std::max(nominalDuration, 0.0);
}


static const char*
FamilyName(EaseFamily family)
{
	switch (family) {
		case EaseFamily::Linear: return "linear";
		case EaseFamily::Quad: return "quad";
		case EaseFamily::Cubic: return "cubic";
		case EaseFamily::Quart: return "quart";
		case EaseFamily::Quint: return "quint";
		case EaseFamily::Sine: return "sine";
		case EaseFamily::Expo: return "expo";
		case EaseFamily::Back: return "back";
	}
	return "linear";
}


std::string
Curve::ToString() const
{
	char buffer[96];
	switch (fKind) {
		case Kind::Spring:
			std::snprintf(buffer, sizeof(buffer), "spring(%g, %g)", fStiffness,
				fDamping);
			return buffer;
		case Kind::Bezier:
			std::snprintf(buffer, sizeof(buffer), "cubic-bezier(%g, %g, %g, %g)",
				fBezierParams[0], fBezierParams[1], fBezierParams[2],
				fBezierParams[3]);
			return buffer;
		case Kind::Easing:
			if (fFamily == EaseFamily::Linear)
				return "linear";
			std::string mode = fMode == EaseMode::In ? "ease-in-"
				: fMode == EaseMode::Out ? "ease-out-" : "ease-in-out-";
			return mode + FamilyName(fFamily);
	}
	return "linear";
}


static bool
ParseNumbers(const std::string& args, std::vector<double>& out)
{
	out.clear();
	std::string current;
	for (size_t i = 0; i <= args.size(); i++) {
		char c = i < args.size() ? args[i] : ',';
		if (c == ',') {
			char* end = nullptr;
			std::string trimmed;
			for (char d : current) {
				if (d != ' ' && d != '\t')
					trimmed += d;
			}
			if (trimmed.empty())
				return false;
			double value = std::strtod(trimmed.c_str(), &end);
			if (end == nullptr || *end != '\0')
				return false;
			out.push_back(value);
			current.clear();
		} else
			current += c;
	}
	return true;
}


bool
ParseCurve(const std::string& spec, Curve& out, std::string* error)
{
	auto fail = [&](const std::string& message) {
		if (error)
			*error = message;
		return false;
	};
	if (spec == "linear") {
		out = Curve::Linear();
		return true;
	}
	if (spec == "ease") {
		out = Curve::Bezier(0.25, 0.1, 0.25, 1.0);
		return true;
	}
	if (spec == "ease-in-out") {
		out = Curve::Bezier(0.42, 0, 0.58, 1.0);
		return true;
	}
	if (spec == "spring") {
		out = Curve::MakeSpring(420, 0.86);
		return true;
	}
	size_t open = spec.find('(');
	if (open != std::string::npos) {
		if (spec.back() != ')')
			return fail("missing ')'");
		std::string name = spec.substr(0, open);
		std::vector<double> numbers;
		if (!ParseNumbers(spec.substr(open + 1, spec.size() - open - 2),
				numbers))
			return fail("bad numeric arguments");
		if (name == "cubic-bezier") {
			if (numbers.size() != 4)
				return fail("cubic-bezier needs 4 numbers");
			if (numbers[0] < 0 || numbers[0] > 1 || numbers[2] < 0
				|| numbers[2] > 1)
				return fail("cubic-bezier x values must be in [0,1]");
			out = Curve::Bezier(numbers[0], numbers[1], numbers[2], numbers[3]);
			return true;
		}
		if (name == "spring") {
			if (numbers.size() != 2)
				return fail("spring needs (stiffness, dampingRatio)");
			if (numbers[0] <= 0 || numbers[1] <= 0)
				return fail("spring parameters must be positive");
			out = Curve::MakeSpring(numbers[0], numbers[1]);
			return true;
		}
		return fail("unknown curve function '" + name + "'");
	}
	struct ModeName { const char* prefix; EaseMode mode; };
	const ModeName modes[] = {
		{"ease-in-out-", EaseMode::InOut},
		{"ease-in-", EaseMode::In},
		{"ease-out-", EaseMode::Out},
	};
	const EaseFamily families[] = {EaseFamily::Quad, EaseFamily::Cubic,
		EaseFamily::Quart, EaseFamily::Quint, EaseFamily::Sine,
		EaseFamily::Expo, EaseFamily::Back};
	for (const ModeName& m : modes) {
		std::string prefix(m.prefix);
		if (spec.compare(0, prefix.size(), prefix) != 0)
			continue;
		std::string family = spec.substr(prefix.size());
		for (EaseFamily f : families) {
			if (family == FamilyName(f)) {
				out = Curve::Easing(f, m.mode);
				return true;
			}
		}
	}
	return fail("unknown curve '" + spec + "'");
}


std::vector<std::string>
KnownCurveNames()
{
	std::vector<std::string> names = {"linear", "ease", "ease-in-out", "spring"};
	const char* families[] = {"quad", "cubic", "quart", "quint", "sine", "expo",
		"back"};
	for (const char* mode : {"ease-in-", "ease-out-", "ease-in-out-"}) {
		for (const char* family : families)
			names.push_back(std::string(mode) + family);
	}
	return names;
}

}	// namespace anim
}	// namespace lumina
