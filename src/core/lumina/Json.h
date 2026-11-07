/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * JSONC reader/writer for Lumina's *.lumina files: standard JSON plus
 * // and block comments and trailing commas. Errors report line:column.
 */
#ifndef LUMINA_CORE_JSON_H
#define LUMINA_CORE_JSON_H

#include <string>

#include "Value.h"

namespace lumina {

struct JsonError {
	int			line = 0;
	int			column = 0;
	std::string	message;

	std::string ToString() const;
};

bool ParseJson(const std::string& text, Value& out, JsonError* error = nullptr);

struct JsonWriteOptions {
	bool	pretty = true;
	int		indent = 2;		// spaces; 0 uses tabs
};

std::string WriteJson(const Value& value,
	const JsonWriteOptions& options = JsonWriteOptions());

}	// namespace lumina

#endif
