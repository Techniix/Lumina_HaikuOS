/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Captures and restores the system-wide desktop state Lumina may touch, so
 * "Restore stock Haiku" returns the user's *own* Haiku, not a factory one.
 *
 * Captured: decorator add-on, control look, all UI colors, mouse/focus
 * modes, accept-first-click, workspace count and layout, Deskbar placement
 * flags and whether Deskbar was running.
 */
#ifndef LUMINA_HAIKU_SYSTEM_APPEARANCE_H
#define LUMINA_HAIKU_SYSTEM_APPEARANCE_H

#include <SupportDefs.h>

#include <string>
#include <vector>

#include <lumina/Value.h>

namespace lumina {
namespace haiku {
namespace SystemAppearance {

// Reads the current system state into a JSON-able document.
Value Capture();

// Items Lumina itself changes; "Restore stock" restores only these by
// default, so changes the user made in Haiku's own preflets while Lumina
// was active (colours, fonts, mouse, workspaces) are never reverted.
extern const std::vector<std::string> kManagedItems;	// decorator, deskbar

// Applies a captured document. Each item is applied independently; the
// names of items that failed are returned in `failures`. `only` limits the
// restore to the named top-level items (NULL = everything captured).
status_t Restore(const Value& snapshot, std::vector<std::string>* failures,
	const std::vector<std::string>* only = nullptr);

// Ensures a snapshot exists on disk (only the first call ever writes one,
// so later calls can't overwrite the pristine state with Lumina's own).
// Returns true if a snapshot is available after the call.
bool EnsureSnapshot(const std::string& path, bool* created = nullptr);
bool LoadSnapshot(const std::string& path, Value& snapshot);

// Compare two captures (used by the switch-cycle drift test).
std::vector<std::string> Differences(const Value& a, const Value& b);

}	// namespace SystemAppearance
}	// namespace haiku
}	// namespace lumina

#endif
