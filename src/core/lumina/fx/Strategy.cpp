/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Strategy.h"

#include <algorithm>
#include <numeric>

#include "../ConfigSchema.h"
#include "../ConfigStore.h"

namespace lumina {
namespace fx {

const char*
QualityName(Quality q)
{
	switch (q) {
		case Quality::Off: return "off";
		case Quality::Low: return "low";
		case Quality::Medium: return "medium";
		case Quality::High: return "high";
	}
	return "off";
}


bool
ParseQuality(const std::string& name, Quality& q, bool& isAuto)
{
	isAuto = false;
	if (name == "auto") {
		isAuto = true;
		q = Quality::High;
		return true;
	}
	for (int i = 0; i <= int(Quality::High); i++) {
		if (name == QualityName(Quality(i))) {
			q = Quality(i);
			return true;
		}
	}
	return false;
}


const char*
GeometryStrategyName(GeometryStrategy s)
{
	switch (s) {
		case GeometryStrategy::Instant: return "instant";
		case GeometryStrategy::EngineAnimated: return "engine";
		case GeometryStrategy::Proxy: return "proxy";
	}
	return "instant";
}


const char*
GlassStrategyName(GlassStrategy s)
{
	switch (s) {
		case GlassStrategy::Solid: return "solid";
		case GlassStrategy::Wallpaper: return "wallpaper";
		case GlassStrategy::Edge: return "edge";
		case GlassStrategy::Capture: return "capture";
	}
	return "solid";
}


bool
ParseGlassStrategy(const std::string& name, GlassStrategy& s)
{
	for (int i = 0; i <= int(GlassStrategy::Capture); i++) {
		if (name == GlassStrategyName(GlassStrategy(i))) {
			s = GlassStrategy(i);
			return true;
		}
	}
	return false;
}


EffectProfile
EffectProfile::FromConfig(const ConfigStore& config)
{
	EffectProfile p;
	ParseQuality(config.GetString("fx.quality", "off"), p.quality,
		p.autoQuality);
	p.frameBudgetMs = config.GetNumber("fx.frameBudgetMs", 4);

	p.animateWindows = config.GetBool("windows.animation.enabled", false);
	std::string curve = config.GetString("windows.animation.curve",
		"ease-out-cubic");
	if (curve == "spring") {
		p.curve = anim::Curve::MakeSpring(
			config.GetNumber("windows.animation.spring.stiffness", 420),
			config.GetNumber("windows.animation.spring.damping", 0.86));
	} else if (!anim::ParseCurve(curve, p.curve))
		p.curve = anim::Curve::Easing(anim::EaseFamily::Cubic,
			anim::EaseMode::Out);
	p.durationMs = config.GetNumber("windows.animation.durationMs", 180);
	p.maxFps = int(config.GetNumber("windows.animation.maxFps", 60));
	p.resizeMode = config.GetString("windows.animation.resizeMode", "throttled");

	ParseGlassStrategy(config.GetString("fx.panelGlass.mode", "solid"),
		p.glass);
	p.glassParams.blurRadius = int(config.GetNumber("fx.panelGlass.blurRadius",
		12));
	uint8_t r = 220, g = 220, b = 220, a = 255;
	ParseColor(config.GetString("fx.panelGlass.tint", "#dcdcdc"), r, g, b, a);
	p.glassParams.tint = Color::RGB(r, g, b, a);
	p.glassParams.tintAmount = config.GetNumber("fx.panelGlass.tintAmount",
		0.55);
	p.glassParams.saturation = config.GetNumber("fx.panelGlass.saturation",
		1.2);
	p.glassRefreshHz = config.GetNumber("fx.panelGlass.refreshHz", 8);

	p.proxies = config.GetBool("fx.proxies.enabled", false);
	p.proxyMinimize = config.GetBool("fx.proxies.minimize", true);
	p.proxyOpen = config.GetBool("fx.proxies.open", false);
	p.workspaceTransition = config.GetString("fx.proxies.workspace", "none");
	p.proxyDurationMs = config.GetNumber("fx.proxies.durationMs", 220);
	return p;
}


EffectPlan
SelectPlan(const EffectProfile& profile, const Capabilities& caps,
	Quality governed)
{
	EffectPlan plan;
	Quality q = profile.autoQuality ? governed
		: Quality(std::min(int(profile.quality), int(governed)));
	plan.quality = q;
	plan.glassParams = profile.glassParams;
	plan.maxFps = profile.maxFps;
	plan.resizeMode = profile.resizeMode;

	if (q == Quality::Off) {
		plan.reason = profile.quality == Quality::Off && !profile.autoQuality
			? "effects disabled" : "governor disabled effects (over budget)";
		return plan;
	}

	// --- window geometry
	if (profile.animateWindows) {
		if (caps.engineLoaded && caps.engineAnimation) {
			plan.snapGeometry = GeometryStrategy::EngineAnimated;
			plan.durationMs = profile.durationMs;
			if (q == Quality::Low) {
				plan.durationMs *= 0.6;
				plan.maxFps = std::min(plan.maxFps, 30);
				plan.resizeMode = "final";
			} else if (q == Quality::Medium && plan.resizeMode == "live")
				plan.resizeMode = "throttled";
		} else
			plan.reason += "window animation needs the Lumina engine; ";
	}

	// --- minimize/restore transitions
	if (profile.proxies && profile.proxyMinimize) {
		if (caps.screenReadback && q >= Quality::Medium)
			plan.minimizeGeometry = GeometryStrategy::Proxy;
		else if (!caps.screenReadback)
			plan.reason += "proxies need screen readback; ";
	}
	plan.workspaceProxy = profile.proxies
		&& profile.workspaceTransition != "none"
		&& caps.screenReadback && q >= Quality::Medium;

	// --- panel glass, degrading capture → edge → wallpaper → solid
	GlassStrategy glass = profile.glass;
	bool readbackCheap = caps.screenReadback
		&& caps.readbackMsPerMegapixel >= 0
		&& caps.readbackMsPerMegapixel < 40;
	if (glass == GlassStrategy::Capture
		&& !(readbackCheap && q == Quality::High)) {
		glass = GlassStrategy::Edge;
		plan.reason += "capture glass degraded to edge; ";
	}
	if (glass == GlassStrategy::Edge
		&& !(caps.screenReadback && q >= Quality::Medium)) {
		glass = GlassStrategy::Wallpaper;
		plan.reason += "edge glass degraded to wallpaper; ";
	}
	plan.glass = glass;
	switch (q) {
		case Quality::Low:
			plan.glassParams.downsample = 3;
			plan.glassRefreshHz = 0;	// static, refresh on change only
			break;
		case Quality::Medium:
			plan.glassParams.downsample = 2;
			plan.glassRefreshHz = std::min(profile.glassRefreshHz, 4.0);
			break;
		case Quality::High:
			plan.glassParams.downsample = 1;
			plan.glassRefreshHz = profile.glassRefreshHz;
			break;
		case Quality::Off:
			break;
	}
	if (plan.reason.empty())
		plan.reason = "ok";
	return plan;
}


// #pragma mark - Governor


Governor::Governor(double budgetMs, Quality ceiling)
	:
	fBudgetMs(budgetMs),
	fCeiling(ceiling),
	fCurrent(ceiling)
{
}


void
Governor::SetCeiling(Quality q)
{
	fCeiling = q;
	if (int(fCurrent) > int(q))
		fCurrent = q;
}


double
Governor::AverageMs() const
{
	if (fWindow.empty())
		return 0;
	return std::accumulate(fWindow.begin(), fWindow.end(), 0.0)
		/ double(fWindow.size());
}


void
Governor::Record(double frameMs, double now)
{
	fWindow.push_back(frameMs);
	while (fWindow.size() > 30)
		fWindow.pop_front();

	if (frameMs > fBudgetMs)
		fOverCount++;
	else
		fOverCount = std::max(0, fOverCount - 1);

	// Downgrade: sustained overruns (8 net over-budget frames), at most
	// once per 0.5 s so one level has time to show its effect.
	if (fOverCount >= 8 && fCurrent != Quality::Off
		&& now - fLastChange > 0.5) {
		fCurrent = Quality(int(fCurrent) - 1);
		fOverCount = 0;
		fCheapSince = -1;
		fLastChange = now;
		fDowngrades++;
		return;
	}

	// Upgrade: 5 s of average cost below half the budget.
	if (AverageMs() < fBudgetMs / 2 && fWindow.size() >= 10) {
		if (fCheapSince < 0)
			fCheapSince = now;
		if (now - fCheapSince > 5 && int(fCurrent) < int(fCeiling)) {
			fCurrent = Quality(int(fCurrent) + 1);
			fCheapSince = now;
			fLastChange = now;
		}
	} else
		fCheapSince = -1;
}

}	// namespace fx
}	// namespace lumina
