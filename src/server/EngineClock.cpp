/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "EngineClock.h"

#include <Autolock.h>
#include <Message.h>

#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/Protocol.h>

namespace lumina {
namespace server {

namespace {
const bigtime_t kMaxAnimationRun = 4000000;	// 4 s hard stop
}


EngineClock::EngineClock(haiku::EngineLink* link)
	:
	fLink(link),
	fThread(-1),
	fWake(-1),
	fQuit(false),
	fLock("engine clock"),
	fFps(60),
	fDeadline(0),
	fTicksSent(0)
{
}


EngineClock::~EngineClock()
{
	Stop();
}


status_t
EngineClock::Start()
{
	fWake = create_sem(0, "engine clock wake");
	if (fWake < 0)
		return fWake;
	fQuit = false;
	fThread = spawn_thread(_Entry, "lumina engine clock", B_DISPLAY_PRIORITY,
		this);
	if (fThread < 0)
		return fThread;
	return resume_thread(fThread);
}


void
EngineClock::Stop()
{
	if (fThread < 0)
		return;
	fQuit = true;
	release_sem(fWake);
	status_t result;
	wait_for_thread(fThread, &result);
	fThread = -1;
	delete_sem(fWake);
	fWake = -1;
}


bool
EngineClock::IsRunning() const
{
	BAutolock _(fLock);
	return !fActive.empty();
}


void
EngineClock::AnimationStarted(int32 token, int32 fps)
{
	BAutolock _(fLock);
	bool wasIdle = fActive.empty();
	fActive.insert(token);
	fFps = fps > 0 && fps <= 240 ? fps : 60;
	fDeadline = system_time() + kMaxAnimationRun;
	if (wasIdle && fWake >= 0)
		release_sem(fWake);
}


void
EngineClock::AnimationFinished(int32 token)
{
	BAutolock _(fLock);
	fActive.erase(token);
}


status_t
EngineClock::_Entry(void* data)
{
	static_cast<EngineClock*>(data)->_Loop();
	return B_OK;
}


void
EngineClock::_Loop()
{
	bigtime_t next = system_time();
	while (!fQuit) {
		bool active;
		bigtime_t interval;
		{
			BAutolock _(fLock);
			if (!fActive.empty() && system_time() > fDeadline)
				fActive.clear();	// engine never reported "done": stop
			active = !fActive.empty();
			interval = 1000000 / fFps;
		}
		if (!active) {
			acquire_sem(fWake);	// sleep until an animation starts
			next = system_time();
			continue;
		}
		BMessage tick(lumina::haiku::LUMINA_ENGINE_COMMAND);
		tick.AddString(LUMINA_ENGINE_FIELD_OP, "tick");
		if (fLink->Send(tick) == B_OK)
			fTicksSent++;
		next += interval;
		bigtime_t now = system_time();
		if (next < now)
			next = now;	// we fell behind: don't burst to catch up
		snooze_until(next, B_SYSTEM_TIMEBASE);
	}
}

}	// namespace server
}	// namespace lumina
