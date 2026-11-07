/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Authoritative window model for panels and rules. Fed by Window Engine
 * events when the engine is active, otherwise by polling the WindowInfo
 * API once a second (exactly what Deskbar does) — the same model either way.
 */
#ifndef LUMINA_SERVER_WINDOW_REGISTRY_H
#define LUMINA_SERVER_WINDOW_REGISTRY_H

#include <Message.h>

#include <map>
#include <string>
#include <vector>

#include <lumina/haiku/PrivateApi.h>

namespace lumina {
namespace server {

struct WindowChange {
	std::string				op;		// added | removed | changed
	haiku::WindowSnapshot	window;
};

class WindowRegistry {
public:
	// Polling path: replaces the model with a fresh list, returns changes.
	std::vector<WindowChange> Synchronize(
		const std::vector<haiku::WindowSnapshot>& windows);

	// Event path: applies one engine event; returns the change (op empty if
	// nothing user-visible changed).
	WindowChange ApplyEngineEvent(const BMessage& event);

	void Archive(BMessage& out) const;	// repeated "window" sub-messages
	static void ArchiveWindow(const haiku::WindowSnapshot& w, BMessage& out);

	size_t Count() const { return fWindows.size(); }
	bool Find(int32 token, haiku::WindowSnapshot& out) const;

private:
	std::map<int32, haiku::WindowSnapshot> fWindows;
};

}	// namespace server
}	// namespace lumina

#endif
