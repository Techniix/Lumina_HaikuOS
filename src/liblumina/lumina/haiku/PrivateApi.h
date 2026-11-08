/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Compatibility interface over Haiku's semi-private APIs. This is the only
 * code that includes the vendored private headers (compat/haiku_private).
 * Every function degrades gracefully: on failure it returns an error or an
 * empty result, never crashes, and callers have a public-API fallback.
 */
#ifndef LUMINA_HAIKU_PRIVATE_API_H
#define LUMINA_HAIKU_PRIVATE_API_H

#include <OS.h>
#include <Rect.h>
#include <String.h>
#include <SupportDefs.h>

#include <vector>

namespace lumina {
namespace haiku {

struct WindowSnapshot {
	int32		token = -1;		// app_server server token
	team_id		team = -1;
	BString		title;
	BString		signature;		// application signature (from BRoster)
	BRect		frame;
	uint32		workspaces = 0;
	int32		layer = 0;
	uint32		feel = 0;
	int32		look = -1;		// window_look; -1 = unknown (engine events only)
	uint32		flags = 0;
	bool		minimized = false;
	bool		hidden = false;		// show_hide_level > 0
	float		tabHeight = 0;
	float		borderSize = 0;
};

namespace PrivateApi {

// ---- windows (WindowInfo.h, as used by Deskbar) ----------------------
// All windows of all teams (team < 0) or of one team. Deskbar's own and
// desktop/menu windows are included; filter with IsUserWindow().
status_t ListWindows(std::vector<WindowSnapshot>& out, team_id team = -1);
status_t GetWindow(int32 token, WindowSnapshot& out);
bool IsUserWindow(const WindowSnapshot& window);
// Front-to-back token order on `workspace` (-1 = current).
status_t WindowOrder(int32 workspace, std::vector<int32>& tokens);
status_t MinimizeWindow(int32 token, bool minimize = true);
status_t BringToFront(int32 token);
status_t BringTeamToFront(team_id team);
status_t MinimizeTeam(team_id team);

// ---- launch_daemon (LaunchRoster.h) ------------------------------------
status_t StopJob(const char* name, bool force = false);
status_t StartJob(const char* name);
status_t SetJobEnabled(const char* name, bool enabled);
bool IsJobRunning(const char* name);

// ---- decorator & control look (DecoratorPrivate.h / InterfacePrivate.h)
bool GetDecorator(BString& path);
status_t SetDecorator(const BString& path);
bool GetControlLook(BString& path);
status_t SetControlLook(const BString& path);
void GetWorkspacesLayout(uint32& columns, uint32& rows);
void SetWorkspacesLayout(uint32 columns, uint32 rows);

// ---- system revision -----------------------------------------------------
// e.g. "hrev59123" (from uname), empty if unknown.
BString RunningHaikuRevision();

}	// namespace PrivateApi
}	// namespace haiku
}	// namespace lumina

#endif
