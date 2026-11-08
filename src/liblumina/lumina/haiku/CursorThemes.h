/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Cursor themes, Haiku side: finds theme directories, turns a theme into
 * app_server's CursorSet file format and reports the UI scale.
 *
 * The CursorSet file is a flattened BMessage — exactly what
 * src/servers/app/CursorSet.cpp loads:
 *
 *   "name"              string
 *   "<role key>"        BMessage per cursor, e.g. "I-beam", "Resize East":
 *       "class"     "cursor"      (CursorSet::FindCursor requires this;
 *                                  CursorSet::AddCursor writes "bitmap",
 *                                  which its own loader then rejects)
 *       "_frame"    BRect         (0, 0, size-1, size-1)
 *       "_cspace"   int32         B_RGBA32
 *       "hotspot"   BPoint
 *       "_data"     raw           premultiplied RGBA32, rows of size*4 bytes
 *
 * The Window Engine applies the file inside app_server with
 * CursorManager::SetCursorSet() (see LuminaEngineLifecycle.cpp).
 */
#ifndef LUMINA_HAIKU_CURSOR_THEMES_H
#define LUMINA_HAIKU_CURSOR_THEMES_H

#include <String.h>
#include <SupportDefs.h>

#include <string>
#include <vector>

namespace lumina {
namespace haiku {
namespace CursorThemes {

// Theme directories, highest priority first: user settings
// (~/config/settings/Lumina/cursors) then every data directory
// (…/data/Lumina/cursors).
std::vector<std::string> SearchDirs();
// Names of all themes found (directories containing theme.json).
std::vector<std::string> List();
// Resolves a theme name to its directory; empty if not found.
std::string Find(const std::string& name);

// UI scale used by app_server for its stock cursors (bold font size / 12).
float CurrentScale();

// Builds (or reuses, when newer than the theme) the CursorSet file for
// `name` at `scale` under ~/config/settings/Lumina/cache/. Returns the
// file path in `outPath`; problems (unknown roles, unreadable images …)
// are appended to `problems`. Fails if no cursor could be built.
status_t Build(const std::string& name, float scale, std::string& outPath,
	std::vector<std::string>& problems);

}	// namespace CursorThemes
}	// namespace haiku
}	// namespace lumina

#endif
