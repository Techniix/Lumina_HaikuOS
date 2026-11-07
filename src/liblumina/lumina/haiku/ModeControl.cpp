/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "ModeControl.h"

#include <stdio.h>

#include <filesystem>

#include <Application.h>
#include <Messenger.h>
#include <Roster.h>

#include <lumina/Json.h>
#include <lumina/Log.h>
#include <lumina/Profile.h>

#include "EngineLink.h"
#include "Paths.h"
#include "PrivateApi.h"
#include "Protocol.h"
#include "SystemAppearance.h"

namespace lumina {
namespace haiku {
namespace ModeControl {

static const char* kDeskbarSignature = "application/x-vnd.Be-TSKB";


std::string
EnabledMarker()
{
	return Paths::SettingsDir() + "/enabled";
}


bool
IsEnabledAtLogin()
{
	std::error_code ec;
	return std::filesystem::exists(EnabledMarker(), ec);
}


status_t
SetEnabledAtLogin(bool enabled)
{
	if (enabled) {
		return WriteFileAtomic(EnabledMarker(),
			"Lumina starts at login while this file exists.\n")
			? B_OK : B_ERROR;
	}
	std::error_code ec;
	std::filesystem::remove(EnabledMarker(), ec);
	return ec ? B_ERROR : B_OK;
}


bool
LoadUserLayer(Value& layer)
{
	std::string text;
	layer = Value::MakeObject();
	if (!ReadTextFile(Paths::UserLayerFile(), text))
		return false;
	Value parsed;
	JsonError error;
	if (!ParseJson(text, parsed, &error) || !parsed.IsObject()) {
		LUMINA_LOG_ERROR("mode", "user settings unreadable (%s); ignoring",
			error.ToString().c_str());
		return false;
	}
	layer = parsed;
	return true;
}


bool
SaveUserLayer(const Value& layer)
{
	return WriteFileAtomic(Paths::UserLayerFile(), WriteJson(layer), true);
}


bool
IsDeskbarRunning()
{
	return BMessenger(kDeskbarSignature).IsValid();
}


status_t
HideDeskbar()
{
	// Disable first so launch_daemon does not restart the service.
	status_t status = PrivateApi::SetJobEnabled(LUMINA_DESKBAR_JOB, false);
	if (status == B_OK)
		status = PrivateApi::StopJob(LUMINA_DESKBAR_JOB);
	if (status != B_OK && IsDeskbarRunning()) {
		BMessenger deskbar(kDeskbarSignature);
		status = deskbar.SendMessage(B_QUIT_REQUESTED);
	}
	return status;
}


status_t
ShowDeskbar()
{
	PrivateApi::SetJobEnabled(LUMINA_DESKBAR_JOB, true);
	if (IsDeskbarRunning())
		return B_OK;
	status_t status = PrivateApi::StartJob(LUMINA_DESKBAR_JOB);
	if (status != B_OK)
		status = be_roster->Launch(kDeskbarSignature);
	for (int i = 0; i < 100 && !IsDeskbarRunning(); i++)
		snooze(20000);
	return IsDeskbarRunning() ? B_OK : status;
}


status_t
RestoreStockLocally(std::vector<std::string>* failures, BString* report,
	bool full)
{
	BString log;
	// 1. Deskbar back first: the user must always be able to launch apps.
	if (ShowDeskbar() != B_OK) {
		log << "Deskbar could not be restarted. ";
		if (failures)
			failures->push_back("deskbar");
	}

	// 2. Stop Lumina components that may still run (never ourselves: the
	// caller quits on its own; a synchronous send to our own port could
	// block forever). Bounded timeouts throughout.
	for (const char* signature : {LUMINA_PANEL_SIGNATURE,
			LUMINA_SERVER_SIGNATURE}) {
		BMessenger app(signature);
		if (!app.IsValid() || app.Team() == be_app->Team())
			continue;
		BMessage quit(B_QUIT_REQUESTED);
		app.SendMessage(&quit, (BHandler*)NULL, 500000);
	}

	// 3. System appearance: the pristine snapshot, else built-in defaults.
	Value snapshot;
	std::vector<std::string> local;	// always tracked, even if caller passes NULL
	if (SystemAppearance::LoadSnapshot(Paths::StockSnapshotFile(), snapshot)) {
		SystemAppearance::Restore(snapshot, &local,
			full ? nullptr : &SystemAppearance::kManagedItems);
		log << (full ? "Restored the full saved stock configuration. "
			: "Restored the stock decorator and Deskbar. ");
		// The snapshot describes the state *before* Lumina was enabled. Once
		// restored it is retired, so the next activation captures whatever
		// the user changes in stock mode meanwhile.
		if (local.empty()) {
			std::error_code ec;
			std::filesystem::rename(Paths::StockSnapshotFile(),
				Paths::StockSnapshotFile() + ".last", ec);
		}
	} else {
		if (EngineLink::IsEngineSelected())
			PrivateApi::SetDecorator("Default");
		log << "No snapshot found; reset the decorator to Default. ";
	}

	if (failures != NULL)
		failures->insert(failures->end(), local.begin(), local.end());

	// 4. Don't start Lumina at the next login, and record the mode.
	SetEnabledAtLogin(false);
	Value layer;
	LoadUserLayer(layer);
	layer.SetPath("session.mode", "stock");
	SaveUserLayer(layer);
	log << "Lumina is off.";
	LUMINA_LOG_INFO("mode", "restore stock: %s", log.String());
	if (report)
		*report = log;
	return failures != NULL && !failures->empty() ? B_ERROR : B_OK;
}

}	// namespace ModeControl
}	// namespace haiku
}	// namespace lumina
