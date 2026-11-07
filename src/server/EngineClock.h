/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Frame clock for the thread-free Window Engine. While animations run it
 * sends one "tick" command per frame through the EngineLink window; the
 * engine advances its timeline inside app_server's locked hook. The clock
 * sleeps (zero CPU) when nothing animates.
 */
#ifndef LUMINA_SERVER_ENGINE_CLOCK_H
#define LUMINA_SERVER_ENGINE_CLOCK_H

#include <Locker.h>
#include <OS.h>

#include <set>

namespace lumina {
namespace haiku { class EngineLink; }

namespace server {

class EngineClock {
public:
	explicit EngineClock(haiku::EngineLink* link);
	~EngineClock();

	status_t Start();
	void Stop();

	void AnimationStarted(int32 token, int32 fps);
	void AnimationFinished(int32 token);

	uint64 TicksSent() const { return fTicksSent; }
	bool IsRunning() const;

private:
	static status_t _Entry(void* data);
	void _Loop();

	haiku::EngineLink*	fLink;
	thread_id			fThread;
	sem_id				fWake;
	volatile bool		fQuit;
	mutable BLocker		fLock;
	std::set<int32>		fActive;
	int32				fFps;
	bigtime_t			fDeadline;	// safety: stop even without "done"
	uint64				fTicksSent;
};

}	// namespace server
}	// namespace lumina

#endif
