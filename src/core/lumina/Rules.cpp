/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Rules.h"

#include <cmath>

namespace lumina {

bool
RuleSet::Load(const Value& rules, std::vector<RuleIssue>* issues)
{
	fRules.clear();
	bool ok = true;
	for (size_t i = 0; i < rules.Items().size(); i++) {
		const Value& item = rules.Items()[i];
		auto issue = [&](const std::string& message) {
			ok = false;
			if (issues)
				issues->push_back({int(i), message});
		};
		if (!item.IsObject()) {
			issue("rule is not an object");
			continue;
		}
		Rule rule;
		if (const Value* v = item.Find("name"))
			rule.name = v->AsString();
		if (rule.name.empty())
			rule.name = "rule " + std::to_string(i);
		const Value* match = item.Find("match");
		const Value* set = item.Find("set");
		if (match == nullptr || !match->IsObject()) {
			issue("missing 'match' object");
			continue;
		}
		if (set == nullptr || !set->IsObject()) {
			issue("missing 'set' object");
			continue;
		}
		bool bad = false;
		for (const Value::Member& m : match->Members()) {
			const std::string& key = m.first;
			if (key == "signature") {
				rule.signature = m.second.AsString();
				if (!rule.signature.empty() && rule.signature.back() == '*') {
					rule.signaturePrefix = true;
					rule.signature.pop_back();
				}
			} else if (key == "title") {
				rule.title = m.second.AsString();
				rule.hasTitle = true;
			} else if (key == "title~") {
				try {
					rule.titleRegex = std::make_shared<std::regex>(
						m.second.AsString(), std::regex::ECMAScript);
				} catch (const std::regex_error&) {
					issue("invalid title regex");
					bad = true;
				}
			} else if (key == "look") {
				rule.look = m.second.AsString();
			} else if (key == "feel") {
				rule.feel = m.second.AsString();
			} else if (key == "workspace") {
				rule.workspace = int(m.second.AsInt(-1));
			} else if (key == "minWidth") {
				rule.minWidth = float(m.second.AsNumber());
			} else if (key == "minHeight") {
				rule.minHeight = float(m.second.AsNumber());
			} else {
				issue("unknown match key '" + key + "'");
				bad = true;
			}
		}
		if (bad)
			continue;
		rule.actions = *set;
		if (const Value* v = item.Find("final"))
			rule.final = v->AsBool();
		fRules.push_back(std::move(rule));
	}
	return ok;
}


bool
RuleSet::_Matches(const Rule& rule, const WindowDescriptor& window) const
{
	if (!rule.signature.empty()) {
		if (rule.signaturePrefix) {
			if (window.signature.compare(0, rule.signature.size(),
					rule.signature) != 0)
				return false;
		} else if (window.signature != rule.signature)
			return false;
	}
	if (rule.hasTitle && window.title != rule.title)
		return false;
	if (rule.titleRegex && !std::regex_search(window.title, *rule.titleRegex))
		return false;
	if (!rule.look.empty() && window.look != rule.look)
		return false;
	if (!rule.feel.empty() && window.feel != rule.feel)
		return false;
	if (rule.workspace >= 0 && window.workspace != rule.workspace)
		return false;
	if (window.width < rule.minWidth || window.height < rule.minHeight)
		return false;
	return true;
}


Value
RuleSet::Evaluate(const WindowDescriptor& window,
	std::vector<std::string>* matchedNames) const
{
	Value result = Value::MakeObject();
	for (const Rule& rule : fRules) {
		if (!_Matches(rule, window))
			continue;
		result.MergeFrom(rule.actions);
		if (matchedNames)
			matchedNames->push_back(rule.name);
		if (rule.final)
			break;
	}
	return result;
}


bool
PlacementRect(const std::string& preset, const RectF& area, RectF& out)
{
	float w = area.Width();
	float h = area.Height();
	float midX = std::floor(area.left + w / 2);
	float midY = std::floor(area.top + h / 2);
	out = area;
	if (preset == "maximize" || preset == "fill") {
		return true;
	} else if (preset == "left-half") {
		out.right = midX;
	} else if (preset == "right-half") {
		out.left = midX + 1;
	} else if (preset == "top-half") {
		out.bottom = midY;
	} else if (preset == "bottom-half") {
		out.top = midY + 1;
	} else if (preset == "top-left-quarter") {
		out.right = midX;
		out.bottom = midY;
	} else if (preset == "top-right-quarter") {
		out.left = midX + 1;
		out.bottom = midY;
	} else if (preset == "bottom-left-quarter") {
		out.right = midX;
		out.top = midY + 1;
	} else if (preset == "bottom-right-quarter") {
		out.left = midX + 1;
		out.top = midY + 1;
	} else if (preset == "center") {
		float cw = std::floor(w * 0.6f), ch = std::floor(h * 0.7f);
		out.left = std::floor(area.left + (w - cw) / 2);
		out.top = std::floor(area.top + (h - ch) / 2);
		out.right = out.left + cw;
		out.bottom = out.top + ch;
	} else
		return false;
	return true;
}


std::string
SnapZoneAt(float x, float y, const RectF& screen, float threshold,
	const std::string& layout)
{
	bool quarters = layout == "quarters" || layout == "halves+quarters";
	bool halves = layout == "halves" || layout == "halves+quarters";
	float corner = std::fmax(threshold, 32.0f);	// corner hot zone size
	bool atLeft = x <= screen.left + threshold;
	bool atRight = x >= screen.right - threshold;
	bool atTop = y <= screen.top + threshold;
	bool atBottom = y >= screen.bottom - threshold;
	bool nearTop = y <= screen.top + corner;
	bool nearBottom = y >= screen.bottom - corner;
	bool nearLeft = x <= screen.left + corner;
	bool nearRight = x >= screen.right - corner;

	if (quarters) {
		if ((atLeft && nearTop) || (atTop && nearLeft))
			return "top-left-quarter";
		if ((atRight && nearTop) || (atTop && nearRight))
			return "top-right-quarter";
		if ((atLeft && nearBottom) || (atBottom && nearLeft))
			return "bottom-left-quarter";
		if ((atRight && nearBottom) || (atBottom && nearRight))
			return "bottom-right-quarter";
	}
	if (halves || quarters) {
		if (atTop)
			return "maximize";
		if (atLeft)
			return halves ? "left-half" : "";
		if (atRight)
			return halves ? "right-half" : "";
	}
	return "";
}

}	// namespace lumina
