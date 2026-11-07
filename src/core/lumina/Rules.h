/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Window rules: "when a window matching X appears, do Y".
 *
 *   { "name": "ssh terminals right",
 *     "match": { "signature": "application/x-vnd.Haiku-Terminal",
 *                "title~": "^ssh ", "workspace": 0 },
 *     "set":   { "placement": "right-half", "workspace": 2 } }
 *
 * Match keys: signature (exact, or prefix with trailing '*'), title (exact),
 * title~ (ECMAScript regex), look, feel, workspace, minWidth, minHeight.
 * Rules are evaluated in order; later rules override earlier "set" keys.
 * A rule with "final": true stops evaluation.
 */
#ifndef LUMINA_CORE_RULES_H
#define LUMINA_CORE_RULES_H

#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "Value.h"

namespace lumina {

struct WindowDescriptor {
	std::string	signature;
	std::string	title;
	std::string	look;		// "titled", "document", "modal", "floating", ...
	std::string	feel;		// "normal", "floating-all", ...
	int			workspace = -1;
	float		width = 0;
	float		height = 0;
};

struct RuleIssue {
	int			index;
	std::string	message;
};

class RuleSet {
public:
	bool Load(const Value& rules, std::vector<RuleIssue>* issues = nullptr);

	// Returns merged "set" actions for the window (empty object if none).
	Value Evaluate(const WindowDescriptor& window,
		std::vector<std::string>* matchedNames = nullptr) const;

	size_t Count() const { return fRules.size(); }

private:
	struct Rule {
		std::string		name;
		std::string		signature;
		bool			signaturePrefix = false;
		std::string		title;
		bool			hasTitle = false;
		std::shared_ptr<std::regex> titleRegex;
		std::string		look;
		std::string		feel;
		int				workspace = -1;
		float			minWidth = 0;
		float			minHeight = 0;
		Value			actions;
		bool			final = false;
	};

	bool _Matches(const Rule& rule, const WindowDescriptor& window) const;

	std::vector<Rule>	fRules;
};

// Placement presets used by rules and snapping. Returns false for unknown
// names. `area` is the usable screen area (minus reserved panel space).
struct RectF {
	float left = 0, top = 0, right = 0, bottom = 0;
	float Width() const { return right - left; }
	float Height() const { return bottom - top; }
	bool operator==(const RectF& o) const
		{ return left == o.left && top == o.top && right == o.right
			&& bottom == o.bottom; }
};

bool PlacementRect(const std::string& preset, const RectF& area, RectF& out);

// Snap zone detection for a pointer released at (x, y) within `screen`.
// Returns the preset name ("left-half", "top-left-quarter", "maximize")
// or "" if no zone applies.
std::string SnapZoneAt(float x, float y, const RectF& screen, float threshold,
	const std::string& layout);

}	// namespace lumina

#endif
