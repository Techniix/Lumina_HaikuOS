/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * EXPERIMENT C — animating *real* application windows through the Window
 * Engine (app_server add-on, thread-free, client-clocked).
 *
 * For each sequence the prototype is the frame clock (one "tick" per frame,
 * like lumina_server's EngineClock) while a sampler thread polls the
 * window's real frame from app_server every 2 ms. Measured:
 *   - achieved frame rate (distinct frames observed)
 *   - tick interval jitter
 *   - lag: observed position vs the curve's prediction at the same time
 *   - landing: final frame == requested target
 *   - client cost: Draw()/FrameResized() calls in the animated window
 *     (move-only should cost the client ~nothing: app_server blits)
 *
 * Sequences: move (ease-out-cubic), move (spring), move+resize with
 * resize modes live / throttled / final, and a mid-flight retarget.
 *
 * Requires the Lumina decorator to be selected (`lumina engine`).
 */
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <cmath>
#include <vector>

#include <Application.h>
#include <Locker.h>
#include <Screen.h>

#include <lumina/anim/Timeline.h>
#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>

#include "../common/FxHarness.h"

using namespace lumina;
using namespace lumina::fxlab;
using namespace lumina::haiku;

namespace {

struct Sample {
	double	ms;
	BRect	frame;
};

class Sampler {
public:
	Sampler(int32 token) : fToken(token), fRun(false), fThread(-1) {}

	void Start()
	{
		fSamples.clear();
		fRun = true;
		fThread = spawn_thread(_Entry, "sampler", B_URGENT_DISPLAY_PRIORITY,
			this);
		resume_thread(fThread);
	}

	std::vector<Sample> Stop()
	{
		fRun = false;
		status_t result;
		wait_for_thread(fThread, &result);
		return fSamples;
	}

private:
	static status_t _Entry(void* data)
	{
		Sampler* self = static_cast<Sampler*>(data);
		while (self->fRun) {
			WindowSnapshot w;
			if (PrivateApi::GetWindow(self->fToken, w) == B_OK)
				self->fSamples.push_back({NowMs(), w.frame});
			snooze(2000);
		}
		return B_OK;
	}

	int32				fToken;
	std::atomic<bool>	fRun;
	thread_id			fThread;
	std::vector<Sample>	fSamples;
};


struct Sequence {
	const char*	name;
	BRect		target;
	float		durationMs;
	const char*	curve;
	const char*	resizeMode;
	bool		retarget;
};


Value
RunSequence(EngineLink& link, PatternWindow* window, int32 token,
	const Sequence& sequence, BRect start)
{
	// Reset position instantly.
	link.MoveResize(token, start);
	snooze(200000);
	window->ResetCounters();

	anim::Curve curve;
	anim::ParseCurve(sequence.curve, curve);
	double expectedMs = curve.Duration(sequence.durationMs / 1000.0) * 1000;

	Sampler sampler(token);
	sampler.Start();
	double t0 = NowMs();
	link.Animate(token, sequence.target, sequence.durationMs / 1000.0f,
		sequence.curve, sequence.resizeMode, 60);

	// Be the clock.
	std::vector<double> tickTimes;
	BRect retargetTo = sequence.target.OffsetByCopy(-300, 120);
	bool retargeted = false;
	double end = expectedMs + 250;
	bigtime_t next = system_time();
	while (NowMs() - t0 < end) {
		BMessage tick(LUMINA_ENGINE_COMMAND);
		tick.AddString(LUMINA_ENGINE_FIELD_OP, "tick");
		link.Send(tick);
		tickTimes.push_back(NowMs());
		if (sequence.retarget && !retargeted && NowMs() - t0 > expectedMs * 0.4) {
			link.Animate(token, retargetTo, sequence.durationMs / 1000.0f,
				sequence.curve, sequence.resizeMode, 60);
			retargeted = true;
		}
		next += 16667;
		snooze_until(next, B_SYSTEM_TIMEBASE);
	}
	std::vector<Sample> samples = sampler.Stop();

	// Distinct frames observed while moving.
	int distinct = 0;
	double firstMove = -1, lastMove = -1;
	for (size_t i = 1; i < samples.size(); i++) {
		if (samples[i].frame != samples[i - 1].frame) {
			distinct++;
			if (firstMove < 0)
				firstMove = samples[i].ms;
			lastMove = samples[i].ms;
		}
	}
	double movingMs = lastMove > firstMove ? lastMove - firstMove : 0;
	std::vector<double> intervals;
	for (size_t i = 1; i < tickTimes.size(); i++)
		intervals.push_back(tickTimes[i] - tickTimes[i - 1]);

	// Lag vs prediction (move sequences only, before any retarget).
	std::vector<double> lag;
	if (!sequence.retarget) {
		anim::RectAnimation predicted({start.left, start.top, start.right,
			start.bottom}, {sequence.target.left, sequence.target.top,
			sequence.target.right, sequence.target.bottom}, curve,
			sequence.durationMs / 1000.0, t0 / 1000.0);
		for (const Sample& sample : samples) {
			RectF p = predicted.Sample(sample.ms / 1000.0);
			lag.push_back(std::fabs(p.left - sample.frame.left)
				+ std::fabs(p.top - sample.frame.top));
		}
	}

	WindowSnapshot final;
	PrivateApi::GetWindow(token, final);
	BRect wanted = sequence.retarget ? retargetTo : sequence.target;
	bool landed = std::fabs(final.frame.left - wanted.left) < 1
		&& std::fabs(final.frame.top - wanted.top) < 1
		&& std::fabs(final.frame.Width() - wanted.Width()) < 1
		&& std::fabs(final.frame.Height() - wanted.Height()) < 1;

	Value result = Value::MakeObject();
	result["curve"] = sequence.curve;
	result["resizeMode"] = sequence.resizeMode;
	result["expectedMs"] = expectedMs;
	result["distinctFrames"] = distinct;
	result["movingMs"] = movingMs;
	result["achievedFps"] = movingMs > 0 ? distinct * 1000.0 / movingMs : 0;
	result["tickIntervalMs"] = Stats::Of(intervals).ToValue();
	if (!lag.empty())
		result["lagPixels"] = Stats::Of(lag).ToValue();
	result["landed"] = landed;
	result["clientDraws"] = window->DrawCount();
	result["clientResizes"] = window->ResizeCount();
	printf("%-18s fps %.1f frames %d draws %d resizes %d landed %s\n",
		sequence.name, result["achievedFps"].AsNumber(), distinct,
		int(window->DrawCount()), int(window->ResizeCount()),
		landed ? "yes" : "NO");
	return result;
}


void
RunSelfTest()
{
	Report report("fx-window-anim");
	EngineLink link;
	EngineInfo info;
	link.Probe(info);
	report.Set("engine.present", info.present);
	report.Set("engine.active", info.active);
	report.Set("engine.reason", info.reason.String());
	if (!info.present || !info.active) {
		report.Verdict("engine available", false,
			"select the Lumina decorator first (lumina mode lumina)");
		report.Save();
		be_app->PostMessage(B_QUIT_REQUESTED);
		return;
	}

	BRect start(120, 120, 520, 420);
	PatternWindow* window = new PatternWindow(start, "fx-window-anim subject");
	window->Show();
	snooze(400000);
	int32 token = window->Token();
	report.Set("token", token);

	BScreen screen;
	BRect s = screen.Frame();
	BRect far(s.right - 520, s.bottom - 420, s.right - 120, s.bottom - 120);
	BRect half(s.left + 1, s.top + 24, std::floor(s.Width() / 2) - 1,
		s.bottom - 1);

	const Sequence sequences[] = {
		{"move-ease", far, 300, "ease-out-cubic", "final", false},
		{"move-spring", far, 0, "spring(420, 0.8)", "final", false},
		{"resize-live", half, 300, "ease-out-cubic", "live", false},
		{"resize-throttled", half, 300, "ease-out-cubic", "throttled", false},
		{"resize-final", half, 300, "ease-out-cubic", "final", false},
		{"retarget-spring", far, 0, "spring(300, 0.9)", "final", true},
	};
	bool allLanded = true;
	double moveFps = 0;
	int liveResizes = 0, finalResizes = 0;
	for (const Sequence& sequence : sequences) {
		Value result = RunSequence(link, window, token, sequence, start);
		allLanded &= result["landed"].AsBool();
		if (strcmp(sequence.name, "move-ease") == 0)
			moveFps = result["achievedFps"].AsNumber();
		if (strcmp(sequence.name, "resize-live") == 0)
			liveResizes = int(result["clientResizes"].AsInt());
		if (strcmp(sequence.name, "resize-final") == 0)
			finalResizes = int(result["clientResizes"].AsInt());
		report.Set(std::string("sequences.") + sequence.name, result);
	}

	EngineInfo after;
	link.Probe(after);
	report.Set("engine.stats", after.statsJson.String());

	report.Verdict("all animations land exactly", allLanded, "final frames "
		"match targets");
	report.Verdict("move animations reach >= 50 fps", moveFps >= 50,
		std::to_string(moveFps) + " fps observed");
	report.Verdict("final-mode resize spares the client", finalResizes
		<= std::max(2, liveResizes / 4), "client resizes: live "
		+ std::to_string(liveResizes) + ", final "
		+ std::to_string(finalResizes));
	report.Save();
	window->Lock();
	window->Quit();
	be_app->PostMessage(B_QUIT_REQUESTED);
}


class App : public BApplication {
public:
	App() : BApplication("application/x-vnd.Lumina-fx-window-anim") {}

	void ReadyToRun()
	{
		thread_id thread = spawn_thread([](void*) -> status_t {
			RunSelfTest();
			return B_OK;
		}, "selftest", B_NORMAL_PRIORITY, NULL);
		resume_thread(thread);
	}
};

}	// namespace


int
main()
{
	App app;
	app.Run();
	return 0;
}
