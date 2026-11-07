/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "PrivateApi.h"

#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include <InterfaceDefs.h>
#include <Roster.h>
#include <Window.h>

#include "../../compat/haiku_private/DecoratorPrivate.h"
#include "../../compat/haiku_private/InterfacePrivateSubset.h"
#include "../../compat/haiku_private/LaunchRoster.h"
#include "../../compat/haiku_private/WindowInfo.h"

namespace lumina {
namespace haiku {
namespace PrivateApi {

static void
FillSnapshot(const client_window_info* info, WindowSnapshot& out)
{
	out.token = info->server_token;
	out.team = info->team;
	out.title = info->name;
	out.frame = BRect(info->window_left, info->window_top, info->window_right,
		info->window_bottom);
	out.workspaces = info->workspaces;
	out.layer = info->layer;
	out.feel = info->feel;
	out.flags = info->flags;
	out.minimized = info->is_mini;
	out.hidden = info->show_hide_level > 0;
	out.tabHeight = info->tab_height;
	out.borderSize = info->border_size;

	app_info appInfo;
	if (be_roster != NULL && be_roster->GetRunningAppInfo(info->team,
			&appInfo) == B_OK) {
		out.signature = appInfo.signature;
	}
}


status_t
ListWindows(std::vector<WindowSnapshot>& out, team_id team)
{
	out.clear();
	int32 count = 0;
	int32* tokens = get_token_list(team, &count);
	if (tokens == NULL)
		return count == 0 ? B_OK : B_ERROR;
	for (int32 i = 0; i < count; i++) {
		client_window_info* info = get_window_info(tokens[i]);
		if (info == NULL)
			continue;	// window went away meanwhile
		WindowSnapshot snapshot;
		FillSnapshot(info, snapshot);
		free(info);
		out.push_back(snapshot);
	}
	free(tokens);
	return B_OK;
}


status_t
GetWindow(int32 token, WindowSnapshot& out)
{
	client_window_info* info = get_window_info(token);
	if (info == NULL)
		return B_ENTRY_NOT_FOUND;
	FillSnapshot(info, out);
	free(info);
	return B_OK;
}


bool
IsUserWindow(const WindowSnapshot& window)
{
	// Same test as Deskbar's TWindowMenu::WindowShouldBeListed(): normal (or
	// BWindowScreen) feel and not explicitly hidden. Minimized windows and
	// windows on other workspaces are listed, as in Deskbar.
	const uint32 kWindowScreenFeel = 1026;	// WindowPrivate.h
	if (window.feel != B_NORMAL_WINDOW_FEEL && window.feel != kWindowScreenFeel)
		return false;
	if (window.hidden && !window.minimized)
		return false;
	if (window.signature == "application/x-vnd.Be-TSKB"
		|| window.signature == "application/x-vnd.Lumina-Panel"
		|| window.signature == "application/x-vnd.Lumina-Server")
		return false;
	return true;
}


status_t
WindowOrder(int32 workspace, std::vector<int32>& tokens)
{
	tokens.clear();
	int32* list = NULL;
	int32 count = 0;
	status_t status = BPrivate::get_window_order(
		workspace < 0 ? current_workspace() : workspace, &list, &count);
	if (status != B_OK)
		return status;
	tokens.assign(list, list + count);
	free(list);
	return B_OK;
}


status_t
MinimizeWindow(int32 token, bool minimize)
{
	if (minimize)
		do_window_action(token, B_MINIMIZE_WINDOW, BRect(), false);
	else
		do_window_action(token, B_BRING_TO_FRONT, BRect(), false);
	return B_OK;
}


status_t
BringToFront(int32 token)
{
	do_window_action(token, B_BRING_TO_FRONT, BRect(), false);
	return B_OK;
}


status_t
BringTeamToFront(team_id team)
{
	do_bring_to_front_team(BRect(), team, false);
	return B_OK;
}


status_t
MinimizeTeam(team_id team)
{
	do_minimize_team(BRect(), team, false);
	return B_OK;
}


status_t
StopJob(const char* name, bool force)
{
	BLaunchRoster roster;
	return roster.Stop(name, force);
}


status_t
StartJob(const char* name)
{
	BLaunchRoster roster;
	return roster.Start(name);
}


status_t
SetJobEnabled(const char* name, bool enabled)
{
	BLaunchRoster roster;
	return roster.SetEnabled(name, enabled);
}


bool
IsJobRunning(const char* name)
{
	BLaunchRoster roster;
	BMessage info;
	if (roster.GetJobInfo(name, info) != B_OK)
		return false;
	bool running = false;
	if (info.FindBool("running", &running) == B_OK)
		return running;
	team_id team;
	return info.FindInt32("team", &team) == B_OK && team >= 0;
}


bool
GetDecorator(BString& path)
{
	return BPrivate::get_decorator(path);
}


status_t
SetDecorator(const BString& path)
{
	return BPrivate::set_decorator(path);
}


bool
GetControlLook(BString& path)
{
	return BPrivate::get_control_look(path);
}


status_t
SetControlLook(const BString& path)
{
	return BPrivate::set_control_look(path);
}


void
GetWorkspacesLayout(uint32& columns, uint32& rows)
{
	BPrivate::get_workspaces_layout(&columns, &rows);
}


void
SetWorkspacesLayout(uint32 columns, uint32 rows)
{
	BPrivate::set_workspaces_layout(columns, rows);
}


BString
RunningHaikuRevision()
{
	struct utsname name;
	if (uname(&name) != 0)
		return BString();
	// Haiku: version is "hrevNNNNN Mon DD YYYY HH:MM:SS"
	BString version(name.version);
	int32 space = version.FindFirst(' ');
	if (space > 0)
		version.Truncate(space);
	return version;
}

}	// namespace PrivateApi
}	// namespace haiku
}	// namespace lumina
