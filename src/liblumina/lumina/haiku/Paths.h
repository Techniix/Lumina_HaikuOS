/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Filesystem locations, resolved with find_directory()/BPathFinder so that
 * packaged (read-only) data and user settings follow Haiku conventions.
 *
 *   ~/config/settings/Lumina/                  settings root
 *       session.lumina                          user layer (session.mode …)
 *       profiles/<name>/                        user profiles
 *       stock-snapshot.json                     pre-Lumina system state
 *       state.json                              crash counters, last mode
 *   ~/config/var/log/Lumina/<component>.log     logs (B_USER_LOG_DIRECTORY)
 *   <data>/Lumina/personalities/<name>/         shipped personalities
 *       (every B_FIND_PATH_DATA_DIRECTORY: system, home, non-packaged)
 *
 * LUMINA_SETTINGS_DIR / LUMINA_DATA_DIR environment variables override the
 * roots (used by tests and by `lumina --sandbox`).
 */
#ifndef LUMINA_HAIKU_PATHS_H
#define LUMINA_HAIKU_PATHS_H

#include <string>
#include <vector>

namespace lumina {
namespace haiku {
namespace Paths {

std::string SettingsDir();
std::string UserProfilesDir();
std::string UserLayerFile();
std::string StockSnapshotFile();
std::string StateFile();
std::string LogDir();
std::vector<std::string> PersonalityDirs();	// low → high priority

}	// namespace Paths
}	// namespace haiku
}	// namespace lumina

#endif
