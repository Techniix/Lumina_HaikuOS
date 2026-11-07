/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Runs the Visual Effects Engine inside lumina_server: owns the effect plan
 * (policy from lumina_core), the governor, the capability measurement and
 * the transition caches, and executes transitions through the Haiku
 * backends in liblumina (VisualEffects.h).
 *
 * Every entry point has an instant fallback, so a disabled, degraded or
 * failing effect never blocks the underlying action (minimize, restore,
 * workspace switch, window open).
 */
#ifndef LUMINA_SERVER_EFFECTS_CONTROLLER_H
#define LUMINA_SERVER_EFFECTS_CONTROLLER_H

#include <Locker.h>
#include <Rect.h>

#include <map>

#include <lumina/fx/Strategy.h>

class BBitmap;
class BMessage;

namespace lumina {
class ConfigStore;
namespace haiku { class EngineLink; }

namespace server {

class EffectsController {
public:
	EffectsController(haiku::EngineLink* engine);
	~EffectsController();

	// Re-reads the profile and recomputes the plan; measures capabilities
	// the first time (or when `remeasure`).
	void Update(const ConfigStore& config, bool engineActive,
		bool remeasure = false);
	const fx::EffectPlan& Plan() const { return fPlan; }
	const fx::Capabilities& Caps() const { return fCaps; }
	bool WantsOpenTransitions() const;

	// Actions with optional effects. `anchor` is the on-screen rectangle the
	// effect flies to/from (a panel button, the launcher …); invalid → none.
	void Minimize(int32 token, BRect anchor);
	void Restore(int32 token, BRect anchor);
	void SwitchWorkspace(int32 index);
	// The engine parked a new window (open transition): animate, unpark.
	void WindowParked(int32 token, BRect frame, const char* signature);
	void WindowGone(int32 token);

	void Describe(BMessage& out) const;

private:
	void _RecordFrame(double ms);
	void _CacheImage(int32 token, BBitmap* image);
	BBitmap* _CachedCopy(int32 token);
	void _Unpark(int32 token);

	haiku::EngineLink*	fEngine;
	mutable BLocker		fLock;
	fx::EffectProfile	fProfile;
	fx::Capabilities	fCaps;
	bool				fMeasured;
	fx::Governor		fGovernor;
	fx::EffectPlan		fPlan;
	BRect				fLauncherAnchor;
	std::map<int32, BBitmap*> fWindowImages;	// minimize cache
	std::map<int32, BBitmap*> fWorkspaceImages;	// workspace cache
	uint64				fTransitions;
};

}	// namespace server
}	// namespace lumina

#endif
