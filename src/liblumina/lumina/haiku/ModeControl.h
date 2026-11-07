/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Mode switching primitives shared by lumina_server, the Lumina Mode
 * preflet/replicant and the CLI. RestoreStockLocally() works WITHOUT a
 * running lumina_server, so the user can always get back to stock Haiku,
 * even if the server is crashed, hung or uninstalled half-way.
 */
#ifndef LUMINA_HAIKU_MODE_CONTROL_H
#define LUMINA_HAIKU_MODE_CONTROL_H

#include <String.h>

#include <string>
#include <vector>

#include <lumina/Value.h>

namespace lumina {
namespace haiku {
namespace ModeControl {

// The launch_daemon gate: lumina_server is started at login only while this
// marker exists (see data/user_launch/lumina).
std::string EnabledMarker();
bool IsEnabledAtLogin();
status_t SetEnabledAtLogin(bool enabled);

// Reads/writes ~/config/settings/Lumina/session.lumina (the user layer).
bool LoadUserLayer(Value& layer);
bool SaveUserLayer(const Value& layer);

// Deskbar handling with launch_daemon, falling back to the roster when the
// launch_daemon job is unknown (e.g. a non-standard session).
status_t HideDeskbar();
status_t ShowDeskbar();
bool IsDeskbarRunning();

// Full, server-independent restore: quits the Lumina panel/server, restores
// the stock snapshot (or sane defaults if none exists), re-enables Deskbar,
// removes the login marker and records session.mode = stock.
status_t RestoreStockLocally(std::vector<std::string>* failures,
	BString* report);

}	// namespace ModeControl
}	// namespace haiku
}	// namespace lumina

#endif
