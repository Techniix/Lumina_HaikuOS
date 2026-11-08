/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Cursor themes: the portable half.
 *
 * A theme is a directory:
 *
 *   MyTheme/
 *     theme.json          manifest (below)
 *     22/default.png      one PNG per cursor per nominal size
 *     22/text.png
 *     44/default.png      optional larger sizes for high-DPI
 *
 *   {
 *     "name": "MyTheme",
 *     "sizes": [22, 44],
 *     "cursors": {
 *       "default": { "file": "default.png", "hotspot": [1, 1] },
 *       "text":    { "file": "text.png",    "hotspot": [11, 11] },
 *       "progress": { "file": "progress.png", "hotspot": [1, 1] }
 *     }
 *   }
 *
 * Hotspots are given for the smallest size and scaled for the others.
 *
 * lumina_server turns a theme into app_server's own CursorSet format
 * (a flattened BMessage, see src/servers/app/CursorSet.cpp) and the Window
 * Engine hands that file to CursorManager::SetCursorSet() inside
 * app_server. Applications keep requesting cursors by BCursorID, so text,
 * resize, grab, wait (progress) … cursors all follow the theme while custom
 * application cursors stay untouched.
 *
 * Everything here is pure data handling and unit-tested on the host.
 */
#ifndef LUMINA_CORE_CURSOR_THEME_H
#define LUMINA_CORE_CURSOR_THEME_H

#include <cstdint>
#include <string>
#include <vector>

#include <lumina/Value.h>

namespace lumina {
namespace cursor {

struct Role {
	const char*	name;		// manifest key ("text")
	int			id;			// BCursorID value (Cursor.h)
	const char*	setKey;		// key inside app_server's CursorSet message
							// (CursorSet::_CursorWhichToString), or
							// nullptr when app_server can't load it
};

// All system cursor roles app_server knows, in BCursorID order.
const std::vector<Role>& Roles();
const Role* FindRole(const std::string& name);

struct Entry {
	std::string	role;
	std::string	file;
	float		hotX = 0;
	float		hotY = 0;
};

struct Theme {
	std::string			name;
	std::vector<int>	sizes;		// ascending, nominal pixel sizes
	std::vector<Entry>	entries;

	const Entry* Find(const std::string& role) const;
};

// Parses and validates a manifest. Unknown roles, missing files names and
// out-of-range hotspots are reported in `problems` (one line each); the
// returned theme contains only the valid entries. Returns false if nothing
// usable remains.
bool ParseTheme(const Value& manifest, Theme& theme,
	std::vector<std::string>& problems);

// The size directory to use for a UI scale (1.0 = 22 px cursors, which is
// what app_server's CursorManager::InitializeCursors uses at scale 1).
// Picks the smallest size ≥ 22*scale, else the largest available.
int PickSize(const Theme& theme, float scale);

// Hotspot scaled from the base (smallest) size to `size`.
void ScaledHotspot(const Theme& theme, const Entry& entry, int size,
	float& x, float& y);

// "<size>/<file>" relative to the theme directory.
std::string RelativePath(const Entry& entry, int size);

// app_server keeps cursor pixels premultiplied (CursorManager::
// _RenderVectorCursor premultiplies its own cursors); PNGs are straight
// alpha. Converts B_RGBA32 rows in place.
void Premultiply(uint8_t* bits, int width, int height, int stride);

}	// namespace cursor
}	// namespace lumina

#endif
