/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "WindowRegistry.h"

#include <string.h>

#include <lumina/haiku/Protocol.h>

using namespace lumina::haiku;

namespace lumina {
namespace server {

static bool
SameVisibleState(const WindowSnapshot& a, const WindowSnapshot& b)
{
	return a.title == b.title && a.workspaces == b.workspaces
		&& a.minimized == b.minimized && a.hidden == b.hidden
		&& a.frame == b.frame;
}


std::vector<WindowChange>
WindowRegistry::Synchronize(const std::vector<WindowSnapshot>& windows)
{
	std::vector<WindowChange> changes;
	std::map<int32, WindowSnapshot> fresh;
	for (const WindowSnapshot& w : windows) {
		if (!PrivateApi::IsUserWindow(w))
			continue;
		fresh[w.token] = w;
		auto it = fWindows.find(w.token);
		if (it == fWindows.end())
			changes.push_back({"added", w});
		else if (!SameVisibleState(it->second, w))
			changes.push_back({"changed", w});
	}
	for (const auto& entry : fWindows) {
		if (fresh.find(entry.first) == fresh.end())
			changes.push_back({"removed", entry.second});
	}
	fWindows.swap(fresh);
	return changes;
}


WindowChange
WindowRegistry::ApplyEngineEvent(const BMessage& event)
{
	WindowChange change;
	const char* op = event.GetString(LUMINA_ENGINE_FIELD_OP, "");
	WindowSnapshot w;
	w.token = event.GetInt32(LUMINA_EV_TOKEN, -1);
	if (w.token < 0)
		return change;
	w.team = event.GetInt32(LUMINA_EV_TEAM, -1);
	w.title = event.GetString(LUMINA_EV_TITLE, "");
	w.signature = event.GetString(LUMINA_EV_SIG, "");
	event.FindRect(LUMINA_EV_FRAME, &w.frame);
	w.workspaces = uint32(event.GetInt32(LUMINA_EV_WORKSPACES, 0));
	w.feel = uint32(event.GetInt32(LUMINA_EV_FEEL, 0));
	w.look = event.GetInt32(LUMINA_EV_LOOK, -1);

	auto it = fWindows.find(w.token);
	if (strcmp(op, LUMINA_EVENT_REMOVED) == 0) {
		if (it != fWindows.end()) {
			change = {"removed", it->second};
			fWindows.erase(it);
		}
		return change;
	}
	if (it != fWindows.end()) {
		// Keep state the event doesn't carry.
		w.minimized = it->second.minimized;
		w.hidden = it->second.hidden;
	}
	if (strcmp(op, LUMINA_EVENT_MINIMIZED) == 0)
		w.minimized = event.GetBool(LUMINA_EV_FLAG, false);
	if (strcmp(op, LUMINA_EVENT_HIDDEN) == 0 && !event.GetBool(LUMINA_EV_FLAG,
			false))
		w.hidden = true;
	if (strcmp(op, LUMINA_EVENT_ADDED) == 0)
		w.hidden = event.GetBool(LUMINA_EV_FLAG, false);	// added while hidden
	if (strcmp(op, LUMINA_EVENT_ACTIVATED) == 0)
		w.hidden = false;	// there is no "shown" hook; activation implies it

	if (!PrivateApi::IsUserWindow(w)) {
		if (it != fWindows.end()) {
			change = {"removed", it->second};
			fWindows.erase(it);
		}
		return change;
	}
	if (it == fWindows.end()) {
		fWindows[w.token] = w;
		change = {"added", w};
	} else {
		bool visibleChange = !SameVisibleState(it->second, w);
		it->second = w;
		if (visibleChange || strcmp(op, LUMINA_EVENT_ACTIVATED) == 0)
			change = {strcmp(op, LUMINA_EVENT_ACTIVATED) == 0 ? "activated"
				: "changed", w};
	}
	return change;
}


void
WindowRegistry::ArchiveWindow(const WindowSnapshot& w, BMessage& out)
{
	out.AddInt32("token", w.token);
	out.AddInt32("team", w.team);
	out.AddString("title", w.title);
	out.AddString("signature", w.signature);
	out.AddRect("frame", w.frame);
	out.AddInt32("workspaces", int32(w.workspaces));
	out.AddBool("minimized", w.minimized);
	out.AddBool("hidden", w.hidden);
}


void
WindowRegistry::Archive(BMessage& out) const
{
	for (const auto& entry : fWindows) {
		BMessage window;
		ArchiveWindow(entry.second, window);
		out.AddMessage("window", &window);
	}
}


bool
WindowRegistry::Find(int32 token, WindowSnapshot& out) const
{
	auto it = fWindows.find(token);
	if (it == fWindows.end())
		return false;
	out = it->second;
	return true;
}

}	// namespace server
}	// namespace lumina
