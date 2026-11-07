/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Paths.h"

#include <stdlib.h>

#include <FindDirectory.h>
#include <Path.h>
#include <PathFinder.h>
#include <StringList.h>

namespace lumina {
namespace haiku {
namespace Paths {

static std::string
FindDir(directory_which which, const char* leaf, const char* fallback)
{
	BPath path;
	if (find_directory(which, &path, true) != B_OK)
		return fallback;
	if (leaf != NULL)
		path.Append(leaf);
	return path.Path();
}


std::string
SettingsDir()
{
	if (const char* overrideDir = getenv("LUMINA_SETTINGS_DIR"))
		return overrideDir;
	return FindDir(B_USER_SETTINGS_DIRECTORY, "Lumina",
		"/boot/home/config/settings/Lumina");
}


std::string
UserProfilesDir()
{
	return SettingsDir() + "/profiles";
}


std::string
UserLayerFile()
{
	return SettingsDir() + "/session.lumina";
}


std::string
StockSnapshotFile()
{
	return SettingsDir() + "/stock-snapshot.json";
}


std::string
StateFile()
{
	return SettingsDir() + "/state.json";
}


std::string
LogDir()
{
	if (const char* overrideDir = getenv("LUMINA_SETTINGS_DIR"))
		return std::string(overrideDir) + "/logs";
	return FindDir(B_USER_LOG_DIRECTORY, "Lumina",
		"/boot/home/config/var/log/Lumina");
}


std::vector<std::string>
PersonalityDirs()
{
	std::vector<std::string> dirs;
	if (const char* overrideDir = getenv("LUMINA_DATA_DIR")) {
		dirs.push_back(std::string(overrideDir) + "/personalities");
		return dirs;
	}
	BStringList paths;
	if (BPathFinder::FindPaths(B_FIND_PATH_DATA_DIRECTORY,
			"Lumina/personalities", B_FIND_PATH_EXISTING_ONLY, paths) == B_OK) {
		// FindPaths returns highest priority (non-packaged/home) first.
		for (int32 i = paths.CountStrings() - 1; i >= 0; i--)
			dirs.push_back(paths.StringAt(i).String());
	}
	return dirs;
}

}	// namespace Paths
}	// namespace haiku
}	// namespace lumina
