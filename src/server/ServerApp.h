/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * lumina_server: session, configuration and mode manager.
 *
 * Responsibilities
 *  - owns the layered ConfigStore (defaults < personality < user < preview)
 *    and broadcasts changes to subscribers;
 *  - enters/leaves Lumina mode: stock snapshot, decorator/engine selection,
 *    Deskbar policy, panel lifecycle — and always restores stock on exit;
 *  - crash watchdog (own restarts and panel restarts) with automatic
 *    fallback to stock Haiku;
 *  - window registry, window rules, engine clock;
 *  - profiles (personalities): apply, save, duplicate, export, import.
 */
#ifndef LUMINA_SERVER_APP_H
#define LUMINA_SERVER_APP_H

#include <Application.h>
#include <Messenger.h>
#include <String.h>

#include <map>
#include <string>
#include <vector>

#include <lumina/ConfigStore.h>
#include <lumina/Profile.h>
#include <lumina/Rules.h>

#include "WindowRegistry.h"

class BMessageRunner;

namespace lumina {
namespace haiku { class EngineLink; }

namespace server {

class EngineClock;
class EffectsController;

class ServerApp : public BApplication {
public:
	ServerApp();
	virtual ~ServerApp();

	virtual void ReadyToRun();
	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();
	virtual void ArgvReceived(int32 argc, char** argv);

private:
	// configuration
	void _LoadUserLayer();
	void _SaveUserLayer();
	bool _LoadPersonality(const std::string& name, BString* error);
	void _OnConfigChanged(const std::vector<std::string>& paths);
	bool _Touches(const std::vector<std::string>& paths, const char* prefix);

	// mode
	status_t _EnterLumina(const std::string& personality, BString* error);
	status_t _ExitToStock(BString* report, bool quitAfter);
	void _ApplyDeskbarPolicy();
	void _ApplyEngineSelection();
	void _ConfigureEngine();
	void _ProbeEngine();
	void _SyncPanels();
	void _StopPanel();

	// watchdog
	bool _RecordStartAndCheckCrashLoop(BString& reason);
	void _MarkCleanExit();
	void _PanelQuit(team_id team);

	// windows
	void _HandleEngineEvent(BMessage* message);
	void _PollWindows();
	void _BroadcastWindowChange(const WindowChange& change);
	void _ApplyRules(const haiku::WindowSnapshot& window);

	// protocol handlers
	void _HandleSubscribe(BMessage* message, bool subscribe);
	void _HandleConfigGet(BMessage* message);
	void _HandleConfigSet(BMessage* message);
	void _HandleConfigUnset(BMessage* message);
	void _HandleConfirm(BMessage* message);
	void _HandleRevert();
	void _HandleMode(BMessage* message);
	void _HandleStatus(BMessage* message);
	void _HandleDiagnostics(BMessage* message);
	void _HandleProfile(BMessage* message);
	void _HandleWindowList(BMessage* message);
	void _HandleWindowCommand(BMessage* message);
	void _HandleReserve(BMessage* message);
	void _HandleNodeMonitor(BMessage* message);

	void _Broadcast(const char* topic, BMessage& message);

	ConfigStore			fConfig;
	ProfileStore		fProfiles;
	RuleSet				fRules;
	std::string			fLastUserText;	// to ignore our own file writes

	haiku::EngineLink*	fEngine;
	EngineClock*		fClock;
	EffectsController*	fEffects;
	bool				fEngineLive;
	BString				fEngineReason;

	WindowRegistry		fWindows;
	BMessageRunner*		fPollRunner;
	BMessageRunner*		fRevertRunner;

	std::map<std::string, std::vector<BMessenger>> fSubscribers;

	struct Reservation {
		int32		screen;
		std::string	edge;
		float		thickness;
	};
	std::map<std::string, Reservation> fReservations;

	bool				fInLumina;
	bool				fPanelExpected;
	std::vector<bigtime_t> fPanelCrashes;
	BString				fFallbackReason;
	int32				fCrashCount;
};

}	// namespace server
}	// namespace lumina

#endif
