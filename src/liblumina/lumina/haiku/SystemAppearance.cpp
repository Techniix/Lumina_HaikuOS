/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "SystemAppearance.h"

#include <stdio.h>

#include <Deskbar.h>
#include <InterfaceDefs.h>
#include <Messenger.h>
#include <Message.h>
#include <String.h>

#include <lumina/ConfigStore.h>
#include <lumina/Json.h>
#include <lumina/Log.h>
#include <lumina/Profile.h>

#include "PrivateApi.h"
#include "Protocol.h"

namespace lumina {
namespace haiku {
namespace SystemAppearance {

static const color_which kColors[] = {
	B_PANEL_BACKGROUND_COLOR, B_PANEL_TEXT_COLOR, B_DOCUMENT_BACKGROUND_COLOR,
	B_DOCUMENT_TEXT_COLOR, B_CONTROL_BACKGROUND_COLOR, B_CONTROL_TEXT_COLOR,
	B_CONTROL_BORDER_COLOR, B_CONTROL_HIGHLIGHT_COLOR, B_CONTROL_MARK_COLOR,
	B_NAVIGATION_BASE_COLOR, B_NAVIGATION_PULSE_COLOR, B_SHINE_COLOR,
	B_SHADOW_COLOR, B_LINK_TEXT_COLOR, B_LINK_HOVER_COLOR,
	B_LINK_VISITED_COLOR, B_LINK_ACTIVE_COLOR, B_MENU_BACKGROUND_COLOR,
	B_MENU_SELECTED_BACKGROUND_COLOR, B_MENU_ITEM_TEXT_COLOR,
	B_MENU_SELECTED_ITEM_TEXT_COLOR, B_MENU_SELECTED_BORDER_COLOR,
	B_LIST_BACKGROUND_COLOR, B_LIST_SELECTED_BACKGROUND_COLOR,
	B_LIST_ITEM_TEXT_COLOR, B_LIST_SELECTED_ITEM_TEXT_COLOR,
	B_SCROLL_BAR_THUMB_COLOR, B_TOOL_TIP_BACKGROUND_COLOR,
	B_TOOL_TIP_TEXT_COLOR, B_STATUS_BAR_COLOR, B_SUCCESS_COLOR,
	B_FAILURE_COLOR, B_WINDOW_TAB_COLOR, B_WINDOW_TEXT_COLOR,
	B_WINDOW_INACTIVE_TAB_COLOR, B_WINDOW_INACTIVE_TEXT_COLOR,
	B_WINDOW_BORDER_COLOR, B_WINDOW_INACTIVE_BORDER_COLOR, B_DESKTOP_COLOR,
};


static std::string
ColorToString(rgb_color c)
{
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", c.red, c.green,
		c.blue, c.alpha);
	return buffer;
}


Value
Capture()
{
	Value doc = Value::MakeObject();
	doc["capturedWith"] = "lumina-1";
	doc["haikuRevision"] = PrivateApi::RunningHaikuRevision().String();

	BString decorator;
	if (PrivateApi::GetDecorator(decorator))
		doc["decorator"] = decorator.String();
	BString controlLook;
	if (PrivateApi::GetControlLook(controlLook))
		doc["controlLook"] = controlLook.String();

	Value colors = Value::MakeObject();
	for (color_which which : kColors) {
		const char* name = ui_color_name(which);
		if (name != NULL)
			colors[name] = ColorToString(ui_color(which));
	}
	doc["uiColors"] = colors;

	doc["mouseMode"] = int(mouse_mode());
	doc["focusFollowsMouseMode"] = int(focus_follows_mouse_mode());
	doc["acceptFirstClick"] = accept_first_click();

	Value workspaces = Value::MakeObject();
	uint32 columns = 0, rows = 0;
	PrivateApi::GetWorkspacesLayout(columns, rows);
	workspaces["count"] = int(count_workspaces());
	workspaces["columns"] = int(columns);
	workspaces["rows"] = int(rows);
	doc["workspaces"] = workspaces;

	Value deskbar = Value::MakeObject();
	bool running = PrivateApi::IsJobRunning(LUMINA_DESKBAR_JOB)
		|| BMessenger("application/x-vnd.Be-TSKB").IsValid();
	deskbar["running"] = running;
	if (running) {
		BDeskbar bar;
		bool expanded = false;
		deskbar["location"] = int(bar.Location(&expanded));
		deskbar["expanded"] = expanded;
		deskbar["alwaysOnTop"] = bar.IsAlwaysOnTop();
		deskbar["autoRaise"] = bar.IsAutoRaise();
		deskbar["autoHide"] = bar.IsAutoHide();
	}
	doc["deskbar"] = deskbar;
	return doc;
}


status_t
Restore(const Value& snapshot, std::vector<std::string>* failures)
{
	auto fail = [&](const char* what) {
		LUMINA_LOG_WARN("appearance", "restore of %s failed", what);
		if (failures)
			failures->push_back(what);
	};

	// Deskbar first: the user must always end up with a way to launch apps.
	if (const Value* deskbar = snapshot.Find("deskbar")) {
		bool wasRunning = deskbar->Find("running")
			? deskbar->Find("running")->AsBool(true) : true;
		if (wasRunning) {
			PrivateApi::SetJobEnabled(LUMINA_DESKBAR_JOB, true);
			if (!BMessenger("application/x-vnd.Be-TSKB").IsValid()
				&& PrivateApi::StartJob(LUMINA_DESKBAR_JOB) != B_OK)
				fail("deskbar.running");
			for (int i = 0; i < 50
					&& !BMessenger("application/x-vnd.Be-TSKB").IsValid(); i++)
				snooze(20000);
			BDeskbar bar;
			if (const Value* v = deskbar->Find("location")) {
				bool expanded = deskbar->Find("expanded")
					? deskbar->Find("expanded")->AsBool() : true;
				bar.SetLocation(deskbar_location(v->AsInt()), expanded);
			}
			if (const Value* v = deskbar->Find("alwaysOnTop"))
				bar.SetAlwaysOnTop(v->AsBool());
			if (const Value* v = deskbar->Find("autoRaise"))
				bar.SetAutoRaise(v->AsBool());
			if (const Value* v = deskbar->Find("autoHide"))
				bar.SetAutoHide(v->AsBool());
		}
	}

	if (const Value* v = snapshot.Find("decorator")) {
		BString current;
		PrivateApi::GetDecorator(current);
		if (current != v->AsString().c_str()
			&& PrivateApi::SetDecorator(v->AsString().c_str()) != B_OK) {
			// Never leave the user on a broken decorator: fall back to the
			// built-in one, which app_server always has.
			if (PrivateApi::SetDecorator("Default") != B_OK)
				fail("decorator");
		}
	}
	if (const Value* v = snapshot.Find("controlLook")) {
		BString current;
		PrivateApi::GetControlLook(current);
		if (current != v->AsString().c_str()
			&& PrivateApi::SetControlLook(v->AsString().c_str()) != B_OK)
			fail("controlLook");
	}

	if (const Value* colors = snapshot.Find("uiColors")) {
		BMessage message;
		for (const Value::Member& m : colors->Members()) {
			uint8_t r, g, b, a;
			if (!ParseColor(m.second.AsString(), r, g, b, a))
				continue;
			if (which_ui_color(m.first.c_str()) == B_NO_COLOR)
				continue;
			rgb_color color = {r, g, b, a};
			message.AddColor(m.first.c_str(), color);
		}
		if (!message.IsEmpty())
			set_ui_colors(&message);
	}

	if (const Value* v = snapshot.Find("mouseMode"))
		set_mouse_mode(mode_mouse(v->AsInt()));
	if (const Value* v = snapshot.Find("focusFollowsMouseMode"))
		set_focus_follows_mouse_mode(mode_focus_follows_mouse(v->AsInt()));
	if (const Value* v = snapshot.Find("acceptFirstClick"))
		set_accept_first_click(v->AsBool());

	if (const Value* w = snapshot.Find("workspaces")) {
		uint32 columns = uint32(w->Find("columns")
			? w->Find("columns")->AsInt() : 0);
		uint32 rows = uint32(w->Find("rows") ? w->Find("rows")->AsInt() : 0);
		if (columns > 0 && rows > 0)
			PrivateApi::SetWorkspacesLayout(columns, rows);
		else if (const Value* count = w->Find("count"))
			set_workspace_count(int32(count->AsInt()));
	}
	return failures != NULL && !failures->empty() ? B_ERROR : B_OK;
}


bool
LoadSnapshot(const std::string& path, Value& snapshot)
{
	std::string text;
	if (!ReadTextFile(path, text))
		return false;
	return ParseJson(text, snapshot) && snapshot.IsObject();
}


bool
EnsureSnapshot(const std::string& path, bool* created)
{
	if (created)
		*created = false;
	Value existing;
	if (LoadSnapshot(path, existing))
		return true;
	Value snapshot = Capture();
	if (!WriteFileAtomic(path, WriteJson(snapshot)))
		return false;
	if (created)
		*created = true;
	LUMINA_LOG_INFO("appearance", "saved stock snapshot to %s", path.c_str());
	return true;
}


std::vector<std::string>
Differences(const Value& a, const Value& b)
{
	Value left = a, right = b;
	for (Value* v : {&left, &right}) {
		v->Erase("capturedWith");
		v->Erase("haikuRevision");
	}
	return ConfigStore::Diff(left, right);
}

}	// namespace SystemAppearance
}	// namespace haiku
}	// namespace lumina
