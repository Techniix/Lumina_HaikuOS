/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "CursorTheme.h"

#include <algorithm>
#include <cmath>

namespace lumina {
namespace cursor {

const std::vector<Role>&
Roles()
{
	// Keys verified against src/servers/app/CursorSet.cpp
	// (_CursorWhichToString). B_CURSOR_ID_CREATE_LINK has no case there,
	// so app_server's CursorSet loader cannot carry it (it maps to
	// "Invalid"); we keep the role for completeness but mark it.
	static const std::vector<Role> kRoles = {
		{ "default",		1,	"System default" },
		{ "text",			2,	"I-beam" },
		{ "context-menu",	3,	"Context menu" },
		{ "copy",			4,	"Copy" },
		{ "crosshair",		5,	"Cross hair" },
		{ "link",			6,	"Follow link" },
		{ "grab",			7,	"Grab" },
		{ "grabbing",		8,	"Grabbing" },
		{ "help",			9,	"Help" },
		{ "text-horizontal", 10, "I-beam horizontal" },
		{ "move",			11,	"Move" },
		{ "none",			12,	"No cursor" },
		{ "not-allowed",	13,	"Not allowed" },
		{ "progress",		14,	"Progress" },
		{ "resize-n",		15,	"Resize North" },
		{ "resize-e",		16,	"Resize East" },
		{ "resize-s",		17,	"Resize South" },
		{ "resize-w",		18,	"Resize West" },
		{ "resize-ne",		19,	"Resize North East" },
		{ "resize-nw",		20,	"Resize North West" },
		{ "resize-se",		21,	"Resize South East" },
		{ "resize-sw",		22,	"Resize South West" },
		{ "resize-ns",		23,	"Resize North South" },
		{ "resize-ew",		24,	"Resize East West" },
		{ "resize-nesw",	25,	"Resize North East South West" },
		{ "resize-nwse",	26,	"Resize North West South East" },
		{ "zoom-in",		27,	"Zoom in" },
		{ "zoom-out",		28,	"Zoom out" },
		{ "create-link",	29,	nullptr },
	};
	return kRoles;
}


const Role*
FindRole(const std::string& name)
{
	for (const Role& role : Roles()) {
		if (name == role.name)
			return &role;
	}
	return nullptr;
}


const Entry*
Theme::Find(const std::string& role) const
{
	for (const Entry& entry : entries) {
		if (entry.role == role)
			return &entry;
	}
	return nullptr;
}


bool
ParseTheme(const Value& manifest, Theme& theme,
	std::vector<std::string>& problems)
{
	theme = Theme();
	if (!manifest.IsObject()) {
		problems.push_back("manifest is not a JSON object");
		return false;
	}
	if (const Value* name = manifest.Find("name"))
		theme.name = name->AsString("");
	if (theme.name.empty())
		theme.name = "Untitled";

	if (const Value* sizes = manifest.Find("sizes")) {
		for (const Value& size : sizes->Items()) {
			int value = int(size.AsInt(0));
			if (value < 8 || value > 256)
				problems.push_back("ignoring size out of range 8..256");
			else
				theme.sizes.push_back(value);
		}
	}
	if (theme.sizes.empty())
		theme.sizes.push_back(22);
	std::sort(theme.sizes.begin(), theme.sizes.end());
	theme.sizes.erase(std::unique(theme.sizes.begin(), theme.sizes.end()),
		theme.sizes.end());
	int base = theme.sizes.front();

	const Value* cursors = manifest.Find("cursors");
	if (cursors == nullptr || !cursors->IsObject()) {
		problems.push_back("manifest has no \"cursors\" object");
		return false;
	}
	for (const Value::Member& member : cursors->Members()) {
		const std::string& role = member.first;
		const Role* known = FindRole(role);
		if (known == nullptr) {
			problems.push_back("unknown cursor role \"" + role + "\"");
			continue;
		}
		if (known->setKey == nullptr) {
			problems.push_back("role \"" + role + "\" cannot be themed through "
				"app_server's CursorSet loader (no key in CursorSet.cpp)");
			continue;
		}
		Entry entry;
		entry.role = role;
		const Value& spec = member.second;
		if (spec.IsString())
			entry.file = spec.AsString();
		else if (const Value* file = spec.Find("file"))
			entry.file = file->AsString("");
		if (entry.file.empty() || entry.file.find("..") != std::string::npos
			|| entry.file.find('/') != std::string::npos) {
			problems.push_back("role \"" + role + "\": missing or unsafe "
				"file name");
			continue;
		}
		if (const Value* hot = spec.Find("hotspot")) {
			const Value::Array& xy = hot->Items();
			if (xy.size() == 2) {
				entry.hotX = float(xy[0].AsNumber(0));
				entry.hotY = float(xy[1].AsNumber(0));
			}
		}
		if (entry.hotX < 0 || entry.hotY < 0 || entry.hotX >= base
			|| entry.hotY >= base) {
			problems.push_back("role \"" + role + "\": hotspot outside the "
				"base size; clamped");
			entry.hotX = std::min(std::max(entry.hotX, 0.0f), float(base - 1));
			entry.hotY = std::min(std::max(entry.hotY, 0.0f), float(base - 1));
		}
		theme.entries.push_back(entry);
	}
	if (theme.Find("default") == nullptr)
		problems.push_back("theme has no \"default\" cursor; the stock arrow "
			"stays in place");
	return !theme.entries.empty();
}


int
PickSize(const Theme& theme, float scale)
{
	if (theme.sizes.empty())
		return 22;
	if (!(scale >= 1.0f))
		scale = 1.0f;
	int wanted = int(std::lround(22.0f * scale));
	for (int size : theme.sizes) {
		if (size >= wanted)
			return size;
	}
	return theme.sizes.back();
}


void
ScaledHotspot(const Theme& theme, const Entry& entry, int size, float& x,
	float& y)
{
	int base = theme.sizes.empty() ? 22 : theme.sizes.front();
	float factor = float(size) / float(base);
	x = std::floor(entry.hotX * factor);
	y = std::floor(entry.hotY * factor);
	x = std::min(std::max(x, 0.0f), float(size - 1));
	y = std::min(std::max(y, 0.0f), float(size - 1));
}


std::string
RelativePath(const Entry& entry, int size)
{
	return std::to_string(size) + "/" + entry.file;
}


void
Premultiply(uint8_t* bits, int width, int height, int stride)
{
	for (int y = 0; y < height; y++) {
		uint8_t* p = bits + size_t(y) * stride;
		for (int x = 0; x < width; x++, p += 4) {
			int a = p[3];
			if (a == 255)
				continue;
			p[0] = uint8_t((p[0] * a + 127) / 255);
			p[1] = uint8_t((p[1] * a + 127) / 255);
			p[2] = uint8_t((p[2] * a + 127) / 255);
		}
	}
}

}	// namespace cursor
}	// namespace lumina
