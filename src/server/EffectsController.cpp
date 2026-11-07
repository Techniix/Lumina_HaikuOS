/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "EffectsController.h"

#include <Autolock.h>
#include <Bitmap.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Roster.h>
#include <Screen.h>

#include <lumina/ConfigStore.h>
#include <lumina/Log.h>
#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>
#include <lumina/haiku/VisualEffects.h>

using namespace lumina::haiku;

namespace lumina {
namespace server {

namespace {
const size_t kMaxCachedWindows = 24;

BRect
OuterFrame(const WindowSnapshot& w)
{
	return BRect(w.frame.left - w.borderSize,
		w.frame.top - w.tabHeight - w.borderSize,
		w.frame.right + w.borderSize, w.frame.bottom + w.borderSize);
}

BBitmap*
IconFor(const char* signature)
{
	entry_ref ref;
	if (signature == NULL || be_roster->FindApp(signature, &ref) != B_OK)
		return NULL;
	BBitmap* icon = new BBitmap(BRect(0, 0, 63, 63), B_RGBA32);
	if (BNodeInfo::GetTrackerIcon(&ref, icon, (icon_size)64) != B_OK) {
		delete icon;
		return NULL;
	}
	return icon;
}
}	// namespace


EffectsController::EffectsController(EngineLink* engine)
	:
	fEngine(engine),
	fLock("effects"),
	fMeasured(false),
	fTransitions(0)
{
	BRect s = BScreen().Frame();
	fLauncherAnchor = BRect(s.left + 4, s.bottom - 30, s.left + 44, s.bottom - 2);
}


EffectsController::~EffectsController()
{
	for (auto& entry : fWindowImages)
		delete entry.second;
	for (auto& entry : fWorkspaceImages)
		delete entry.second;
}


void
EffectsController::Update(const ConfigStore& config, bool engineActive,
	bool remeasure)
{
	fx::EffectProfile profile = fx::EffectProfile::FromConfig(config);
	fx::Capabilities caps = fCaps;
	if (!fMeasured || remeasure) {
		// Only measure when effects could use the result (costs ~10 ms).
		if (profile.quality != fx::Quality::Off || profile.autoQuality) {
			caps = VisualEffects::MeasureCapabilities(engineActive);
			fMeasured = true;
		}
	}
	caps.engineLoaded = caps.engineAnimation = engineActive;

	BAutolock _(fLock);
	fProfile = profile;
	fCaps = caps;
	fGovernor.SetBudget(profile.frameBudgetMs);
	fGovernor.SetCeiling(profile.autoQuality ? fx::Quality::High
		: profile.quality);
	fPlan = fx::SelectPlan(fProfile, fCaps, fGovernor.Current());
	LUMINA_LOG_INFO("fx", "plan: quality=%s snap=%s minimize=%s glass=%s "
		"workspace=%s (%s)", fx::QualityName(fPlan.quality),
		fx::GeometryStrategyName(fPlan.snapGeometry),
		fx::GeometryStrategyName(fPlan.minimizeGeometry),
		fx::GlassStrategyName(fPlan.glass),
		fPlan.workspaceProxy ? "slide" : "instant", fPlan.reason.c_str());
}


bool
EffectsController::WantsOpenTransitions() const
{
	BAutolock _(fLock);
	return fProfile.proxies && fProfile.proxyOpen && fCaps.engineLoaded
		&& fPlan.quality >= fx::Quality::Medium;
}


void
EffectsController::_RecordFrame(double ms)
{
	BAutolock _(fLock);
	fx::Quality before = fGovernor.Current();
	fGovernor.Record(ms, system_time() / 1e6);
	if (fGovernor.Current() != before) {
		fPlan = fx::SelectPlan(fProfile, fCaps, fGovernor.Current());
		LUMINA_LOG_WARN("fx", "governor: quality %s -> %s (avg %.2f ms)",
			fx::QualityName(before), fx::QualityName(fGovernor.Current()),
			fGovernor.AverageMs());
	}
}


void
EffectsController::_CacheImage(int32 token, BBitmap* image)
{
	BAutolock _(fLock);
	auto it = fWindowImages.find(token);
	if (it != fWindowImages.end()) {
		delete it->second;
		fWindowImages.erase(it);
	}
	if (image == NULL)
		return;
	if (fWindowImages.size() >= kMaxCachedWindows) {
		delete fWindowImages.begin()->second;
		fWindowImages.erase(fWindowImages.begin());
	}
	fWindowImages[token] = image;
}


BBitmap*
EffectsController::_CachedCopy(int32 token)
{
	BAutolock _(fLock);
	auto it = fWindowImages.find(token);
	if (it == fWindowImages.end())
		return NULL;
	return new BBitmap(it->second);
}


void
EffectsController::Minimize(int32 token, BRect anchor)
{
	fx::EffectPlan plan;
	{
		BAutolock _(fLock);
		plan = fPlan;
	}
	WindowSnapshot window;
	bool known = PrivateApi::GetWindow(token, window) == B_OK;
	if (!known || plan.minimizeGeometry != fx::GeometryStrategy::Proxy
		|| !anchor.IsValid() || window.minimized) {
		PrivateApi::MinimizeWindow(token, true);
		return;
	}
	BRect outer = OuterFrame(window);
	double captureMs = 0;
	BBitmap* image = VisualEffects::CaptureScreen(outer, &captureMs);
	if (image == NULL) {
		PrivateApi::MinimizeWindow(token, true);
		return;
	}
	_CacheImage(token, new BBitmap(image));
	_RecordFrame(captureMs);

	VisualEffects::ProxySpec spec;
	spec.from = outer;
	spec.to = anchor;
	spec.durationMs = plan.durationMs > 0 ? plan.durationMs : 220;
	spec.curve = anim::Curve::Easing(anim::EaseFamily::Cubic,
		anim::EaseMode::In);
	spec.image = image;
	spec.settleMs = 0;
	spec.onCovered = [token]() { PrivateApi::MinimizeWindow(token, true); };
	spec.onFrame = [this](double ms) { _RecordFrame(ms); };
	if (VisualEffects::RunProxyTransition(spec) != B_OK)
		PrivateApi::MinimizeWindow(token, true);
	fTransitions++;
}


void
EffectsController::Restore(int32 token, BRect anchor)
{
	fx::EffectPlan plan;
	{
		BAutolock _(fLock);
		plan = fPlan;
	}
	WindowSnapshot window;
	bool known = PrivateApi::GetWindow(token, window) == B_OK;
	if (!known || plan.minimizeGeometry != fx::GeometryStrategy::Proxy
		|| !anchor.IsValid() || !window.minimized) {
		PrivateApi::BringToFront(token);
		return;
	}
	VisualEffects::ProxySpec spec;
	spec.from = anchor;
	spec.to = OuterFrame(window);
	spec.durationMs = plan.durationMs > 0 ? plan.durationMs : 220;
	spec.image = _CachedCopy(token);
	if (spec.image == NULL)
		spec.icon = IconFor(window.signature.String());
	spec.settleMs = 40;	// the client repaints beneath the proxy
	spec.onArrived = [token]() { PrivateApi::BringToFront(token); };
	spec.onFrame = [this](double ms) { _RecordFrame(ms); };
	if (VisualEffects::RunProxyTransition(spec) != B_OK)
		PrivateApi::BringToFront(token);
	fTransitions++;
}


void
EffectsController::SwitchWorkspace(int32 index)
{
	int32 current = current_workspace();
	if (index < 0 || index >= count_workspaces() || index == current)
		return;
	fx::EffectPlan plan;
	std::string transition;
	{
		BAutolock _(fLock);
		plan = fPlan;
		transition = fProfile.workspaceTransition;
	}
	if (!plan.workspaceProxy) {
		activate_workspace(index);
		return;
	}
	double captureMs = 0;
	BBitmap* now = VisualEffects::CaptureScreen(BScreen().Frame(), &captureMs);
	if (now == NULL) {
		activate_workspace(index);
		return;
	}
	_RecordFrame(captureMs);
	BBitmap* next = NULL;
	{
		BAutolock _(fLock);
		auto old = fWorkspaceImages.find(current);
		if (old != fWorkspaceImages.end())
			delete old->second;
		fWorkspaceImages[current] = new BBitmap(now);
		auto cached = fWorkspaceImages.find(index);
		if (cached != fWorkspaceImages.end())
			next = new BBitmap(cached->second);
	}
	VisualEffects::SlideSpec spec;
	spec.current = now;
	spec.next = next;	// NULL on first visit: desktop colour slides in
	spec.forward = index > current;
	spec.durationMs = plan.durationMs > 0 ? plan.durationMs + 60 : 260;
	spec.onCovered = [index]() { activate_workspace(index); };
	spec.onFrame = [this](double ms) { _RecordFrame(ms); };
	if (VisualEffects::RunWorkspaceSlide(spec) != B_OK)
		activate_workspace(index);
	fTransitions++;
}


void
EffectsController::_Unpark(int32 token)
{
	BMessage unpark(LUMINA_ENGINE_COMMAND);
	unpark.AddString(LUMINA_ENGINE_FIELD_OP, "unpark");
	unpark.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
	fEngine->Send(unpark);
}


void
EffectsController::WindowParked(int32 token, BRect frame, const char* signature)
{
	if (!WantsOpenTransitions()) {
		_Unpark(token);
		return;
	}
	fx::EffectPlan plan;
	{
		BAutolock _(fLock);
		plan = fPlan;
	}
	VisualEffects::ProxySpec spec;
	spec.from = fLauncherAnchor;
	// The parked event reports the content frame; approximate decoration
	// with the default border/tab metrics so the placeholder lands on the
	// whole window.
	spec.to = BRect(frame.left - 5, frame.top - 26, frame.right + 5,
		frame.bottom + 5);
	spec.durationMs = plan.durationMs > 0 ? plan.durationMs : 200;
	spec.icon = IconFor(signature);
	spec.settleMs = 50;
	spec.onArrived = [this, token]() { _Unpark(token); };
	spec.onFrame = [this](double ms) { _RecordFrame(ms); };
	if (VisualEffects::RunProxyTransition(spec) != B_OK)
		_Unpark(token);
	fTransitions++;
}


void
EffectsController::WindowGone(int32 token)
{
	_CacheImage(token, NULL);
}


void
EffectsController::Describe(BMessage& out) const
{
	BAutolock _(fLock);
	out.AddString("fx_quality", fx::QualityName(fPlan.quality));
	out.AddString("fx_glass", fx::GlassStrategyName(fPlan.glass));
	out.AddString("fx_minimize", fx::GeometryStrategyName(fPlan.minimizeGeometry));
	out.AddString("fx_snap", fx::GeometryStrategyName(fPlan.snapGeometry));
	out.AddBool("fx_workspace_slide", fPlan.workspaceProxy);
	out.AddString("fx_reason", fPlan.reason.c_str());
	out.AddDouble("fx_readback_ms_per_mp", fCaps.readbackMsPerMegapixel);
	out.AddInt32("fx_governor_downgrades", fGovernor.Downgrades());
	out.AddUInt64("fx_transitions", fTransitions);
}

}	// namespace server
}	// namespace lumina
