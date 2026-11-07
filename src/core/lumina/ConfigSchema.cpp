/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "ConfigSchema.h"

#include <cmath>
#include <cstdlib>

#include "Json.h"

namespace lumina {

const char*
SchemaTypeName(SchemaType type)
{
	switch (type) {
		case SchemaType::Bool: return "bool";
		case SchemaType::Int: return "int";
		case SchemaType::Number: return "number";
		case SchemaType::String: return "string";
		case SchemaType::Enum: return "enum";
		case SchemaType::Color: return "color";
		case SchemaType::Array: return "array";
		case SchemaType::Object: return "object";
	}
	return "?";
}


static bool
ParseSchemaType(const std::string& name, SchemaType& type)
{
	for (int i = 0; i <= int(SchemaType::Object); i++) {
		if (name == SchemaTypeName(SchemaType(i))) {
			type = SchemaType(i);
			return true;
		}
	}
	return false;
}


static int
HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


bool
ParseColor(const std::string& text, uint8_t& r, uint8_t& g, uint8_t& b,
	uint8_t& a)
{
	if (text.empty() || text[0] != '#')
		return false;
	std::string hex = text.substr(1);
	for (char c : hex) {
		if (HexDigit(c) < 0)
			return false;
	}
	auto byteAt = [&](size_t i) {
		return uint8_t(HexDigit(hex[i]) * 16 + HexDigit(hex[i + 1]));
	};
	if (hex.size() == 3) {
		r = uint8_t(HexDigit(hex[0]) * 17);
		g = uint8_t(HexDigit(hex[1]) * 17);
		b = uint8_t(HexDigit(hex[2]) * 17);
		a = 255;
		return true;
	}
	if (hex.size() == 6 || hex.size() == 8) {
		r = byteAt(0);
		g = byteAt(2);
		b = byteAt(4);
		a = hex.size() == 8 ? byteAt(6) : 255;
		return true;
	}
	return false;
}


const ConfigSchema&
ConfigSchema::Builtin()
{
	static ConfigSchema sSchema;
	static bool sLoaded = false;
	if (!sLoaded) {
		std::string error;
		if (!sSchema.LoadFromJson(kBuiltinSchemaJson, &error))
			std::abort();	// programming error: built-in schema must parse
		sLoaded = true;
	}
	return sSchema;
}


bool
ConfigSchema::LoadFromJson(const std::string& jsonText, std::string* error)
{
	Value doc;
	JsonError jsonError;
	if (!ParseJson(jsonText, doc, &jsonError)) {
		if (error)
			*error = jsonError.ToString();
		return false;
	}
	const Value* entries = doc.Find("entries");
	if (entries == nullptr || !entries->IsArray()) {
		if (error)
			*error = "schema has no 'entries' array";
		return false;
	}
	std::vector<SchemaEntry> parsed;
	for (const Value& item : entries->Items()) {
		SchemaEntry entry;
		entry.path = item.Find("path") ? item.Find("path")->AsString() : "";
		std::string typeName = item.Find("type")
			? item.Find("type")->AsString() : "";
		if (entry.path.empty() || !ParseSchemaType(typeName, entry.type)) {
			if (error)
				*error = "bad schema entry '" + entry.path + "'";
			return false;
		}
		if (const Value* v = item.Find("default"))
			entry.defaultValue = *v;
		if (const Value* v = item.Find("min")) {
			entry.hasMin = true;
			entry.min = v->AsNumber();
		}
		if (const Value* v = item.Find("max")) {
			entry.hasMax = true;
			entry.max = v->AsNumber();
		}
		if (const Value* v = item.Find("values")) {
			for (const Value& option : v->Items())
				entry.values.push_back(option.AsString());
		}
		if (const Value* v = item.Find("description"))
			entry.description = v->AsString();
		if (const Value* v = item.Find("group"))
			entry.group = v->AsString();
		if (const Value* v = item.Find("live"))
			entry.live = v->AsBool(true);
		if (const Value* v = item.Find("risky"))
			entry.risky = v->AsBool(false);
		parsed.push_back(std::move(entry));
	}
	// Defaults must themselves be valid.
	fEntries = std::move(parsed);
	for (const SchemaEntry& entry : fEntries) {
		if (entry.defaultValue.IsNull())
			continue;
		std::string message;
		if (!ValidateValue(entry.path, entry.defaultValue, &message)) {
			if (error)
				*error = "default for '" + entry.path + "' invalid: " + message;
			fEntries.clear();
			return false;
		}
	}
	return true;
}


bool
ConfigSchema::PathMatches(const std::string& pattern, const std::string& path)
{
	std::vector<std::string> p = SplitPath(pattern);
	std::vector<std::string> s = SplitPath(path);
	if (p.size() != s.size())
		return false;
	for (size_t i = 0; i < p.size(); i++) {
		if (p[i] == "*") {
			if (s[i].empty())
				return false;
			for (char c : s[i]) {
				if (c < '0' || c > '9')
					return false;
			}
			continue;
		}
		if (p[i] != s[i])
			return false;
	}
	return true;
}


const SchemaEntry*
ConfigSchema::Lookup(const std::string& path) const
{
	for (const SchemaEntry& entry : fEntries) {
		if (PathMatches(entry.path, path))
			return &entry;
	}
	return nullptr;
}


bool
ConfigSchema::ValidateValue(const std::string& path, const Value& value,
	std::string* message) const
{
	const SchemaEntry* entry = Lookup(path);
	if (entry == nullptr) {
		if (message)
			*message = "unknown setting";
		return false;
	}
	auto fail = [&](const std::string& text) {
		if (message)
			*message = text;
		return false;
	};
	switch (entry->type) {
		case SchemaType::Bool:
			if (!value.IsBool())
				return fail("expected bool");
			return true;
		case SchemaType::Int:
		case SchemaType::Number:
		{
			if (!value.IsNumber())
				return fail("expected number");
			double n = value.AsNumber();
			if (entry->type == SchemaType::Int && n != std::floor(n))
				return fail("expected integer");
			if (entry->hasMin && n < entry->min)
				return fail("below minimum " + std::to_string(entry->min));
			if (entry->hasMax && n > entry->max)
				return fail("above maximum " + std::to_string(entry->max));
			return true;
		}
		case SchemaType::String:
			if (!value.IsString())
				return fail("expected string");
			return true;
		case SchemaType::Enum:
			if (!value.IsString())
				return fail("expected string");
			for (const std::string& option : entry->values) {
				if (option == value.AsString())
					return true;
			}
			return fail("'" + value.AsString() + "' is not an allowed value");
		case SchemaType::Color:
		{
			uint8_t r, g, b, a;
			if (!value.IsString() || !ParseColor(value.AsString(), r, g, b, a))
				return fail("expected color '#rrggbb[aa]'");
			return true;
		}
		case SchemaType::Array:
			if (!value.IsArray())
				return fail("expected array");
			return true;
		case SchemaType::Object:
			if (!value.IsObject())
				return fail("expected object");
			return true;
	}
	return fail("unsupported type");
}


void
ConfigSchema::_Validate(const Value& node, const std::string& path,
	std::vector<ValidationIssue>& issues) const
{
	const SchemaEntry* entry = path.empty() ? nullptr : Lookup(path);
	if (entry != nullptr) {
		std::string message;
		if (!ValidateValue(path, node, &message)) {
			issues.push_back({path, message, true});
			return;
		}
		// Descend into arrays/objects that have per-element entries.
		if (entry->type == SchemaType::Array) {
			for (size_t i = 0; i < node.Items().size(); i++) {
				std::string child = path + "." + std::to_string(i);
				bool described = false;
				for (const SchemaEntry& e : fEntries) {
					if (e.path.compare(0, entry->path.size() + 2,
							entry->path + ".*") == 0) {
						described = true;
						break;
					}
				}
				if (described)
					_Validate(node.Items()[i], child, issues);
			}
		}
		if (entry->type != SchemaType::Object)
			return;
	}
	if (node.IsObject()) {
		for (const Value::Member& m : node.Members()) {
			std::string child = path.empty() ? m.first : path + "." + m.first;
			_Validate(m.second, child, issues);
		}
		return;
	}
	if (entry == nullptr) {
		// Leaf with no schema entry. Is any entry below this path? If the
		// path is a prefix of an entry (e.g. an object we haven't described
		// as a whole), it's fine; otherwise warn.
		issues.push_back({path, "unknown setting (kept)", false});
	}
}


std::vector<ValidationIssue>
ConfigSchema::Validate(const Value& document) const
{
	std::vector<ValidationIssue> issues;
	_Validate(document, "", issues);
	return issues;
}


std::vector<ValidationIssue>
ConfigSchema::Sanitize(Value& document) const
{
	std::vector<ValidationIssue> issues = Validate(document);
	std::vector<ValidationIssue> removed;
	// Erase deepest paths first so that indices stay meaningful.
	for (auto it = issues.rbegin(); it != issues.rend(); ++it) {
		if (!it->fatal)
			continue;
		document.ErasePath(it->path);
		removed.push_back(*it);
	}
	return removed;
}


Value
ConfigSchema::Defaults() const
{
	Value doc = Value::MakeObject();
	for (const SchemaEntry& entry : fEntries) {
		if (entry.defaultValue.IsNull())
			continue;
		if (entry.path.find('*') != std::string::npos)
			continue;	// element defaults are applied per element
		doc.SetPath(entry.path, entry.defaultValue);
	}
	return doc;
}

}	// namespace lumina
