/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Visual Effects Engine — policy layer (portable, no Haiku APIs).
 *
 * Effects are described by an EffectProfile (what the user asked for), the
 * runtime Capabilities (what this machine/session can do, measured by the
 * Haiku backend), and the Governor's current quality level (what we can
 * afford right now). SelectPlan() turns those into concrete strategies.
 * Backends (engine geometry, proxy windows, glass renderers) only execute
 * plans; they never decide policy.
 */
#ifndef LUMINA_CORE_FX_STRATEGY_H
#define LUMINA_CORE_FX_STRATEGY_H

#include <deque>
#include <string>

#include "../anim/Easing.h"
#include "Glass.h"

namespace lumina {

class ConfigStore;

namespace fx {

enum class Quality { Off = 0, Low, Medium, High };

const char* QualityName(Quality q);
bool ParseQuality(const std::string& name, Quality& q, bool& isAuto);

enum class GeometryStrategy {
	Instant,			// apply final geometry immediately
	EngineAnimated,		// Window Engine interpolates the real window
	Proxy,				// bitmap stand-in animates; real window jumps once
};

enum class GlassStrategy { Solid, Wallpaper, Edge, Capture };

const char* GeometryStrategyName(GeometryStrategy s);
const char* GlassStrategyName(GlassStrategy s);
bool ParseGlassStrategy(const std::string& name, GlassStrategy& s);

struct Capabilities {
	bool	engineLoaded = false;		// Lumina decorator add-on active
	bool	engineAnimation = false;	// engine reported animation support
	bool	screenReadback = false;		// BScreen::ReadBitmap works
	double	readbackMsPerMegapixel = -1;	// measured; <0 = unknown
	int		cpuCount = 1;
	bool	lowMemory = false;
};

struct EffectProfile {
	Quality		quality = Quality::Off;
	bool		autoQuality = false;
	double		frameBudgetMs = 4;

	// window geometry
	bool		animateWindows = false;
	anim::Curve	curve = anim::Curve::Easing(anim::EaseFamily::Cubic,
					anim::EaseMode::Out);
	double		durationMs = 180;
	int			maxFps = 60;
	std::string	resizeMode = "throttled";

	// panel glass
	GlassStrategy glass = GlassStrategy::Solid;
	GlassParams	glassParams;
	double		glassRefreshHz = 8;

	// proxies
	bool		proxies = false;
	bool		proxyMinimize = true;
	bool		proxyOpen = false;
	std::string	workspaceTransition = "none";
	double		proxyDurationMs = 220;

	static EffectProfile FromConfig(const ConfigStore& config);
};

struct EffectPlan {
	Quality				quality = Quality::Off;
	GeometryStrategy	snapGeometry = GeometryStrategy::Instant;
	GeometryStrategy	minimizeGeometry = GeometryStrategy::Instant;
	GlassStrategy		glass = GlassStrategy::Solid;
	GlassParams			glassParams;
	double				glassRefreshHz = 0;
	double				durationMs = 0;
	int					maxFps = 60;
	std::string			resizeMode = "throttled";
	bool				workspaceProxy = false;
	std::string			reason;		// human-readable explanation
};

EffectPlan SelectPlan(const EffectProfile& profile, const Capabilities& caps,
	Quality governed);

// Frame-time governor with hysteresis. Feed it the measured CPU cost of each
// effect frame; it lowers quality when the budget is exceeded repeatedly and
// raises it again after a sustained cheap period.
class Governor {
public:
	explicit Governor(double budgetMs = 4, Quality ceiling = Quality::High);

	void SetBudget(double ms) { fBudgetMs = ms; }
	void SetCeiling(Quality q);
	void Record(double frameMs, double nowSeconds);
	Quality Current() const { return fCurrent; }
	double AverageMs() const;
	int Downgrades() const { return fDowngrades; }

private:
	double			fBudgetMs;
	Quality			fCeiling;
	Quality			fCurrent;
	std::deque<double> fWindow;
	int				fOverCount = 0;
	double			fCheapSince = -1;
	double			fLastChange = -1e9;
	int				fDowngrades = 0;
};

}	// namespace fx
}	// namespace lumina

#endif
