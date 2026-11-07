/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Configuration schema. The schema itself is data (a JSONC document compiled
 * into the library, see ConfigSchemaData.cpp) so that the settings UI,
 * the CLI and the docs can all be generated from a single source.
 *
 * Paths are dotted; a "*" segment matches any array index, e.g.
 * "panels.*.thickness".
 */
#ifndef LUMINA_CORE_CONFIG_SCHEMA_H
#define LUMINA_CORE_CONFIG_SCHEMA_H

#include <string>
#include <vector>

#include "Value.h"

namespace lumina {

enum class SchemaType { Bool, Int, Number, String, Enum, Color, Array, Object };

const char* SchemaTypeName(SchemaType type);

struct SchemaEntry {
	std::string			path;
	SchemaType			type = SchemaType::String;
	Value				defaultValue;
	bool				hasMin = false;
	bool				hasMax = false;
	double				min = 0;
	double				max = 0;
	std::vector<std::string> values;	// for Enum
	std::string			description;
	std::string			group;			// UI grouping
	bool				live = true;	// applies without restart
	bool				risky = false;	// requires try-mode with revert
};

struct ValidationIssue {
	std::string	path;
	std::string	message;
	bool		fatal = true;	// false: warning (e.g. unknown key)
};

// Parses "#rgb", "#rrggbb" or "#rrggbbaa". Returns false on bad input.
bool ParseColor(const std::string& text, uint8_t& r, uint8_t& g, uint8_t& b,
	uint8_t& a);

class ConfigSchema {
public:
	// The built-in Lumina schema.
	static const ConfigSchema& Builtin();

	bool LoadFromJson(const std::string& jsonText, std::string* error);

	const SchemaEntry* Lookup(const std::string& path) const;
	const std::vector<SchemaEntry>& Entries() const { return fEntries; }

	// Validates one value against the entry for `path`.
	bool ValidateValue(const std::string& path, const Value& value,
		std::string* message) const;

	// Validates a whole document; unknown keys produce warnings.
	std::vector<ValidationIssue> Validate(const Value& document) const;

	// Removes invalid leaves from `document` (keeps unknown keys).
	// Returns the issues that caused removal.
	std::vector<ValidationIssue> Sanitize(Value& document) const;

	// Builds a document containing every default value.
	Value Defaults() const;

	static bool PathMatches(const std::string& pattern, const std::string& path);

private:
	void _Validate(const Value& node, const std::string& path,
		std::vector<ValidationIssue>& issues) const;

	std::vector<SchemaEntry> fEntries;
};

extern const char* kBuiltinSchemaJson;

}	// namespace lumina

#endif
