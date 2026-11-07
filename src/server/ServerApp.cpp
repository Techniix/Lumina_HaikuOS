/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "ServerApp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>

#include <Deskbar.h>
#include <Screen.h>

#include <Entry.h>
#include <MessageRunner.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Notification.h>
#include <Roster.h>

#include <lumina/Json.h>
#include <lumina/Log.h>
#include <lumina/fx/Strategy.h>
#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/HaikuLog.h>
#include <lumina/haiku/MessageCodec.h>
#include <lumina/haiku/ModeControl.h>
#include <lumina/haiku/Paths.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>
#include <lumina/haiku/SystemAppearance.h>

#include "EngineClock.h"

using namespace lumina::haiku;

namespace lumina {
namespace server {

namespace {
enum {
	kMsgPollWindows = 'Lpol',
	kMsgRevertPreview = 'Lrev',
};

const char* kLog = "server";

void
Notify(const char* title, const char* text, notification_type type
	= B_INFORMATION_NOTIFICATION)
{
	BNotification notification(type);
	notification.SetGroup("Lumina");
	notification.SetTitle(title);
	notification.SetContent(text);
	notification.Send();
}
}	// namespace


ServerApp::ServerApp()
	:
	BApplication(LUMINA_SERVER_SIGNATURE),
	fProfiles(Paths::PersonalityDirs(), Paths::UserProfilesDir()),
	fEngine(NULL),
	fClock(NULL),
	fEngineLive(false),
	fPollRunner(NULL),
	fRevertRunner(NULL),
	fInLumina(false),
	fPanelExpected(false),
	fCrashCount(0)
{
	fConfig.AddLayer("personality", true);
	fConfig.AddLayer("user", false);
	fConfig.AddLayer("preview", false);
}


ServerApp::~ServerApp()
{
	delete fPollRunner;
	delete fRevertRunner;
	if (fClock != NULL)
		fClock->Stop();
	delete fClock;
	delete fEngine;
}


void
ServerApp::ArgvReceived(int32 argc, char** argv)
{
	// `lumina_server --restore-stock` is the last-resort CLI entry point.
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--restore-stock") == 0) {
			BString report;
			_ExitToStock(&report, true);
		}
	}
}


// #pragma mark - startup


void
ServerApp::ReadyToRun()
{
	InitLogging("server");
	LUMINA_LOG_INFO(kLog, "lumina_server starting (pid %d, Haiku %s)",
		int(getpid()), PrivateApi::RunningHaikuRevision().String());

	_LoadUserLayer();
	LogLevel level;
	if (ParseLogLevel(fConfig.GetString("logging.level", "info"), level))
		Logger::Default().SetLevel(level);

	BString crashReason;
	bool crashLoop = _RecordStartAndCheckCrashLoop(crashReason);

	fEngine = new EngineLink();
	fClock = new EngineClock(fEngine);
	fClock->Start();

	be_roster->StartWatching(BMessenger(this), B_REQUEST_QUIT);

	// Watch our settings directory for edits made outside the server.
	BEntry dir(Paths::SettingsDir().c_str());
	node_ref nodeRef;
	if (dir.GetNodeRef(&nodeRef) == B_OK)
		watch_node(&nodeRef, B_WATCH_DIRECTORY, BMessenger(this));

	if (crashLoop) {
		fFallbackReason = crashReason;
		LUMINA_LOG_ERROR(kLog, "crash loop detected: %s", crashReason.String());
		BString report;
		ModeControl::RestoreStockLocally(NULL, &report);
		Notify("Lumina switched back to stock Haiku", crashReason.String(),
			B_ERROR_NOTIFICATION);
		_MarkCleanExit();
		Quit();
		return;
	}

	if (fConfig.GetString("session.mode", "stock") == "lumina") {
		BString error;
		if (_EnterLumina(fConfig.GetString("session.personality", "classic"),
				&error) != B_OK) {
			LUMINA_LOG_ERROR(kLog, "cannot enter Lumina mode: %s",
				error.String());
			Notify("Lumina could not start", error.String(),
				B_ERROR_NOTIFICATION);
			BString report;
			_ExitToStock(&report, true);
			return;
		}
	} else {
		// Started manually (CLI/preflet) while in stock mode: stay idle and
		// wait for a LUMINA_MODE_SET request. Nothing is changed.
		LUMINA_LOG_INFO(kLog, "stock mode: idle, waiting for requests");
	}

	fPollRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgPollWindows), 1000000);
}


bool
ServerApp::QuitRequested()
{
	LUMINA_LOG_INFO(kLog, "quit requested");
	if (fInLumina) {
		// A normal quit while Lumina is active (logout/shutdown): leave the
		// session as configured, but make sure Deskbar comes back for the
		// next login if we were hiding it.
		_StopPanel();
		if (fConfig.GetString("deskbar.mode", "keep") == "hidden")
			ModeControl::ShowDeskbar();
	}
	if (fClock != NULL)
		fClock->Stop();
	_MarkCleanExit();
	Logger::Default().Flush();
	return true;
}


// #pragma mark - configuration


void
ServerApp::_LoadUserLayer()
{
	Value layer;
	ModeControl::LoadUserLayer(layer);
	ReadTextFile(Paths::UserLayerFile(), fLastUserText);
	std::vector<ValidationIssue> issues;
	std::vector<std::string> changed = fConfig.ReplaceLayer("user", layer,
		&issues);
	for (const ValidationIssue& issue : issues) {
		if (issue.fatal) {
			LUMINA_LOG_WARN(kLog, "user setting %s ignored: %s",
				issue.path.c_str(), issue.message.c_str());
		}
	}
	if (!changed.empty())
		_OnConfigChanged(changed);
}


void
ServerApp::_SaveUserLayer()
{
	const ConfigStore::Layer* layer = fConfig.GetLayer("user");
	if (layer == NULL)
		return;
	if (!ModeControl::SaveUserLayer(layer->data))
		LUMINA_LOG_ERROR(kLog, "cannot save %s", Paths::UserLayerFile().c_str());
	ReadTextFile(Paths::UserLayerFile(), fLastUserText);
}


bool
ServerApp::_LoadPersonality(const std::string& name, BString* error)
{
	Value document;
	std::vector<ValidationIssue> issues;
	std::string loadError;
	if (!fProfiles.Load(name, document, &issues, &loadError)) {
		if (error)
			*error = loadError.c_str();
		return false;
	}
	for (const ValidationIssue& issue : issues) {
		LUMINA_LOG_WARN(kLog, "personality %s: %s: %s", name.c_str(),
			issue.path.c_str(), issue.message.c_str());
	}
	// A personality must not switch the session itself.
	document.Erase("session");
	std::vector<std::string> changed = fConfig.ReplaceLayer("personality",
		document);
	LUMINA_LOG_INFO(kLog, "personality '%s' loaded (%d changes)", name.c_str(),
		int(changed.size()));
	_OnConfigChanged(changed);
	return true;
}


bool
ServerApp::_Touches(const std::vector<std::string>& paths, const char* prefix)
{
	size_t length = strlen(prefix);
	for (const std::string& path : paths) {
		if (path.compare(0, length, prefix) == 0)
			return true;
	}
	return false;
}


void
ServerApp::_OnConfigChanged(const std::vector<std::string>& paths)
{
	if (paths.empty())
		return;

	BMessage message(LUMINA_CONFIG_CHANGED);
	for (const std::string& path : paths)
		message.AddString(LUMINA_FIELD_PATH, path.c_str());
	message.AddUInt64("generation", fConfig.Generation());
	_Broadcast(LUMINA_TOPIC_CONFIG, message);

	if (_Touches(paths, "logging.level")) {
		LogLevel level;
		if (ParseLogLevel(fConfig.GetString("logging.level", "info"), level))
			Logger::Default().SetLevel(level);
	}
	if (_Touches(paths, "windows.rules")) {
		std::vector<RuleIssue> issues;
		fRules.Load(fConfig.Get("windows.rules"), &issues);
		for (const RuleIssue& issue : issues) {
			LUMINA_LOG_WARN(kLog, "rule %d: %s", issue.index,
				issue.message.c_str());
		}
	}
	if (!fInLumina)
		return;
	if (_Touches(paths, "windows.engine"))
		_ApplyEngineSelection();
	if (_Touches(paths, "windows.") || _Touches(paths, "fx."))
		_ConfigureEngine();
	if (_Touches(paths, "deskbar."))
		_ApplyDeskbarPolicy();
	if (_Touches(paths, "panels"))
		_SyncPanels();
}


void
ServerApp::_Broadcast(const char* topic, BMessage& message)
{
	auto it = fSubscribers.find(topic);
	if (it == fSubscribers.end())
		return;
	std::vector<BMessenger>& list = it->second;
	for (size_t i = 0; i < list.size();) {
		status_t status = list[i].SendMessage(&message, (BHandler*)NULL,
			100000);
		if (status == B_BAD_PORT_ID) {
			list.erase(list.begin() + long(i));
			continue;
		}
		i++;
	}
}


// #pragma mark - mode


status_t
ServerApp::_EnterLumina(const std::string& personality, BString* error)
{
	LUMINA_LOG_INFO(kLog, "entering Lumina mode, personality '%s'",
		personality.c_str());

	// 1. Never touch anything before the pristine state is saved.
	bool created = false;
	if (!SystemAppearance::EnsureSnapshot(Paths::StockSnapshotFile(),
			&created)) {
		if (error)
			*error = "cannot save the stock snapshot; refusing to continue";
		return B_ERROR;
	}

	// 2. Personality layer.
	if (!_LoadPersonality(personality, error)) {
		LUMINA_LOG_WARN(kLog, "personality '%s' missing, using classic",
			personality.c_str());
		if (personality == "classic" || !_LoadPersonality("classic", error))
			fConfig.ReplaceLayer("personality", Value::MakeObject());
	}

	fInLumina = true;
	ModeControl::SetEnabledAtLogin(true);

	// 3. Apply.
	_ApplyEngineSelection();
	_ConfigureEngine();
	_ApplyDeskbarPolicy();
	_SyncPanels();
	fRules.Load(fConfig.Get("windows.rules"));

	BMessage changed(LUMINA_MODE_CHANGED);
	changed.AddString("mode", "lumina");
	changed.AddString("personality", personality.c_str());
	_Broadcast(LUMINA_TOPIC_MODE, changed);
	return B_OK;
}


status_t
ServerApp::_ExitToStock(BString* report, bool quitAfter)
{
	LUMINA_LOG_INFO(kLog, "returning to stock Haiku");
	fInLumina = false;
	if (fClock != NULL)
		fClock->Stop();
	if (fEngine != NULL) {
		fEngine->Cancel(0);
		fEngine->Unsubscribe(BMessenger(this));
	}
	_StopPanel();

	std::vector<std::string> failures;
	status_t status = ModeControl::RestoreStockLocally(&failures, report);
	// RestoreStockLocally rewrote the user layer on disk: pick it up.
	_LoadUserLayer();

	BMessage changed(LUMINA_MODE_CHANGED);
	changed.AddString("mode", "stock");
	_Broadcast(LUMINA_TOPIC_MODE, changed);

	if (quitAfter)
		PostMessage(B_QUIT_REQUESTED);
	return status;
}


void
ServerApp::_ApplyDeskbarPolicy()
{
	std::string mode = fConfig.GetString("deskbar.mode", "keep");
	if (mode == "hidden") {
		// Only hide Deskbar if a Lumina panel can take over launching.
		if (fConfig.Get("panels").Size() == 0) {
			LUMINA_LOG_WARN(kLog, "deskbar.mode=hidden ignored: no panels");
			ModeControl::ShowDeskbar();
			return;
		}
		ModeControl::HideDeskbar();
	} else {
		ModeControl::ShowDeskbar();
		BDeskbar deskbar;
		Value snapshot;
		bool stockAutoHide = false;
		if (SystemAppearance::LoadSnapshot(Paths::StockSnapshotFile(), snapshot)
			&& snapshot.FindPath("deskbar.autoHide") != NULL)
			stockAutoHide = snapshot.FindPath("deskbar.autoHide")->AsBool();
		deskbar.SetAutoHide(mode == "autohide" ? true : stockAutoHide);
	}
}


void
ServerApp::_ApplyEngineSelection()
{
	bool wanted = fConfig.GetBool("windows.engine.enabled", true);
	BString path;
	if (wanted) {
		if (!EngineLink::FindEngineAddOn(path)) {
			fEngineLive = false;
			fEngineReason = "engine add-on not installed";
			LUMINA_LOG_WARN(kLog, "%s", fEngineReason.String());
			return;
		}
		if (!EngineLink::IsEngineSelected()) {
			status_t status = PrivateApi::SetDecorator(path);
			LUMINA_LOG_INFO(kLog, "selecting engine decorator %s: %s",
				path.String(), strerror(status));
		}
		_ProbeEngine();
		if (fEngineLive)
			fEngine->Subscribe(BMessenger(this));
	} else if (EngineLink::IsEngineSelected()) {
		Value snapshot;
		BString decorator("Default");
		if (SystemAppearance::LoadSnapshot(Paths::StockSnapshotFile(), snapshot)
			&& snapshot.Find("decorator") != NULL)
			decorator = snapshot.Find("decorator")->AsString().c_str();
		if (decorator.EndsWith("/LuminaEngine"))
			decorator = "Default";
		PrivateApi::SetDecorator(decorator);
		fEngineLive = false;
		fEngineReason = "disabled by configuration";
	}
}


void
ServerApp::_ProbeEngine()
{
	EngineInfo info;
	// Give app_server a moment to finish the decorator switch.
	for (int i = 0; i < 10; i++) {
		if (fEngine->Probe(info) == B_OK && info.present)
			break;
		snooze(50000);
	}
	fEngineLive = info.present && info.active;
	fEngineReason = !info.present ? "engine not responding (decorator not "
		"selected?)" : info.reason;
	LUMINA_LOG_INFO(kLog, "engine: present=%d active=%d built=%s running=%s %s",
		info.present, info.active, info.builtAbi.String(),
		info.runningAbi.String(), info.reason.String());
}


void
ServerApp::_ConfigureEngine()
{
	if (!fEngineLive)
		return;
	fx::EffectProfile profile = fx::EffectProfile::FromConfig(fConfig);
	fx::Capabilities caps;
	caps.engineLoaded = true;
	caps.engineAnimation = true;
	fx::EffectPlan plan = fx::SelectPlan(profile, caps, fx::Quality::High);

	Value config = Value::MakeObject();
	Value& snap = config["snap"];
	snap["enabled"] = fConfig.GetBool("windows.snap.enabled", false);
	snap["threshold"] = fConfig.GetNumber("windows.snap.edgeThreshold", 2);
	snap["layout"] = fConfig.GetString("windows.snap.layout", "halves+quarters");
	Value& animation = config["animation"];
	animation["enabled"] = plan.snapGeometry
		== fx::GeometryStrategy::EngineAnimated;
	animation["durationMs"] = plan.durationMs;
	animation["curve"] = profile.curve.ToString();
	animation["resizeMode"] = plan.resizeMode;
	animation["maxFps"] = plan.maxFps;
	Value reserved = Value::MakeArray();
	for (const auto& entry : fReservations) {
		Value r = Value::MakeObject();
		r["screen"] = entry.second.screen;
		r["edge"] = entry.second.edge;
		r["thickness"] = double(entry.second.thickness);
		reserved.Push(r);
	}
	config["reserved"] = reserved;
	fEngine->Configure(WriteJson(config, {false, 0}).c_str());
	LUMINA_LOG_DEBUG(kLog, "engine configured: %s (%s)",
		WriteJson(config, {false, 0}).c_str(), plan.reason.c_str());
}


void
ServerApp::_SyncPanels()
{
	bool wanted = fInLumina && fConfig.Get("panels").Size() > 0;
	BMessenger panel(LUMINA_PANEL_SIGNATURE);
	if (wanted && !panel.IsValid()) {
		fPanelExpected = true;
		status_t status = be_roster->Launch(LUMINA_PANEL_SIGNATURE);
		LUMINA_LOG_INFO(kLog, "launching panel: %s", strerror(status));
	} else if (!wanted && panel.IsValid())
		_StopPanel();
	// Running panels follow config changes through their subscription.
}


void
ServerApp::_StopPanel()
{
	fPanelExpected = false;
	BMessenger panel(LUMINA_PANEL_SIGNATURE);
	if (panel.IsValid())
		panel.SendMessage(B_QUIT_REQUESTED);
}


// #pragma mark - watchdog


bool
ServerApp::_RecordStartAndCheckCrashLoop(BString& reason)
{
	Value state = Value::MakeObject();
	std::string text;
	if (ReadTextFile(Paths::StateFile(), text))
		ParseJson(text, state);
	if (!state.IsObject())
		state = Value::MakeObject();

	int64 now = int64(real_time_clock());
	int maxCrashes = int(fConfig.GetNumber("session.watchdog.maxCrashes", 3));
	int window = int(fConfig.GetNumber("session.watchdog.windowSeconds", 60));

	Value crashes = state.Find("crashes") ? *state.Find("crashes")
		: Value::MakeArray();
	if (state.Find("running") != NULL) {
		// The previous instance never reached _MarkCleanExit().
		crashes.Push(Value(now));
		LUMINA_LOG_WARN(kLog, "previous lumina_server did not exit cleanly");
	}
	Value recent = Value::MakeArray();
	for (const Value& t : crashes.Items()) {
		if (now - t.AsInt() <= window)
			recent.Push(t);
	}
	fCrashCount = int32(recent.Size());
	state["crashes"] = recent;
	Value running = Value::MakeObject();
	running["pid"] = int(getpid());
	running["since"] = Value(now);
	state["running"] = running;
	WriteFileAtomic(Paths::StateFile(), WriteJson(state));

	if (int(recent.Size()) >= maxCrashes) {
		reason.SetToFormat("lumina_server crashed %d times within %d s. "
			"Stock Haiku was restored; re-enable Lumina from Lumina Mode.",
			int(recent.Size()), window);
		return true;
	}
	return false;
}


void
ServerApp::_MarkCleanExit()
{
	Value state = Value::MakeObject();
	std::string text;
	if (ReadTextFile(Paths::StateFile(), text))
		ParseJson(text, state);
	if (!state.IsObject())
		state = Value::MakeObject();
	state.Erase("running");
	if (!fFallbackReason.IsEmpty()) {
		state["lastFallback"] = fFallbackReason.String();
		state["crashes"] = Value::MakeArray();
	}
	WriteFileAtomic(Paths::StateFile(), WriteJson(state));
}


void
ServerApp::_PanelQuit(team_id team)
{
	if (!fPanelExpected || !fInLumina)
		return;
	bigtime_t now = system_time();
	fPanelCrashes.push_back(now);
	bigtime_t window = bigtime_t(fConfig.GetNumber(
		"session.watchdog.windowSeconds", 60)) * 1000000;
	fPanelCrashes.erase(std::remove_if(fPanelCrashes.begin(),
		fPanelCrashes.end(), [&](bigtime_t t) { return now - t > window; }),
		fPanelCrashes.end());
	int maxCrashes = int(fConfig.GetNumber("session.watchdog.maxCrashes", 3));
	LUMINA_LOG_WARN(kLog, "panel exited unexpectedly (%d recent)",
		int(fPanelCrashes.size()));
	if (int(fPanelCrashes.size()) >= maxCrashes) {
		fFallbackReason = "The Lumina panel crashed repeatedly. Stock Haiku "
			"was restored.";
		Notify("Lumina switched back to stock Haiku", fFallbackReason.String(),
			B_ERROR_NOTIFICATION);
		BString report;
		_ExitToStock(&report, true);
		return;
	}
	// Deskbar may be hidden: make sure the user can launch apps meanwhile.
	if (fConfig.GetString("deskbar.mode", "keep") == "hidden")
		ModeControl::ShowDeskbar();
	_SyncPanels();
	if (fConfig.GetString("deskbar.mode", "keep") == "hidden")
		_ApplyDeskbarPolicy();
}


// #pragma mark - windows


void
ServerApp::_BroadcastWindowChange(const WindowChange& change)
{
	if (change.op.empty())
		return;
	BMessage message(LUMINA_WINDOWS_CHANGED);
	message.AddString("op", change.op.c_str());
	WindowRegistry::ArchiveWindow(change.window, message);
	_Broadcast(LUMINA_TOPIC_WINDOWS, message);
	if (change.op == "added")
		_ApplyRules(change.window);
}


void
ServerApp::_HandleEngineEvent(BMessage* message)
{
	const char* op = message->GetString(LUMINA_ENGINE_FIELD_OP, "");
	int32 token = message->GetInt32(LUMINA_EV_TOKEN, -1);
	if (strcmp(op, "animation-started") == 0) {
		fClock->AnimationStarted(token, message->GetInt32("lumina:fps", 60));
		return;
	}
	if (strcmp(op, LUMINA_EVENT_ANIM_DONE) == 0)
		fClock->AnimationFinished(token);
	if (strcmp(op, LUMINA_EVENT_SNAPPED) == 0) {
		LUMINA_LOG_DEBUG(kLog, "window %d snapped to %s", int(token),
			message->GetString(LUMINA_EV_ZONE, "?"));
	}
	_BroadcastWindowChange(fWindows.ApplyEngineEvent(*message));
}


void
ServerApp::_PollWindows()
{
	// With a live engine the registry is event-driven; polling only covers
	// the engine-less case (and catches anything the engine dropped every
	// 10 s).
	static int sCounter = 0;
	if (fEngineLive && (++sCounter % 10) != 0)
		return;
	std::vector<WindowSnapshot> windows;
	if (PrivateApi::ListWindows(windows) != B_OK)
		return;
	for (const WindowChange& change : fWindows.Synchronize(windows))
		_BroadcastWindowChange(change);
}


void
ServerApp::_ApplyRules(const WindowSnapshot& window)
{
	if (!fInLumina || !fEngineLive || fRules.Count() == 0)
		return;
	WindowDescriptor descriptor;
	descriptor.signature = window.signature.String();
	descriptor.title = window.title.String();
	descriptor.width = window.frame.Width();
	descriptor.height = window.frame.Height();
	for (int32 i = 0; i < 32; i++) {
		if ((window.workspaces & (1UL << i)) != 0) {
			descriptor.workspace = i;
			break;
		}
	}
	std::vector<std::string> matched;
	Value actions = fRules.Evaluate(descriptor, &matched);
	if (actions.Size() == 0)
		return;
	LUMINA_LOG_INFO(kLog, "rules [%zu] for '%s'", matched.size(),
		window.title.String());
	if (const Value* placement = actions.Find("placement")) {
		BScreen screen;
		BRect frame = screen.Frame();
		RectF area{frame.left, frame.top, frame.right, frame.bottom};
		for (const auto& entry : fReservations) {
			if (entry.second.edge == "bottom")
				area.bottom -= entry.second.thickness;
			else if (entry.second.edge == "top")
				area.top += entry.second.thickness;
		}
		RectF target;
		if (PlacementRect(placement->AsString(), area, target)) {
			// Approximate decoration insets with the window's own values.
			BRect content(target.left + window.borderSize,
				target.top + window.tabHeight + window.borderSize,
				target.right - window.borderSize,
				target.bottom - window.borderSize);
			fEngine->MoveResize(window.token, content);
		}
	}
	if (const Value* workspace = actions.Find("workspace"))
		fEngine->SetWorkspace(window.token, int32(workspace->AsInt()));
	if (const Value* minimized = actions.Find("minimized")) {
		if (minimized->AsBool())
			fEngine->Minimize(window.token, true);
	}
}


// #pragma mark - protocol


void
ServerApp::_HandleSubscribe(BMessage* message, bool subscribe)
{
	BMessenger target;
	if (message->FindMessenger(LUMINA_FIELD_TARGET, &target) != B_OK) {
		ReplyStatus(message, B_BAD_VALUE, "missing target");
		return;
	}
	for (auto& entry : fSubscribers) {
		auto& list = entry.second;
		list.erase(std::remove(list.begin(), list.end(), target), list.end());
	}
	if (subscribe) {
		const char* topic;
		for (int32 i = 0; message->FindString(LUMINA_FIELD_TOPIC, i, &topic)
				== B_OK; i++)
			fSubscribers[topic].push_back(target);
	}
	ReplyStatus(message, B_OK);
}


void
ServerApp::_HandleConfigGet(BMessage* message)
{
	const char* path = message->GetString(LUMINA_FIELD_PATH, "");
	BMessage reply(B_REPLY);
	PutJson(reply, LUMINA_FIELD_JSON, fConfig.Get(path));
	reply.AddString("source", fConfig.SourceOf(path).c_str());
	reply.AddUInt64("generation", fConfig.Generation());
	ReplyStatus(message, B_OK, NULL, &reply);
}


void
ServerApp::_HandleConfigSet(BMessage* message)
{
	const char* path = message->GetString(LUMINA_FIELD_PATH, "");
	Value value;
	if (GetJson(*message, LUMINA_FIELD_JSON, value) != B_OK) {
		ReplyStatus(message, B_BAD_DATA, "value is not valid JSON");
		return;
	}
	bool preview = message->GetBool("preview", false);
	std::string error;
	std::vector<std::string> changed;
	if (strcmp(path, "session.mode") == 0) {
		ReplyStatus(message, B_NOT_ALLOWED,
			"use LUMINA_MODE_SET (lumina mode …) to change the mode");
		return;
	}
	if (!fConfig.Set(preview ? "preview" : "user", path, value, &error,
			&changed)) {
		ReplyStatus(message, B_BAD_VALUE, error.c_str());
		return;
	}
	if (preview) {
		bigtime_t after = message->GetInt64("revert_after", 15000000);
		delete fRevertRunner;
		fRevertRunner = new BMessageRunner(BMessenger(this),
			new BMessage(kMsgRevertPreview), after, 1);
	} else
		_SaveUserLayer();
	_OnConfigChanged(changed);
	ReplyStatus(message, B_OK);
}


void
ServerApp::_HandleConfigUnset(BMessage* message)
{
	const char* path = message->GetString(LUMINA_FIELD_PATH, "");
	std::vector<std::string> changed;
	if (!fConfig.Unset("user", path, &changed)) {
		ReplyStatus(message, B_ENTRY_NOT_FOUND, "not set in the user layer");
		return;
	}
	_SaveUserLayer();
	_OnConfigChanged(changed);
	ReplyStatus(message, B_OK);
}


void
ServerApp::_HandleConfirm(BMessage* message)
{
	delete fRevertRunner;
	fRevertRunner = NULL;
	const ConfigStore::Layer* preview = fConfig.GetLayer("preview");
	if (preview != NULL) {
		std::vector<std::string> paths;
		CollectLeafPaths(preview->data, paths);
		for (const std::string& path : paths) {
			const Value* v = preview->data.FindPath(path);
			if (v != NULL)
				fConfig.Set("user", path, *v);
		}
		fConfig.ReplaceLayer("preview", Value::MakeObject());
		_SaveUserLayer();
	}
	ReplyStatus(message, B_OK);
}


void
ServerApp::_HandleRevert()
{
	delete fRevertRunner;
	fRevertRunner = NULL;
	LUMINA_LOG_INFO(kLog, "preview not confirmed: reverting");
	_OnConfigChanged(fConfig.ReplaceLayer("preview", Value::MakeObject()));
}


void
ServerApp::_HandleMode(BMessage* message)
{
	BString mode = message->GetString("mode", "");
	BString personality = message->GetString("personality",
		fConfig.GetString("session.personality", "classic").c_str());
	BString error;
	status_t status = B_OK;

	if (mode == "stock") {
		ReplyStatus(message, B_OK);	// reply first: we quit afterwards
		if (!fInLumina) {
			// Idle server in stock mode: nothing was changed, so nothing
			// is restored (the snapshot may be older than the user's
			// current stock settings).
			ModeControl::SetEnabledAtLogin(false);
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		BString report;
		_ExitToStock(&report, true);
		return;
	}
	if (mode != "lumina") {
		ReplyStatus(message, B_BAD_VALUE, "mode must be 'stock' or 'lumina'");
		return;
	}
	ProfileInfo info;
	if (!fProfiles.Find(personality.String(), info)) {
		ReplyStatus(message, B_ENTRY_NOT_FOUND, "unknown personality");
		return;
	}
	fConfig.Set("user", "session.mode", Value("lumina"));
	fConfig.Set("user", "session.personality", Value(personality.String()));
	_SaveUserLayer();
	if (fInLumina) {
		if (!_LoadPersonality(personality.String(), &error))
			status = B_ERROR;
		else {
			_ApplyEngineSelection();
			_ConfigureEngine();
			_ApplyDeskbarPolicy();
			_SyncPanels();
		}
	} else
		status = _EnterLumina(personality.String(), &error);
	ReplyStatus(message, status, status == B_OK ? NULL : error.String());
}


void
ServerApp::_HandleStatus(BMessage* message)
{
	BMessage reply(B_REPLY);
	reply.AddString("mode", fInLumina ? "lumina" : "stock");
	reply.AddString("personality",
		fConfig.GetString("session.personality", "classic").c_str());
	reply.AddBool("engine_loaded", fEngineLive);
	reply.AddString("engine_reason", fEngineReason);
	reply.AddInt32("crash_count", fCrashCount);
	reply.AddUInt64("generation", fConfig.Generation());
	reply.AddString("deskbar_mode",
		fConfig.GetString("deskbar.mode", "keep").c_str());
	reply.AddBool("deskbar_running", ModeControl::IsDeskbarRunning());
	reply.AddBool("enabled_at_login", ModeControl::IsEnabledAtLogin());
	reply.AddInt32("windows", int32(fWindows.Count()));
	reply.AddUInt64("clock_ticks", fClock->TicksSent());
	if (!fFallbackReason.IsEmpty())
		reply.AddString("fallback_reason", fFallbackReason);
	ReplyStatus(message, B_OK, NULL, &reply);
}


void
ServerApp::_HandleDiagnostics(BMessage* message)
{
	BMessage reply(B_REPLY);
	for (const LogEntry& entry : Logger::Default().RecentEntries())
		reply.AddString("log", FormatLogEntry(entry).c_str());
	reply.AddInt64("warnings", int64(Logger::Default().CountFor(
		LogLevel::Warning)));
	reply.AddInt64("errors", int64(Logger::Default().CountFor(LogLevel::Error)));
	EngineInfo info;
	if (fEngine->Probe(info) == B_OK && info.present)
		reply.AddString("engine_stats", info.statsJson);
	PutJson(reply, "config", fConfig.Merged());
	ReplyStatus(message, B_OK, NULL, &reply);
}


void
ServerApp::_HandleProfile(BMessage* message)
{
	BString op = message->GetString("op", "");
	BMessage reply(B_REPLY);
	std::string error;

	if (op == "list") {
		for (const ProfileInfo& info : fProfiles.List()) {
			BMessage item;
			item.AddString("name", info.name.c_str());
			item.AddString("displayName", info.displayName.c_str());
			item.AddString("description", info.description.c_str());
			item.AddBool("readOnly", info.readOnly);
			item.AddBool("customized", info.shadowsSystem);
			reply.AddMessage("profile", &item);
		}
		ReplyStatus(message, B_OK, NULL, &reply);
	} else if (op == "export") {
		std::string bundle;
		if (!fProfiles.Export(message->GetString("name", ""), bundle, &error)) {
			ReplyStatus(message, B_ERROR, error.c_str());
			return;
		}
		reply.AddString("bundle", bundle.c_str());
		ReplyStatus(message, B_OK, NULL, &reply);
	} else if (op == "import") {
		ImportReport report = fProfiles.Import(
			message->GetString("bundle", ""), message->GetString("name", ""));
		if (!report.ok) {
			ReplyStatus(message, B_ERROR, report.error.c_str());
			return;
		}
		reply.AddString("name", report.name.c_str());
		for (const std::string& path : report.needsApproval)
			reply.AddString("needsApproval", path.c_str());
		for (const ValidationIssue& issue : report.issues)
			reply.AddString("issue", (issue.path + ": " + issue.message).c_str());
		ReplyStatus(message, B_OK, NULL, &reply);
	} else if (op == "duplicate") {
		if (!fProfiles.Duplicate(message->GetString("name", ""),
				message->GetString("target", ""), &error)) {
			ReplyStatus(message, B_ERROR, error.c_str());
			return;
		}
		ReplyStatus(message, B_OK);
	} else if (op == "remove") {
		if (!fProfiles.Remove(message->GetString("name", ""), &error)) {
			ReplyStatus(message, B_ERROR, error.c_str());
			return;
		}
		ReplyStatus(message, B_OK);
	} else if (op == "save") {
		// Current personality + user overrides become a new profile.
		Value document = Value::MakeObject();
		if (const ConfigStore::Layer* p = fConfig.GetLayer("personality"))
			document.MergeFrom(p->data);
		if (const ConfigStore::Layer* u = fConfig.GetLayer("user"))
			document.MergeFrom(u->data);
		document.Erase("session");
		ProfileInfo info;
		info.displayName = message->GetString("displayName",
			message->GetString("name", ""));
		info.description = message->GetString("description", "");
		info.basedOn = fConfig.GetString("session.personality", "classic");
		if (!fProfiles.Save(message->GetString("name", ""), document, info,
				&error)) {
			ReplyStatus(message, B_ERROR, error.c_str());
			return;
		}
		ReplyStatus(message, B_OK);
	} else
		ReplyStatus(message, B_BAD_VALUE, "unknown profile op");
}


void
ServerApp::_HandleWindowList(BMessage* message)
{
	if (fWindows.Count() == 0)
		_PollWindows();
	BMessage reply(B_REPLY);
	fWindows.Archive(reply);
	reply.AddBool("engine", fEngineLive);
	ReplyStatus(message, B_OK, NULL, &reply);
}


void
ServerApp::_HandleWindowCommand(BMessage* message)
{
	BString op = message->GetString(LUMINA_ENGINE_FIELD_OP, "");
	int32 token = message->GetInt32(LUMINA_ENGINE_FIELD_TOKEN, -1);
	if (!fEngineLive) {
		// Without the engine only Deskbar-level actions exist.
		if (op == LUMINA_OP_MINIMIZE) {
			PrivateApi::MinimizeWindow(token,
				message->GetBool("lumina:value", true));
			ReplyStatus(message, B_OK);
		} else if (op == LUMINA_OP_ACTIVATE) {
			PrivateApi::BringToFront(token);
			ReplyStatus(message, B_OK);
		} else
			ReplyStatus(message, B_NOT_SUPPORTED, fEngineReason.String());
		return;
	}
	BMessage command(*message);
	ReplyStatus(message, fEngine->Send(command));
}


void
ServerApp::_HandleReserve(BMessage* message)
{
	Reservation r;
	std::string owner = message->GetString("owner", "?");
	r.screen = message->GetInt32("screen", 0);
	r.edge = message->GetString("edge", "bottom");
	r.thickness = message->GetFloat("thickness", 0);
	if (r.thickness <= 0)
		fReservations.erase(owner);
	else
		fReservations[owner] = r;
	_ConfigureEngine();
	ReplyStatus(message, B_OK);
}


void
ServerApp::_HandleNodeMonitor(BMessage* message)
{
	// Any change in the settings directory: if session.lumina differs from
	// what we last wrote, someone edited it by hand — reload.
	std::string text;
	if (!ReadTextFile(Paths::UserLayerFile(), text) || text == fLastUserText)
		return;
	LUMINA_LOG_INFO(kLog, "%s changed on disk: reloading",
		Paths::UserLayerFile().c_str());
	std::string previousMode = fConfig.GetString("session.mode", "stock");
	_LoadUserLayer();
	std::string mode = fConfig.GetString("session.mode", "stock");
	if (mode != previousMode) {
		BMessage request(LUMINA_MODE_SET);
		request.AddString("mode", mode.c_str());
		PostMessage(&request);
	}
}


void
ServerApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case LUMINA_SUBSCRIBE: _HandleSubscribe(message, true); break;
		case LUMINA_UNSUBSCRIBE: _HandleSubscribe(message, false); break;
		case LUMINA_CONFIG_GET: _HandleConfigGet(message); break;
		case LUMINA_CONFIG_SET: _HandleConfigSet(message); break;
		case LUMINA_CONFIG_UNSET: _HandleConfigUnset(message); break;
		case LUMINA_CONFIG_CONFIRM: _HandleConfirm(message); break;
		case LUMINA_MODE_SET: _HandleMode(message); break;
		case LUMINA_STATUS_GET: _HandleStatus(message); break;
		case LUMINA_DIAG_GET: _HandleDiagnostics(message); break;
		case LUMINA_PROFILE_OP: _HandleProfile(message); break;
		case LUMINA_WINDOW_LIST_GET: _HandleWindowList(message); break;
		case LUMINA_WINDOW_COMMAND: _HandleWindowCommand(message); break;
		case LUMINA_RESERVE_AREA: _HandleReserve(message); break;
		case LUMINA_RESTORE_STOCK:
		{
			ReplyStatus(message, B_OK);
			BString report;
			_ExitToStock(&report, true);
			break;
		}
		case LUMINA_PING: ReplyStatus(message, B_OK); break;
		case LUMINA_ENGINE_EVENT: _HandleEngineEvent(message); break;
		case kMsgPollWindows: _PollWindows(); break;
		case kMsgRevertPreview: _HandleRevert(); break;
		case B_NODE_MONITOR: _HandleNodeMonitor(message); break;
		case B_SOME_APP_QUIT:
		{
			const char* signature = NULL;
			if (message->FindString("be:signature", &signature) == B_OK
				&& strcasecmp(signature, LUMINA_PANEL_SIGNATURE) == 0)
				_PanelQuit(message->GetInt32("be:team", -1));
			break;
		}
		default:
			BApplication::MessageReceived(message);
	}
}

}	// namespace server
}	// namespace lumina


int
main()
{
	lumina::server::ServerApp app;
	app.Run();
	return 0;
}
